#pragma once
#include <wrl/client.h>
#include <d3d12.h>
#include <string>

using Microsoft::WRL::ComPtr;

namespace vrperfkit {
	void CheckResult(const std::string &action, HRESULT result);

	ComPtr<D3D12_SHADER_RESOURCE_VIEW_DESC> CreateShaderResourceView(ID3D12Device *device, ID3D12Resource *texture, int arrayIndex = 0); 
	ComPtr<D3D12_UNORDERED_ACCESS_VIEW_DESC> CreateUnorderedAccessView(ID3D12Device *device, ID3D12Resource *texture, int arrayIndex = 0);
	ComPtr<ID3D12Resource> CreateResolveTexture(ID3D12Device *device, ID3D12Resource *texture, DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN);
	ComPtr<ID3D12Resource> CreatePostProcessTexture(ID3D12Device *device, uint32_t width, uint32_t height, DXGI_FORMAT format);
	ComPtr<ID3D12Resource> CreateConstantsBuffer(ID3D12Device *device, uint32_t size);
	ComPtr<D3D12_STATIC_SAMPLER_DESC> CreateLinearSampler(ID3D12Device *device);

	DXGI_FORMAT TranslateTypelessFormats(DXGI_FORMAT format);
	DXGI_FORMAT MakeSrgbFormatsTypeless(DXGI_FORMAT format);
	bool IsSrgbFormat(DXGI_FORMAT format);

	struct D3D12State {
		ComPtr<vertex_shader> vertexShader;
		ComPtr<ID3D12PixelShader> pixelShader;
		ComPtr<ID3D12ComputeShader> computeShader;
		ComPtr<ID3D12InputLayout> inputLayout;
		D3D12_PRIMITIVE_TOPOLOGY topology;
		ID3D12Resource *vertexBuffers[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
		UINT strides[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
		UINT offsets[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
		ComPtr<ID3D12Resource> indexBuffer;
		DXGI_FORMAT format;
		UINT offset;
		ID3D12RenderTargetView *renderTargets[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT];
		ComPtr<ID3D12DepthStencilView> depthStencil;
		ComPtr<ID3D12RasterizerState> rasterizerState;
		ComPtr<ID3D12DepthStencilState> depthStencilState;
		UINT stencilRef;
		D3D12_VIEWPORT viewports[D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
		UINT numViewports = 0;
		ComPtr<ID3D12Resource> vsConstantBuffer;
		ComPtr<ID3D12Resource> psConstantBuffer;
		ComPtr<ID3D12Resource> csConstantBuffer;
		ID3D12ShaderResourceView *csShaderResources[D3D12_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
		ID3D12UnorderedAccessView *csUavs[D3D12_1_UAV_SLOT_COUNT];
	};

	void StoreD3D12State(ID3D12DeviceContext *context, D3D12State &state);
	void RestoreD3D12State(ID3D12DeviceContext *context, const D3D12State &state);
}
