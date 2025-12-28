#include "d3d12_injector.h"
#include "hooks.h"

#include "config.h"

namespace vrperfkit {
	namespace {
		bool alreadyInsideHook = false;

		class HookGuard {
		public:
			HookGuard() {
				state = alreadyInsideHook;
				alreadyInsideHook = true;
			}

			~HookGuard() {
				alreadyInsideHook = state;
			}

			const bool AlreadyInsideHook() const { return state; }

		private:
			bool state;
		};

		template<typename T>
		D3D12Injector *GetInjector(T *object) {
			D3D12Injector *injector = nullptr;
			UINT size = sizeof(injector);
			object->GetPrivateData(__uuidof(D3D12Injector), &size, &injector);
			return injector;
		}

		void D3D12ContextHook_PSSetSamplers(ID3D12DeviceContext *self, UINT StartSlot, UINT NumSamplers, ID3D12SamplerState * const *ppSamplers) {
			HookGuard hookGuard;

			D3D12Injector *injector = GetInjector(self);
			if (injector != nullptr && !hookGuard.AlreadyInsideHook()) {
				if (injector->PrePSSetSamplers(StartSlot, NumSamplers, ppSamplers)) {
					return;
				}
			}

			hooks::CallOriginal(D3D12ContextHook_PSSetSamplers)(self, StartSlot, NumSamplers, ppSamplers);
		}

		void D3D12ContextHook_OMSetRenderTargets(
				ID3D12DeviceContext *self,
				UINT NumViews, ID3D12RenderTargetView * const *ppRenderTargetViews,
				ID3D12DepthStencilView *pDepthStencilView) {
			HookGuard hookGuard;

			hooks::CallOriginal(D3D12ContextHook_OMSetRenderTargets)(self, NumViews, ppRenderTargetViews, pDepthStencilView);

			if (D3D12Injector *injector = GetInjector(self)) {
				injector->PostOMSetRenderTargets(NumViews, ppRenderTargetViews, pDepthStencilView);
			}
		}

		void D3D12ContextHook_OMSetRenderTargetsAndUnorderedAccessViews(
				ID3D12DeviceContext *self,
				UINT NumRTVs,
				ID3D12RenderTargetView * const *ppRenderTargetViews,
				ID3D12DepthStencilView *pDepthStencilView,
				UINT UAVStartSlot,
				UINT NumUAVs,
				ID3D12UnorderedAccessView * const *ppUnorderedAccessViews,
				const UINT *pUAVInitialCounts) {
			HookGuard hookGuard;

			hooks::CallOriginal(D3D12ContextHook_OMSetRenderTargetsAndUnorderedAccessViews)(self, NumRTVs, ppRenderTargetViews, pDepthStencilView, UAVStartSlot, NumUAVs, ppUnorderedAccessViews, pUAVInitialCounts);

			if (D3D12Injector *injector = GetInjector(self)) {
				injector->PostOMSetRenderTargets(NumRTVs, ppRenderTargetViews, pDepthStencilView);
			}
		}

		void D3D12ContextHook_ClearDepthStencilView(
				ID3D12DeviceContext *self,
				ID3D12DepthStencilView *pDepthStencilView,
				UINT ClearFlags,
				FLOAT Depth,
				UINT8 Stencil) {
			HookGuard hookGuard;

			hooks::CallOriginal(D3D12ContextHook_ClearDepthStencilView)(self, pDepthStencilView, ClearFlags, Depth, Stencil);

			if (D3D12Injector *injector = GetInjector(self)) {
				injector->ClearDepthStencilView(pDepthStencilView, ClearFlags, Depth, Stencil);
			}
		}
	}

	D3D12Injector::D3D12Injector(ComPtr<ID3D12Device> device) {
		this->device = device;
		device->GetImmediateContext(context.GetAddressOf());

		D3D12Injector *instance = this;
		UINT size = sizeof(instance);
		device->SetPrivateData(__uuidof(D3D12Injector), size, &instance);
		context->SetPrivateData(__uuidof(D3D12Injector), size, &instance);

		// Upscaling and FFR
		if (g_config.upscaling.enabled || (g_config.ffr.enabled && g_config.ffr.method == FixedFoveatedMethod::VRS)) {
			hooks::InstallVirtualFunctionHook("ID3D12DeviceContext::PSSetSamplers", context.Get(), 10, (void*)&D3D12ContextHook_PSSetSamplers);
			hooks::InstallVirtualFunctionHook("ID3D12DeviceContext::OMSetRenderTargets", context.Get(), 33, (void*)&D3D12ContextHook_OMSetRenderTargets);
			hooks::InstallVirtualFunctionHook("ID3D12DeviceContext::OMSetRenderTargetsAndUnorderedAccessViews", context.Get(), 34, (void*)&D3D12ContextHook_OMSetRenderTargetsAndUnorderedAccessViews);
		}

		// HRM
		if (g_config.hiddenMask.enabled || (g_config.ffr.enabled && g_config.ffr.method == FixedFoveatedMethod::RDM)) {
			hooks::InstallVirtualFunctionHook("ID3D12DeviceContext::ClearDepthStencilView", context.Get(), 53, (void*)&D3D12ContextHook_ClearDepthStencilView);
		}
	}

	D3D12Injector::~D3D12Injector() {
		// Upscaling && FFR
		if (g_config.upscaling.enabled || (g_config.ffr.enabled && g_config.ffr.method == FixedFoveatedMethod::VRS)) {
			hooks::RemoveHook((void*)&D3D12ContextHook_PSSetSamplers);
			hooks::RemoveHook((void*)&D3D12ContextHook_OMSetRenderTargets);
			hooks::RemoveHook((void*)&D3D12ContextHook_OMSetRenderTargetsAndUnorderedAccessViews);
		}
		
		// HRM
		if (g_config.hiddenMask.enabled || (g_config.ffr.enabled && g_config.ffr.method == FixedFoveatedMethod::RDM)) {
			hooks::RemoveHook((void*)&D3D12ContextHook_ClearDepthStencilView);
		}

		device->SetPrivateData(__uuidof(D3D12Injector), 0, nullptr);
		context->SetPrivateData(__uuidof(D3D12Injector), 0, nullptr);
	}

	void D3D12Injector::AddListener(D3D12Listener *listener) {
		if (std::find(listeners.begin(), listeners.end(), listener) == listeners.end()) {
			listeners.push_back(listener);
		}
	}

	void D3D12Injector::RemoveListener(D3D12Listener *listener) {
		auto it = std::find(listeners.begin(), listeners.end(), listener);
		if (it != listeners.end()) {
			listeners.erase(it);
		}
	}

	bool D3D12Injector::PrePSSetSamplers(UINT startSlot, UINT numSamplers, ID3D12SamplerState * const *ppSamplers) {
		for (D3D12Listener *listener : listeners) {
			if (listener->PrePSSetSamplers(startSlot, numSamplers, ppSamplers)) {
				return true;
			}
		}

		return false;
	}

	void D3D12Injector::PostOMSetRenderTargets(UINT numViews, ID3D12RenderTargetView *const *renderTargetViews, ID3D12DepthStencilView *depthStencilView) {
		for (D3D12Listener *listener : listeners) {
			listener->PostOMSetRenderTargets(numViews, renderTargetViews, depthStencilView);
		}
	}

	HRESULT D3D12Injector::ClearDepthStencilView(ID3D12DepthStencilView *pDepthStencilView, UINT ClearFlags, FLOAT Depth, UINT8 Stencil) {
		if (ClearFlags & D3D12_CLEAR_DEPTH) {
			for (D3D12Listener * listener : listeners) {
				listener->ClearDepthStencilView(pDepthStencilView, ClearFlags, Depth, Stencil);
			}
		}

		return 0;
	}
}
