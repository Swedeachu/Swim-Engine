#include "Engine/Systems/Renderer/RHI/Backends/D3D12/D3D12RhiBackend.h"

#include "Engine/Systems/Renderer/RHI/Backends/D3D12/D3D12Device.h"

#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace Swim::RhiD3D12
{

	namespace
	{

		class D3D12Adapter final : public Rhi::Adapter
		{

		  public:
			D3D12Adapter(std::shared_ptr<D3D12SystemState> systemValue, ComPtr<IDXGIAdapter4> adapterValue, Rhi::AdapterInfo infoValue,
				D3D12Features featuresValue)
				: system(std::move(systemValue)), adapter(std::move(adapterValue)), info(std::move(infoValue)), features(featuresValue)
			{
			}

			std::uintptr_t GetNativeHandle() const override { return reinterpret_cast<std::uintptr_t>(adapter.Get()); }

			const Rhi::AdapterInfo& GetInfo() const override { return info; }

			std::unique_ptr<Rhi::Device> CreateDevice() override { return CreateD3D12Device(system, adapter, info, features); }

		  private:
			std::shared_ptr<D3D12SystemState> system;
			ComPtr<IDXGIAdapter4> adapter;
			Rhi::AdapterInfo info;
			D3D12Features features;
		};

		class D3D12GraphicsSystem final : public Rhi::GraphicsSystem
		{

		  public:
			D3D12GraphicsSystem(std::shared_ptr<D3D12SystemState> systemValue, std::vector<std::unique_ptr<D3D12Adapter>> adaptersValue)
				: system(std::move(systemValue)), adapters(std::move(adaptersValue))
			{
			}

			bool IsValidationEnabled() const override { return system->Validation; }

			Rhi::ValidationConfiguration GetValidationConfiguration() const override { return { system->Validation, system->Checks }; }

			std::shared_ptr<Rhi::DiagnosticLog> GetDiagnostics() const override { return system->Log; }

			std::uint32_t GetAdapterCount() const override { return static_cast<std::uint32_t>(adapters.size()); }

			Rhi::Adapter& GetAdapter(std::uint32_t adapterIndex) override { return *adapters.at(adapterIndex); }

		  private:
			std::shared_ptr<D3D12SystemState> system;
			std::vector<std::unique_ptr<D3D12Adapter>> adapters;
		};

		bool WantsValidation(Rhi::ValidationMode mode)
		{
#if defined(_DEBUG)
			return mode != Rhi::ValidationMode::Disabled;
#else
			return mode == Rhi::ValidationMode::IfAvailable || mode == Rhi::ValidationMode::Required;
#endif
		}

		// The debug layer (and GPU-based validation when asked) must be on before any device exists.
		bool EnableDebugLayer(const Rhi::GraphicsSystemDesc& desc)
		{
			ComPtr<ID3D12Debug> debug;

			if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
			{
				return false;
			}

			debug->EnableDebugLayer();
			ComPtr<ID3D12Debug1> debug1;

			if (SUCCEEDED(debug.As(&debug1)))
			{
				debug1->SetEnableGPUBasedValidation(desc.Checks.GpuAssisted ? TRUE : FALSE);
				debug1->SetEnableSynchronizedCommandQueueValidation(desc.Checks.Synchronization ? TRUE : FALSE);
			}

			return true;
		}

		// Device-removed breadcrumbs and page-fault reports, with validation or when
		// SWIM_D3D12_DRED=1 asks for them without it (they cost a little GPU time).
		void EnableRemovalReports(bool validation)
		{
			const char* asked = std::getenv("SWIM_D3D12_DRED");

			if (!validation && (asked == nullptr || std::strcmp(asked, "1") != 0))
			{
				return;
			}

			ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dred;

			if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dred))))
			{
				dred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
				dred->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
			}

			// Debug-label names beside the breadcrumbs: the report names the pass.
			ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> contexts;

			if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&contexts))))
			{
				contexts->SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
			}
		}

		Rhi::AdapterInfo DescribeAdapter(IDXGIAdapter4* adapter, ID3D12Device* device, const D3D12Features& features)
		{
			DXGI_ADAPTER_DESC3 desc{};
			adapter->GetDesc3(&desc);
			Rhi::AdapterInfo info;
			info.Api = Rhi::GraphicsApi::D3D12;
			info.Name = Narrow(desc.Description);
			info.DriverName = "D3D12";
			info.VendorId = desc.VendorId;
			info.DeviceId = desc.DeviceId;
			info.DedicatedVideoMemory = desc.DedicatedVideoMemory;
			LARGE_INTEGER umd{};

			if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd)))
			{
				const auto v = static_cast<std::uint64_t>(umd.QuadPart);
				info.DriverVersion = static_cast<std::uint32_t>(v);
				info.DriverInfo = std::to_string(v >> 48) + "." + std::to_string((v >> 32) & 0xffff) + "." +
					std::to_string((v >> 16) & 0xffff) + "." + std::to_string(v & 0xffff);
			}

			info.ApiVersion = "12 (" + DescribeFeatures(features) + ")";
			D3D12_FEATURE_DATA_ARCHITECTURE1 architecture{};
			const bool uma = SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE1, &architecture, sizeof(architecture))) &&
				architecture.UMA;
			info.Integrated = uma;
			info.Discrete = !uma;
			std::memcpy(info.Luid.data(), &desc.AdapterLuid, info.Luid.size());
			info.LuidValid = true;
			info.Capabilities = ToCapabilities(features, device);
			return info;
		}

	} // namespace

	std::unique_ptr<Rhi::GraphicsSystem> CreateGraphicsSystem(const Rhi::GraphicsSystemDesc& desc)
	{
		if (desc.Validation == Rhi::ValidationMode::Disabled && desc.Checks.Any())
		{
			throw std::invalid_argument("D3D12: validation checks need validation enabled");
		}

		auto system = std::make_shared<D3D12SystemState>();
		system->Log = desc.Diagnostics ? desc.Diagnostics : std::make_shared<Rhi::DiagnosticLog>();
		system->Echo = desc.EchoDiagnostics;
		system->Validation = (WantsValidation(desc.Validation) || desc.Checks.Any()) && EnableDebugLayer(desc);
		EnableRemovalReports(system->Validation);
		system->Checks = system->Validation ? desc.Checks : Rhi::ValidationChecks{};

		if (desc.Validation == Rhi::ValidationMode::Required && !system->Validation)
		{
			throw std::runtime_error("D3D12: the debug layer is required but not installed (Graphics Tools)");
		}

		if (FAILED(CreateDXGIFactory2(system->Validation ? DXGI_CREATE_FACTORY_DEBUG : 0u, IID_PPV_ARGS(&system->Factory))))
		{
			return nullptr;
		}

		std::vector<std::unique_ptr<D3D12Adapter>> adapters;
		ComPtr<IDXGIAdapter4> adapter;

		// High-performance order puts the discrete GPU of a hybrid laptop first.
		for (UINT index = 0; system->Factory->EnumAdapterByGpuPreference(
								 index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND;
			++index)
		{
			DXGI_ADAPTER_DESC3 desc3{};
			ComPtr<ID3D12Device> probe;

			if (!adapter || FAILED(adapter->GetDesc3(&desc3)) || (desc3.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE) ||
				FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&probe))))
			{
				continue;
			}

			const auto features = QueryFeatures(probe.Get(), system->Factory.Get());
			auto info = DescribeAdapter(adapter.Get(), probe.Get(), features);
			adapters.push_back(std::make_unique<D3D12Adapter>(system, adapter, std::move(info), features));
		}

		return std::make_unique<D3D12GraphicsSystem>(system, std::move(adapters));
	}

	bool RegisterGraphicsBackend(Rhi::GraphicsFactory& factory)
	{
		return factory.Register(Rhi::GraphicsApi::D3D12, &CreateGraphicsSystem);
	}

} // namespace Swim::RhiD3D12
