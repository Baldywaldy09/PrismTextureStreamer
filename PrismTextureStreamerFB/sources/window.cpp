#define _CRT_SECURE_NO_WARNINGS // Thanks microsoft

#include "window.h"

#include "../scs_logging.h"
using namespace scs_logging;

#include <vector>
#include <mutex>

struct FindWindowData {
    const char* exeName;
    HWND result;
};
HWND FindWindowByExeName(const char* exeName)
{
    FindWindowData data{ exeName, nullptr };

    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        auto* data = reinterpret_cast<FindWindowData*>(lParam);

        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (!pid) return TRUE;

        HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!hProc) return TRUE;

        char path[MAX_PATH];
        DWORD size = MAX_PATH;
        bool ok = QueryFullProcessImageNameA(hProc, 0, path, &size);
        CloseHandle(hProc);
        if (!ok) return TRUE;

        // grab just the filename part
        const char* fileName = strrchr(path, '\\');
        fileName = fileName ? fileName + 1 : path;

        if (_stricmp(fileName, data->exeName) == 0) { // comare case insensitive
            data->result = hwnd;
            return FALSE; // found, stop checking windows
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&data));

    return data.result;
}


namespace sources {
    class WindowSource : public IContentSource
    {
    private:
        char* m_appname{};
        HWND m_hwnd{};
        std::atomic<uint32_t> m_width{};
        std::atomic<uint32_t> m_height{};
        uint8_t m_framerate{};

        std::vector<uint8_t> m_frameBuffer;
        std::mutex m_bufferMutex;

        std::atomic<bool> m_haveFrame{};

        std::thread m_thread;
        std::atomic<bool> m_stopRequested{};

        void CaptureLoop()
        {
            HDC windowDC = GetDC(m_hwnd);
            HDC memDC = CreateCompatibleDC(windowDC);
            HBITMAP bitmap = CreateCompatibleBitmap(windowDC, m_width, m_height);
            HGDIOBJ oldObj = SelectObject(memDC, bitmap);

            BITMAPINFO bmi = {};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = static_cast<LONG>(m_width);
            bmi.bmiHeader.biHeight = -static_cast<LONG>(m_height);
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;


            const size_t arraysize = static_cast<size_t>(m_width) * m_height * 4;
            std::vector<uint8_t> bgraScratch(arraysize);
            m_frameBuffer.resize(arraysize);

            while (!m_stopRequested.load())
            {
                const auto frameInterval = std::chrono::milliseconds(1000 / m_framerate);
                auto frameStart = std::chrono::steady_clock::now();

                if (!IsWindow(m_hwnd)) { scs_log(0, "[WindowSource] Target window '%s' no longer exists, stopping capture for this window", m_appname); break; }

                RECT rect;
                GetClientRect(m_hwnd, &rect);
                const uint32_t width = rect.right - rect.left;
                const uint32_t height = rect.bottom - rect.top;

                bmi.bmiHeader.biWidth = static_cast<LONG>(width);
                bmi.bmiHeader.biHeight = -static_cast<LONG>(height);

                const size_t arraysize = static_cast<size_t>(width) * height * 4;
                if (bgraScratch.size() != arraysize)
                {
                    bgraScratch.resize(arraysize);
                    SelectObject(memDC, oldObj); // deselect current bitmap before deleting
                    DeleteObject(bitmap);
                    bitmap = CreateCompatibleBitmap(windowDC, width, height);
                    SelectObject(memDC, bitmap);
                }

                PrintWindow(m_hwnd, memDC, 2 /* PW_RENDERFULLCONTENT */);
                GetDIBits(memDC, bitmap, 0, height, bgraScratch.data(), &bmi, DIB_RGB_COLORS);

                {
                    std::lock_guard<std::mutex> lock(m_bufferMutex);

                    if (m_frameBuffer.size() != arraysize)
                        m_frameBuffer.resize(arraysize);

                    const uint8_t* src = bgraScratch.data();
                    uint8_t* dst = m_frameBuffer.data();
                    const size_t pixelCount = static_cast<size_t>(width) * height;
                    for (size_t i = 0; i < pixelCount; ++i)
                    {
                        dst[i * 4 + 0] = src[i * 4 + 2];
                        dst[i * 4 + 1] = src[i * 4 + 1];
                        dst[i * 4 + 2] = src[i * 4 + 0];
                        dst[i * 4 + 3] = 255;
                    }
                    m_haveFrame = true;
                }

                m_width = width;
                m_height = height;

                auto elapsed = std::chrono::steady_clock::now() - frameStart;
                if (elapsed < frameInterval) std::this_thread::sleep_for(frameInterval - elapsed);
            }

            SelectObject(memDC, oldObj);
            DeleteObject(bitmap);
            DeleteDC(memDC);
            ReleaseDC(m_hwnd, windowDC);

            scs_log(0, "[WindowSource] Source for %s has stopped", m_appname);
        }

    public:
        explicit WindowSource(const char* application_name)
        {
            // Create our own ownership
            m_appname = new char[strlen(application_name) + 1]{};
            strcpy(m_appname, application_name);
        }
        ~WindowSource() override
        {
            m_stopRequested = true;
            if (m_thread.joinable()) m_thread.join();

            if (m_appname) delete[] m_appname;
        }


        bool Start(uint8_t framerate)
        {
            m_framerate = framerate;
            m_hwnd = FindWindowByExeName(m_appname);
            if (!m_hwnd) { scs_log(2, "[WindowSource] Application %s not found at source startup", m_appname); return false; }

            RECT rect;
            GetClientRect(m_hwnd, &rect);
            m_width = rect.right - rect.left;
            m_height = rect.bottom - rect.top;

            if (m_width == 0 || m_height == 0) { scs_log(2, "[WindowSource] Application %s has zero client area", m_appname); return false; }

            m_thread = std::thread(&WindowSource::CaptureLoop, this);

            scs_log(0, "[WindowSource] Source for %s has started", m_appname);
            return true;
        }

        uint32_t GetWidth() const override { return m_width.load(); }
        uint32_t GetHeight() const override { return m_height.load(); }
        void SetFramerate(uint8_t framerate) override { m_framerate = framerate; }

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


	std::unique_ptr<IContentSource> CreateWindowSource(const char* application_name, uint8_t framerate)
	{
		auto src = std::make_unique<WindowSource>(application_name);
		if (!src->Start(framerate)) return nullptr;
		return src;
	}
}
