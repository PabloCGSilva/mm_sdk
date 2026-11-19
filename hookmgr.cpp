#include "plugin.h"
#include <string>
#include <sstream>
#include <fstream>

#include <intrin.h>
#pragma intrinsic(_ReturnAddress)

bool HookMgr::isSteam = false;
bool HookMgr::isGOG = false;

static bool ReadableQword(uintptr_t addr, uint64_t& out) {
	MEMORY_BASIC_INFORMATION mbi = {};
	if (!VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi))) return false;
	if (mbi.State != MEM_COMMIT) return false;
	const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
		PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
	if ((mbi.Protect & readable) == 0 || (mbi.Protect & PAGE_GUARD)) return false;
	if (addr + sizeof(uint64_t) > (uintptr_t)mbi.BaseAddress + mbi.RegionSize) return false;
	out = *(const uint64_t*)addr;
	return true;
}

bool HookMgr::IsKnownGogBuild() {
	static int cached = -1;
	if (cached < 0) {
		uint64_t v = 0;
		cached = (ReadableQword(0x14192A150ull, v) && v == 0x1c3c00001bf0ull) ? 1 : 0;
	}
	return cached == 1;
}

bool HookMgr::SdkAddressesUsable() {
	// The SDK's ADDRESS(gog, steam) pairs only describe two specific builds.
	// The GOG one is verified by its magic value; the Steam one is assumed
	// when the magic address is readable but holds something else. Anything
	// else (address not mapped at all) means the SDK addresses are unusable
	// and callers must not follow them.
	uint64_t v = 0;
	return ReadableQword(0x14192A150ull, v);
}

std::ofstream out;
std::ofstream outSpawn;

void Log(const char* str, ...)
{
	char buffer[0xFFA];
	va_list arg;
	va_start(arg, str);
	vsnprintf(buffer, sizeof(buffer), str, arg);
	va_end(arg);

	static bool once = false;

	if (!once)
	{
		AllocConsole();
		FILE* fp;
		freopen_s(&fp, "CONOUT$", "w", stdout);
		out.open("C://mm_log.txt");
		once = true;
	}

	if (out.is_open())
	{
		out << buffer << std::endl;
	}

	std::cout << buffer << std::endl;
}

void HookMgr::Initialize() {
	MH_Initialize();

	ACT_EXE(isGOG = true, isSteam = true);

	if (isSteam) {

		static void*(*GetStartupInfoW_orig)(LPSTARTUPINFOW, uintptr_t) = nullptr;
		Install(GetStartupInfoW, [](LPSTARTUPINFOW info, uintptr_t valid) {
				auto addr = _ReturnAddress();
				static bool once = false;
				if (!once && ((uintptr_t)addr == 0x14303978D || valid == 0x14303978D)) { // _tmainCRTStartup
					PluginHooks();
					once = true;
					valid = 0x14303978D;
				}
				GetStartupInfoW_orig(info, valid); // call other hooks on this address
				// the "valid" argument passes to the following plugins that this hook call worked in the right place (_tmainCRTStartup) , 
				// since _ReturnAddress will have a different value if the hook is placed on another hook of another plugin
				
			}, 
			GetStartupInfoW_orig);
	}
	else {
		PluginHooks();
	}
}