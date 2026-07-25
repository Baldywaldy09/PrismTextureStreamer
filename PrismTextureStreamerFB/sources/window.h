#pragma once

#include "content_source.h"
#include <memory>

namespace sources {
	std::unique_ptr<IContentSource> CreateWindowSource(const char* application_name, uint8_t framerate = 30);
}
