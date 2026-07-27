#define _CRT_SECURE_NO_WARNINGS // Thanks microsoft

#include "wgc_window.h"

#include "../scs_logging.h"
using namespace scs_logging;

#include "../dx11/dx11.h"

#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Foundation.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
using namespace winrt::Windows;

#include "wgc_dispatcher.h"

#include <vector>
#include <mutex>

#pragma comment(lib, "dxgi.lib")

struct FindWindowData {
    const char* exeName;
    const char* windowTitle;
    HWND result;
};
static HWND FindWindowByNameAndTitle(const char* exeName, const char* windowTitle)
{
    FindWindowData data{ exeName, windowTitle, nullptr };

    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        auto* data = reinterpret_cast<FindWindowData*>(lParam);

        if (!IsWindowVisible(hwnd))
            return TRUE;

        LONG exStyle = GetWindowLongA(hwnd, GWL_EXSTYLE);
        if (exStyle & WS_EX_TOOLWINDOW)
            return TRUE;

        bool titleMatch{};
        if (data->windowTitle) {
            LRESULT lengthResult = 0;
            if (!SendMessageTimeoutA(hwnd, WM_GETTEXTLENGTH, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 200, (PDWORD_PTR)&lengthResult)) {
                return TRUE; // didn't respond in time - skip it
            }
            int titleLen = static_cast<int>(lengthResult);

            if (titleLen != 0)
            {
                std::string windowTitle = std::string(titleLen + 1, '\0');
                GetWindowTextA(hwnd, windowTitle.data(), titleLen + 1);
                windowTitle.resize(titleLen);

                if (windowTitle == std::string_view(data->windowTitle))
                    titleMatch = true; // Title matches, but so must the exe name
            }
            else
                return TRUE; // Window title was given as a search filter, so if no title, skip
        }

        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);

        HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!hProc) return TRUE;

        char path[MAX_PATH]{};
        DWORD size = MAX_PATH;
        QueryFullProcessImageNameA(hProc, 0, path, &size);
        CloseHandle(hProc);

        std::string_view applicationName(path);
        auto pos = applicationName.rfind('\\');
        applicationName = pos != std::string::npos ? applicationName.substr(pos + 1) : applicationName;

        bool exeMatch = applicationName == std::string_view(data->exeName);

        if (exeMatch && (!data->windowTitle || titleMatch)) {
            data->result = hwnd;
            return FALSE; // Exe matches, and there is either no title, or the title matched
        }

        return TRUE;
    }, reinterpret_cast<LPARAM>(&data));

    return data.result;
}

static Graphics::Capture::GraphicsCaptureItem CreateCaptureItemForWindow(HWND hwnd)
{
    auto interopFactory = winrt::get_activation_factory<Graphics::Capture::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();

    Graphics::Capture::GraphicsCaptureItem item{ nullptr };
    winrt::check_hresult(interopFactory->CreateForWindow(
        hwnd,
        winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
        winrt::put_abi(item))
    );
    return item;
}

static Graphics::DirectX::Direct3D11::IDirect3DDevice CreateD3DDeviceForWgc(ID3D11Device* d3dDevice)
{
    winrt::com_ptr<IDXGIDevice> dxgiDevice;
    d3dDevice->QueryInterface(IID_PPV_ARGS(dxgiDevice.put()));

    winrt::com_ptr<::IInspectable> inspectable;
    winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.get(), inspectable.put()));
    return inspectable.as<Graphics::DirectX::Direct3D11::IDirect3DDevice>();
}

winrt::com_ptr<IDXGIAdapter> GetAdapterForWindow(HWND hwnd)
{
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);

    winrt::com_ptr<IDXGIFactory1> factory;
    winrt::check_hresult(CreateDXGIFactory1(IID_PPV_ARGS(factory.put())));

    winrt::com_ptr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapters1(i, adapter.put()) != DXGI_ERROR_NOT_FOUND; ++i, adapter = nullptr)
    {
        winrt::com_ptr<IDXGIOutput> output;
        for (UINT j = 0; adapter->EnumOutputs(j, output.put()) != DXGI_ERROR_NOT_FOUND; ++j, output = nullptr)
        {
            DXGI_OUTPUT_DESC desc;
            output->GetDesc(&desc);
            if (desc.Monitor == monitor)
                return adapter.as<IDXGIAdapter>();
        }
    }

    return nullptr; // caller falls back to default adapter
}

winrt::com_ptr<ID3D11Device> CreateWgcCaptureDevice(HWND hwnd)
{
    winrt::com_ptr<ID3D11Device> device;
    winrt::com_ptr<ID3D11DeviceContext> context;

    D3D_FEATURE_LEVEL featureLevel;
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    auto adapter = GetAdapterForWindow(hwnd);
    D3D_DRIVER_TYPE driverType = adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE;

    HRESULT hr = D3D11CreateDevice(
        adapter.get(),
        driverType,
        nullptr,
        flags,
        nullptr, 0,
        D3D11_SDK_VERSION,
        device.put(),
        &featureLevel,
        context.put());

    winrt::check_hresult(hr);
    return device;
}

namespace sources {
    class WgcWindowSource : public IContentSource
    {
    private:
        char* m_appname{};
        char* m_apptitle{};
        HWND m_hwnd{};
        std::atomic<uint32_t> m_width{};
        std::atomic<uint32_t> m_height{};

        winrt::com_ptr < ID3D11Device> m_d3dDevice{};
        Graphics::DirectX::Direct3D11::IDirect3DDevice m_wgcDevice{ nullptr };

        Graphics::Capture::GraphicsCaptureItem m_item{ nullptr };
        Graphics::Capture::Direct3D11CaptureFramePool m_framePool{ nullptr };
        Graphics::Capture::GraphicsCaptureSession m_session{ nullptr };
        Graphics::Capture::Direct3D11CaptureFramePool::FrameArrived_revoker m_frameArrivedRevoker;

        Graphics::SizeInt32 m_lastSize{};

        std::vector<uint8_t> m_frameBuffer;
        std::mutex m_bufferMutex;
        std::mutex m_frameMutex; // Stops the frame arrived running for things like destruction

        std::atomic<bool> m_haveFrame{};
        std::atomic<bool> m_stopping{};


        void OnFrameArrived(Graphics::Capture::Direct3D11CaptureFramePool const& sender, Foundation::IInspectable const&)
        {
            std::lock_guard<std::mutex> lockFrame(m_frameMutex);
            if (m_stopping.load())
                return;

            try {
                auto frame = sender.TryGetNextFrame();
                if (!frame) return;

                auto contentSize = frame.ContentSize();

                if (contentSize.Width != m_lastSize.Width || contentSize.Height != m_lastSize.Height)
                {
                    m_lastSize = contentSize;
                    m_framePool.Recreate(m_wgcDevice, Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, contentSize);
                    return; // next frame arrives at the correct size
                }

                auto access = frame.Surface().as<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
                winrt::com_ptr<ID3D11Texture2D> gpuTexture;
                access->GetInterface(IID_PPV_ARGS(gpuTexture.put()));

                D3D11_TEXTURE2D_DESC desc;
                gpuTexture->GetDesc(&desc);
                desc.Usage = D3D11_USAGE_STAGING;
                desc.BindFlags = 0;
                desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                desc.MiscFlags = 0;

                winrt::com_ptr<ID3D11Texture2D> staging;
                m_d3dDevice->CreateTexture2D(&desc, nullptr, staging.put());

                winrt::com_ptr<ID3D11DeviceContext> ctx;
                m_d3dDevice->GetImmediateContext(ctx.put());
                ctx->CopyResource(staging.get(), gpuTexture.get());

                D3D11_MAPPED_SUBRESOURCE mapped;
                if (SUCCEEDED(ctx->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped)))
                {
                    // mapped.pData is BGRA. RowPitch may not equal width*4, so walk rows manually.
                    // We also swap B/R here so the output is RGBA to match liveTexture's format.
                    const auto rowBytes = desc.Width * 4;
                    std::lock_guard<std::mutex> lock(m_bufferMutex);
                    m_frameBuffer.resize(rowBytes * desc.Height);

                    for (UINT y = 0; y < desc.Height; ++y)
                    {
                        const uint8_t* srcRow = static_cast<uint8_t*>(mapped.pData) + y * mapped.RowPitch;
                        uint8_t* dstRow = m_frameBuffer.data() + y * rowBytes;

                        for (UINT x = 0; x < desc.Width; ++x)
                        {
                            dstRow[x * 4 + 0] = srcRow[x * 4 + 2]; // R <- B
                            dstRow[x * 4 + 1] = srcRow[x * 4 + 1]; // G <- G
                            dstRow[x * 4 + 2] = srcRow[x * 4 + 0]; // B <- R
                            dstRow[x * 4 + 3] = srcRow[x * 4 + 3]; // A <- A
                        }
                    }

                    m_width = desc.Width;
                    m_height = desc.Height;
                    ctx->Unmap(staging.get(), 0);
                    m_haveFrame = true;
                }
            }
            catch (const winrt::hresult_error& e) {
                scs_log(2, "[WgcWindowSource] OnFrameArrived failed: 0x%08X", e.code().value);
            }
        }

    public:
        explicit WgcWindowSource(const char* application_name, const char* application_title)
        {
            // Create our own ownership
            m_appname = new char[strlen(application_name) + 1] {};
            strcpy(m_appname, application_name);

            // title is optional
            if (application_title) {
                m_apptitle = new char[strlen(application_title) + 1] {};
                strcpy(m_apptitle, application_title);
            }
        }
        ~WgcWindowSource() override
        {
            {
                std::lock_guard<std::mutex> lock(m_frameMutex);
                m_stopping = true;
            }

            // Uses post result so it blocks, we dont want to deconstruct before this is done
            WgcDispatcher::Instance().PostResult([this]() {
                m_frameArrivedRevoker.revoke();
                if (m_session) m_session.Close();
                if (m_framePool) m_framePool.Close();
            });


            scs_log(0, "[WgcWindowSource] Source for %s (%s) has stopped", m_appname, m_apptitle ? m_apptitle : "NO_TITLE");
            if (m_appname) delete[] m_appname;
            if (m_apptitle) delete[] m_apptitle;
        }

        bool Start()
        {
            try {
                m_hwnd = FindWindowByNameAndTitle(m_appname, m_apptitle);
                if (!m_hwnd) { scs_log(2, "[WgcWindowSource] Application %s (%s) not found at source startup", m_appname, m_apptitle ? m_apptitle : "NO_TITLE"); return false; }

                m_d3dDevice = CreateWgcCaptureDevice(m_hwnd);

                m_wgcDevice = CreateD3DDeviceForWgc(m_d3dDevice.get());
                m_item = CreateCaptureItemForWindow(m_hwnd);

                m_lastSize = m_item.Size();
                if (m_lastSize.Width == 0 || m_lastSize.Height == 0) {
                    scs_log(2, "[WgcWindowSource] target window has zero size, deferring start");
                    return false;
                }

                m_framePool = Graphics::Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
                    m_wgcDevice,
                    Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
                    2,
                    m_lastSize
                );

                m_session = m_framePool.CreateCaptureSession(m_item);
                m_frameArrivedRevoker = m_framePool.FrameArrived(winrt::auto_revoke, { this, &WgcWindowSource::OnFrameArrived });

                m_session.StartCapture();
                //m_session.IsBorderRequired(false); // Disable the windows orange border from capturing

                scs_log(0, "[WgcWindowSource] Source for %s (%s) has started", m_appname, m_apptitle ? m_apptitle : "NO_TITLE");

                return true;
            }
            catch (const winrt::hresult_error& e)
            {
                scs_log(2, "[WgcWindowSource] Start failed: 0x%08X %ls", e.code().value, e.message().c_str());
                return false;
            }
        }

        uint32_t GetWidth() const override { return m_width.load(); }
        uint32_t GetHeight() const override { return m_height.load(); }
        void SetFramerate(uint8_t framerate) override {  }

        bool CopyLatestFrame(std::vector<uint8_t>& dst) override
        {
            if (!m_haveFrame.load()) return false;

            std::lock_guard<std::mutex> lock(m_bufferMutex);
            if (dst.size() != m_frameBuffer.size())
                dst.resize(m_frameBuffer.size());

            memcpy(dst.data(), m_frameBuffer.data(), m_frameBuffer.size());
            return true;
        }
    };


    std::unique_ptr<IContentSource> CreateWgcWindowSource(const char* application_name, const char* window_title)
    {
        std::string appname(application_name);
        std::string apptitle = window_title ? window_title : std::string();

        return WgcDispatcher::Instance().PostResult([appname, apptitle]() -> std::unique_ptr<IContentSource> {
            auto src = std::make_unique<WgcWindowSource>(appname.c_str(), apptitle.empty() ? nullptr : apptitle.c_str());
            if (!src->Start()) return nullptr;
            return src;
        });
    }
}
