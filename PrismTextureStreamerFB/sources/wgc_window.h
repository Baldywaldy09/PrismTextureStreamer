#pragma once

#include "content_source.h"
#include <memory>

namespace sources {
	std::unique_ptr<IContentSource> CreateWgcWindowSource(const char* application_name, const char* window_title = nullptr);
}
