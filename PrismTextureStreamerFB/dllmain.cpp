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
        static uint64_t patch_addr{};
        if (!patch_addr) {
            patch_addr = bmem::patternScan("49 8B 96 ?? ?? ?? ?? 48 85 D2 0F 84 ?? ?? ?? ?? 4D 8B 86") + 10;
        }

        DWORD oldProtect;
        VirtualProtect((void*)patch_addr, 2, PAGE_EXECUTE_READWRITE, &oldProtect);

        if (has_gps) {
            // JMP
            *(reinterpret_cast<uint8_t*>(patch_addr + 0)) = 0x90;
            *(reinterpret_cast<uint8_t*>(patch_addr + 1)) = 0xE9;
        }
        else {
            // JE
            *(reinterpret_cast<uint8_t*>(patch_addr + 0)) = 0x0F;
            *(reinterpret_cast<uint8_t*>(patch_addr + 1)) = 0x84;
        }

        VirtualProtect((void*)patch_addr, 2, oldProtect, &oldProtect);

        gps_patched = has_gps;
    }

    if (has_dash != dash_patched)
    {
        static uint64_t patch_addr{};
        if (!patch_addr) {
            patch_addr = bmem::patternScan("4C 8D 3D ?? ?? ?? ?? 48 85 D2 0F 84") + 10;
        }

        DWORD oldProtect;
        VirtualProtect((void*)patch_addr, 2, PAGE_EXECUTE_READWRITE, &oldProtect);

        if (has_dash) {
            // JMP
            *(reinterpret_cast<uint8_t*>(patch_addr + 0)) = 0x90;
            *(reinterpret_cast<uint8_t*>(patch_addr + 1)) = 0xE9;
        }
        else {
            // JE
            *(reinterpret_cast<uint8_t*>(patch_addr + 0)) = 0x0F;
            *(reinterpret_cast<uint8_t*>(patch_addr + 1)) = 0x84;
        }

        VirtualProtect((void*)patch_addr, 2, oldProtect, &oldProtect);

        dash_patched = has_dash;
    }

    if (has_custom != custom_patched)
    {
        static uint64_t patch_addr{};
        if (!patch_addr) {
            patch_addr = bmem::patternScan("0F 84 ?? ?? ?? ?? 45 84 E4 4D 0F 45 FD");
        }

        DWORD oldProtect;
        VirtualProtect((void*)patch_addr, 2, PAGE_EXECUTE_READWRITE, &oldProtect);

        if (has_custom) {
            // JMP
            *(reinterpret_cast<uint8_t*>(patch_addr + 0)) = 0x90;
            *(reinterpret_cast<uint8_t*>(patch_addr + 1)) = 0xE9;
        }
        else {
            // JE
            *(reinterpret_cast<uint8_t*>(patch_addr + 0)) = 0x0F;
            *(reinterpret_cast<uint8_t*>(patch_addr + 1)) = 0x84;
        }

        VirtualProtect((void*)patch_addr, 2, oldProtect, &oldProtect);

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
    prism::init();

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