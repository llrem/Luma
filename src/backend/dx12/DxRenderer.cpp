#include <stdexcept>
#include <cassert>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <glfw3.h>
#include <glfw3native.h>
#include <d3dcompiler.h>

#include "DxRenderer.h"
#include "CD3DX12.h"
#include "src/common/Utils.h"

void DxRenderer::init(GLFWwindow* window) {
    int width, height;
    glfwGetWindowSize(window, &width, &height);
    mCamera = Camera(glm::vec3{-100.0, 20.0, 100.0}, -12.0, -50.0, (float)width / height, 45.0, 0.1);
    glfwSetWindowUserPointer(window, &mCamera);

    // set viewport and scissor rect
    mScreenViewport = CD3DX12_VIEWPORT{0.0f, 0.0f, (FLOAT)width, (FLOAT)height};
    mScissorRect = CD3DX12_RECT{0, 0, (LONG)width, (LONG)height};

#if defined(DEBUG) || defined(_DEBUG)
    // Enable the D3D12 debug layer.
    {
        ComPtr<ID3D12Debug> debugController;
        ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
        debugController->EnableDebugLayer();
    }
    // Enable run-time memory check
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    // create dxgiFactory
    ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&mDxgiFactory)));

    // create device
    HRESULT result = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&mDevice));
    if (FAILED(result)) {
        ComPtr<IDXGIAdapter> pWarpAdapter;
        ThrowIfFailed(mDxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&pWarpAdapter)));
        ThrowIfFailed(D3D12CreateDevice(pWarpAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&mDevice)));
    }

    // create commandList
    ThrowIfFailed(mDevice->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(mCommandAllocator.GetAddressOf()))
    );
    ThrowIfFailed(mDevice->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, mCommandAllocator.Get(), nullptr, IID_PPV_ARGS(&mCommandList))
    );
    ThrowIfFailed(mCommandList->Close());

    // create commandQueue
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    ThrowIfFailed(mDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&mCommandQueue)));

    // create swapChain
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = mNumFrames;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    ComPtr<IDXGISwapChain1> swapChain;
    ThrowIfFailed(mDxgiFactory->CreateSwapChainForHwnd(
        mCommandQueue.Get(), glfwGetWin32Window(window), &swapChainDesc, nullptr, nullptr, &swapChain
    ));
    swapChain.As(&mSwapChain);

    // check msaa level
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS colorLevels = {DXGI_FORMAT_R16G16B16A16_FLOAT, mSamples};
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS depthStencilLevels = {DXGI_FORMAT_D24_UNORM_S8_UINT, mSamples};

    ThrowIfFailed(mDevice->CheckFeatureSupport(
        D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, 
        &colorLevels,
        sizeof(colorLevels)
    ));
    ThrowIfFailed(mDevice->CheckFeatureSupport(
        D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, 
        &depthStencilLevels,
        sizeof(depthStencilLevels)
    ));

    // ckeck root signature version
    D3D12_FEATURE_DATA_ROOT_SIGNATURE rootSignatureFeature = {D3D_ROOT_SIGNATURE_VERSION_1_1};
    ThrowIfFailed(mDevice->CheckFeatureSupport(
        D3D12_FEATURE_ROOT_SIGNATURE, &rootSignatureFeature, sizeof(D3D12_FEATURE_DATA_ROOT_SIGNATURE)
    ));
    mRootSignatureVersion = rootSignatureFeature.HighestVersion;

    // create descriptor heaps
    mRtvHeap = createDescriptorHeap({D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 16, D3D12_DESCRIPTOR_HEAP_FLAG_NONE});
    mDsvHeap = createDescriptorHeap({D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 16, D3D12_DESCRIPTOR_HEAP_FLAG_NONE});
    mCbvSrvUavHeap = createDescriptorHeap(
        {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE}
    );

    // create RTV and DSV
    for (int i = 0; i < mNumFrames; i++) {
        mSwapChainBuffers[i] = createSwapChainBuffer(i);
        mDepthStencilBuffers[i] = createDepthStencilBuffer(width, height, 1, DXGI_FORMAT_D24_UNORM_S8_UINT);

        mFrameBuffers[i] = createFrameBuffer(width, height, mSamples, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_D24_UNORM_S8_UINT);

        if(mSamples > 1) {
			mResolveFrameBuffers[i] = createFrameBuffer(width, height, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, (DXGI_FORMAT)0);
		}
        else {
            mResolveFrameBuffers[i] = mFrameBuffers[i];
		}

        mFrameResources[i] = FrameResource(mDevice.Get());
        mFrameResources[i].transformCBs = createConstantBuffer<TransformCB>(1);
        mFrameResources[i].shadingCBs = createConstantBuffer<ShadingCB>(1);
    }

    // create Fence
    ThrowIfFailed(mDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence)));
}

void DxRenderer::setup() {
    // ------------------------------------ pre compute environment map --------------------------------------
    // create compute root signature
    ComPtr<ID3D12RootSignature> computeRootSignature;
    {
        CD3DX12_DESCRIPTOR_RANGE1 descriptorRanges[] = {
            {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC},
            {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE},
        };
        CD3DX12_ROOT_PARAMETER1 rootParameters[3];
        rootParameters[0].InitAsDescriptorTable(1, &descriptorRanges[0]);
        rootParameters[1].InitAsDescriptorTable(1, &descriptorRanges[1]);
        rootParameters[2].InitAsConstants(1, 0);

        CD3DX12_STATIC_SAMPLER_DESC samplerDesc{0, D3D12_FILTER_MIN_MAG_MIP_LINEAR};

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init_1_1(3, rootParameters, 1, &samplerDesc);
        computeRootSignature = createRootSignature(rootSignatureDesc);
    }

    ID3D12DescriptorHeap *computeDescriptorHeaps[] = {mCbvSrvUavHeap.heap.Get()};

    // convert equirectangular map to cube map
    Texture envTexture = createTexture(1024, 1024, 6, DXGI_FORMAT_R16G16B16A16_FLOAT);
    {
        DescriptorHeapMark mark(mCbvSrvUavHeap);
        Texture equirectTexture = createTexture(Image::fromFile("assets/environment.hdr"), DXGI_FORMAT_R32G32B32A32_FLOAT, 1);
        createTextureUAV(envTexture, 0);

        ComPtr<ID3DBlob> equirect2cubeShader = compileShader("src/backend/dx12/shaders/equirect2cube.hlsl", "main", "cs_5_0");
        
        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = computeRootSignature.Get();
        psoDesc.CS = CD3DX12_SHADER_BYTECODE(equirect2cubeShader.Get());

        ComPtr<ID3D12PipelineState> pipelineState;
        ThrowIfFailed(mDevice->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState)));

        mCommandList->Reset(mFrameResources[mFrameIndex].mCommandAllocator.Get(), nullptr);

        auto commom_to_unorderedAccess = CD3DX12_RESOURCE_BARRIER::Transition(
            envTexture.texture.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS
        );
        mCommandList->ResourceBarrier(1, &commom_to_unorderedAccess);

        mCommandList->SetDescriptorHeaps(1, computeDescriptorHeaps);
        mCommandList->SetPipelineState(pipelineState.Get());
        mCommandList->SetComputeRootSignature(computeRootSignature.Get());
        mCommandList->SetComputeRootDescriptorTable(0, equirectTexture.srv.gpuHandle);
        mCommandList->SetComputeRootDescriptorTable(1, envTexture.uav.gpuHandle);
        mCommandList->Dispatch(envTexture.width / 32, envTexture.height / 32, 6);

        auto unorderedAccess_to_common = CD3DX12_RESOURCE_BARRIER::Transition(
            envTexture.texture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON
        );
        mCommandList->ResourceBarrier(1, &unorderedAccess_to_common);

        executeCommandList();
        waitForGPU();
    }
    generateMipmaps(envTexture);
    mTextures["envTexture"] = envTexture;

    // compute diffuse irradiance map
    Texture irradianceTexture = createTexture(32, 32, 6, DXGI_FORMAT_R16G16B16A16_FLOAT, 1);
    {
        DescriptorHeapMark mark(mCbvSrvUavHeap);
        createTextureUAV(irradianceTexture, 0);

        ComPtr<ID3DBlob> irradianceShader = compileShader("src/backend/dx12/shaders/irradiance.hlsl", "main", "cs_5_0");

        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = computeRootSignature.Get();
        psoDesc.CS = CD3DX12_SHADER_BYTECODE{irradianceShader.Get()};

        ComPtr<ID3D12PipelineState> pipelineState;
        ThrowIfFailed(mDevice->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState)));

        mCommandList->Reset(mFrameResources[mFrameIndex].mCommandAllocator.Get(), nullptr);

        mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            irradianceTexture.texture.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS
        ));
        mCommandList->SetDescriptorHeaps(1, computeDescriptorHeaps);
        mCommandList->SetPipelineState(pipelineState.Get());
        mCommandList->SetComputeRootSignature(computeRootSignature.Get());
        mCommandList->SetComputeRootDescriptorTable(0, envTexture.srv.gpuHandle);
        mCommandList->SetComputeRootDescriptorTable(1, irradianceTexture.uav.gpuHandle);

        mCommandList->Dispatch(irradianceTexture.width / 32, irradianceTexture.height / 32, 6);

		mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            irradianceTexture.texture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON
        ));

		executeCommandList();
		waitForGPU();
    }
    mTextures["irradiance"] = irradianceTexture;

    // compute pre-filtered specular map
    Texture prefilterTexture = createTexture(1024, 1024, 6, DXGI_FORMAT_R16G16B16A16_FLOAT);
    {
        DescriptorHeapMark mark(mCbvSrvUavHeap);

        ComPtr<ID3DBlob> prefilterShader = compileShader("src/backend/dx12/shaders/prefilter.hlsl", "main", "cs_5_0");

        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = computeRootSignature.Get();
        psoDesc.CS = CD3DX12_SHADER_BYTECODE{prefilterShader.Get()};

        ComPtr<ID3D12PipelineState> pipelineState;
        ThrowIfFailed(mDevice->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState)));

        D3D12_RESOURCE_BARRIER preCopyBarriers[] = {
			CD3DX12_RESOURCE_BARRIER::Transition(prefilterTexture.texture.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST),
			CD3DX12_RESOURCE_BARRIER::Transition(envTexture.texture.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE)
		};
		D3D12_RESOURCE_BARRIER postCopyBarriers[] = {
			CD3DX12_RESOURCE_BARRIER::Transition(prefilterTexture.texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			CD3DX12_RESOURCE_BARRIER::Transition(envTexture.texture.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON)
		};

        mCommandList->Reset(mFrameResources[mFrameIndex].mCommandAllocator.Get(), nullptr);

		mCommandList->ResourceBarrier(2, preCopyBarriers);
		for(UINT arraySlice = 0; arraySlice < 6; ++arraySlice) {
			UINT subresourceIndex = D3D12CalcSubresource(0, arraySlice, 0, prefilterTexture.levels, 6);

            CD3DX12_TEXTURE_COPY_LOCATION srcCopyLocation = {prefilterTexture.texture.Get(), subresourceIndex};
            CD3DX12_TEXTURE_COPY_LOCATION destCopyLocation = {envTexture.texture.Get(), subresourceIndex};

			mCommandList->CopyTextureRegion(&srcCopyLocation, 0, 0, 0, &destCopyLocation, nullptr);
		}
		mCommandList->ResourceBarrier(2, postCopyBarriers);

        mCommandList->SetDescriptorHeaps(1, computeDescriptorHeaps);
		mCommandList->SetPipelineState(pipelineState.Get());
        mCommandList->SetComputeRootSignature(computeRootSignature.Get());
		mCommandList->SetComputeRootDescriptorTable(0, envTexture.srv.gpuHandle);

		float deltaRoughness = 1.0f / std::max(float(prefilterTexture.levels - 1), 1.0f);

		for(UINT level = 1, size = 512; level < prefilterTexture.levels; ++level, size /= 2) {
			UINT numGroups = std::max<UINT>(1, size/32);
			float roughness = level * deltaRoughness;
			createTextureUAV(prefilterTexture, level);

			mCommandList->SetComputeRootDescriptorTable(1, prefilterTexture.uav.gpuHandle);
			mCommandList->SetComputeRoot32BitConstants(2, 1, &roughness, 0);
			mCommandList->Dispatch(numGroups, numGroups, 6);
		}
		mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            prefilterTexture.texture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON
        ));
		executeCommandList();
		waitForGPU();
    }
    mTextures["prefilter"] = prefilterTexture;

    // compute Cook-Torrance BRDF LUT
    Texture brdfTexture = createTexture(256, 256, 1, DXGI_FORMAT_R16G16_FLOAT, 1);
    {
        DescriptorHeapMark mark(mCbvSrvUavHeap);
        createTextureUAV(brdfTexture, 0);

        ComPtr<ID3DBlob> spBRDFShader = compileShader("src/backend/dx12/shaders/brdf.hlsl", "main", "cs_5_0");

        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = computeRootSignature.Get();
        psoDesc.CS = CD3DX12_SHADER_BYTECODE{spBRDFShader.Get()};
        
        ComPtr<ID3D12PipelineState> pipelineState;
        ThrowIfFailed(mDevice->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState)));

        mCommandList->Reset(mFrameResources[mFrameIndex].mCommandAllocator.Get(), nullptr);

        mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            brdfTexture.texture.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
        );
        mCommandList->SetDescriptorHeaps(1, computeDescriptorHeaps);
        mCommandList->SetPipelineState(pipelineState.Get());
        mCommandList->SetComputeRootSignature(computeRootSignature.Get());
        mCommandList->SetComputeRootDescriptorTable(1, brdfTexture.uav.gpuHandle);
        mCommandList->Dispatch(brdfTexture.width / 32, brdfTexture.height / 32, 1);
        mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            brdfTexture.texture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON)
        );
        executeCommandList();
        waitForGPU();
    }
    mTextures["brdf"] = brdfTexture;

    // ----------------------------------------- setup pipeline state -------------------------------------------
    // create skybox pipeline state
    ComPtr<ID3D12RootSignature> skyboxRootSignature;
    ComPtr<ID3D12PipelineState> skyboxPipelineState;
    {
        std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
        };

        ComPtr<ID3DBlob> skyboxVS = compileShader("src/backend/dx12/shaders/skybox.hlsl", "main_vs", "vs_5_0");
        ComPtr<ID3DBlob> skyboxPS = compileShader("src/backend/dx12/shaders/skybox.hlsl", "main_ps", "ps_5_0");

        CD3DX12_DESCRIPTOR_RANGE1 descriptorRanges[] = {
            {D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC},
            {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC}
        };
        CD3DX12_ROOT_PARAMETER1 rootParameters[2];
        rootParameters[0].InitAsDescriptorTable(1, &descriptorRanges[0], D3D12_SHADER_VISIBILITY_VERTEX);
        rootParameters[1].InitAsDescriptorTable(1, &descriptorRanges[1], D3D12_SHADER_VISIBILITY_PIXEL);

        CD3DX12_STATIC_SAMPLER_DESC samplerDesc{0, D3D12_FILTER_ANISOTROPIC};
        samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init_1_1(
            2, rootParameters, 1, &samplerDesc, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
        );
        skyboxRootSignature = createRootSignature(rootSignatureDesc);

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = skyboxRootSignature.Get();
        psoDesc.InputLayout = {inputLayout.data(), (UINT)inputLayout.size()};
        psoDesc.VS = CD3DX12_SHADER_BYTECODE(skyboxVS.Get());
        psoDesc.PS = CD3DX12_SHADER_BYTECODE(skyboxPS.Get());
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.RasterizerState.FrontCounterClockwise = true;
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        psoDesc.SampleDesc.Count = mSamples;
        psoDesc.SampleMask = UINT_MAX;
        ThrowIfFailed(mDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&skyboxPipelineState)));
    }
    mRootSignatures["skybox"] = skyboxRootSignature;
    mPipelineStates["skybox"] = skyboxPipelineState;

    // create tonemap pipeline state
    ComPtr<ID3D12RootSignature> tonemapRootSignature;
    ComPtr<ID3D12PipelineState> tonemapPipelineState;
    {
        ComPtr<ID3DBlob> tonemapVS = compileShader("src/backend/dx12/shaders/tonemap.hlsl", "main_vs", "vs_5_0");
        ComPtr<ID3DBlob> tonemapPS = compileShader("src/backend/dx12/shaders/tonemap.hlsl", "main_ps", "ps_5_0");

		CD3DX12_DESCRIPTOR_RANGE1 descriptorRanges[] = {
			{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE},
		};
		CD3DX12_ROOT_PARAMETER1 rootParameters[1];
		rootParameters[0].InitAsDescriptorTable(1, &descriptorRanges[0], D3D12_SHADER_VISIBILITY_PIXEL);

        CD3DX12_STATIC_SAMPLER_DESC samplerDesc{0, D3D12_FILTER_MIN_MAG_MIP_LINEAR};

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(
            1, rootParameters, 1, &samplerDesc, 
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | 
            D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS
        );
		tonemapRootSignature = createRootSignature(rootSignatureDesc);

		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.pRootSignature = tonemapRootSignature.Get();
		psoDesc.VS = CD3DX12_SHADER_BYTECODE(tonemapVS.Get());
		psoDesc.PS = CD3DX12_SHADER_BYTECODE(tonemapPS.Get());
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC{D3D12_DEFAULT};
		psoDesc.RasterizerState.FrontCounterClockwise = true;
		psoDesc.BlendState = CD3DX12_BLEND_DESC{D3D12_DEFAULT};
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.SampleDesc.Count = 1;
		psoDesc.SampleMask = UINT_MAX;

		ThrowIfFailed(mDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&tonemapPipelineState)));
	}
    mRootSignatures["tonemap"] = tonemapRootSignature;
    mPipelineStates["tonemap"] = tonemapPipelineState;

    // create pbr pipeline state
    ComPtr<ID3D12RootSignature> pbrRootSignature;
    ComPtr<ID3D12PipelineState> pbrPipelineState;
    {
		std::vector<D3D12_INPUT_ELEMENT_DESC> meshInputLayout = {
			{ "POSITION",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "NORMAL",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TANGENT",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "BITANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD",  0, DXGI_FORMAT_R32G32_FLOAT,    0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};

		ComPtr<ID3DBlob> pbrVS = compileShader("src/backend/dx12/shaders/pbr.hlsl", "main_vs", "vs_5_0");
		ComPtr<ID3DBlob> pbrPS = compileShader("src/backend/dx12/shaders/pbr.hlsl", "main_ps", "ps_5_0");

		const CD3DX12_DESCRIPTOR_RANGE1 descriptorRanges[] = {
			{D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC},
			{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 7, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC},
		};
		CD3DX12_ROOT_PARAMETER1 rootParameters[3];
		rootParameters[0].InitAsDescriptorTable(1, &descriptorRanges[0], D3D12_SHADER_VISIBILITY_VERTEX);
		rootParameters[1].InitAsDescriptorTable(1, &descriptorRanges[0], D3D12_SHADER_VISIBILITY_PIXEL);
		rootParameters[2].InitAsDescriptorTable(1, &descriptorRanges[1], D3D12_SHADER_VISIBILITY_PIXEL);
        
        CD3DX12_STATIC_SAMPLER_DESC defaultSamplerDesc{0, D3D12_FILTER_ANISOTROPIC};
        defaultSamplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        CD3DX12_STATIC_SAMPLER_DESC brdfSamplerDesc{1, D3D12_FILTER_MIN_MAG_MIP_LINEAR};
        brdfSamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        brdfSamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        brdfSamplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		D3D12_STATIC_SAMPLER_DESC staticSamplers[2] = {defaultSamplerDesc, brdfSamplerDesc};

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC signatureDesc;
		signatureDesc.Init_1_1(
            3, rootParameters, 2, staticSamplers, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
        );
		pbrRootSignature = createRootSignature(signatureDesc);

		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.pRootSignature = pbrRootSignature.Get();
		psoDesc.InputLayout = { meshInputLayout.data(), (UINT)meshInputLayout.size() };
		psoDesc.VS = CD3DX12_SHADER_BYTECODE(pbrVS.Get());
		psoDesc.PS = CD3DX12_SHADER_BYTECODE(pbrPS.Get());
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.RasterizerState.FrontCounterClockwise = true;
		psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
		psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
		psoDesc.SampleDesc.Count = mSamples;
		psoDesc.SampleMask = UINT_MAX;

		ThrowIfFailed(mDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pbrPipelineState)));
	}
    mRootSignatures["pbr"] = pbrRootSignature;
    mPipelineStates["pbr"] = pbrPipelineState;

    // create pbr texture
    mTextures["albedo"] = createTexture(Image::fromFile("assets/textures/cerberus_A.png"), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
	mTextures["normal"] = createTexture(Image::fromFile("assets/textures/cerberus_N.png"), DXGI_FORMAT_R8G8B8A8_UNORM);
	mTextures["metalness"] = createTexture(Image::fromFile("assets/textures/cerberus_M.png", 1), DXGI_FORMAT_R8_UNORM);
	mTextures["roughness"] = createTexture(Image::fromFile("assets/textures/cerberus_R.png", 1), DXGI_FORMAT_R8_UNORM);

    // create mesh
    mMeshBuffers["model"]  = createMeshBuffer(Mesh::fromFile("assets/meshes/cerberus.fbx"));
    mMeshBuffers["skybox"] = createMeshBuffer(Mesh::fromFile("assets/meshes/skybox.obj"));
}

void DxRenderer::updateFrameResources() {
    FrameResource frameResource = mFrameResources[mFrameIndex];

    if (frameResource.Fence != 0 && mFence->GetCompletedValue() < frameResource.Fence) {
        HANDLE eventHandle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(frameResource.Fence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

    glm::mat4 proj = glm::perspective(glm::radians(mCamera.fov), mCamera.aspect, 1.0f, 1000.0f);
    glm::mat4 view = mCamera.getViewMatrix();
    const glm::vec3 cameraPos = mCamera.position;

    ShadingCB *shadingCB = frameResource.shadingCBs.as<ShadingCB>();
    shadingCB->lights[0].position = glm::vec4(5.0, 5.0, 5.0, 0.0);
    shadingCB->lights[0].radiance = glm::vec4(1.0, 1.0, 1.0, 0.0);
    shadingCB->cameraPos = glm::vec4{cameraPos, 0.0f};

    TransformCB *transformCB = frameResource.transformCBs.as<TransformCB>();
    transformCB->viewProj = proj * view;
    transformCB->skyboxProj = proj * glm::mat4(glm::mat3(view));
}

void DxRenderer::draw() {
    // update transform/shading constant buffer
    updateFrameResources();

    FrameBuffer& frameBuffer = mFrameBuffers[mFrameIndex];
    FrameBuffer& resolveFrameBuffer = mResolveFrameBuffers[mFrameIndex];
    SwapChainBuffer& swapChainBuffer = mSwapChainBuffers[mFrameIndex];
    DepthStentilBuffer &depthStencilBuffer = mDepthStencilBuffers[mFrameIndex];
    FrameResource &frameResource = mFrameResources[mFrameIndex];

    ThrowIfFailed(frameResource.mCommandAllocator->Reset());
    ThrowIfFailed(mCommandList->Reset(
        frameResource.mCommandAllocator.Get(), mPipelineStates["skybox"].Get()
    ));

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    // resource barrier
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        frameBuffer.colorBuffer.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET
    ));

    // clear render target and depth/stencil buffer
    mCommandList->ClearRenderTargetView(frameBuffer.rtv.cpuHandle, DirectX::Colors::LightBlue, 0, nullptr);
    mCommandList->ClearDepthStencilView(
        frameBuffer.dsv.cpuHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr
    );

    mCommandList->OMSetRenderTargets(
        1, &frameBuffer.rtv.cpuHandle, true, &frameBuffer.dsv.cpuHandle
    );
    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ID3D12DescriptorHeap *descriptorHeaps[] = {mCbvSrvUavHeap.heap.Get()};
    mCommandList->SetDescriptorHeaps(1, descriptorHeaps);

    // skybox pass
    mCommandList->SetPipelineState(mPipelineStates["skybox"].Get());
    mCommandList->SetGraphicsRootSignature(mRootSignatures["skybox"].Get());
    mCommandList->SetGraphicsRootDescriptorTable(0, frameResource.transformCBs.cbv.gpuHandle);
    mCommandList->SetGraphicsRootDescriptorTable(1, mTextures["envTexture"].srv.gpuHandle);

    mCommandList->IASetVertexBuffers(0, 1, &mMeshBuffers["skybox"].vbv);
    mCommandList->IASetIndexBuffer(&mMeshBuffers["skybox"].ibv);
    mCommandList->DrawIndexedInstanced(mMeshBuffers["skybox"].numIndices, 1, 0, 0, 0);

    // pbr pass
    mCommandList->SetPipelineState(mPipelineStates["pbr"].Get());
    mCommandList->SetGraphicsRootSignature(mRootSignatures["pbr"].Get());
    mCommandList->SetGraphicsRootDescriptorTable(0, frameResource.transformCBs.cbv.gpuHandle);
    mCommandList->SetGraphicsRootDescriptorTable(1, frameResource.shadingCBs.cbv.gpuHandle);
    mCommandList->SetGraphicsRootDescriptorTable(2, mTextures["irradiance"].srv.gpuHandle);

    mCommandList->IASetVertexBuffers(0, 1, &mMeshBuffers["model"].vbv);
    mCommandList->IASetIndexBuffer(&mMeshBuffers["model"].ibv);
    mCommandList->DrawIndexedInstanced(mMeshBuffers["model"].numIndices, 1, 0, 0, 0);

    // resolve frame buffer
    if (frameBuffer.samples > 1) {
        resolveSubresource(frameBuffer, resolveFrameBuffer, DXGI_FORMAT_R16G16B16A16_FLOAT);
    } else {
        mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            frameBuffer.colorBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
        ));
    }

    // tonemap pass
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        swapChainBuffer.buffer.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET
    ));
    mCommandList->OMSetRenderTargets(1, &swapChainBuffer.rtv.cpuHandle, true, &depthStencilBuffer.dsv.cpuHandle);

    mCommandList->SetPipelineState(mPipelineStates["tonemap"].Get());
    mCommandList->SetGraphicsRootSignature(mRootSignatures["tonemap"].Get());
    mCommandList->SetGraphicsRootDescriptorTable(0, resolveFrameBuffer.srv.gpuHandle);

    mCommandList->DrawInstanced(6, 1, 0, 0);

    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        swapChainBuffer.buffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT
    ));
    executeCommandList();

    ThrowIfFailed(mSwapChain->Present(0, 0));

    mFrameIndex = (mFrameIndex + 1) % mNumFrames;
    frameResource.Fence = ++mCurrentFence;

    mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void DxRenderer::exit() {

}

DescriptorHeap DxRenderer::createDescriptorHeap(D3D12_DESCRIPTOR_HEAP_DESC desc) {
    DescriptorHeap heap;
    heap.descriptorSize = mDevice->GetDescriptorHandleIncrementSize(desc.Type);
    heap.numDescriptor = desc.NumDescriptors;
    heap.numDescriptorAlloced = 0;

    ThrowIfFailed(mDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap.heap)));

    return heap;
}

ComPtr<ID3D12RootSignature> DxRenderer::createRootSignature(D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc) {
    D3D12_ROOT_SIGNATURE_FLAGS standardFlags = 
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS   |                                             
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS;

    switch (desc.Version) {
    case D3D_ROOT_SIGNATURE_VERSION_1_0:
        desc.Desc_1_0.Flags |= standardFlags;
        break;
    case D3D_ROOT_SIGNATURE_VERSION_1_1:
        desc.Desc_1_1.Flags |= standardFlags;
        break;
    }

    ComPtr<ID3DBlob> signatureBlob, errorBlob;
    ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&desc, mRootSignatureVersion, &signatureBlob, &errorBlob));

    ComPtr<ID3D12RootSignature> rootSignature;
    ThrowIfFailed(mDevice->CreateRootSignature(
        0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature)
    ));

    return rootSignature;
}

Texture DxRenderer::createTexture(
    UINT width, UINT height, UINT depth, DXGI_FORMAT format, UINT levels
) {
    Texture texture;
    texture.width = width;
    texture.height = height;
    texture.levels = levels > 0 ? levels : mipmapLevels(width, height);

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = depth;
    desc.MipLevels = levels;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    ThrowIfFailed(mDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES{D3D12_HEAP_TYPE_DEFAULT},
        D3D12_HEAP_FLAG_NONE, 
        &desc, 
        D3D12_RESOURCE_STATE_COMMON, 
        nullptr,
        IID_PPV_ARGS(&texture.texture)
    ));

    D3D12_SRV_DIMENSION srvDim;
    switch (depth) {
    case 1:
        srvDim = D3D12_SRV_DIMENSION_TEXTURE2D;
        break;
    case 6:
        srvDim = D3D12_SRV_DIMENSION_TEXTURECUBE;
        break;
    default:
        srvDim = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        break;
    }

    createTextureSRV(texture, srvDim);
    return texture;
}

Texture DxRenderer::createTexture(
    std::shared_ptr<Image> image, DXGI_FORMAT format, UINT levels
) {
    Texture texture = createTexture(image->width(), image->height(), 1, format, levels);

    D3D12_SUBRESOURCE_DATA data{image->pixels<void>(), image->pitch()};
    StagingBuffer stagingBuffer = createStagingBuffer(texture.texture, 0, 1, &data);

    CD3DX12_TEXTURE_COPY_LOCATION destCopyLocation{texture.texture.Get(), 0};
    CD3DX12_TEXTURE_COPY_LOCATION srcCopyLocation{stagingBuffer.buffer.Get(), stagingBuffer.layouts[0]};

    mCommandList->Reset(mFrameResources[mFrameIndex].mCommandAllocator.Get(), nullptr);

    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        texture.texture.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST, 0
    ));
    mCommandList->CopyTextureRegion(&destCopyLocation, 0, 0, 0, &srcCopyLocation, nullptr);

    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        texture.texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON, 0
    ));
    executeCommandList();
    waitForGPU();

    if (texture.levels > 1 && texture.width == texture.height && IsPowerOfTwo(texture.width)) {
        generateMipmaps(texture);
    }
    return texture;
}

void DxRenderer::createTextureSRV(
    Texture& texture, D3D12_SRV_DIMENSION dimension, UINT mostDetailedMip, UINT mipLevels
) {
    D3D12_RESOURCE_DESC desc = texture.texture->GetDesc();
    UINT effectiveMipLevels = (mipLevels > 0) ? mipLevels : (desc.MipLevels - mostDetailedMip);
    assert(!(desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE));

    texture.srv = mCbvSrvUavHeap.alloc();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = dimension;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    switch (dimension) {
    case D3D12_SRV_DIMENSION_TEXTURE2D:
        srvDesc.Texture2D.MostDetailedMip = mostDetailedMip;
        srvDesc.Texture2D.MipLevels = effectiveMipLevels;
        break;
    case D3D12_SRV_DIMENSION_TEXTURE2DARRAY:
        srvDesc.Texture2DArray.MostDetailedMip = mostDetailedMip;
        srvDesc.Texture2DArray.MipLevels = effectiveMipLevels;
        srvDesc.Texture2DArray.FirstArraySlice = 0;
        srvDesc.Texture2DArray.ArraySize = desc.DepthOrArraySize;
        break;
    case D3D12_SRV_DIMENSION_TEXTURECUBE:
        srvDesc.TextureCube.MostDetailedMip = mostDetailedMip;
        srvDesc.TextureCube.MipLevels = effectiveMipLevels;
        break;
    }
    mDevice->CreateShaderResourceView(texture.texture.Get(), &srvDesc, texture.srv.cpuHandle);
}

void DxRenderer::createTextureUAV(Texture &texture, UINT mipSlice) {
    D3D12_RESOURCE_DESC desc = texture.texture->GetDesc();
    assert(desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    texture.uav = mCbvSrvUavHeap.alloc();

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = desc.Format;

    if (desc.DepthOrArraySize > 1) {
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        uavDesc.Texture2DArray.MipSlice = mipSlice;
        uavDesc.Texture2DArray.FirstArraySlice = 0;
        uavDesc.Texture2DArray.ArraySize = desc.DepthOrArraySize;
    } else {
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = mipSlice;
    }
    mDevice->CreateUnorderedAccessView(texture.texture.Get(), nullptr, &uavDesc, texture.uav.cpuHandle);
}

SwapChainBuffer DxRenderer::createSwapChainBuffer(UINT index) {
    SwapChainBuffer swapChainBuffer;

    ThrowIfFailed(mSwapChain->GetBuffer(index, IID_PPV_ARGS(&swapChainBuffer.buffer)));
    swapChainBuffer.rtv = mRtvHeap.alloc();

    mDevice->CreateRenderTargetView(
        swapChainBuffer.buffer.Get(), nullptr, swapChainBuffer.rtv.cpuHandle
    );
    return swapChainBuffer;
}

DepthStentilBuffer DxRenderer::createDepthStencilBuffer(
    UINT width, UINT height, UINT samples, DXGI_FORMAT depthstencilFormat
) {
    DepthStentilBuffer depthStencilBuffer;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = samples;
    desc.Format = depthstencilFormat;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;

    ThrowIfFailed(mDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES{D3D12_HEAP_TYPE_DEFAULT},
        D3D12_HEAP_FLAG_NONE, 
        &desc, 
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &CD3DX12_CLEAR_VALUE{depthstencilFormat, 1.0f, 0},
        IID_PPV_ARGS(&depthStencilBuffer.buffer)
    ));

    depthStencilBuffer.dsv = mDsvHeap.alloc();

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = desc.Format;
    dsvDesc.ViewDimension = (samples > 1) ? D3D12_DSV_DIMENSION_TEXTURE2DMS : D3D12_DSV_DIMENSION_TEXTURE2D;
    mDevice->CreateDepthStencilView(depthStencilBuffer.buffer.Get(), &dsvDesc, depthStencilBuffer.dsv.cpuHandle);

    return depthStencilBuffer;
}

FrameBuffer DxRenderer::createFrameBuffer(
    UINT width, UINT height, UINT samples, DXGI_FORMAT colorFormat, DXGI_FORMAT depthstencilFormat
) {
    FrameBuffer framebuffer;
    framebuffer.width = width;
    framebuffer.height = height;
    framebuffer.samples = samples;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = samples;

    if (colorFormat != DXGI_FORMAT_UNKNOWN) {
        desc.Format = colorFormat;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        float clearColor[] = {0.678431392f, 0.847058892f, 0.901960850f, 1.f};

        ThrowIfFailed(mDevice->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES{D3D12_HEAP_TYPE_DEFAULT},
            D3D12_HEAP_FLAG_NONE, 
            &desc, 
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            &CD3DX12_CLEAR_VALUE{colorFormat, clearColor},
            IID_PPV_ARGS(&framebuffer.colorBuffer)
        ));

        framebuffer.rtv = mRtvHeap.alloc();

        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = desc.Format;
        rtvDesc.ViewDimension = (samples > 1) ? D3D12_RTV_DIMENSION_TEXTURE2DMS : D3D12_RTV_DIMENSION_TEXTURE2D;
        mDevice->CreateRenderTargetView(framebuffer.colorBuffer.Get(), &rtvDesc, framebuffer.rtv.cpuHandle);

        if (samples <= 1) {
            framebuffer.srv = mCbvSrvUavHeap.alloc();

            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = desc.Format;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.MipLevels = 1;
            mDevice->CreateShaderResourceView(framebuffer.colorBuffer.Get(), &srvDesc, framebuffer.srv.cpuHandle);
        }
    }

    if (depthstencilFormat != DXGI_FORMAT_UNKNOWN) {
        desc.Format = depthstencilFormat;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;

        ThrowIfFailed(mDevice->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES{D3D12_HEAP_TYPE_DEFAULT},
            D3D12_HEAP_FLAG_NONE, 
            &desc, 
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &CD3DX12_CLEAR_VALUE{depthstencilFormat, 1.0f, 0},
            IID_PPV_ARGS(&framebuffer.depthStencilBuffer)
        ));

        framebuffer.dsv = mDsvHeap.alloc();

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = desc.Format;
        dsvDesc.ViewDimension = (samples > 1) ? D3D12_DSV_DIMENSION_TEXTURE2DMS : D3D12_DSV_DIMENSION_TEXTURE2D;
        mDevice->CreateDepthStencilView(framebuffer.depthStencilBuffer.Get(), &dsvDesc, framebuffer.dsv.cpuHandle);
    }

    return framebuffer;
}

StagingBuffer DxRenderer::createStagingBuffer(
    ComPtr<ID3D12Resource> resource, UINT firstSubresource, UINT numSubresources, D3D12_SUBRESOURCE_DATA* data
) {
    StagingBuffer stagingBuffer;
    stagingBuffer.firstSubresource = firstSubresource;
    stagingBuffer.numSubresources = numSubresources;
    stagingBuffer.layouts.resize(numSubresources);

    D3D12_RESOURCE_DESC resourceDesc = resource->GetDesc();

    std::vector<UINT> numRows(numSubresources);
    std::vector<UINT64> rowBytes(numSubresources);
    UINT64 numBytesTotol;
    mDevice->GetCopyableFootprints(  // 确定资源（如纹理、缓冲）在内存中的布局信息
        &resourceDesc, 
        firstSubresource, 
        numSubresources, 
        0, 
        stagingBuffer.layouts.data(), 
        numRows.data(), 
        rowBytes.data(), 
        &numBytesTotol
    );

    ThrowIfFailed(mDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE, 
        &CD3DX12_RESOURCE_DESC::Buffer(numBytesTotol),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, 
        IID_PPV_ARGS(&stagingBuffer.buffer)
    ));

    if (data) {
        void *bufferMemory;
        ThrowIfFailed(stagingBuffer.buffer->Map(0, &CD3DX12_RANGE{0, 0}, &bufferMemory));

        for (UINT i = 0; i < numSubresources; i++) {
            uint8_t *subresourceMemory = reinterpret_cast<uint8_t *>(bufferMemory) + stagingBuffer.layouts[i].Offset;

            if (resourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
                // copy buffer
                std::memcpy(subresourceMemory, data->pData, numBytesTotol);
            }
            else {
                // copy texture
                for (UINT row = 0; row < numRows[i]; row++) {
                    const uint8_t *srcRow = reinterpret_cast<const uint8_t *>(data[i].pData) + row * data[i].RowPitch;
                    uint8_t *destRow = subresourceMemory + row * stagingBuffer.layouts[i].Footprint.RowPitch;
                    std::memcpy(destRow, srcRow, rowBytes[i]);
                }
            }
        }
        stagingBuffer.buffer->Unmap(0, nullptr);
    }
    return stagingBuffer;
}

UploadBuffer DxRenderer::createUploadBuffer(UINT size) {
    UploadBuffer uploadBuffer;
    uploadBuffer.size = size;

    ThrowIfFailed(mDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(size),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&uploadBuffer.buffer)
    ));

    ThrowIfFailed(uploadBuffer.buffer->Map(
        0, &CD3DX12_RANGE{0, 0}, reinterpret_cast<void**>(&uploadBuffer.cpuAddress)
    ));
    uploadBuffer.gpuAddress = uploadBuffer.buffer->GetGPUVirtualAddress();

    return uploadBuffer;
}

template <typename T>
ConstantBuffer DxRenderer::createConstantBuffer(UINT count) {
    ConstantBuffer constantBuffer;

    constantBuffer.buffer = createUploadBuffer(AlignedByteSize(sizeof(T), 256) * count);
    constantBuffer.cbv = mCbvSrvUavHeap.alloc();

    D3D12_CONSTANT_BUFFER_VIEW_DESC desc = {};
    desc.BufferLocation = constantBuffer.buffer.gpuAddress;
    desc.SizeInBytes = constantBuffer.buffer.size;
    mDevice->CreateConstantBufferView(&desc, constantBuffer.cbv.cpuHandle);

    return constantBuffer;
}

MeshBuffer DxRenderer::createMeshBuffer(std::shared_ptr<Mesh> mesh) {
    MeshBuffer meshBuffer;
    meshBuffer.numVertices = static_cast<UINT>(mesh->vertices().size());
    meshBuffer.numIndices = static_cast<UINT>(mesh->faces().size() * 3);

    size_t vertexByteSize = mesh->vertices().size() * sizeof(Mesh::Vertex);
    size_t indexByteSize = mesh->faces().size() * sizeof(Mesh::Face);

    ThrowIfFailed(mDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES{D3D12_HEAP_TYPE_DEFAULT},
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(vertexByteSize),
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&meshBuffer.vertexBuffer)
    ));
    meshBuffer.vbv.BufferLocation = meshBuffer.vertexBuffer->GetGPUVirtualAddress();
    meshBuffer.vbv.SizeInBytes = static_cast<UINT>(vertexByteSize);
    meshBuffer.vbv.StrideInBytes = sizeof(Mesh::Vertex);

    ThrowIfFailed(mDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES{D3D12_HEAP_TYPE_DEFAULT},
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(indexByteSize),
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&meshBuffer.indexBuffer)
    ));
    meshBuffer.ibv.BufferLocation = meshBuffer.indexBuffer->GetGPUVirtualAddress();
    meshBuffer.ibv.SizeInBytes = static_cast<UINT>(indexByteSize);
    meshBuffer.ibv.Format = DXGI_FORMAT_R32_UINT;

    D3D12_SUBRESOURCE_DATA vertexData = {mesh->vertices().data()};
    StagingBuffer vertexStagingBuffer = createStagingBuffer(meshBuffer.vertexBuffer, 0, 1, &vertexData);

    D3D12_SUBRESOURCE_DATA indexData = {mesh->faces().data()};
    StagingBuffer indexStagingBuffer = createStagingBuffer(meshBuffer.indexBuffer, 0, 1, &indexData);

    mCommandList->Reset(mFrameResources[mFrameIndex].mCommandAllocator.Get(), nullptr);

    mCommandList->CopyResource(meshBuffer.vertexBuffer.Get(), vertexStagingBuffer.buffer.Get());
    mCommandList->CopyResource(meshBuffer.indexBuffer.Get(), indexStagingBuffer.buffer.Get());

    D3D12_RESOURCE_BARRIER barriers[] = {
		CD3DX12_RESOURCE_BARRIER::Transition(meshBuffer.vertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
		CD3DX12_RESOURCE_BARRIER::Transition(meshBuffer.indexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER)
	};
	mCommandList->ResourceBarrier(2, barriers);

    executeCommandList();
	waitForGPU();

	return meshBuffer;
}

ComPtr<ID3DBlob> DxRenderer::compileShader(
    std::string filename, std::string entryPoint, std::string profile
) {
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if _DEBUG
    flags |= D3DCOMPILE_DEBUG;
    flags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ComPtr<ID3DBlob> shader;
    ComPtr<ID3DBlob> errorBlob;

    HRESULT hr = D3DCompileFromFile(
        ConvertToUTF16(filename).c_str(), 
        nullptr, 
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entryPoint.c_str(), 
        profile.c_str(), 
        flags, 0, &shader, &errorBlob
    );

    if (FAILED(hr)) {
        std::string errorMsg = "Shader compilation failed: " + filename;
        if (errorBlob) {
            errorMsg += std::string("\n") + static_cast<const char *>(errorBlob->GetBufferPointer());
        }
        throw std::runtime_error(errorMsg);
    }
    return shader;
}

void DxRenderer::generateMipmaps(Texture &texture) {
    assert(texture.width == texture.height);
    assert(IsPowerOfTwo(texture.width));

    if (mRootSignatures.find("mipmap") == mRootSignatures.end()) {
        const CD3DX12_DESCRIPTOR_RANGE1 descriptorRanges[] = {
            {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE},
            {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE},
        };
        CD3DX12_ROOT_PARAMETER1 rootParameters[2];
        rootParameters[0].InitAsDescriptorTable(1, &descriptorRanges[0]);
        rootParameters[1].InitAsDescriptorTable(1, &descriptorRanges[1]);

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init_1_1(2, rootParameters);
        mRootSignatures["mipmap"] = createRootSignature(rootSignatureDesc);
    }

    ID3D12PipelineState* pipelineState = nullptr;
    const D3D12_RESOURCE_DESC desc = texture.texture->GetDesc();

    if (desc.DepthOrArraySize == 1 && desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
        if (mPipelineStates.find("gammaTexture") == mPipelineStates.end()) {
            ComPtr<ID3DBlob> computeShader =
                compileShader("src/backend/dx12/shaders/downsample.hlsl", "downsample_gamma", "cs_5_0");

            D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
            psoDesc.pRootSignature = mRootSignatures["mipmap"].Get();
            psoDesc.CS = CD3DX12_SHADER_BYTECODE(computeShader.Get());

            ThrowIfFailed(mDevice->CreateComputePipelineState(
                &psoDesc, IID_PPV_ARGS(&mPipelineStates["gammaTexture"])
            ));
        }
        pipelineState = mPipelineStates["gammaTexture"].Get();
    } 
    else if (desc.DepthOrArraySize > 1 && desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
        if (mPipelineStates.find("arrayTexture") == mPipelineStates.end()) {
            ComPtr<ID3DBlob> computeShader =
                compileShader("src/backend/dx12/shaders/downsample_array.hlsl", "downsample_linear", "cs_5_0");

            D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
            psoDesc.pRootSignature = mRootSignatures["mipmap"].Get();
            psoDesc.CS = CD3DX12_SHADER_BYTECODE(computeShader.Get());

            ThrowIfFailed(mDevice->CreateComputePipelineState(
                &psoDesc, IID_PPV_ARGS(&mPipelineStates["arrayTexture"])
            ));
        }
        pipelineState = mPipelineStates["arrayTexture"].Get();
    } 
    else {
        assert(desc.DepthOrArraySize == 1);
        if (mPipelineStates.find("linearTexture") == mPipelineStates.end()) {
            ComPtr<ID3DBlob> computeShader =
                compileShader("src/backend/dx12/shaders/downsample.hlsl", "downsample_linear", "cs_5_0");

            D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
            psoDesc.pRootSignature = mRootSignatures["mipmap"].Get();
            psoDesc.CS = CD3DX12_SHADER_BYTECODE(computeShader.Get());

            ThrowIfFailed(mDevice->CreateComputePipelineState(
                &psoDesc, IID_PPV_ARGS(&mPipelineStates["linearTexture"])
            ));
        }
        pipelineState = mPipelineStates["gammaTexture"].Get();
    }

    DescriptorHeapMark mark(mCbvSrvUavHeap);
    ID3D12DescriptorHeap *descriptorHeaps[] = {mCbvSrvUavHeap.heap.Get()};

    mCommandList->Reset(mFrameResources[mFrameIndex].mCommandAllocator.Get(), nullptr);
    
    // tempTexture is needed here because createTextureSRV and createTextureUAV will change the properties of srv and uav
    Texture tempTexture = texture;
    if(desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
        pipelineState = mPipelineStates["gammaTexture"].Get();
        tempTexture = createTexture(texture.width, texture.height, 1, DXGI_FORMAT_R8G8B8A8_UNORM, texture.levels);

		mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            tempTexture.texture.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST
        ));
        mCommandList->CopyResource(tempTexture.texture.Get(), texture.texture.Get());

		mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            tempTexture.texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON
        ));
	}

    mCommandList->SetDescriptorHeaps(1, descriptorHeaps);
    mCommandList->SetPipelineState(pipelineState);
    mCommandList->SetComputeRootSignature(mRootSignatures["mipmap"].Get());

    std::vector<CD3DX12_RESOURCE_BARRIER> preDispatchBarriers(desc.DepthOrArraySize);
    std::vector<CD3DX12_RESOURCE_BARRIER> postDispatchBarriers(desc.DepthOrArraySize);
    UINT levelWidth = texture.width / 2;
    UINT levelHeight = texture.height / 2;
    
    for (UINT level = 1; level < texture.levels; ++level, levelWidth /= 2, levelHeight /= 2) {
        createTextureSRV(
            tempTexture, desc.DepthOrArraySize > 1 ? D3D12_SRV_DIMENSION_TEXTURE2DARRAY : D3D12_SRV_DIMENSION_TEXTURE2D, level - 1, 1
        );
        createTextureUAV(tempTexture, level);

        for (UINT arraySlice = 0; arraySlice < desc.DepthOrArraySize; ++arraySlice) {
            const UINT subresourceIndex = D3D12CalcSubresource(level, arraySlice, 0, texture.levels, desc.DepthOrArraySize);

            preDispatchBarriers[arraySlice] = CD3DX12_RESOURCE_BARRIER::Transition(
                tempTexture.texture.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, subresourceIndex
            );
            postDispatchBarriers[arraySlice] = CD3DX12_RESOURCE_BARRIER::Transition(
                tempTexture.texture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, subresourceIndex
            );
        }

        mCommandList->ResourceBarrier(desc.DepthOrArraySize, preDispatchBarriers.data());
        mCommandList->SetComputeRootDescriptorTable(0, tempTexture.srv.gpuHandle);
        mCommandList->SetComputeRootDescriptorTable(1, tempTexture.uav.gpuHandle);
        mCommandList->Dispatch(
            glm::max(UINT(1), levelWidth / 8), glm::max(UINT(1), levelHeight / 8), desc.DepthOrArraySize
        );
        mCommandList->ResourceBarrier(desc.DepthOrArraySize, postDispatchBarriers.data());
    }

    if (texture.texture == tempTexture.texture) {
        mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            texture.texture.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON
        ));
    } else {
        mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            texture.texture.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST
        ));
        mCommandList->CopyResource(texture.texture.Get(), tempTexture.texture.Get());

        mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
            texture.texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON
        ));
    }
    executeCommandList();
    waitForGPU();
}

void DxRenderer::resolveSubresource(const FrameBuffer &srcBuffer, const FrameBuffer &dstBuffer, DXGI_FORMAT format) {
    CD3DX12_RESOURCE_BARRIER preResolveBarriers[] = {
		CD3DX12_RESOURCE_BARRIER::Transition(srcBuffer.colorBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE),
		CD3DX12_RESOURCE_BARRIER::Transition(dstBuffer.colorBuffer.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RESOLVE_DEST)
	};
	CD3DX12_RESOURCE_BARRIER postResolveBarriers[] = {
		CD3DX12_RESOURCE_BARRIER::Transition(srcBuffer.colorBuffer.Get(), D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
		CD3DX12_RESOURCE_BARRIER::Transition(dstBuffer.colorBuffer.Get(), D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
	};

	if (srcBuffer.colorBuffer != dstBuffer.colorBuffer) {
		mCommandList->ResourceBarrier(2, preResolveBarriers);
		mCommandList->ResolveSubresource(dstBuffer.colorBuffer.Get(), 0, srcBuffer.colorBuffer.Get(), 0, format);
		mCommandList->ResourceBarrier(2, postResolveBarriers);
	}
}

void DxRenderer::executeCommandList() {
    ThrowIfFailed(mCommandList->Close());

    ID3D12CommandList *lists[] = {mCommandList.Get()};
    mCommandQueue->ExecuteCommandLists(1, lists);
}

void DxRenderer::waitForGPU() {
    mCurrentFence++;

    ThrowIfFailed(mCommandQueue->Signal(mFence.Get(), mCurrentFence));

    if (mFence->GetCompletedValue() < mCurrentFence) {
        HANDLE eventHandle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrentFence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }
}