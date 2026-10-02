#pragma once

#include <windows.h>
#include <string>

namespace DebugLog {

void Init(const std::wstring& path, bool enabled);
void Write(const wchar_t* fmt, ...);

}  // namespace DebugLog
