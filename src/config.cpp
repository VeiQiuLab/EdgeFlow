#include "config.h"

#include <cstdio>
#include <cstdlib>
#include <cwchar>

namespace {

std::wstring ReadStr(const std::wstring& path, const wchar_t* sec,
                     const wchar_t* key, const wchar_t* def) {
    wchar_t buf[2048]{};
    GetPrivateProfileStringW(sec, key, def, buf, 2048, path.c_str());
    return buf;
}

int ReadInt(const std::wstring& path, const wchar_t* sec, const wchar_t* key, int def) {
    return static_cast<int>(GetPrivateProfileIntW(sec, key, def, path.c_str()));
}

bool ReadBool(const std::wstring& path, const wchar_t* sec, const wchar_t* key, bool def) {
    const std::wstring v = ReadStr(path, sec, key, def ? L"true" : L"false");
    return _wcsicmp(v.c_str(), L"true") == 0 || v == L"1" || _wcsicmp(v.c_str(), L"yes") == 0;
}

void WriteStr(const std::wstring& path, const wchar_t* sec, const wchar_t* key,
              const std::wstring& v) {
    WritePrivateProfileStringW(sec, key, v.c_str(), path.c_str());
}

void WriteInt(const std::wstring& path, const wchar_t* sec, const wchar_t* key, int v) {
    wchar_t buf[32]{};
    swprintf_s(buf, L"%d", v);
    WritePrivateProfileStringW(sec, key, buf, path.c_str());
}

void WriteBool(const std::wstring& path, const wchar_t* sec, const wchar_t* key, bool v) {
    WritePrivateProfileStringW(sec, key, v ? L"true" : L"false", path.c_str());
}

// 读取一个 section 的全部 key=value
std::vector<std::pair<std::wstring, std::wstring>> ReadSection(
    const std::wstring& path, const wchar_t* sec) {
    std::vector<std::pair<std::wstring, std::wstring>> out;

    std::vector<wchar_t> keys(8192);
    const DWORD n = GetPrivateProfileStringW(
        sec, nullptr, L"", keys.data(), static_cast<DWORD>(keys.size()), path.c_str());
    if (n == 0) return out;

    const wchar_t* p = keys.data();
    while (*p) {
        std::wstring key = p;
        p += key.size() + 1;

        wchar_t val[1024]{};
        GetPrivateProfileStringW(sec, key.c_str(), L"", val, 1024, path.c_str());
        out.emplace_back(std::move(key), std::wstring(val));
    }
    return out;
}

bool StartsWithI(const std::wstring& s, const wchar_t* prefix) {
    const size_t n = wcslen(prefix);
    return s.size() >= n && _wcsnicmp(s.c_str(), prefix, n) == 0;
}

}  // namespace

namespace Config {

std::wstring DefaultPath() {
    wchar_t buf[MAX_PATH]{};
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0) return L"edgehide.ini";

    std::wstring path(buf, n);
    const wchar_t kBackslash = static_cast<wchar_t>(0x5C);
    const size_t pos = path.find_last_of(kBackslash);
    if (pos != std::wstring::npos) {
        path.resize(pos + 1);
    } else {
        path.clear();
    }
    return path + L"edgehide.ini";
}

Settings Load(const std::wstring& path) {
    Settings s;

    s.enabled = ReadBool(path, L"Global", L"Enabled", s.enabled);
    s.autoDock = ReadBool(path, L"Global", L"AutoDock", s.autoDock);
    s.autoHide = ReadBool(path, L"Global", L"AutoHide", s.autoHide);
    s.qqStyle = ReadBool(path, L"Global", L"QQStyle", s.qqStyle);

    s.edgeLeft = ReadBool(path, L"Edge", L"Left", s.edgeLeft);
    s.edgeRight = ReadBool(path, L"Edge", L"Right", s.edgeRight);
    s.edgeTop = ReadBool(path, L"Edge", L"Top", s.edgeTop);
    s.edgeBottom = ReadBool(path, L"Edge", L"Bottom", s.edgeBottom);
    s.edgeCorners = ReadBool(path, L"Edge", L"Corners", s.edgeCorners);
    s.dockDistanceDip = ReadInt(path, L"Edge", L"DockDistanceDip", s.dockDistanceDip);
    s.magnetDistanceDip = ReadInt(path, L"Edge", L"MagnetDistanceDip", s.magnetDistanceDip);
    s.revealZonePx = ReadInt(path, L"Edge", L"RevealZonePx", s.revealZonePx);
    s.visiblePixels = ReadInt(path, L"Edge", L"VisiblePixels", s.visiblePixels);

    s.hideDelayMs = ReadInt(path, L"Animation", L"HideDelayMs", s.hideDelayMs);
    s.animationTimeMs = ReadInt(path, L"Animation", L"AnimationTimeMs", s.animationTimeMs);
    s.idleIntervalMs = ReadInt(path, L"Animation", L"IdleIntervalMs", s.idleIntervalMs);

    s.fullscreenGuard = ReadBool(path, L"Behavior", L"FullscreenGuard", s.fullscreenGuard);
    s.globalAutoManage = ReadBool(path, L"Behavior", L"GlobalAutoManage", s.globalAutoManage);

    s.hotCornerEnabled = ReadBool(path, L"HotCorner", L"Enabled", s.hotCornerEnabled);
    s.hotCornerLT = ReadBool(path, L"HotCorner", L"LeftTop", s.hotCornerLT);
    s.hotCornerRT = ReadBool(path, L"HotCorner", L"RightTop", s.hotCornerRT);
    s.hotCornerLB = ReadBool(path, L"HotCorner", L"LeftBottom", s.hotCornerLB);
    s.hotCornerRB = ReadBool(path, L"HotCorner", L"RightBottom", s.hotCornerRB);
    s.hotCornerTriggerDelayMs =
        ReadInt(path, L"HotCorner", L"TriggerDelayMs", s.hotCornerTriggerDelayMs);

    s.debugLog = ReadBool(path, L"Debug", L"DebugLog", s.debugLog);

    // [Blacklist]: Process*/Class* 前缀区分
    for (const auto& kv : ReadSection(path, L"Blacklist")) {
        if (kv.second.empty()) continue;
        if (StartsWithI(kv.first, L"Process")) {
            s.blacklistProcesses.push_back(kv.second);
        } else if (StartsWithI(kv.first, L"Class")) {
            s.blacklistClasses.push_back(kv.second);
        }
    }

    // [IgnoredN]: 每个子节一个排除项
    for (int i = 0; i < 512; ++i) {
        wchar_t sec[32]{};
        swprintf_s(sec, L"Ignored%d", i);

        const std::wstring proc = ReadStr(path, sec, L"Process", L"");
        const std::wstring cls = ReadStr(path, sec, L"Class", L"");
        if (proc.empty() && cls.empty()) continue;

        IgnoreEntry e;
        e.process = proc;
        e.className = cls;
        s.ignoredWindows.push_back(std::move(e));
    }

    // 数值下限保护
    if (s.dockDistanceDip < 0) s.dockDistanceDip = 0;
    if (s.magnetDistanceDip < s.dockDistanceDip) s.magnetDistanceDip = s.dockDistanceDip;
    if (s.revealZonePx < 1) s.revealZonePx = 1;
    if (s.visiblePixels < 0) s.visiblePixels = 0;
    if (s.hideDelayMs < 0) s.hideDelayMs = 0;
    if (s.animationTimeMs < 1) s.animationTimeMs = 1;
    if (s.idleIntervalMs < 10) s.idleIntervalMs = 10;

    return s;
}

void Save(const std::wstring& path, const Settings& s) {
    // 清空后整份重写，避免删除条目后残留
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());

    WriteBool(path, L"Global", L"Enabled", s.enabled);
    WriteBool(path, L"Global", L"AutoDock", s.autoDock);
    WriteBool(path, L"Global", L"AutoHide", s.autoHide);
    WriteBool(path, L"Global", L"QQStyle", s.qqStyle);

    WriteBool(path, L"Edge", L"Left", s.edgeLeft);
    WriteBool(path, L"Edge", L"Right", s.edgeRight);
    WriteBool(path, L"Edge", L"Top", s.edgeTop);
    WriteBool(path, L"Edge", L"Bottom", s.edgeBottom);
    WriteBool(path, L"Edge", L"Corners", s.edgeCorners);
    WriteInt(path, L"Edge", L"DockDistanceDip", s.dockDistanceDip);
    WriteInt(path, L"Edge", L"MagnetDistanceDip", s.magnetDistanceDip);
    WriteInt(path, L"Edge", L"RevealZonePx", s.revealZonePx);
    WriteInt(path, L"Edge", L"VisiblePixels", s.visiblePixels);

    WriteInt(path, L"Animation", L"HideDelayMs", s.hideDelayMs);
    WriteInt(path, L"Animation", L"AnimationTimeMs", s.animationTimeMs);
    WriteInt(path, L"Animation", L"IdleIntervalMs", s.idleIntervalMs);

    WriteBool(path, L"Behavior", L"FullscreenGuard", s.fullscreenGuard);
    WriteBool(path, L"Behavior", L"GlobalAutoManage", s.globalAutoManage);

    WriteBool(path, L"HotCorner", L"Enabled", s.hotCornerEnabled);
    WriteBool(path, L"HotCorner", L"LeftTop", s.hotCornerLT);
    WriteBool(path, L"HotCorner", L"RightTop", s.hotCornerRT);
    WriteBool(path, L"HotCorner", L"LeftBottom", s.hotCornerLB);
    WriteBool(path, L"HotCorner", L"RightBottom", s.hotCornerRB);
    WriteInt(path, L"HotCorner", L"TriggerDelayMs", s.hotCornerTriggerDelayMs);

    WriteBool(path, L"Debug", L"DebugLog", s.debugLog);

    for (size_t i = 0; i < s.blacklistProcesses.size(); ++i) {
        wchar_t key[32]{};
        swprintf_s(key, L"Process%zu", i + 1);
        WriteStr(path, L"Blacklist", key, s.blacklistProcesses[i]);
    }
    for (size_t i = 0; i < s.blacklistClasses.size(); ++i) {
        wchar_t key[32]{};
        swprintf_s(key, L"Class%zu", i + 1);
        WriteStr(path, L"Blacklist", key, s.blacklistClasses[i]);
    }

    for (size_t i = 0; i < s.ignoredWindows.size(); ++i) {
        wchar_t sec[32]{};
        swprintf_s(sec, L"Ignored%zu", i);
        WriteStr(path, sec, L"Process", s.ignoredWindows[i].process);
        WriteStr(path, sec, L"Class", s.ignoredWindows[i].className);
    }
}

}  // namespace Config
