#include "prism.h"

#include "../bmem.h"

#include "../scs_logging.h"
using namespace scs_logging;

#include "../screens.h"

#include <atomic>

typedef char(__fastcall* memserver_texture_queue_processor_t)(uint8_t* memserver);
static memserver_texture_queue_processor_t original_memserver_texture_queue_processor{};

char memserver_texture_queue_processor(uint8_t* memserver)
{
    static std::atomic<bool> logged_first_call{};
    if (!logged_first_call.exchange(true))
        scs_log(SCS_LOG_TYPE_message, "[prism::memserver_texture_queue] Hook active (first call)");

    // Layout verified against 1.62: queue head at +0x170, end sentinel at +0x180
    prism::list_node_t<prism::mem_tobj_t>* first_node = *(prism::list_node_t<prism::mem_tobj_t>**)(memserver + 0x170);
    void* fake_node = (void*)(memserver + 0x180);

    if (first_node != fake_node)
    {
        prism::list_node_t<prism::mem_tobj_t>* target = first_node;
        while (target && target != fake_node)
        {
            prism::mem_tobj_t* tobj = target->m_item;
            if (!tobj || !tobj->m_file_path.m_string) {
                target = target->m_next;
                continue;
            }

            std::lock_guard<std::mutex> lock(g_screens_mutex);
            for (auto& screen : g_screens) {
                if (!screen.source.get()) continue;

                if (screen.original_texture == std::string_view(tobj->m_file_path.m_string))
                {
                    tobj->m_file_path.allocate(screen.override_texture.size() + 1);
                    memcpy(tobj->m_file_path.m_string, screen.override_texture.data(), screen.override_texture.size());
                    tobj->m_file_path.m_string[screen.override_texture.size()] = '\0';

                    tobj->m_file_path.m_size = screen.override_texture.size();

                    scs_log(0, "[prism::memserver_texture_queue] Replaced '%s' with '%s'", screen.original_texture.c_str(), screen.override_texture.c_str());
                }
            }

            target = target->m_next;
        }
    }

    return original_memserver_texture_queue_processor(memserver);
}


namespace prism::memserver_texture_queue {
	bool init() {
        // 1.60 - 1.62: the pattern hits "lea rbp, [rax-xx]" inside the prologue,
        // the function itself starts 19 bytes earlier with "mov rax, rsp"
        const uintptr_t hit = bmem::patternScan("48 8D 68 ?? 48 81 EC ?? ?? ?? ?? 48 8B F9 4C");
        if (!hit) {
            scs_log(SCS_LOG_TYPE_error, "[prism::memserver_texture_queue] Pattern not found, texture replacement disabled");
            return false;
        }

        const uintptr_t address = hit - 19;
        static const uint8_t expected_prologue[] = { 0x48, 0x8B, 0xC4 }; // mov rax, rsp
        if (memcmp(reinterpret_cast<void*>(address), expected_prologue, sizeof(expected_prologue)) != 0) {
            scs_log(SCS_LOG_TYPE_error, "[prism::memserver_texture_queue] Unexpected prologue at eurotrucks2.exe+0x%llX, texture replacement disabled",
                static_cast<unsigned long long>(address - bmem::moduleBase));
            return false;
        }

        MH_STATUS status = MH_CreateHook(
            reinterpret_cast<LPVOID>(address),
            &memserver_texture_queue_processor,
            reinterpret_cast<void**>(&original_memserver_texture_queue_processor)
        );
        if (status != MH_OK) {
            scs_log(SCS_LOG_TYPE_error, "[prism::memserver_texture_queue] MH_CreateHook failed: %s", MH_StatusToString(status));
            return false;
        }

        status = MH_EnableHook(reinterpret_cast<LPVOID>(address));
        if (status != MH_OK) {
            scs_log(SCS_LOG_TYPE_error, "[prism::memserver_texture_queue] MH_EnableHook failed: %s", MH_StatusToString(status));
            MH_RemoveHook(reinterpret_cast<LPVOID>(address));
            return false;
        }

        scs_log(SCS_LOG_TYPE_message, "[prism::memserver_texture_queue] Hooked at eurotrucks2.exe+0x%llX",
            static_cast<unsigned long long>(address - bmem::moduleBase));
        return true;
	}
}
