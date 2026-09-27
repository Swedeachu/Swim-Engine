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
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Swim::AssetCompiler
{
	namespace
	{
		// The cooked files the inspection read, by path: the load publishes them without
		// reading any file a second time.
		using CookedFileCache = std::unordered_map<std::string, std::vector<std::byte>>;

		struct CookInspection
		{
			bool Current = false;
			std::filesystem::path RootSasset;
			CookedFileCache Files;
		};

		// Cooked files are written whole by the cooker (WriteFileReplace) and parsed with their
		// structure and sizes checked, so their content hashes are not recomputed on every
		// start (that was about six SHA-256 passes over every cooked byte). SWIM_VERIFY_ASSETS=1
		// turns the full hash validation back on.
		bool VerifyCookedHashes()
		{
			static const bool verify = []
			{
				const char* value = std::getenv("SWIM_VERIFY_ASSETS");
				return value != nullptr && value[0] == '1';
			}();
			return verify;
		}

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

		// A source file's size and modification time, the key of the source stamp.
		std::string SourceStampLine(const std::string& logicalPath, const std::filesystem::path& path)
		{
			std::error_code error;
			const auto size = std::filesystem::file_size(path, error);
			if (error)
			{
				return {};
			}
			const auto time = std::filesystem::last_write_time(path, error);
			if (error)
			{
				return {};
			}
			std::ostringstream line;
			line << logicalPath << '\t' << size << '\t' << time.time_since_epoch().count();
			return line.str();
		}

		std::filesystem::path SourceStampPath(const std::filesystem::path& rootSasset)
		{
			std::filesystem::path path = rootSasset;
			path += ".stamp";
			return path;
		}

		// The stamp next to a cooked root: the source graph hash and, per source file, its
		// size and modification time when that hash was computed. While every source still
		// has them, the hash is taken from the stamp instead of re-reading and hashing sources.
		std::string BuildSourceStamp(const std::filesystem::path& assetRoot,
			std::span<const Swim::Assets::SassetSourceDependency> stored, const Swim::Assets::ContentHash& hash)
		{
			std::string stamp = hash.ToHex() + "\n";
			for (const auto& dependency : stored)
			{
				const std::string line = SourceStampLine(dependency.LogicalPath, assetRoot / std::filesystem::path(dependency.LogicalPath));
				if (line.empty())
				{
					return {};
				}
				stamp += line + "\n";
			}
			return stamp;
		}

		std::optional<Swim::Assets::ContentHash> StampedSourceHash(const std::filesystem::path& assetRoot,
			const std::filesystem::path& rootSasset, std::span<const Swim::Assets::SassetSourceDependency> stored)
		{
			std::ifstream file(SourceStampPath(rootSasset), std::ios::binary);
			if (!file)
			{
				return std::nullopt;
			}
			std::string hashLine;
			std::getline(file, hashLine);
			std::string expected;
			for (const auto& dependency : stored)
			{
				const std::string line = SourceStampLine(dependency.LogicalPath, assetRoot / std::filesystem::path(dependency.LogicalPath));
				if (line.empty())
				{
					return std::nullopt;
				}
				expected += line + "\n";
			}
			const std::string rest((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
			if (rest != expected)
			{
				return std::nullopt;
			}
			try
			{
				return Swim::Assets::ContentHash::FromHex(hashLine);
			}
			catch (const std::exception&)
			{
				return std::nullopt;
			}
		}

		void WriteSourceStamp(const std::filesystem::path& assetRoot, const std::filesystem::path& rootSasset,
			std::span<const Swim::Assets::SassetSourceDependency> stored, const Swim::Assets::ContentHash& hash)
		{
			const std::string stamp = BuildSourceStamp(assetRoot, stored, hash);
			if (stamp.empty())
			{
				return;
			}
			std::ofstream file(SourceStampPath(rootSasset), std::ios::binary | std::ios::trunc);
			file << stamp;
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
			std::unordered_set<Swim::Assets::AssetId>& visited,
			CookedFileCache& files)
		{
			std::error_code error;
			if (!std::filesystem::is_regular_file(path, error) || error)
			{
				return false;
			}

			auto bytes = ReadFile(path);
			const auto parsed = Swim::Assets::ParseSasset(bytes, VerifyCookedHashes());
			if (!parsed)
			{
				return false;
			}
			if (!visited.insert(parsed.Metadata.Id).second)
			{
				return true;
			}
			const auto dependencies = parsed.Metadata.Dependencies;
			files[path.string()] = std::move(bytes);

			for (const Swim::Assets::AssetId dependency : dependencies)
			{
				if (!ValidateCookedGraph(ObjectPath(cookedRoot, dependency), cookedRoot, visited, files))
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
			const auto parsed = Swim::Assets::ParseSasset(bytes, VerifyCookedHashes());
			if (!parsed || parsed.Metadata.Type != Swim::Assets::SassetAssetType::Model)
			{
				return result;
			}
			if (parsed.Metadata.CompilerProfileHash != GetStaticModelCompilerProfileHash())
			{
				return result;
			}
			// The sources' hash from the stamp while their sizes and times are unchanged;
			// otherwise hashed from the files (and the stamp refreshed when they still match).
			auto currentSourceHash = StampedSourceHash(assetRoot, result.RootSasset, parsed.Metadata.SourceDependencies);
			if (!currentSourceHash.has_value() || *currentSourceHash != parsed.Metadata.SourceHash)
			{
				currentSourceHash = ComputeCurrentSourceHash(assetRoot, parsed.Metadata.SourceDependencies);
				if (!currentSourceHash.has_value() || *currentSourceHash != parsed.Metadata.SourceHash)
				{
					return result;
				}
				WriteSourceStamp(assetRoot, result.RootSasset, parsed.Metadata.SourceDependencies, *currentSourceHash);
			}
			std::unordered_set<Swim::Assets::AssetId> validated;
			CookedFileCache files;
			if (!ValidateCookedGraph(result.RootSasset, cookedRoot, validated, files))
			{
				return result;
			}
			result.Files = std::move(files);
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
			std::string& errorMessage,
			CookedFileCache& files)
		{
			std::vector<std::byte> bytes;
			if (const auto cached = files.find(rootPath.string()); cached != files.end())
			{
				bytes = std::move(cached->second);
				files.erase(cached);
			}
			else
			{
				bytes = ReadFile(rootPath);
			}
			const auto parsed = Swim::Assets::ParseSasset(bytes, VerifyCookedHashes());
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
				if (!LoadSassetGraph(dependencyPath, cookedRoot, assets, loaded, loadedCount, errorMessage, files))
				{
					return false;
				}
			}

			const auto loadedAsset = Swim::Assets::LoadSasset(assets, bytes, VerifyCookedHashes());
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
		CookedFileCache files; // What the inspections read, handed to the load.
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
				files.merge(inspection.Files);
				continue;
			}

			const GltfImportResult imported = importer.Import(source);
			if (!imported)
			{
				if (imported.Error.Code == GltfImportErrorCode::UnsupportedFeature)
				{
					++result.Stats.SourcesSkippedUnsupported;
					continue;
				}
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
			if (!compiled.Assets.empty())
			{
				// The fresh cook's sources are current: stamp them so the next start skips hashing.
				const auto rootBytes = ReadFile(inspection.RootSasset);
				const auto rootParsed = Swim::Assets::ParseSasset(rootBytes, false);
				if (rootParsed)
				{
					WriteSourceStamp(assetRoot, inspection.RootSasset, rootParsed.Metadata.SourceDependencies, rootParsed.Metadata.SourceHash);
				}
			}
		}

		std::unordered_set<Swim::Assets::AssetId> loaded;
		for (const std::filesystem::path& root : rootsToLoad)
		{
			// The root's id and type first (its bytes are still cached; the load consumes them).
			std::optional<Swim::Assets::AssetId> rootModel;
			{
				const auto cached = files.find(root.string());
				const auto rootBytes = cached != files.end() ? std::vector<std::byte>() : ReadFile(root);
				const auto parsed = Swim::Assets::ParseSasset(cached != files.end() ? std::span<const std::byte>(cached->second) : std::span<const std::byte>(rootBytes), false);
				if (parsed && parsed.Metadata.Type == Swim::Assets::SassetAssetType::Model)
				{
					rootModel = parsed.Metadata.Id;
				}
			}
			std::string loadError;
			if (!LoadSassetGraph(root, cookedRoot, assets, loaded, result.Stats.SassetsLoaded, loadError, files))
			{
				result.Errors.push_back({ DevelopmentAssetErrorStage::Load, root, std::move(loadError) });
				continue;
			}
			if (rootModel)
			{
				result.RootModels.push_back(*rootModel);
				++result.Stats.RootModelsLoaded;
			}
		}

		return result;
	}

}
