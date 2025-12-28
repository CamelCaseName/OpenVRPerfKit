#pragma once
#include "d3d12_post_processor.h"

#include <d3d12.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace vrperfkit {
	class D3D12NisUpscaler : public D3D12Upscaler {
	public:
		D3D12NisUpscaler(ID3D12Device *device);
		void Upscale(const D3D12PostProcessInput &input, const Viewport &outputViewport) override;

	private:
		ComPtr<ID3D12DeviceContext> context;
		ComPtr<ID3D12ComputeShader> upscaleShader;
		ComPtr<ID3D12ComputeShader> sharpenShader;
		ComPtr<ID3D12Resource> constantsBuffer;
		ComPtr<ID3D12SamplerState> sampler;
		ComPtr<ID3D12Resource> scalerCoeffTexture;
		ComPtr<ID3D12ShaderResourceView> scalerCoeffView;
		ComPtr<ID3D12Resource> usmCoeffTexture;
		ComPtr<ID3D12ShaderResourceView> usmCoeffView;
	};
}
