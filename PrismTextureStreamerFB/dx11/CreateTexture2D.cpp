#include "dx11.h"
#include <d3d11.h>

#include <MinHook/MinHook.h>

#include "../scs_logging.h"
using namespace scs_logging;

#include "../screens.h"


typedef HRESULT(__stdcall* CreateTexture2D_t)(ID3D11Device*, const D3D11_TEXTURE2D_DESC*, const D3D11_SUBRESOURCE_DATA*, ID3D11Texture2D**);
static CreateTexture2D_t CreateTexture2D_Original = nullptr;

HRESULT HookedCreateTexture2D(ID3D11Device* pDevice, const D3D11_TEXTURE2D_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Texture2D** ppTexture2D)
{
    std::lock_guard<std::mutex> lock(g_screens_mutex);

    for (screen_t& screen : g_screens)
    {
        if (!screen.source.get()) continue; // no source, cant use this

        if (!pDesc) continue;
        if (pDesc->Width  != screen.override_texture_size_w) continue;
        if (pDesc->Height != screen.override_texture_size_h) continue;
        if (pDesc->Format != DXGI_FORMAT_BC3_UNORM) continue;
        if (pDesc->Usage != D3D11_USAGE_DEFAULT) continue;
        if (pDesc->BindFlags != D3D11_BIND_SHADER_RESOURCE) continue;
        if (pInitialData) continue;
        if (pDesc->MipLevels != 1) continue; // Dynamic textures require exactly 1 mip


        D3D11_TEXTURE2D_DESC modifiedDesc = *pDesc;
        modifiedDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        modifiedDesc.Usage = D3D11_USAGE_DYNAMIC;
        modifiedDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        modifiedDesc.MiscFlags = 0;
        modifiedDesc.Width = screen.targetLiveTextureWidth;
        modifiedDesc.Height = screen.targetLiveTextureHeight;

        HRESULT hr = CreateTexture2D_Original(pDevice, &modifiedDesc, pInitialData, ppTexture2D);
        if (SUCCEEDED(hr) && ppTexture2D && *ppTexture2D)
        {
            if (screen.liveTexture) screen.liveTexture->Release();
            if (screen.immediateContext) screen.immediateContext->Release();

            screen.liveTextureWidth = modifiedDesc.Width;
            screen.liveTextureHeight = modifiedDesc.Height;

            screen.liveTexture = *ppTexture2D;
            screen.liveTexture->AddRef(); // own a ref independent of the games
            pDevice->GetImmediateContext(&screen.immediateContext);

            scs_log(0, "[dx11::create_texture_2d] matched texture '%s'", screen.original_texture.c_str());
        }
        else {
            scs_log(2, "[dx11::create_texture_2d] rewrite of %s FAILED, hr=0x%08X", screen.original_texture.c_str(), hr);
        }
        return hr;
    }

    return CreateTexture2D_Original(pDevice, pDesc, pInitialData, ppTexture2D);
}


void new_frame()
{
    std::lock_guard<std::mutex> lock(g_screens_mutex);
    for (auto& screen : g_screens)
    {
        if (!screen.source.get())
            continue;

        if (!screen.liveTexture || !screen.immediateContext)
            continue;

        if (!screen.source->CopyLatestFrame(screen.frameScratch))
            continue;

        const UINT srcWidth = screen.source->GetWidth();
        const UINT srcHeight = screen.source->GetHeight();
        const UINT dstWidth = screen.liveTextureWidth;
        const UINT dstHeight = screen.liveTextureHeight;
        if (srcWidth == 0 || srcHeight == 0 || dstWidth == 0 || dstHeight == 0)
            continue;


        D3D11_MAPPED_SUBRESOURCE mapped;
        if (FAILED(screen.immediateContext->Map(screen.liveTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            continue;

        const uint8_t* src = screen.frameScratch.data();
        uint8_t* dstBase = static_cast<uint8_t*>(mapped.pData);

        for (UINT y = 0; y < dstHeight; ++y)
        {
            const UINT srcY = static_cast<UINT>(static_cast<uint64_t>(y) * srcHeight / dstHeight);
            const UINT dstRow = screen.flipVertical ? (dstHeight - 1 - y) : y;
            const uint8_t* srcRow = src + static_cast<size_t>(srcY) * srcWidth * 4;
            uint8_t* dstRowPtr = dstBase + static_cast<size_t>(dstRow) * mapped.RowPitch;

            if (srcWidth == dstWidth) {
                memcpy(dstRowPtr, srcRow, static_cast<size_t>(dstWidth) * 4);
                continue;
            }
            for (UINT x = 0; x < dstWidth; ++x) {
                const UINT srcX = static_cast<UINT>(static_cast<uint64_t>(x) * srcWidth / dstWidth);
                memcpy(dstRowPtr + static_cast<size_t>(x) * 4, srcRow + static_cast<size_t>(srcX) * 4, 4);
            }
        }

        screen.immediateContext->Unmap(screen.liveTexture, 0);
    }
}


namespace dx11::create_texture_2d {
	bool init()
	{
        dx11::present::on_frame(new_frame);

        ID3D11Device* pDummyDevice = nullptr;
        ID3D11DeviceContext* pDummyContext = nullptr;

        if (FAILED(D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION,
            &pDummyDevice, nullptr, &pDummyContext)))
        {
            scs_log(0, "[dx11::create_texture_2d] D3D11CreateDevice failed");
            return false;
        }

        void** deviceVtbl = *reinterpret_cast<void***>(pDummyDevice);
        void* createTexture2DAddr = deviceVtbl[5];

        MH_CreateHook(createTexture2DAddr, &HookedCreateTexture2D, reinterpret_cast<LPVOID*>(&CreateTexture2D_Original));
        MH_EnableHook(createTexture2DAddr);

        pDummyContext->Release();
        pDummyDevice->Release();

        return true;
	}
}