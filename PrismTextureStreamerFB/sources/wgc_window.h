#pragma once

#include "content_source.h"
#include <memory>

namespace sources {
	std::unique_ptr<IContentSource> CreateWgcWindowSource(HWND application_hwnd, const char* application_title = nullptr);
}