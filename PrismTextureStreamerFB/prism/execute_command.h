#pragma once

namespace prism {
	class string;
}
namespace prism::execute_command {
	uint64_t get();
	bool call(prism::string* command, int queue_id);
}
