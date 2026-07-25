#pragma once
#include <string>
#include <vector>
#include <scs_sdk/scssdk_telemetry.h>
#include <cstdarg>

namespace scs_logging
{
    namespace values {
        inline std::string projectName = "plugin";
        inline bool initalized = false;
        inline scs_log_t game_logger;
    }

    inline void init(const scs_telemetry_init_params_t* const params, std::string projectName)
	{
		if (values::initalized) return;

        values::projectName = projectName;

        const scs_telemetry_init_params_v101_t* version_params = reinterpret_cast<const scs_telemetry_init_params_v101_t*>(params);
        values::game_logger = version_params->common.log;

        values::initalized = true;
	}

    inline void scs_log(scs_log_type_t log_type, const char* format, ...)
	{
		if (!values::initalized) return;

        va_list args;
        va_start(args, format);

        std::vector<char> buffer(1024);
        int len = std::vsnprintf(buffer.data(), buffer.size(), format, args);

        if (len < 0) {
            va_end(args);
            return;
        }

        if (static_cast<size_t>(len) >= buffer.size()) {
            buffer.resize(len + 1);
            std::vsnprintf(buffer.data(), buffer.size(), format, args);
        }

        va_end(args);

        values::game_logger(log_type, ("[" + values::projectName + "] " + buffer.data()).c_str());
	}

    inline void shutdown()
    {
        scs_log(0, "Plugin Unloaded");
    }

}