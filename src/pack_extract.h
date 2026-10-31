#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
namespace packx {
using LogFn = void (*)(const char* fmt, ...);
bool ExtractFromPack(const wchar_t* gameRoot, LogFn log);
bool FullMod();
const wchar_t* MainIniName();
const wchar_t* AddonIds();
}
