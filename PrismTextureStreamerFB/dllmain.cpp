#include <Windows.h>
#include <scs_sdk/scssdk_telemetry.h>
#include "scs_logging.h"

#include "version.h"

#include "sources/wgc_dispatcher.h"
#include <MinHook/MinHook.h>
#include "bmem.h"
#include "screens.h"
#include "win32/win32.h"
#include "dx11/dx11.h"
#include "dinput8/dinput8.h"
#include "prism/prism.h"
#include "menu/menu.h"

using namespace scs_logging;
#pragma comment(lib, "minhook.x64.lib")

#ifdef _DEBUG
#pragma comment(lib, "ImGuiD.lib")
#else
#pragma comment(lib, "ImGuiR.lib")
#endif



namespace
{
    // A conditional jump in the game's in-cab screen renderer that we flip to an
    // unconditional jump, so the game stops drawing over our replaced texture.
    struct jump_patch_t
    {
        const char* name;
        const char* pattern;
        int64_t     offset;       // from pattern start to the jump instruction
        uint8_t     original[2];  // bytes of the conditional jump (unpatched)
        uint8_t     patched[2];   // bytes of the unconditional jump (patched)
        size_t      length;       // number of bytes to swap (1 or 2)

        uintptr_t   address = 0;
        bool        failed  = false;
    };

    // Each patch keeps the jump displacement, only the opcode bytes change:
    //   near: 0F 84 rel32 (je)  ->  90 E9 rel32 (nop; jmp)  - same target
    //   short: 74 rel8    (je)  ->  EB rel8     (jmp)       - same target
    jump_patch_t g_gps_patch{
        "gps", "49 8B 96 ?? ?? ?? ?? 48 85 D2 0F 84 ?? ?? ?? ?? 4D 8B 86", 10,
        { 0x0F, 0x84 }, { 0x90, 0xE9 }, 2 };

    jump_patch_t g_dash_patch{
        "dashboard", "4C 8D 3D ?? ?? ?? ?? 48 85 D2 0F 84", 10,
        { 0x0F, 0x84 }, { 0x90, 0xE9 }, 2 };

    // 1.61 and older: "0F 84 ?? ?? ?? ?? 45 84 E4 4D 0F 45 FD" (near je)
    // 1.62: the compiler emits a short je here instead
    jump_patch_t g_custom_patch{
        "custom", "74 ?? 45 84 E4 4D 0F 45 FD", 0,
        { 0x74 }, { 0xEB }, 1 };

    bool resolve_patch(jump_patch_t& patch)
    {
        if (patch.address) return true;
        if (patch.failed) return false;

        const uintptr_t hit = bmem::patternScan(patch.pattern);
        if (!hit) {
            scs_log(SCS_LOG_TYPE_error, "[patch] '%s': pattern not found, patch disabled (unsupported game version?)", patch.name);
            patch.failed = true;
            return false;
        }

        const uintptr_t address = hit + patch.offset;
        const bool is_original = memcmp(reinterpret_cast<void*>(address), patch.original, patch.length) == 0;
        const bool is_patched = memcmp(reinterpret_cast<void*>(address), patch.patched, patch.length) == 0;
        if (!is_original && !is_patched) {
            scs_log(SCS_LOG_TYPE_error, "[patch] '%s': unexpected bytes at eurotrucks2.exe+0x%llX, patch disabled",
                patch.name, static_cast<unsigned long long>(address - bmem::moduleBase));
            patch.failed = true;
            return false;
        }

        patch.address = address;
        scs_log(SCS_LOG_TYPE_message, "[patch] '%s' found at eurotrucks2.exe+0x%llX",
            patch.name, static_cast<unsigned long long>(address - bmem::moduleBase));
        return true;
    }

    void set_patch(jump_patch_t& patch, bool enable)
    {
        if (!resolve_patch(patch))
            return;

        void* target = reinterpret_cast<void*>(patch.address);

        DWORD oldProtect;
        if (!VirtualProtect(target, patch.length, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            scs_log(SCS_LOG_TYPE_error, "[patch] '%s': VirtualProtect failed (%lu), patch disabled", patch.name, GetLastError());
            patch.failed = true;
            patch.address = 0;
            return;
        }

        memcpy(target, enable ? patch.patched : patch.original, patch.length);

        VirtualProtect(target, patch.length, oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), target, patch.length);
    }
}

SCSAPI_VOID telemetry_tick(const scs_event_t event, const void* const event_info, scs_context_t context)
{
    static bool gps_patched{};
    static bool dash_patched{};
    static bool custom_patched{};

    bool has_gps{};
    bool has_dash{};
    bool has_custom{};

    {
        std::lock_guard<std::mutex> lock(g_screens_mutex);
        for (auto& screen : g_screens)
        {
            if (screen.type == screen_type_t::GPS)
                has_gps = true;
            else if (screen.type == screen_type_t::DASHBOARD)
                has_dash = true;
            else if (screen.type == screen_type_t::CUSTOM)
                has_custom = true;
        }
    }

    if (has_gps != gps_patched)
    {
        set_patch(g_gps_patch, has_gps);
        gps_patched = has_gps;
    }

    if (has_dash != dash_patched)
    {
        set_patch(g_dash_patch, has_dash);
        dash_patched = has_dash;
    }

    if (has_custom != custom_patched)
    {
        set_patch(g_custom_patch, has_custom);
        custom_patched = has_custom;
    }
}

#pragma comment( linker, "/export:scs_telemetry_init=scs_telemetry_init" )
SCSAPI_RESULT scs_telemetry_init(const scs_u32_t version, const scs_telemetry_init_params_t* const params)
{
    scs_logging::init(params, "PrismTextureStreamer v" + std::string(g_version));
    scs_log(0, "Starting PrismTextureStreamer | By: Baldy09");

    const scs_telemetry_init_params_v101_t* version_params = reinterpret_cast<const scs_telemetry_init_params_v101_t*>(params);
    version_params->register_for_event(SCS_TELEMETRY_EVENT_frame_start, telemetry_tick, nullptr);

    if (MH_Initialize() != MH_OK) {
        scs_log(0, "Failed to initialize MinHook!");
        return SCS_RESULT_generic_error;
    }

    scs_log(0, "Starting Direct Input 8 hooks...");
    dinput8::init();

    scs_log(0, "Starting DX11 hooks...");
    dx11::init();

    scs_log(0, "Starting Prism3D hooks...");
    if (!prism::init())
        scs_log(SCS_LOG_TYPE_error, "Some Prism3D hooks failed to initialize, see errors above");

    scs_log(0, "Starting Menu GUI...");
    Gui::init();


    scs_log(0, "Plugin Started");
    return SCS_RESULT_ok;
}

#pragma comment( linker, "/export:scs_telemetry_shutdown=scs_telemetry_shutdown" )
SCSAPI_VOID scs_telemetry_shutdown()
{
    {
        std::lock_guard<std::mutex> lock(g_screens_mutex);
        for (auto& screen : g_screens)
        {
            if (screen.liveTexture) screen.liveTexture->Release();
            if (screen.immediateContext) screen.immediateContext->Release();
        }
        g_screens.clear(); // Stops the sources
    }

    sources::WgcDispatcher::Instance().Stop();

    dx11::shutdown();
    win32::shutdown();
    dinput8::shutdown();
    //prism::shutdown();

    MH_DisableHook(MH_ALL_HOOKS);
    MH_RemoveHook(MH_ALL_HOOKS);
    MH_Uninitialize();

    scs_log(0, "Plugin Shutdown");
}