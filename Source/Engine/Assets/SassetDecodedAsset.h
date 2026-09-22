#pragma once

#include "Engine/Assets/MaterialAsset.h"
#include "Engine/Assets/MeshAsset.h"
#include "Engine/Assets/SassetFormat.h"
#include "Engine/Assets/TextureAsset.h"

#include <variant>

namespace Swim::Assets
{

	// A fully validated, decoded .sasset that has not touched any AssetSystem.
	// DecodeSasset produces it without shared state, so it may run on a job
	// worker; PublishSasset then binds/publishes it on the AssetSystem owner
	// thread. Material instances and models resolve AssetHandles while decoding,
	// so they decode to std::monostate and must go through LoadSasset instead.
	struct SassetDecodedAsset
	{
		SassetMetadata Metadata;
		std::variant<std::monostate, MeshAsset, TextureAsset, SamplerAsset, MaterialTemplateAsset> Asset;

		bool RequiresOwnerThreadDecode() const { return std::holds_alternative<std::monostate>(Asset); }
	};

	struct SassetDecodeResult
	{
		SassetDecodedAsset Decoded;
		SassetError Error;

		explicit operator bool() const { return Error.Code == SassetErrorCode::None; }
	};

	// Thread-safe: parses, validates chunk/content hashes and decodes the payload.
	SassetDecodeResult DecodeSasset(std::span<const std::byte> bytes, bool validateChunkHashes = true);

	// Owner thread only: binds the logical path and publishes the decoded asset
	// with its content hash and dependencies. Rejects owner-thread-only types.
	SassetLoadResult PublishSasset(AssetSystem& assets, SassetDecodedAsset decoded);

} // namespace Swim::Assets
