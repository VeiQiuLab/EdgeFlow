#include "debuglog.h"

#include <cstdarg>
#include <cstdio>

namespace {

bool g_enabled = false;
std::wstring g_path;

}  // namespace

namespace DebugLog {

void Init(const std::wstring& path, bool enabled) {
    g_path = path;
    g_enabled = enabled;
    if (g_enabled) {
        Write(L"--- EdgeFlow start ---");
    }
}

void Write(const wchar_t* fmt, ...) {
    if (!g_enabled || g_path.empty()) return;

    wchar_t body[1024]{};
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(body, _TRUNCATE, fmt, args);
    va_end(args);

    SYSTEMTIME st{};
    GetLocalTime(&st);

    wchar_t line[1200]{};
    swprintf_s(line, L"[%02d:%02d:%02d.%03d] %s\r\n",
               st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, body);

    // 追加写入，避免覆盖历史
    FILE* f = nullptr;
    if (_wfopen_s(&f, g_path.c_str(), L"a, ccs=UTF-8") == 0 && f != nullptr) {
        fputws(line, f);
        fclose(f);
    }
}

}  // namespace DebugLog
