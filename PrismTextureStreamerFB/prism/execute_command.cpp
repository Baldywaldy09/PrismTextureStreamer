#include "prism.h"

#include "../bmem.h"

#include "../scs_logging.h"
using namespace scs_logging;

namespace prism::execute_command {
	inline uint64_t address{};
	inline bool scan_failed{};

	uint64_t get() {
		if (address)
			return address;
		if (scan_failed)
			return 0;

		// Verified unique on 1.62
		address = bmem::patternScan("40 53 48 81 EC 40 ?? ?? ?? 48 8B D9 83 FA FF");
		if (!address) {
			scan_failed = true;
			scs_log(SCS_LOG_TYPE_error, "[prism::execute_command] Pattern not found, console commands disabled");
			return 0;
		}

		scs_log(SCS_LOG_TYPE_message, "[prism::execute_command] Found at eurotrucks2.exe+0x%llX",
			static_cast<unsigned long long>(address - bmem::moduleBase));
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

