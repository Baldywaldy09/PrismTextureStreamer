#pragma once
#include <functional>

namespace dx11::present {
	bool init();
	void shutdown();
	void on_frame(std::function<void()> callback);
}
