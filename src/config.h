#pragma once

#include <windows.h>
#include <string>
#include <vector>

// 停靠边（四方向）
enum class DockEdge {
    None,
    Left,
    Right,
    Top,
    Bottom,
};

// 按 进程名 + 窗口类 持久化的排除项
struct IgnoreEntry {
    std::wstring process;
    std::wstring className;
};

// 全部可配置项，键名与 edgehide.ini 一一对应
struct Settings {
    // [Global]
    bool enabled = true;
    bool autoDock = true;
    bool autoHide = true;
    bool qqStyle = true;

    // [Edge]
    bool edgeLeft = true;
    bool edgeRight = true;
    bool edgeTop = true;
    bool edgeBottom = true;
    bool edgeCorners = true;
    int dockDistanceDip = 20;
    int magnetDistanceDip = 40;
    int revealZonePx = 15;
    int visiblePixels = 5;

    // [Animation]
    int hideDelayMs = 280;
    int animationTimeMs = 230;
    int idleIntervalMs = 80;

    // [Behavior]
    bool fullscreenGuard = true;
    bool globalAutoManage = true;

    // [HotCorner]（当前阶段仅触发框架，默认关闭）
    bool hotCornerEnabled = false;
    bool hotCornerLT = false;
    bool hotCornerRT = false;
    bool hotCornerLB = false;
    bool hotCornerRB = false;
    int hotCornerTriggerDelayMs = 250;

    // [Debug]
    bool debugLog = false;

    // [Blacklist]
    std::vector<std::wstring> blacklistProcesses;
    std::vector<std::wstring> blacklistClasses;

    // [Ignored]
    std::vector<IgnoreEntry> ignoredWindows;
};

namespace Config {

std::wstring DefaultPath();
Settings Load(const std::wstring& path);
void Save(const std::wstring& path, const Settings& s);

}  // namespace Config
