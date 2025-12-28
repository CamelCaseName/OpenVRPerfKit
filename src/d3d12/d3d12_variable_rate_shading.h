#pragma once
#define NOMINMAX
#include "d3d12_injector.h"
#include <d3d12.h>
#include <wrl/client.h>
#include "nvapi.h"
#include "types.h"

namespace vrperfkit {
	using Microsoft::WRL::ComPtr;

	class D3D12VariableRateShading : public vrperfkit::D3D12Listener {
	public:
		D3D12VariableRateShading(ComPtr<ID3D12Device> device);
		~D3D12VariableRateShading() { Shutdown(); }

		void UpdateTargetInformation(int targetWidth, int targetHeight, TextureMode mode, float leftProjX, float leftProjY, float rightProjX, float rightProjY);
		void EndFrame();

		void PostOMSetRenderTargets(UINT numViews, ID3D12RenderTargetView * const *renderTargetViews, ID3D12DepthStencilView *depthStencilView) override;

	private:
		bool nvapiLoaded = false;
		bool active = false;

		int ignoreFirstTargetRenders = 0;
		int ignoreLastTargetRenders = 0;
		int renderOnlyTarget = 0;

		int targetWidth = 1000000;
		int targetHeight = 1000000;
		TextureMode targetMode = TextureMode::SINGLE;
		float proj[2][2] = { 0, 0, 0, 0 };

		ComPtr<ID3D12Device> device;
		ComPtr<ID3D12DeviceContext> context;
		int singleWidth[2] = { 0, 0 };
		int singleHeight[2] = { 0, 0 };
		ComPtr<ID3D12Resource> singleEyeVRSTex[2];
		ComPtr<ID3D12NvShadingRateResourceView> singleEyeVRSView[2];
		std::string singleEyeOrder;
		int currentSingleEyeRT = 0;
		int combinedWidth = 0;
		int combinedHeight = 0;
		ComPtr<ID3D12Resource> combinedVRSTex;
		ComPtr<ID3D12NvShadingRateResourceView> combinedVRSView;
		int arrayWidth = 0;
		int arrayHeight = 0;
		ComPtr<ID3D12Resource> arrayVRSTex;
		ComPtr<ID3D12NvShadingRateResourceView> arrayVRSView;

		void Shutdown();

		void EnableVRS();
		void DisableVRS();

		void ApplyCombinedVRS(int width, int height);
		void ApplyArrayVRS(int width, int height);
		void ApplySingleEyeVRS(int eye, int width, int height);

		void SetupSingleEyeVRS(int eye, int width, int height, float projX, float projY);
		void SetupCombinedVRS(int width, int height, float leftProjX, float leftProjY, float rightProjX, float rightProjY);
		void SetupArrayVRS(int width, int height, float leftProjX, float leftProjY, float rightProjX, float rightProjY);
	};
}
