#define _CRT_SECURE_NO_WARNINGS
#include "lua_co_await.h"
#include <iris_common.inl>
#include "plugins.inl"
using namespace iris;

// run this tutorial with:
// require("lua_co_await").new():run_tutorials()

extern "C"
#if defined(_MSC_VER)
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
int luaopen_lua_co_await(lua_State* L) {
	iris_register_plugins(L);
	return lua_t::forward(L, [](lua_t lua) {
		return lua.make_type<lua_co_await_t>("lua_co_await");
	});
}

// For starting from RunDll32 on Win32
#ifdef _WIN32
#include <filesystem>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")

static HMODULE DllHandle = NULL;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
	switch (reason) {
		case DLL_PROCESS_ATTACH:
			DllHandle = hModule;
			break;
	}

	return TRUE;
}

extern "C" void CALLBACK Main(HWND hwnd, HINSTANCE hinst, LPTSTR lpCmdLine, int nCmdShow) {
	if (lpCmdLine[0] == '\0')
		return;

	LPTSTR fileName = ::PathFindFileName(lpCmdLine);
	if (fileName != lpCmdLine) {
		auto ch = *fileName;
		*fileName = 0;
		bool ret = ::SetCurrentDirectory(lpCmdLine);
		if (!ret) {
			fprintf(stderr, "Cannot change directory!");
			return;
		}

		*fileName = ch;
	}

	WCHAR szDllPath[MAX_PATH * 2];
	::GetModuleFileNameW(DllHandle, szDllPath, MAX_PATH * 2 * sizeof(TCHAR));
	LPWSTR dllFileName = ::PathFindFileNameW(szDllPath);
	if (dllFileName != szDllPath) {
		*dllFileName = 0;
		::AddDllDirectory(szDllPath);
	}

#ifdef _DEBUG
	if (::AllocConsole()) {
		freopen("conout$", "w", stdout);
		freopen("conout$", "w", stderr);
	}
#endif

	lua_State* L = luaL_newstate();
	luaL_openlibs(L);
	if (luaL_dofile(L, fileName) != LUA_OK) {
		fprintf(stderr, "Lua Error: %s\n", lua_tostring(L, -1));
	}

	lua_close(L);
}

#endif
