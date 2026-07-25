#include "dx11.h"
#include "../win32/win32.h"
#include <MinHook/MinHook.h>
#include "../scs_logging.h"
using namespace scs_logging;

#include <ImGui/imgui.h>
#include <ImGui/imgui_impl_dx11.h>
#include <ImGui/imgui_impl_win32.h>

#include <vector>
#include <functional>

static std::vector<std::function<void()>> frame_callbacks{};
static std::vector<std::function<void()>> pending_callbacks{};

static ID3D11Device* device{};
static ID3D11DeviceContext* context{};
static ID3D11RenderTargetView* mainRenderTargetView{};

static void* present_function_address{};
static void* resize_buffers_function_address{};

typedef HRESULT(__stdcall* present_t)(IDXGISwapChain*, UINT, UINT);
static present_t original_present{};
HRESULT hooked_present(IDXGISwapChain* SwapChain, UINT SyncInterval, UINT Flags)
{
    static bool init{};
    if (!init) {
        DXGI_SWAP_CHAIN_DESC desc;
        HRESULT result = SwapChain->GetDesc(&desc);
        if (!SUCCEEDED(result)) {
            scs_log(2, "Failed to get description.");
            return result;
        }

        result = SwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&device);
        if (!SUCCEEDED(result)) {
            scs_log(2, "Failed to get device.");
            return result;
        }

        device->GetImmediateContext(&context);


        // Create render target view
        ID3D11Texture2D* backBuffer{};
        result = SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&backBuffer);
        if (!SUCCEEDED(result)) {
            scs_log(2, "Failed to create back buffer.");
            return result;
        }

        result = device->CreateRenderTargetView(backBuffer, NULL, &mainRenderTargetView);
        if (!SUCCEEDED(result)) {
            scs_log(2, "Failed to create render target view.");
            backBuffer->Release();
            return result;
        }

        backBuffer->Release();

        win32::init(desc.OutputWindow);

        ImGuiContext* ctx = ImGui::CreateContext();
        ImGui::SetCurrentContext(ctx);

        ImGui_ImplWin32_Init(desc.OutputWindow);
        ImGui_ImplDX11_Init(device, context);

        ImGuiIO& io = ImGui::GetIO();
        io.FontDefault = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\Arial.ttf", 15.0f);
        io.Fonts->Build();

        init = true;
    }


    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    if (!pending_callbacks.empty()) {
        for (auto& cb : pending_callbacks)
            frame_callbacks.push_back(std::move(cb));
        pending_callbacks.clear();
    }

    for (auto& cb : frame_callbacks)
        cb();

    ImGui::EndFrame();
    ImGui::Render();

    context->OMSetRenderTargets(1, &mainRenderTargetView, NULL);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    return original_present(SwapChain, SyncInterval, Flags);
}


typedef HRESULT(__stdcall* resize_buffers_t)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
static resize_buffers_t original_resize_buffers{};
HRESULT hooked_resize_buffers(IDXGISwapChain* SwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
    if (!device || !context) // We are not ready to handle this ourself
        return original_resize_buffers(SwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);


    context->OMSetRenderTargets(0, nullptr, nullptr);
    if (mainRenderTargetView) {
        mainRenderTargetView->Release();
        mainRenderTargetView = nullptr;
    }

    HRESULT result = original_resize_buffers(SwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
    if (!SUCCEEDED(result)) {
        scs_log(2, "Original \"resize buffers\" function failed.");
        return result;
    }

    ID3D11Texture2D* backBuffer{};
    result = SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&backBuffer);
    if (!SUCCEEDED(result)) {
        scs_log(2, "Failed to create back buffer.");
        return result;
    }

    result = device->CreateRenderTargetView(backBuffer, NULL, &mainRenderTargetView);
    if (!SUCCEEDED(result)) {
        scs_log(2, "Failed to create render target view.");
        backBuffer->Release();
        return result;
    }

    backBuffer->Release();

    return result;
}



namespace dx11::present {
	bool init()
	{
        // Create fakes to grab real vtable
        IDXGISwapChain* swapChain{};
        ID3D11Device* device{};
        ID3D11DeviceContext* context{};

        DXGI_SWAP_CHAIN_DESC desc = {};
        desc.BufferCount = 1;
        desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.OutputWindow = GetForegroundWindow();
        desc.SampleDesc.Count = 1;
        desc.Windowed = TRUE;

        D3D_FEATURE_LEVEL level;
        if (FAILED(D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            0,
            nullptr,
            0,
            D3D11_SDK_VERSION,
            &desc,
            &swapChain,
            &device,
            &level,
            &context
        ))) {
            scs_log(0, "[dx11::present] D3D11CreateDeviceAndSwapChain failed.");
            return false;
        }

        void** vtable = *reinterpret_cast<void***>(swapChain);
        present_function_address = vtable[8];
        MH_CreateHook(
            present_function_address,
            &hooked_present,
            reinterpret_cast<void**>(&original_present)
        );
        MH_EnableHook(present_function_address);

        resize_buffers_function_address = vtable[13];
        MH_CreateHook(
            resize_buffers_function_address,
            &hooked_resize_buffers,
            reinterpret_cast<void**>(&original_resize_buffers)
        );
        MH_EnableHook(resize_buffers_function_address);


        // Have vtable, just get rid of fakes
        context->Release();
        device->Release();
        swapChain->Release();

        return true;
	}

    void shutdown()
    {
        MH_DisableHook(present_function_address);
        MH_RemoveHook(present_function_address);

        MH_DisableHook(resize_buffers_function_address);
        MH_RemoveHook(resize_buffers_function_address);


        if (device) device->Release();
        if (context) context->Release();
        if (mainRenderTargetView) mainRenderTargetView->Release();
    }

    void on_frame(std::function<void()> callback)
    {
        pending_callbacks.push_back(callback);
    }
}
