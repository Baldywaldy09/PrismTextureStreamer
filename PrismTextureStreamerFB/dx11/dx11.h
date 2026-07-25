#pragma once
#include <d3d11.h>

#include "present.h"
#include "CreateTexture2D.h"

#pragma comment(lib, "d3d11.lib")

namespace dx11 {
	inline bool init()
	{
		// TODO Create fake devices here and pass from here, instead of each creating their own

		if (!present::init())
			return false;

		if (!create_texture_2d::init())
			return false;
	}

	inline void shutdown()
	{
		present::shutdown();
		// create_texture_2d::shutdown();
	}
}
