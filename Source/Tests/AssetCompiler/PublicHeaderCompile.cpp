#include "Tools/AssetCompiler/GltfImporter.h"
#include "Tools/AssetCompiler/DevelopmentAssetPipeline.h"
#include "Tools/AssetCompiler/IntermediateModel.h"
#include "Tools/AssetCompiler/Ktx2TextureCompiler.h"
#include "Tools/AssetCompiler/MeshOptimizer.h"
#include "Tools/AssetCompiler/SassetWriter.h"
#include "Tools/AssetCompiler/StaticModelCompiler.h"

namespace
{
	[[maybe_unused]] Swim::AssetCompiler::IntermediateModel Model;
	[[maybe_unused]] Swim::AssetCompiler::GltfImporter Importer;
	[[maybe_unused]] Swim::AssetCompiler::MeshOptimizer Optimizer;
}
