#include <vector>
#include <unordered_map>
#include <d3d12.h>
#include <wrl.h>
#include <dxgi1_4.h>

#include "FrameResource.h"
#include "Structs.h"
#include "src/common/IRenderer.h"
#include "src/common/Mesh.h"
#include "src/common/Utils.h"
#include "src/common/Camera.h"


using Microsoft::WRL::ComPtr;

class DxRenderer : public IRenderer {
public:
    DxRenderer() {}
    ~DxRenderer() {}

    void init(GLFWwindow* window) override;
    void setup() override;
    void draw() override;
    void exit() override;

private:
    DescriptorHeap createDescriptorHeap(D3D12_DESCRIPTOR_HEAP_DESC desc);
    
    ComPtr<ID3D12RootSignature> createRootSignature(D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc);

    Texture createTexture(UINT width, UINT height, UINT depth, DXGI_FORMAT format, UINT levels = 0);
    Texture createTexture(std::shared_ptr<Image> image, DXGI_FORMAT format, UINT levels = 0);

    void createTextureSRV(
        Texture &texture, D3D12_SRV_DIMENSION dimension, UINT mostDetailedMip = 0, UINT mipLevels = 0
    );
    void createTextureUAV(Texture &texture, UINT mipSlice);

    SwapChainBuffer createSwapChainBuffer(UINT index);

    DepthStentilBuffer createDepthStencilBuffer(
        UINT width, UINT height, UINT samples, DXGI_FORMAT depthstencilFormat
    );

    FrameBuffer createFrameBuffer(
        UINT width, UINT height, UINT samples, DXGI_FORMAT colorFormat, DXGI_FORMAT depthstencilFormat
    );

    StagingBuffer createStagingBuffer(
        ComPtr<ID3D12Resource> resource, UINT firstSubresource, UINT numSubresources, D3D12_SUBRESOURCE_DATA *data
    );

    UploadBuffer createUploadBuffer(UINT capacity);

    template <typename T> 
    ConstantBuffer createConstantBuffer(UINT count);
   
    MeshBuffer createMeshBuffer(std::shared_ptr<Mesh> mesh);

    ComPtr<ID3DBlob> compileShader(std::string filename, std::string entryPoint, std::string profile);

    void resolveSubresource(const FrameBuffer &srcBuffer, const FrameBuffer &dstBuffer, DXGI_FORMAT format);
    void generateMipmaps(Texture &texture);
    void executeCommandList();
    void waitForGPU();
    void updateFrameResources();

private:
    Camera mCamera;

private:
    ComPtr<ID3D12Device> mDevice;
    ComPtr<IDXGIFactory4> mDxgiFactory;
    ComPtr<IDXGISwapChain1> mSwapChain;

    D3D12_VIEWPORT mScreenViewport;
    D3D12_RECT mScissorRect;

    ComPtr<ID3D12GraphicsCommandList> mCommandList;
    ComPtr<ID3D12CommandAllocator> mCommandAllocator;
    ComPtr<ID3D12CommandQueue> mCommandQueue;

    DescriptorHeap mRtvHeap;
    DescriptorHeap mDsvHeap;
    DescriptorHeap mCbvSrvUavHeap;

    ComPtr<ID3D12Fence> mFence;
    UINT64 mCurrentFence = 0;

    D3D_ROOT_SIGNATURE_VERSION mRootSignatureVersion;

    static const UINT mNumFrames = 2;
    UINT mFrameIndex = 0;
    SwapChainBuffer mSwapChainBuffers[mNumFrames];
    DepthStentilBuffer mDepthStencilBuffers[mNumFrames];
    FrameBuffer mFrameBuffers[mNumFrames];
    FrameBuffer mResolveFrameBuffers[mNumFrames];
    FrameResource mFrameResources[mNumFrames];

    UINT mSamples = 4;

    std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPipelineStates;
    std::unordered_map<std::string, ComPtr<ID3D12RootSignature>> mRootSignatures;
    std::unordered_map<std::string, Texture> mTextures;
    std::unordered_map<std::string, MeshBuffer> mMeshBuffers;
};