#pragma once

#include "content_source.h"
#include <memory>

namespace sources {
	std::unique_ptr<IContentSource> CreateWindowSource(HWND application_hwnd, const char* application_title = nullptr, uint8_t framerate = 30);
}