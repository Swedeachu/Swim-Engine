#pragma once

#include "Engine/Commands/CommandRegistry.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace Engine
{

	class FrameRenderer;

	struct ShaderHotReloadDesc
	{
		std::filesystem::path Compiler;	 // The pinned slangc (DevPaths.cfg SlangCompiler).
		std::filesystem::path Sources;	 // The runtime shader source manifest (DevPaths.cfg ShaderSources).
		std::filesystem::path CacheRoot; // Compiled artifacts, by content hash.
		// Set for a backend that runs DXIL (DevPaths.cfg DxcDirectory): reloads also build the
		// program's DXIL and register reflection, as the build does.
		std::filesystem::path DxcDirectory;
		std::string DxilProfile = "sm_6_8"; // DevPaths.cfg DxilProfile.
	};

	// Development shader hot reload. A worker thread watches every runtime program's source
	// and the files it includes (from its depfile) and recompiles a changed program with the
	// build's slangc command line in a child process. Results are cached by a hash of the
	// command line and every input file, so returning to an earlier version is instant.
	// Update (owner thread) hands each new program to FrameRenderer::ReplaceProgram, which
	// swaps it in once the GPU is done with the old one; a failed compile keeps the old
	// program and prints the compiler output.
	//   shader_hot_reload 0|1     watch the files (default 1)
	//   shader_reload <name|all>  recompile now
	class ShaderHotReload
	{

	  public:

		ShaderHotReload(ShaderHotReloadDesc desc, Swim::Commands::CommandRegistry& commands, FrameRenderer& renderer);

		~ShaderHotReload();

		ShaderHotReload(const ShaderHotReload&) = delete;

		ShaderHotReload& operator=(const ShaderHotReload&) = delete;

		// False when the compiler or the manifest is missing (nothing is watched).
		bool IsActive() const;

		// Delivers finished compiles. Owner thread, once per frame.
		void Update();

		// Queues a recompile of a program, or of every program for "all"; false for an unknown name.
		bool Reload(std::string_view name);

	  private:

		struct Impl;
		std::unique_ptr<Impl> impl;

	};

} // namespace Engine
