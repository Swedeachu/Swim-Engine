#include "Tools/AssetCompiler/DevelopmentAssetPipeline.h"

#include "Engine/Assets/AssetSystem.h"
#include "Engine/Assets/ContentHash.h"
#include "Engine/Assets/SassetFormat.h"
#include "Tools/AssetCompiler/GltfImporter.h"
#include "Tools/AssetCompiler/MeshOptimizer.h"
#include "Tools/AssetCompiler/SassetWriter.h"
#include "Tools/AssetCompiler/StaticModelCompiler.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace Swim::AssetCompiler
{
	namespace
	{
		struct CookInspection
		{
			bool Current = false;
			std::filesystem::path RootSasset;
		};

		std::string LowerExtension(const std::filesystem::path& path)
		{
			std::string extension = path.extension().string();
			std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value)
			{
				return static_cast<char>(std::tolower(value));
			});
			return extension;
		}

		bool IsSourceModel(const std::filesystem::path& path)
		{
			const std::string extension = LowerExtension(path);
			return extension == ".gltf" || extension == ".glb";
		}

		bool IsInsideCookedDirectory(const std::filesystem::path& relativePath)
		{
			for (const auto& part : relativePath)
			{
				std::string value = part.string();
				std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
				{
					return static_cast<char>(std::tolower(character));
				});
				if (value == "cooked")
				{
					return true;
				}
			}
			return false;
		}

		std::vector<std::byte> ReadFile(const std::filesystem::path& path)
		{
			std::ifstream file(path, std::ios::binary | std::ios::ate);
			if (!file)
			{
				throw std::runtime_error("failed to open file: " + path.string());
			}
			const std::streamsize size = file.tellg();
			if (size < 0)
			{
				throw std::runtime_error("failed to query file size: " + path.string());
			}
			std::vector<std::byte> bytes(static_cast<std::size_t>(size));
			file.seekg(0, std::ios::beg);
			if (size > 0 && !file.read(reinterpret_cast<char*>(bytes.data()), size))
			{
				throw std::runtime_error("failed to read file: " + path.string());
			}
			return bytes;
		}

		std::string AssetIdHex(Swim::Assets::AssetId id)
		{
			std::ostringstream stream;
			stream << std::hex << std::setfill('0') << std::setw(16) << id.Value;
			return stream.str();
		}

		std::filesystem::path ObjectPath(const std::filesystem::path& cookedRoot, Swim::Assets::AssetId id)
		{
			return cookedRoot / ".objects" / (AssetIdHex(id) + ".sasset");
		}

		std::filesystem::path RootCookedPath(
			const std::filesystem::path& assetRoot,
			const std::filesystem::path& cookedRoot,
			const std::filesystem::path& source)
		{
			std::filesystem::path relative = std::filesystem::relative(source, assetRoot);
			relative.replace_extension(".sasset");
			return cookedRoot / relative;
		}

		bool WriteFileReplace(const std::filesystem::path& path, std::span<const std::byte> bytes)
		{
			std::error_code error;
			std::filesystem::create_directories(path.parent_path(), error);
			if (error)
			{
				return false;
			}

			std::filesystem::path temporary = path;
			temporary += ".new";
			std::filesystem::path backup = path;
			backup += ".old";
			std::filesystem::remove(temporary, error);
			error.clear();
			std::filesystem::remove(backup, error);
			error.clear();

			{
				std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
				if (!file)
				{
					return false;
				}
				if (!bytes.empty())
				{
					file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
				}
				file.flush();
				if (!file)
				{
					std::filesystem::remove(temporary, error);
					return false;
				}
			}

			const bool destinationExists = std::filesystem::is_regular_file(path, error) && !error;
			error.clear();
			if (destinationExists)
			{
				std::filesystem::rename(path, backup, error);
				if (error)
				{
					std::filesystem::remove(temporary, error);
					return false;
				}
			}

			std::filesystem::rename(temporary, path, error);
			if (error)
			{
				std::error_code cleanupError;
				std::filesystem::remove(temporary, cleanupError);
				if (destinationExists)
				{
					std::filesystem::rename(backup, path, cleanupError);
				}
				return false;
			}

			if (destinationExists)
			{
				std::filesystem::remove(backup, error);
			}
			return true;
		}

		std::filesystem::path CanonicalDependencyPath(
			const std::filesystem::path& assetRoot,
			const std::filesystem::path& path)
		{
			std::error_code error;
			const std::filesystem::path root = std::filesystem::weakly_canonical(assetRoot, error);
			if (error)
			{
				throw std::runtime_error("could not canonicalize asset root");
			}
			const std::filesystem::path absolute = std::filesystem::weakly_canonical(path, error);
			if (error)
			{
				throw std::runtime_error("could not canonicalize source dependency: " + path.string());
			}
			const std::filesystem::path relative = std::filesystem::relative(absolute, root, error);
			if (error || relative.empty())
			{
				throw std::runtime_error("could not make source dependency relative to asset root: " + path.string());
			}
			for (const auto& part : relative)
			{
				if (part == "..")
				{
					throw std::runtime_error("source dependency escapes the asset root: " + path.string());
				}
			}
			return relative.lexically_normal();
		}

		std::vector<Swim::Assets::SassetSourceDependency> BuildSourceDependencies(
			const std::filesystem::path& assetRoot,
			const std::filesystem::path& source,
			const IntermediateModel& model)
		{
			std::set<std::string> paths;
			paths.insert(CanonicalDependencyPath(assetRoot, source).generic_string());
			for (const std::string& dependency : model.ExternalDependencies)
			{
				const std::filesystem::path absolute = source.parent_path() / std::filesystem::path(dependency);
				paths.insert(CanonicalDependencyPath(assetRoot, absolute).generic_string());
			}

			std::vector<Swim::Assets::SassetSourceDependency> result;
			result.reserve(paths.size());
			for (const std::string& logicalPath : paths)
			{
				const std::filesystem::path absolute = assetRoot / std::filesystem::path(logicalPath);
				result.push_back({ Swim::Assets::NormalizeAssetPath(logicalPath), Swim::Assets::ComputeContentHash(ReadFile(absolute)) });
			}
			return result;
		}

		std::optional<Swim::Assets::ContentHash> ComputeCurrentSourceHash(
			const std::filesystem::path& assetRoot,
			std::span<const Swim::Assets::SassetSourceDependency> stored)
		{
			if (stored.empty())
			{
				return std::nullopt;
			}
			std::vector<Swim::Assets::SassetSourceDependency> current;
			current.reserve(stored.size());
			for (const auto& dependency : stored)
			{
				const std::filesystem::path path = assetRoot / std::filesystem::path(dependency.LogicalPath);
				std::error_code error;
				if (!std::filesystem::is_regular_file(path, error) || error)
				{
					return std::nullopt;
				}
				current.push_back({ dependency.LogicalPath, Swim::Assets::ComputeContentHash(ReadFile(path)) });
			}
			return ComputeSourceGraphHash(current);
		}

		bool ValidateCookedGraph(
			const std::filesystem::path& path,
			const std::filesystem::path& cookedRoot,
			std::unordered_set<Swim::Assets::AssetId>& visited)
		{
			std::error_code error;
			if (!std::filesystem::is_regular_file(path, error) || error)
			{
				return false;
			}

			const auto bytes = ReadFile(path);
			const auto parsed = Swim::Assets::ParseSasset(bytes, true);
			if (!parsed)
			{
				return false;
			}
			if (!visited.insert(parsed.Metadata.Id).second)
			{
				return true;
			}

			for (const Swim::Assets::AssetId dependency : parsed.Metadata.Dependencies)
			{
				if (!ValidateCookedGraph(ObjectPath(cookedRoot, dependency), cookedRoot, visited))
				{
					return false;
				}
			}
			return true;
		}

		CookInspection InspectCooked(
			const std::filesystem::path& assetRoot,
			const std::filesystem::path& cookedRoot,
			const std::filesystem::path& source)
		{
			CookInspection result;
			result.RootSasset = RootCookedPath(assetRoot, cookedRoot, source);
			std::error_code error;
			if (!std::filesystem::is_regular_file(result.RootSasset, error) || error)
			{
				return result;
			}

			const auto bytes = ReadFile(result.RootSasset);
			const auto parsed = Swim::Assets::ParseSasset(bytes, true);
			if (!parsed || parsed.Metadata.Type != Swim::Assets::SassetAssetType::Model)
			{
				return result;
			}
			if (parsed.Metadata.CompilerProfileHash != GetStaticModelCompilerProfileHash())
			{
				return result;
			}
			const auto currentSourceHash = ComputeCurrentSourceHash(assetRoot, parsed.Metadata.SourceDependencies);
			if (!currentSourceHash.has_value() || *currentSourceHash != parsed.Metadata.SourceHash)
			{
				return result;
			}
			std::unordered_set<Swim::Assets::AssetId> validated;
			if (!ValidateCookedGraph(result.RootSasset, cookedRoot, validated))
			{
				return result;
			}
			result.Current = true;
			return result;
		}

		bool PublishCookedAssets(
			const std::filesystem::path& rootPath,
			const std::filesystem::path& cookedRoot,
			const StaticModelCompileResult& compiled)
		{
			const CompiledSasset* root = nullptr;
			for (const CompiledSasset& asset : compiled.Assets)
			{
				if (asset.IsRoot)
				{
					root = &asset;
					continue;
				}
				if (!WriteFileReplace(ObjectPath(cookedRoot, asset.Id), asset.Bytes))
				{
					return false;
				}
			}
			return root && WriteFileReplace(rootPath, root->Bytes);
		}

		bool LoadSassetGraph(
			const std::filesystem::path& rootPath,
			const std::filesystem::path& cookedRoot,
			Swim::Assets::AssetSystem& assets,
			std::unordered_set<Swim::Assets::AssetId>& loaded,
			std::size_t& loadedCount,
			std::string& errorMessage)
		{
			const auto bytes = ReadFile(rootPath);
			const auto parsed = Swim::Assets::ParseSasset(bytes, true);
			if (!parsed)
			{
				errorMessage = parsed.Error.Message;
				return false;
			}
			if (loaded.contains(parsed.Metadata.Id))
			{
				return true;
			}

			for (const Swim::Assets::AssetId dependency : parsed.Metadata.Dependencies)
			{
				const std::filesystem::path dependencyPath = ObjectPath(cookedRoot, dependency);
				std::error_code filesystemError;
				if (!std::filesystem::is_regular_file(dependencyPath, filesystemError) || filesystemError)
				{
					errorMessage = "missing cooked dependency object " + AssetIdHex(dependency);
					return false;
				}
				if (!LoadSassetGraph(dependencyPath, cookedRoot, assets, loaded, loadedCount, errorMessage))
				{
					return false;
				}
			}

			const auto loadedAsset = Swim::Assets::LoadSasset(assets, bytes);
			if (!loadedAsset)
			{
				errorMessage = loadedAsset.Error.Message;
				return false;
			}
			loaded.insert(loadedAsset.Id);
			++loadedCount;
			return true;
		}
	}

	DevelopmentAssetBootstrapResult RunDevelopmentAssetBootstrap(
		const std::filesystem::path& assetRoot,
		Swim::Assets::AssetSystem& assets)
	{
		DevelopmentAssetBootstrapResult result;
		std::error_code error;
		if (!std::filesystem::exists(assetRoot, error) || error)
		{
			return result;
		}

		const std::filesystem::path cookedRoot = assetRoot / "Cooked";
		std::vector<std::filesystem::path> sources;
		for (std::filesystem::recursive_directory_iterator iterator(assetRoot, std::filesystem::directory_options::skip_permission_denied, error), end;
			iterator != end; iterator.increment(error))
		{
			if (error)
			{
				error.clear();
				continue;
			}
			if (!iterator->is_regular_file(error) || error)
			{
				error.clear();
				continue;
			}
			const std::filesystem::path relative = std::filesystem::relative(iterator->path(), assetRoot, error);
			if (error || IsInsideCookedDirectory(relative))
			{
				error.clear();
				continue;
			}
			if (IsSourceModel(iterator->path()))
			{
				sources.push_back(iterator->path());
			}
		}
		std::sort(sources.begin(), sources.end());
		result.Stats.SourcesDiscovered = sources.size();

		GltfImporter importer;
		MeshOptimizer optimizer;
		StaticModelCompiler compiler;
		std::vector<std::filesystem::path> rootsToLoad;
		for (const std::filesystem::path& source : sources)
		{
			CookInspection inspection;
			try
			{
				inspection = InspectCooked(assetRoot, cookedRoot, source);
			}
			catch (const std::exception& inspectError)
			{
				result.Errors.push_back({ DevelopmentAssetErrorStage::Inspect, source, inspectError.what() });
				inspection.RootSasset = RootCookedPath(assetRoot, cookedRoot, source);
			}

			if (inspection.Current)
			{
				++result.Stats.SourcesCurrent;
				rootsToLoad.push_back(inspection.RootSasset);
				continue;
			}

			const GltfImportResult imported = importer.Import(source);
			if (!imported)
			{
				result.Errors.push_back({ DevelopmentAssetErrorStage::Import, source, imported.Error.Message });
				continue;
			}

			IntermediateModel optimizedModel = imported.Model;
			const MeshOptimizationResult optimized = optimizer.Optimize(optimizedModel);
			if (!optimized)
			{
				result.Errors.push_back({ DevelopmentAssetErrorStage::Optimize, source, optimized.Error.Message });
				continue;
			}

			std::vector<Swim::Assets::SassetSourceDependency> sourceDependencies;
			try
			{
				sourceDependencies = BuildSourceDependencies(assetRoot, source, optimizedModel);
			}
			catch (const std::exception& dependencyError)
			{
				result.Errors.push_back({ DevelopmentAssetErrorStage::Compile, source, dependencyError.what() });
				continue;
			}

			const std::string sourceLogicalPath = CanonicalDependencyPath(assetRoot, source).generic_string();
			const StaticModelCompileResult compiled = compiler.Compile(optimizedModel, sourceLogicalPath, std::move(sourceDependencies));
			if (!compiled)
			{
				result.Errors.push_back({ DevelopmentAssetErrorStage::Compile, source, compiled.Error.Message });
				continue;
			}

			if (!PublishCookedAssets(inspection.RootSasset, cookedRoot, compiled))
			{
				result.Errors.push_back({ DevelopmentAssetErrorStage::Publish, source, "failed to publish one or more cooked .sasset files" });
				continue;
			}
			++result.Stats.SourcesCooked;
			rootsToLoad.push_back(inspection.RootSasset);
		}

		std::unordered_set<Swim::Assets::AssetId> loaded;
		for (const std::filesystem::path& root : rootsToLoad)
		{
			std::string loadError;
			if (!LoadSassetGraph(root, cookedRoot, assets, loaded, result.Stats.SassetsLoaded, loadError))
			{
				result.Errors.push_back({ DevelopmentAssetErrorStage::Load, root, std::move(loadError) });
				continue;
			}
			const auto rootBytes = ReadFile(root);
			const auto parsed = Swim::Assets::ParseSasset(rootBytes, false);
			if (parsed && parsed.Metadata.Type == Swim::Assets::SassetAssetType::Model)
			{
				result.RootModels.push_back(parsed.Metadata.Id);
				++result.Stats.RootModelsLoaded;
			}
		}

		return result;
	}

}
