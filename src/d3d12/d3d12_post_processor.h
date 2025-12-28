#pragma once
#include "types.h"
#include "d3d12_helper.h"
#include "d3d12_injector.h"

#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "openvr.h"

namespace vrperfkit {
	struct D3D12PostProcessInput {
		ID3D12Resource *inputTexture;
		ID3D12Resource *outputTexture;
		ID3D12ShaderResourceView *inputView;
		ID3D12ShaderResourceView *outputView;
		ID3D12UnorderedAccessView *outputUav;
		Viewport inputViewport;
		int eye;
		TextureMode mode;
		Point<float> projectionCenter;
	};

	class D3D12Upscaler {
	public:
		virtual void Upscale(const D3D12PostProcessInput &input, const Viewport &outputViewport) = 0;
	};

	class D3D12PostProcessor : public D3D12Listener {
	public:
		D3D12PostProcessor(ComPtr<ID3D12Device> device);

		HRESULT ClearDepthStencilView(ID3D12DepthStencilView *pDepthStencilView, UINT ClearFlags, FLOAT Depth, UINT8 Stencil);

		bool Apply(const D3D12PostProcessInput &input, Viewport &outputViewport);

		bool PrePSSetSamplers(UINT startSlot, UINT numSamplers, ID3D12SamplerState * const *ppSamplers) override;

		void D3D12PostProcessor::SetProjCenters(float LX, float LY, float RX, float RY);

	private:
		ComPtr<ID3D12Device> device;
		ComPtr<ID3D12DeviceContext> context;
		std::unique_ptr<D3D12Upscaler> upscaler;
		UpscaleMethod upscaleMethod;

		void PrepareUpscaler(ID3D12Resource *outputTexture);

		std::unordered_set<ID3D12SamplerState*> passThroughSamplers;
		std::unordered_map<ID3D12SamplerState*, ComPtr<ID3D12SamplerState>> mappedSamplers;
		float mipLodBias = 0.0f;


		struct DynamicProfileQuery {
			ComPtr<ID3D12Query> queryDisjoint;
			ComPtr<ID3D12Query> queryStart;
			ComPtr<ID3D12Query> queryEnd;
		};
		FILETIME ft;
		unsigned int dynamicTimeUs = 0;
		int dynamicSleepCount = 0;
		bool is_DynamicProfiling = false;
		bool enableDynamic = false;
		bool hiddenMaskApply = false;
		bool is_rdm = false;
		bool preciseResolution = false;
		int ignoreFirstTargetRenders = 0;
		int ignoreLastTargetRenders = 0;
		int renderOnlyTarget = 0;

		void CreateDynamicProfileQueries();
		void StartDynamicProfiling();
		void EndDynamicProfiling();

		ComPtr<ID3D12Resource> copiedTexture;
		ComPtr<ID3D12ShaderResourceView> copiedTextureView;
		ComPtr<ID3D12SamplerState> sampler;
		bool hrmInitialized = false;
		uint32_t textureWidth = 0;
		uint32_t textureHeight = 0;
		bool requiresCopy = false;
		bool inputIsSrgb = false;
		ComPtr<ID3D12VertexShader> hrmFullTriVertexShader;
		ComPtr<ID3D12PixelShader> hrmMaskingShader;
		ComPtr<ID3D12PixelShader> rdmMaskingShader;
		ComPtr<ID3D12ComputeShader> rdmReconstructShader;
		ComPtr<ID3D12Resource> hrmMaskingConstantsBuffer[2];
		ComPtr<ID3D12Resource> rdmReconstructConstantsBuffer[2];
		ComPtr<ID3D12Resource> rdmReconstructedTexture;
		ComPtr<ID3D12UnorderedAccessView> rdmReconstructedUav;
		ComPtr<ID3D12ShaderResourceView> rdmReconstructedView;
		ComPtr<ID3D12DepthStencilState> hrmDepthStencilState;
		ComPtr<ID3D12RasterizerState> hrmRasterizerState;
		float projX[2];
		float projY[2];
		int depthClearCount = 0;
		int depthClearCountMax = 0;
		float edgeRadius = 1.15f;
		
		struct DepthStencilViews {
			ComPtr<ID3D12DepthStencilView> view[2];
		};
		std::unordered_map<ID3D12Resource*, DepthStencilViews> depthStencilViews;

		bool D3D12PostProcessor::HasBlacklistedTextureName(ID3D12Resource *tex);
		ID3D12DepthStencilView * D3D12PostProcessor::GetDepthStencilView(ID3D12Resource *depthStencilTex, vr::EVREye eye);
		void D3D12PostProcessor::PrepareResources(ID3D12Resource *inputTexture);
		void D3D12PostProcessor::PrepareCopyResources(DXGI_FORMAT format);
		void D3D12PostProcessor::PrepareRdmResources(DXGI_FORMAT format);
		void D3D12PostProcessor::ApplyRadialDensityMask(ID3D12Resource *depthStencilTex, float depth, uint8_t stencil);
		void D3D12PostProcessor::ReconstructRdmRender(const D3D12PostProcessInput &input);
	};
}
