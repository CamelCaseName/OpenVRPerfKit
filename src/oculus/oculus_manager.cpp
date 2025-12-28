#include "oculus_manager.h"

#include "hotkeys.h"
#include "logging.h"
#include "resolution_scaling.h"
#include "d3d12/d3d12_helper.h"
#include "d3d12/d3d12_injector.h"
#include "d3d12/d3d12_post_processor.h"
#include "d3d12/d3d12_variable_rate_shading.h"

#include <wrl/client.h>
#include <d3d12.h>
#include <OVR_CAPI_D3D.h>

using Microsoft::WRL::ComPtr;

namespace vrperfkit {
	namespace {
		void Check(const std::string &action, ovrResult result) {
			if (OVR_FAILURE(result)) {
				ovrErrorInfo info;
				ovr_GetLastErrorInfo(&info);
				std::string message = "Failed " + action + ": " + info.ErrorString + " (" + std::to_string(result) + ")";
				throw std::exception(message.c_str());
			}
		}

		ovrTextureFormat DetermineOutputFormat(const ovrTextureSwapChainDesc &desc) {
			if (desc.MiscFlags & ovrTextureMisc_DX_Typeless) {
				// if the incoming texture is physically in a typeless state, then we don't need to care
				// about whether or not it's SRGB
				return desc.Format;
			}

			// if the texture is not typeless, then if it is SRGB, applying upscaling will automatically unwrap
			// the SRGB values in our shader and thus produce non-SRGB values, so we need to use a non-SRGB
			// output format in these instances
			switch (desc.Format) {
			case OVR_FORMAT_B8G8R8A8_UNORM_SRGB:
				return OVR_FORMAT_B8G8R8A8_UNORM;
			case OVR_FORMAT_B8G8R8X8_UNORM_SRGB:
				return OVR_FORMAT_B8G8R8X8_UNORM;
			case OVR_FORMAT_R8G8B8A8_UNORM_SRGB:
				return OVR_FORMAT_R8G8B8A8_UNORM;
			default:
				return desc.Format;
			}
		}

		bool ShouldCreateTypelessSwapchain(ovrTextureFormat format) {
			switch (format) {
			case OVR_FORMAT_B8G8R8A8_UNORM_SRGB:
			case OVR_FORMAT_B8G8R8A8_UNORM:
			case OVR_FORMAT_B8G8R8X8_UNORM_SRGB:
			case OVR_FORMAT_B8G8R8X8_UNORM:
			case OVR_FORMAT_R8G8B8A8_UNORM_SRGB:
			case OVR_FORMAT_R8G8B8A8_UNORM:
				return true;
			default:
				return false;
			}
		}
	}

	OculusManager g_oculus;

	struct OculusD3D12Resources {
		std::unique_ptr<D3D12Injector> injector;
		std::unique_ptr<D3D12VariableRateShading> variableRateShading;
		std::unique_ptr<D3D12PostProcessor> postProcessor;
		ComPtr<ID3D12Device> device;
		ComPtr<ID3D12DeviceContext> context;
		std::vector<ComPtr<ID3D12Resource>> submittedTextures[2];
		ComPtr<ID3D12Resource> resolveTexture[2];
		std::vector<ComPtr<ID3D12ShaderResourceView>> submittedViews[2];
		std::vector<ComPtr<ID3D12Resource>> outputTextures[2];
		std::vector<ComPtr<ID3D12ShaderResourceView>> outputViews[2];
		std::vector<ComPtr<ID3D12UnorderedAccessView>> outputUavs[2];
		bool multisampled[2];
		bool usingArrayTex;
	};

	void OculusManager::Init(ovrSession session, ovrTextureSwapChain leftEyeChain, ovrTextureSwapChain rightEyeChain) {
		this->session = session;
		submittedEyeChains[0] = leftEyeChain;
		submittedEyeChains[1] = rightEyeChain;

		LOG_INFO << "Initializing Oculus frame submission...";

		try {
			// determine which graphics API the swapchains are created with
			ComPtr<ID3D12Resource> d3d12Tex;

			ovrResult result = ovr_GetTextureSwapChainBufferDX(session, leftEyeChain, 0, IID_PPV_ARGS(d3d12Tex.GetAddressOf()));
			if (OVR_SUCCESS(result)) {
				InitD3D12();
			}
		}
		catch (const std::exception &e) {
			LOG_ERROR << "Failed to create graphics resources: " << e.what();
		}

		if (!initialized) {
			LOG_ERROR << "Could not initialize graphics resources; game may be using an unsupported graphics API";
			Shutdown();
			failed = true;
		}

		FlushLog();
	}

	void OculusManager::Shutdown() {
		initialized = false;
		failed = false;
		graphicsApi = GraphicsApi::UNKNOWN;
		d3d12Res.reset();
		for (int i = 0; i < 2; ++i) {
			if (outputEyeChains[i] != nullptr) {
				ovr_DestroyTextureSwapChain(session, outputEyeChains[i]);
			}
			submittedEyeChains[i] = nullptr;
			outputEyeChains[i] = nullptr;
		}
		session = nullptr;
	}

	void OculusManager::EnsureInit(ovrSession session, ovrTextureSwapChain leftEyeChain, ovrTextureSwapChain rightEyeChain) {
		if (!initialized || session != this->session || leftEyeChain != submittedEyeChains[0] || rightEyeChain != submittedEyeChains[1]) {
			Shutdown();
			Init(session, leftEyeChain, rightEyeChain);
		}
	}

	void OculusManager::OnFrameSubmission(ovrSession session, ovrLayerEyeFovDepth &eyeLayer) {
		if (failed || session == nullptr || eyeLayer.ColorTexture[0] == nullptr) {
			return;
		}
		EnsureInit(session, eyeLayer.ColorTexture[0], eyeLayer.ColorTexture[1]);
		if (failed) {
			return;
		}

		try {
			if (graphicsApi == GraphicsApi::D3D12) {
				PostProcessD3D12(eyeLayer);
			}

			CheckHotkeys();
		}
		catch (const std::exception &e) {
			LOG_ERROR << "Failed during post processing: " << e.what();
			Shutdown();
			failed = true;
		}
	}

	ProjectionCenters OculusManager::CalculateProjectionCenter(const ovrFovPort *fov) {
		ProjectionCenters projCenters;
		for (int eye = 0; eye < 2; ++eye) {
			projCenters.eyeCenter[eye].x = 0.5f * (1.f + (fov[eye].LeftTan - fov[eye].RightTan) / (fov[eye].RightTan + fov[eye].LeftTan));
			projCenters.eyeCenter[eye].y = 0.5f * (1.f + (fov[eye].DownTan - fov[eye].UpTan) / (fov[eye].DownTan + fov[eye].UpTan));
		}

		d3d12Res->postProcessor.get()->SetProjCenters(projCenters.eyeCenter[0].x, projCenters.eyeCenter[0].y, projCenters.eyeCenter[1].x, projCenters.eyeCenter[1].y);
		
		return projCenters;
	}

	void OculusManager::InitD3D12() {
		LOG_INFO << "Game is using D3D12 swapchains, initializing D3D12 resources";
		graphicsApi = GraphicsApi::D3D12;
		d3d12Res.reset(new OculusD3D12Resources);

		for (int eye = 0; eye < 2; ++eye) {
			d3d12Res->multisampled[eye] = false;
			if (submittedEyeChains[eye] == nullptr || (eye == 1 && submittedEyeChains[1] == submittedEyeChains[0]))
				continue;

			int length = 0;
			Check("getting texture swapchain length", ovr_GetTextureSwapChainLength(session, submittedEyeChains[eye], &length));
			for (int i = 0; i < length; ++i) {
				ComPtr<ID3D12Resource> texture;
				Check("getting swapchain texture", ovr_GetTextureSwapChainBufferDX(session, submittedEyeChains[eye], i, IID_PPV_ARGS(texture.GetAddressOf())));
				d3d12Res->submittedTextures[eye].push_back(texture);
			}
			d3d12Res->submittedTextures[eye][0]->GetDevice(d3d12Res->device.ReleaseAndGetAddressOf());
			d3d12Res->device->GetImmediateContext(d3d12Res->context.ReleaseAndGetAddressOf());

			ovrTextureSwapChainDesc chainDesc;
			Check("getting swapchain description", ovr_GetTextureSwapChainDesc(session, submittedEyeChains[eye], &chainDesc));
			LOG_INFO << "Swap chain has format " << chainDesc.Format << ", bind flags " << chainDesc.BindFlags << " and misc flags " << chainDesc.MiscFlags;
			ovrTextureFormat outputFormat = DetermineOutputFormat(chainDesc);
			if (chainDesc.SampleCount > 1) {
				LOG_INFO << "Submitted textures are multi-sampled, creating resolve texture";
				d3d12Res->resolveTexture[eye] = CreateResolveTexture(d3d12Res->device.Get(), d3d12Res->submittedTextures[0][0].Get());
				d3d12Res->multisampled[eye] = true;
			}

			for (int i = 0; i < length; ++i) {
				auto view = CreateShaderResourceView(d3d12Res->device.Get(), 
					chainDesc.SampleCount > 1 
						? d3d12Res->resolveTexture[eye].Get()
						: d3d12Res->submittedTextures[eye][i].Get());
				d3d12Res->submittedViews[eye].push_back(view);
			}

			chainDesc.SampleCount = 1;
			chainDesc.MipLevels = 1;
			chainDesc.BindFlags = ovrTextureBind_DX_UnorderedAccess;
			chainDesc.MiscFlags = ovrTextureMisc_AutoGenerateMips;
			if (ShouldCreateTypelessSwapchain(outputFormat)) {
				chainDesc.MiscFlags = chainDesc.MiscFlags | ovrTextureMisc_DX_Typeless;
			}
			chainDesc.Format = outputFormat;
			chainDesc.StaticImage = false;
			LOG_INFO << "Eye " << eye << ": submitted textures have resolution " << chainDesc.Width << "x" << chainDesc.Height;
			AdjustOutputResolution(chainDesc.Width, chainDesc.Height);
			LOG_INFO << "Eye " << eye << ": output resolution is " << chainDesc.Width << "x" << chainDesc.Height;
			LOG_INFO << "Creating output swapchain in format " << chainDesc.Format;
			Check("creating output swapchain", ovr_CreateTextureSwapChainDX(session, d3d12Res->device.Get(), &chainDesc, &outputEyeChains[eye]));

			Check("getting texture swapchain length", ovr_GetTextureSwapChainLength(session, outputEyeChains[eye], &length));
			for (int i = 0; i < length; ++i) {
				ComPtr<ID3D12Resource> texture;
				Check("getting swapchain texture", ovr_GetTextureSwapChainBufferDX(session, outputEyeChains[eye], i, IID_PPV_ARGS(texture.GetAddressOf())));
				d3d12Res->outputTextures[eye].push_back(texture);

				auto view = CreateShaderResourceView(d3d12Res->device.Get(), texture.Get());
				d3d12Res->outputViews[eye].push_back(view);

				auto uav = CreateUnorderedAccessView(d3d12Res->device.Get(), texture.Get());
				d3d12Res->outputUavs[eye].push_back(uav);
			}
		}

		d3d12Res->usingArrayTex = false;

		if (outputEyeChains[1] == nullptr) {
			outputEyeChains[1] = outputEyeChains[0];
			LOG_INFO << "Game is using a single texture for both eyes";
			d3d12Res->submittedTextures[1] = d3d12Res->submittedTextures[0];
			d3d12Res->resolveTexture[1] = d3d12Res->resolveTexture[0];
			d3d12Res->outputTextures[1] = d3d12Res->outputTextures[0];
			ovrTextureSwapChainDesc chainDesc;
			Check("getting swapchain description", ovr_GetTextureSwapChainDesc(session, submittedEyeChains[0], &chainDesc));
			if (chainDesc.ArraySize == 1) {
				d3d12Res->submittedViews[1] = d3d12Res->submittedViews[0];
				d3d12Res->outputViews[1] = d3d12Res->outputViews[0];
				d3d12Res->outputUavs[1] = d3d12Res->outputUavs[0];
			}
			else {
				LOG_INFO << "Game is using an array texture";
				d3d12Res->usingArrayTex = true;
				for (auto tex : d3d12Res->submittedTextures[0]) {
					auto view = CreateShaderResourceView(d3d12Res->device.Get(), tex.Get(), 1);
					d3d12Res->submittedViews[1].push_back(view);
				}
				for (auto tex : d3d12Res->outputTextures[0]) {
					auto resolvedTex = d3d12Res->resolveTexture[1] != nullptr ? d3d12Res->resolveTexture[1] : tex;
					auto view = CreateShaderResourceView(d3d12Res->device.Get(), resolvedTex.Get(), 1);
					d3d12Res->outputViews[1].push_back(view);
					auto uav = CreateUnorderedAccessView(d3d12Res->device.Get(), resolvedTex.Get(), 1);
					d3d12Res->outputUavs[1].push_back(uav);
				}
			}
		}

		d3d12Res->postProcessor.reset(new D3D12PostProcessor(d3d12Res->device));
		d3d12Res->variableRateShading.reset(new D3D12VariableRateShading(d3d12Res->device));
		d3d12Res->injector.reset(new D3D12Injector(d3d12Res->device));
		d3d12Res->injector->AddListener(d3d12Res->postProcessor.get());
		d3d12Res->injector->AddListener(d3d12Res->variableRateShading.get());

		LOG_INFO << "D3D12 resource creation complete";
		initialized = true;
	}

	void OculusManager::PostProcessD3D12(ovrLayerEyeFovDepth &eyeLayer) {
		auto projCenters = CalculateProjectionCenter(eyeLayer.Fov);
		bool successfulPostprocessing = false;
		bool isFlippedY = eyeLayer.Header.Flags & ovrLayerFlag_TextureOriginAtBottomLeft;

		for (int eye = 0; eye < 2; ++eye) {
			int index;
			ovrTextureSwapChain curSwapChain = submittedEyeChains[eye] != nullptr ? submittedEyeChains[eye] : submittedEyeChains[0];
			Check("getting current swapchain index", ovr_GetTextureSwapChainCurrentIndex(session, curSwapChain, &index));
			// since the current submitted texture has already been committed, the index will point past the current texture
			index = (index - 1 + d3d12Res->submittedTextures[eye].size()) % d3d12Res->submittedTextures[eye].size();

			// if the incoming texture is multi-sampled, we need to resolve it before we can post-process it
			if (d3d12Res->multisampled[eye]) {
				if (d3d12Res->usingArrayTex || submittedEyeChains[eye] != nullptr) {
					D3D12_TEXTURE2D_DESC td;
					d3d12Res->submittedTextures[eye][index]->GetDesc(&td);
					d3d12Res->context->ResolveSubresource(
						d3d12Res->resolveTexture[eye].Get(),
						D3D12CalcSubresource(0, d3d12Res->usingArrayTex ? eye : 0, 1),
						d3d12Res->submittedTextures[eye][index].Get(),
						D3D12CalcSubresource(0, d3d12Res->usingArrayTex ? eye : 0, td.MipLevels),
						TranslateTypelessFormats(td.Format));
				}
			}

			int outIndex = 0;
			ovr_GetTextureSwapChainCurrentIndex(session, outputEyeChains[eye], &outIndex);

			D3D12PostProcessInput input;
			input.inputTexture = d3d12Res->submittedTextures[eye][index].Get();
			input.inputView = d3d12Res->submittedViews[eye][index].Get();
			input.outputTexture = d3d12Res->outputTextures[eye][outIndex].Get();
			input.outputView = d3d12Res->outputViews[eye][outIndex].Get();
			input.outputUav = d3d12Res->outputUavs[eye][outIndex].Get();
			input.inputViewport.x = eyeLayer.Viewport[eye].Pos.x;
			input.inputViewport.y = eyeLayer.Viewport[eye].Pos.y;
			input.inputViewport.width = eyeLayer.Viewport[eye].Size.w;
			input.inputViewport.height = eyeLayer.Viewport[eye].Size.h;
			input.eye = eye;
			input.projectionCenter = projCenters.eyeCenter[eye];

			if (isFlippedY) {
				input.projectionCenter.y = 1.f - input.projectionCenter.y;
			}

			if (submittedEyeChains[1] == nullptr || submittedEyeChains[1] == submittedEyeChains[0]) {
				if (d3d12Res->usingArrayTex) {
					input.mode = TextureMode::ARRAY;
				} else {
					input.mode = TextureMode::COMBINED;
				}
			} else {
				input.mode = TextureMode::SINGLE;
			}

			Viewport outputViewport;
			if (d3d12Res->postProcessor->Apply(input, outputViewport)) {
				eyeLayer.ColorTexture[eye] = outputEyeChains[eye];
				eyeLayer.Viewport[eye].Pos.x = outputViewport.x;
				eyeLayer.Viewport[eye].Pos.y = outputViewport.y;
				eyeLayer.Viewport[eye].Size.w = outputViewport.width;
				eyeLayer.Viewport[eye].Size.h = outputViewport.height;
				successfulPostprocessing = true;
			}

			D3D12_TEXTURE2D_DESC td;
			input.inputTexture->GetDesc(&td);
			float projLX = projCenters.eyeCenter[0].x;
			float projLY = isFlippedY ? 1.f - projCenters.eyeCenter[0].y : projCenters.eyeCenter[0].y;
			float projRX = projCenters.eyeCenter[1].x;
			float projRY = isFlippedY ? 1.f - projCenters.eyeCenter[1].y : projCenters.eyeCenter[1].y;
			d3d12Res->variableRateShading->UpdateTargetInformation(td.Width, td.Height, input.mode, projLX, projLY, projRX, projRY);
		}

		d3d12Res->variableRateShading->EndFrame();

		if (successfulPostprocessing) {
			ovr_CommitTextureSwapChain(session, outputEyeChains[0]);
			if (outputEyeChains[1] != outputEyeChains[0]) {
				ovr_CommitTextureSwapChain(session, outputEyeChains[1]);
			}
		}
	}
}
