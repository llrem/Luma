#pragma once

#include <d3d12.h>
#include "Structs.h"
#include "src/common/Utils.h"

using Microsoft::WRL::ComPtr;

class FrameResource {
public:
    FrameResource() = default;

    FrameResource(ID3D12Device *device) {
        ThrowIfFailed(device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(mCommandAllocator.GetAddressOf())
        ));
    }

    ConstantBuffer transformCBs;
    ConstantBuffer shadingCBs;
    ComPtr<ID3D12CommandAllocator> mCommandAllocator;
    UINT64 Fence = 0;
};