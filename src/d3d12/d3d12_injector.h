#pragma once
#include "d3d12_helper.h"

#include <vector>

namespace vrperfkit {
	class D3D12Listener {
	public:
		virtual bool PrePSSetSamplers(UINT startSlot, UINT numSamplers, ID3D12SamplerState *const *ppSamplers) { return false; }
		virtual void PostOMSetRenderTargets(UINT numViews, ID3D12RenderTargetView *const *renderTargetViews, ID3D12DepthStencilView *depthStencilView) {}
		
		virtual HRESULT ClearDepthStencilView(ID3D12DepthStencilView *pDepthStencilView, UINT ClearFlags, FLOAT Depth, UINT8 Stencil) { return 0; }

	protected:
		~D3D12Listener() = default;
	};

	class __declspec(uuid("c0d7b492-1bfb-4099-9c67-7144e1f586ed")) D3D12Injector {
	public:
		explicit D3D12Injector(ComPtr<ID3D12Device> device);
		~D3D12Injector();

		void AddListener(D3D12Listener *listener);
		void RemoveListener(D3D12Listener *listener);

		bool PrePSSetSamplers(UINT startSlot, UINT numSamplers, ID3D12SamplerState *const *ppSamplers);
		void PostOMSetRenderTargets(UINT numViews, ID3D12RenderTargetView *const *renderTargetViews, ID3D12DepthStencilView *depthStencilView);

		HRESULT ClearDepthStencilView(ID3D12DepthStencilView *pDepthStencilView, UINT ClearFlags, FLOAT Depth, UINT8 Stencil);

	private:
		ComPtr<ID3D12Device> device;
		ComPtr<ID3D12DeviceContext> context;

		std::vector<D3D12Listener*> listeners;
	};
}
