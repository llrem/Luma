#pragma once

#include <vector>
#include <d3d12.h>
#include <glm.hpp>

using Microsoft::WRL::ComPtr;

struct Descriptor {
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle;
};

struct DescriptorHeap {
    ComPtr<ID3D12DescriptorHeap> heap;
    UINT descriptorSize;
    UINT numDescriptor;
    UINT numDescriptorAlloced;

    Descriptor alloc() { 
        return (*this)[numDescriptorAlloced++];
    }

    Descriptor operator[](UINT index) const {
        assert(index < numDescriptor);
        return {
            D3D12_CPU_DESCRIPTOR_HANDLE{heap->GetCPUDescriptorHandleForHeapStart().ptr + index * descriptorSize},
            D3D12_GPU_DESCRIPTOR_HANDLE{heap->GetGPUDescriptorHandleForHeapStart().ptr + index * descriptorSize}
        };
    }
};

struct DescriptorHeapMark {
    DescriptorHeapMark(DescriptorHeap &heap) 
        : heap(heap), mark(heap.numDescriptorAlloced) { }

    ~DescriptorHeapMark() { 
        heap.numDescriptorAlloced = mark;
    }

    DescriptorHeap &heap;
    UINT mark;
};

struct Texture {
    ComPtr<ID3D12Resource> texture;
    Descriptor srv;
    Descriptor uav;
    UINT width, height, levels;
};

struct SwapChainBuffer {
    ComPtr<ID3D12Resource> buffer;
    Descriptor rtv;
};

struct DepthStentilBuffer {
    ComPtr<ID3D12Resource> buffer;
    Descriptor dsv;
};

struct FrameBuffer {
    ComPtr<ID3D12Resource> colorBuffer;
    ComPtr<ID3D12Resource> depthStencilBuffer;
    Descriptor rtv;
    Descriptor srv;
	Descriptor dsv;
    UINT width, height, samples;
};

struct StagingBuffer {
    ComPtr<ID3D12Resource> buffer;
    UINT firstSubresource;
    UINT numSubresources;
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts;
};

struct UploadBuffer {
    ComPtr<ID3D12Resource> buffer;
    uint8_t *cpuAddress;
    D3D12_GPU_VIRTUAL_ADDRESS gpuAddress;
    UINT size;
};

struct ConstantBuffer {
    UploadBuffer buffer;
    Descriptor cbv;

    template <typename T> 
    T* as() const { 
        return reinterpret_cast<T*>(buffer.cpuAddress); 
    }
};

struct TransformCB {
    glm::mat4 viewProj;
    glm::mat4 skyboxProj;
};

const int NumLights = 1;

struct ShadingCB {
    struct {
        glm::vec4 position;
        glm::vec4 radiance;
    } lights[NumLights];

    glm::vec4 cameraPos;
};

struct MeshBuffer {
    ComPtr<ID3D12Resource> vertexBuffer;
    ComPtr<ID3D12Resource> indexBuffer;
    D3D12_VERTEX_BUFFER_VIEW vbv;
    D3D12_INDEX_BUFFER_VIEW ibv;
    UINT numVertices;
    UINT numIndices;
};