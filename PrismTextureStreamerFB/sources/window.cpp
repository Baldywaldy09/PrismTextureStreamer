#define _CRT_SECURE_NO_WARNINGS // Thanks microsoft

#include "window.h"

#include "../scs_logging.h"
using namespace scs_logging;

#include <vector>
#include <mutex>

namespace sources {
    class WindowSource : public IContentSource
    {
    private:
        HWND m_apphwnd{};
        char* m_apptitle{};
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
            HDC windowDC = GetDC(m_apphwnd);
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

                if (!IsWindow(m_apphwnd)) { scs_log(0, "[WindowSource] Target window %s no longer exists, stopping capture for this window", m_apptitle ? m_apptitle : "NO_TITLE"); break; }
                if (IsIconic(m_apphwnd)) { std::this_thread::sleep_for(frameInterval); continue; } // minimized

                RECT rect;
                GetClientRect(m_apphwnd, &rect);
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

                BOOL pwOk = PrintWindow(m_apphwnd, memDC, 2 /* PW_RENDERFULLCONTENT */);
                if (!pwOk)
                {
                    scs_log(0, "[WindowSource] PrintWindow failed for %s, err=%lu", m_apptitle ? m_apptitle : "NO_TITLE", GetLastError());
                    std::this_thread::sleep_for(frameInterval);
                    continue;
                }
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
            ReleaseDC(m_apphwnd, windowDC);

            scs_log(0, "[WindowSource] Source for %s has stopped", m_apptitle ? m_apptitle : "NO_TITLE");
        }

    public:
        explicit WindowSource(HWND application_hwnd, const char* application_title)
        {
            m_apphwnd = application_hwnd;

            if (application_title) {
                m_apptitle = new char[strlen(application_title) + 1] {};
                strcpy(m_apptitle, application_title);
            }
        }
        ~WindowSource() override
        {
            m_stopRequested = true;
            if (m_thread.joinable()) m_thread.join();

            if (m_apptitle) delete[] m_apptitle;
        }


        bool Start(uint8_t framerate)
        {
            m_framerate = framerate;
            if (!IsWindow(m_apphwnd)) { scs_log(2, "[WindowSource] Application %s not found at source startup", m_apptitle ? m_apptitle : "NO_TITLE"); return false; }

            m_thread = std::thread(&WindowSource::CaptureLoop, this);

            scs_log(0, "[WindowSource] Source for %s has started", m_apptitle ? m_apptitle : "NO_TITLE");
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


	std::unique_ptr<IContentSource> CreateWindowSource(HWND application_hwnd, const char* application_title, uint8_t framerate)
	{
		auto src = std::make_unique<WindowSource>(application_hwnd, application_title);
		if (!src->Start(framerate)) return nullptr;
		return src;
	}
}