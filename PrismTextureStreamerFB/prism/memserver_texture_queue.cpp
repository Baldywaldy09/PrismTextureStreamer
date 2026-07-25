#include "prism.h"

#include "../bmem.h"

#include "../scs_logging.h"
using namespace scs_logging;

#include "../screens.h"

typedef char(__fastcall* memserver_texture_queue_processor_t)(uint8_t* memserver);
static memserver_texture_queue_processor_t original_memserver_texture_queue_processor{};

char memserver_texture_queue_processor(uint8_t* memserver)
{
    prism::list_node_t<prism::mem_tobj_t>* first_node = *(prism::list_node_t<prism::mem_tobj_t>**)(memserver + 0x170);
    void* fake_node = (void*)(memserver + 0x180);

    if (first_node != fake_node)
    {
        prism::list_node_t<prism::mem_tobj_t>* target = first_node;
        while (target != fake_node)
        {
            prism::mem_tobj_t* tobj = target->m_item;

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
        // 1.60
        uint64_t memserver_texture_queue_processor_address = bmem::patternScan("48 8D 68 ?? 48 81 EC ?? ?? ?? ?? 48 8B F9 4C") - 19;

        MH_CreateHook(
            (LPVOID)memserver_texture_queue_processor_address,
            &memserver_texture_queue_processor,
            reinterpret_cast<void**>(&original_memserver_texture_queue_processor)
        );

        MH_EnableHook((LPVOID)memserver_texture_queue_processor_address);

        return true;
	}
}
