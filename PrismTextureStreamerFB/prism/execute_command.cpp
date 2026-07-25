#include "prism.h"

#include "../bmem.h"

namespace prism::execute_command {
	inline uint64_t address{};

	uint64_t get() {
		if (address)
			return address;

		address = bmem::patternScan("40 53 48 81 EC 40 ?? ?? ?? 48 8B D9 83 FA FF");
		// todo checks

		return address;
	}


	bool call(prism::string* command, int queue_id) {
		if (!address && !get()) {
			return false;
		}

		typedef bool(*function_t)(prism::string* command, int queue_id);
		function_t function = (function_t)address;

		return function(command, queue_id);
	}
}

