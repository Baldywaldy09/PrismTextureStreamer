#pragma once
#include <winrt\base.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>

namespace sources {

    // This allows us to keep all our wgc source logic to 1 thread designed for this
    class WgcDispatcher
    {
        bool m_stopping{};

        std::thread m_thread;
        std::thread::id m_threadId;

        std::mutex m_mutex;
        std::condition_variable m_cv;

        std::deque<std::function<void()>> m_queue;

        WgcDispatcher() {
            m_thread = std::thread([this] { Run(); });
            m_threadId = m_thread.get_id();
        }

        void Run()
        {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);

            while (true)
            {
                MSG msg;
                while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }


                std::function<void()> job;

                {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    while (!m_stopping && m_queue.empty()) {
                        m_cv.wait_for(lock, std::chrono::milliseconds(10));
                    }

                    if (m_stopping && m_queue.empty())
                        break;

                    job = std::move(m_queue.front());
                    m_queue.pop_front();
                }

                job();
            }

            winrt::uninit_apartment();
        }


    public:
        static WgcDispatcher& Instance()
        {
            static WgcDispatcher instance;
            return instance;
        }

        // Run a function on the WGC thread and get its result
        template<typename Fn>
        auto PostResult(Fn&& fn) -> decltype(fn())
        {
            if (std::this_thread::get_id() == m_threadId) {
                // Already on the dispatcher thread, running this normally would deadlock the queue against itself, call it inline
                return fn();
            }

            using return_type = decltype(fn());

            std::packaged_task<return_type()> task(std::forward<Fn>(fn));
            std::future<return_type> future = task.get_future();

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_queue.emplace_back([&task]() { task(); });
            }
            m_cv.notify_one();

            return future.get(); // blocks until the WGC thread runs it
        }

        // no return type implimentation
        void PostResult(std::function<void()> fn)
        {
            if (std::this_thread::get_id() == m_threadId) {
                return fn();
            }

            std::packaged_task<void()> task(std::move(fn));
            std::future<void> future = task.get_future();

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_queue.emplace_back([&task]() { task(); });
            }
            m_cv.notify_one();

            future.get(); // blocks
        }

        // Use for things like destruction where you dont need a return value (but still want it run on the right thread)
        void Post(std::function<void()> fn)
        {
            if (std::this_thread::get_id() == m_threadId) {
                return fn();
            }

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_queue.emplace_back(std::move(fn));
            }
            m_cv.notify_one();
        }


        void Stop()
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_stopping = true;
            }
            m_cv.notify_one();

            if (m_thread.joinable()) m_thread.join();
        }
    };

}