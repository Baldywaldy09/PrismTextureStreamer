#pragma once

namespace dinput8 {
	bool init();
	void shutdown();

	void hide_mouse();
	void show_mouse();
	void set_mouse(bool visible);
}
