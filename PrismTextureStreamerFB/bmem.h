/*----------------------------------------*/
// Project	: TrucklineMP
// File		: bmem.h
//
// Author	: Baldywaldy09 | Hunter - github.com/Baldywaldy09
/*-----------------------------------------*/

#pragma once

#include <windows.h>
#include <cstdint>
#include <string>
#include <sstream>
#include <vector>
#include <map>

#define MINHOOK_ENABLED 1

#if MINHOOK_ENABLED
#include <MinHook/MinHook.h>
#pragma comment(lib, "minhook.x64.lib")
#endif

namespace bmem
{
	// custom byte type
	struct pattern_byte
	{
		uint8_t byte{};
		bool ignore{};
	};

	inline uintptr_t moduleBase;
	inline uint64_t moduleSize;
	inline std::string appliedModule = "";

	inline bool setModule(std::string module)
	{
		if (appliedModule == module)
			return true;

		if (module == "current")
			moduleBase = (uintptr_t)GetModuleHandleA(nullptr);
		else
			moduleBase = (uintptr_t)GetModuleHandleA(module.c_str());

		if (!moduleBase)
			return false;


		const auto* header = (IMAGE_DOS_HEADER*)moduleBase;
		const auto* nt_header = (IMAGE_NT_HEADERS64*)((uint8_t*)header + header->e_lfanew);
		moduleSize = nt_header->OptionalHeader.SizeOfImage;

		appliedModule = module;
		return true;
	}



	inline uintptr_t patternScan(const char* patternSTR, std::string moduleToSet = "current")
	{
		if (!setModule(moduleToSet))
			return 0;

		std::vector<pattern_byte> pattern;

		std::istringstream stream(patternSTR);
		std::string token;
		while (stream >> token) {
			if (token.length() > 2 || token.length() < 1)
				return 0; // Invalid pattern

			pattern_byte pbyte;
			if (token == "??" || token == "?") {
				pbyte.ignore = true;
			}
			else
			{
				unsigned int byte;
				std::istringstream(token) >> std::hex >> byte;
				pbyte.byte = (uint8_t)byte;
			}

			pattern.push_back(pbyte);
		}

		if (pattern[0].ignore) {
			return 0;
		}

		uintptr_t patternStart = 0;
		for (uint64_t i = 0; i <= moduleSize - pattern.size(); i++)
		{
			bool matched = true;
			for (size_t j = 0; j < pattern.size(); j++)
			{
				if (pattern[j].ignore)
					continue;

				uint8_t currentByte = *(uint8_t*)(moduleBase + i + j);
				if (currentByte != pattern[j].byte)
				{
					matched = false;
					break; // Pattern doesnt fit
				}
			}

			if (matched)
			{
				patternStart = moduleBase + i;
				break; // We found the pattern, stop scanning the module
			}
		}

		return patternStart;
	}

	inline bool isAddressValid(uintptr_t address)
	{
		if (address < moduleBase || address > moduleBase + moduleSize)
			return false;

		return true;
	}

	inline uintptr_t relativeToAbsolute(uintptr_t instructionAddress, int offsetToAbsolute, int instructionSize)
	{
		int32_t relativeOffset = *reinterpret_cast<int32_t*>(instructionAddress + offsetToAbsolute);
		uintptr_t absoluteAddress = instructionAddress + instructionSize + relativeOffset;
		return absoluteAddress;
	}


	// Allocates memory within 2gb of a target
	inline void* allocateNear(void* target)
	{
		SYSTEM_INFO sysInfo;
		GetSystemInfo(&sysInfo);

		uint64_t pageSize = sysInfo.dwPageSize;
		uint64_t start = (uint64_t)target;

		uint64_t min = start > 0x7FFFFFFF ? start - 0x7FFFFFFF : 0;
		uint64_t max = start + 0x7FFFFFFF;

		for (uint64_t addr = min; addr < max; addr += pageSize)
		{
			void* mem = VirtualAlloc(
				(void*)addr,
				pageSize,
				MEM_RESERVE | MEM_COMMIT,
				PAGE_EXECUTE_READWRITE
			);

			if (mem)
			{
				int64_t diff = (int64_t)mem - (int64_t)target;
				if (diff <= INT32_MAX && diff >= INT32_MIN) {
					return mem; // valid near allocation
				}

				// Not actually near — free and continue
				VirtualFree(mem, 0, MEM_RELEASE);
			}
		}

		return nullptr;
	}


#if MINHOOK_ENABLED
	// Will return true if error
	inline bool minhookError(MH_STATUS result) {
		return result != MH_OK;
	}

	inline MH_STATUS InitMinHook() {
		return MH_Initialize();
	}

	inline void MinHookShutdown()
	{
		MH_RemoveHook(MH_ALL_HOOKS);
		MH_Uninitialize();
	}

	// create and enable a hook using minhook
	inline MH_STATUS hookFunction(LPVOID target, LPVOID detour, LPVOID* original)
	{
		MH_STATUS status = MH_CreateHook(target, detour, original);
		if (status != MH_OK) {
			return status;
		}

		status = MH_EnableHook((LPVOID)target);
		if (status != MH_OK) {
			return status;
		}

		return status;
	}

	inline const char* translateMinhookStatus(MH_STATUS status) {
		return MH_StatusToString(status);
	}
#endif
}