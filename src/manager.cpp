#include "manager.h"

#include <dwmapi.h>
#include <algorithm>
#include <climits>

#include "debuglog.h"

namespace {

constexpr int kMinWindowSize = 100;

std::wstring GetProcessName(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) return {};

    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (proc == nullptr) return {};

    wchar_t buf[MAX_PATH]{};
    DWORD size = MAX_PATH;
    std::wstring name;
    if (QueryFullProcessImageNameW(proc, 0, buf, &size)) {
        std::wstring full(buf, size);
        const size_t pos = full.find_last_of(static_cast<wchar_t>(0x5C));
        name = (pos != std::wstring::npos) ? full.substr(pos + 1) : full;
    }
    CloseHandle(proc);
    return name;
}

std::wstring GetClassNameStr(HWND hwnd) {
    wchar_t buf[256]{};
    GetClassNameW(hwnd, buf, 256);
    return buf;
}

bool EqualsI(const std::wstring& a, const std::wstring& b) {
    return !a.empty() && !b.empty() && _wcsicmp(a.c_str(), b.c_str()) == 0;
}

// 按显示器设备名读取当前刷新率（Hz）。失败/异常回退 60。
int GetMonitorRefreshRate(const std::wstring& device) {
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    if (!EnumDisplaySettingsW(device.c_str(), ENUM_CURRENT_SETTINGS, &dm)) {
        return 60;
    }
    const DWORD hz = dm.dmDisplayFrequency;
    if (hz <= 1) return 60;  // 0/1 表示硬件默认，非有效值
    return static_cast<int>(hz);
}

}  // namespace

Manager::Manager(HWND messageWindow) : msgWnd_(messageWindow) {}

Manager::~Manager() {
    if (msgWnd_ != nullptr) {
        KillTimer(msgWnd_, kManagerTimerId);
    }
    for (HWINEVENTHOOK h : hooks_) {
        if (h != nullptr) UnhookWinEvent(h);
    }
    hooks_.clear();
    if (s_instance_ == this) {
        s_instance_ = nullptr;
    }
}

void Manager::Init() {
    s_instance_ = this;

    struct HookSpec {
        DWORD lo;
        DWORD hi;
    };
    const HookSpec specs[] = {
        {EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZESTART},
        {EVENT_SYSTEM_MOVESIZEEND, EVENT_SYSTEM_MOVESIZEEND},
        {EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND},
        {EVENT_OBJECT_DESTROY, EVENT_OBJECT_DESTROY},
    };

    for (const auto& s : specs) {
        HWINEVENTHOOK h = SetWinEventHook(
            s.lo, s.hi, nullptr, &Manager::WinEventProc, 0, 0,
            WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        if (h != nullptr) hooks_.push_back(h);
    }

    SetTimer(msgWnd_, kManagerTimerId, static_cast<UINT>(settings_.idleIntervalMs), nullptr);

    animPump_.Start(msgWnd_, WM_ANIM_TICK);
}

void Manager::Shutdown() {
    animPump_.Stop();

    // 退出前把所有隐藏窗口拉回可见，避免窗口留在屏幕外
    for (auto& up : windows_) {
        up->RestoreNow();
    }
    SaveConfig();
}

void Manager::LoadConfig() {
    configPath_ = Config::DefaultPath();
    {
        std::wstring p = configPath_;
        const size_t pos = p.find_last_of(static_cast<wchar_t>(0x5C));
        p.resize(pos == std::wstring::npos ? 0 : pos + 1);
        logPath_ = p + L"debug.log";
    }

    settings_ = Config::Load(configPath_);

    DebugLog::Init(logPath_, settings_.debugLog);
    DebugLog::Write(L"config loaded: enabled=%d autoDock=%d autoHide=%d fullscreenGuard=%d",
                    settings_.enabled, settings_.autoDock, settings_.autoHide,
                    settings_.fullscreenGuard);

    if (msgWnd_ != nullptr) {
        SetTimer(msgWnd_, kManagerTimerId, static_cast<UINT>(settings_.idleIntervalMs), nullptr);
    }
}

void Manager::SaveConfig() {
    Config::Save(configPath_, settings_);
}

void Manager::ToggleEnabled() {
    settings_.enabled = !settings_.enabled;
    if (!settings_.enabled) {
        for (auto& up : windows_) {
            up->RestoreNow();
        }
        windows_.clear();
    }
    SaveConfig();
    DebugLog::Write(L"enabled -> %d", settings_.enabled);
}

void Manager::ToggleIgnoreForeground() {
    const HWND fg = GetForegroundWindow();
    if (fg == nullptr || fg == msgWnd_) return;
    if (!IsWindow(fg)) return;

    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (pid == GetCurrentProcessId()) return;

    const std::wstring proc = GetProcessName(fg);
    const std::wstring cls = GetClassNameStr(fg);

    const auto it = std::find_if(
        settings_.ignoredWindows.begin(), settings_.ignoredWindows.end(),
        [&](const IgnoreEntry& e) { return EqualsI(e.process, proc) && EqualsI(e.className, cls); });

    if (it != settings_.ignoredWindows.end()) {
        settings_.ignoredWindows.erase(it);
        Remove(fg);
        DebugLog::Write(L"ignore removed: %s | %s", proc.c_str(), cls.c_str());
    } else {
        IgnoreEntry e;
        e.process = proc;
        e.className = cls;
        settings_.ignoredWindows.push_back(std::move(e));

        Remove(fg);
        DebugLog::Write(L"ignore added: %s | %s", proc.c_str(), cls.c_str());
    }
    SaveConfig();
}

void Manager::ShowAll() {
    for (auto& up : windows_) {
        if (!up->IsDocked()) continue;
        up->BeginShow();
    }
    SyncAnimPump();
    DebugLog::Write(L"show all");
}

void Manager::ClearAll() {
    for (auto& up : windows_) {
        up->RestoreNow();
    }
    windows_.clear();
    SyncAnimPump();
}

void Manager::OnTimer() {
    PurgeInvalid();

    const DWORD now = GetTickCount();
    POINT pt{};
    GetCursorPos(&pt);
    const HWND fg = GetForegroundWindow();

    if (settings_.fullscreenGuard && settings_.enabled) {
        const bool fs = IsFullscreenForeground();
        if (fs != fullscreenGuardActive_) {
            fullscreenGuardActive_ = fs;
            DebugLog::Write(L"FullscreenGuard -> %d", fs);
        }
    } else {
        fullscreenGuardActive_ = false;
    }

    for (auto& up : windows_) {
        ManagedWindow& mw = *up;
        const HWND h = mw.Handle();

        if (!mw.IsDocked()) continue;
        // 动画由 OnAnimationTick 驱动，这里不参与
        if (mw.IsAnimating()) continue;

        if (IsIconic(h) || IsZoomed(h)) {
            mw.NotifyMouseInside();
            continue;
        }

        if (mw.IsHidden()) {
            if (fullscreenGuardActive_) continue;

            if (fg == h) {
                mw.BeginShow();
                continue;
            }
            const RECT hz = mw.HotZone();
            if (PtInRect(&hz, pt)) {
                mw.BeginShow();
                DebugLog::Write(L"reveal by hotzone: %p", h);
            }
        } else {
            if (!settings_.autoHide) {
                mw.NotifyMouseInside();
                continue;
            }

            RECT r{};
            if (!GetWindowRect(h, &r)) continue;
            InflateRect(&r, 8, 8);

            const bool inside = PtInRect(&r, pt) != FALSE;
            const bool active = (fg == h);

            if (inside || active) {
                mw.NotifyMouseInside();
            } else {
                mw.NotifyMouseOutside(now);
                if (mw.ShouldHide(now, settings_.hideDelayMs)) {
                    mw.BeginHide();
                    DebugLog::Write(L"hide: %p", h);
                }
            }
        }
    }

    SyncAnimPump();
}

void Manager::OnAnimationTick() {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);

    for (auto& up : windows_) {
        if (!up->IsAnimating()) continue;

        up->Tick(now.QuadPart);

        if (!up->IsAnimating()) {
            const AnimStats& st = up->LastAnimStats();
            if (st.valid) {
                DebugLog::Write(
                    L"Animation: monitorHz=%d duration=%dms frames=%d "
                    L"avgFrame=%.1fms maxFrame=%.1fms droppedFrames=%d",
                    st.monitorHz, st.durationMs, st.frames,
                    st.avgFrameMs, st.maxFrameMs, st.droppedFrames);
            }
        }
    }

    SyncAnimPump();
}

void Manager::SyncAnimPump() {
    bool any = false;
    int bestHz = 60;
    for (auto& up : windows_) {
        if (up->IsAnimating()) {
            any = true;
            if (up->MonitorHz() > bestHz) bestHz = up->MonitorHz();
        }
    }

    if (any) {
        if (bestHz < 30) bestHz = 60;
        const LONGLONG iv = 10000000LL / static_cast<LONGLONG>(bestHz);
        if (!animPumpActive_ || iv != animPumpInterval100ns_) {
            animPump_.SetAnimating(true, iv);
            animPumpActive_ = true;
            animPumpInterval100ns_ = iv;
        }
    } else if (animPumpActive_) {
        animPump_.SetAnimating(false, 0);
        animPumpActive_ = false;
        animPumpInterval100ns_ = 0;
    }
}

void Manager::OnEvent(WPARAM evt, LPARAM lParam) {
    const HWND hwnd = reinterpret_cast<HWND>(lParam);

    switch (evt) {
        case kEvtMoveStart: {
            if (!settings_.enabled) return;
            ManagedWindow* mw = Find(hwnd);
            if (mw != nullptr) {
                mw->SetDragging(true);
            }
            DebugLog::Write(L"drag start: %p class=%s", hwnd,
                            GetClassNameStr(hwnd).c_str());
            break;
        }
        case kEvtMoveEnd: {
            if (!settings_.enabled || !settings_.autoDock) return;
            if (!IsEligible(hwnd)) return;
            if (IsBlacklisted(hwnd) || IsIgnored(hwnd)) return;
            if (IsIconic(hwnd) || IsZoomed(hwnd)) return;

            ManagedWindow* mw = Find(hwnd);
            if (mw == nullptr) {
                AddForDrag(hwnd);
                mw = Find(hwnd);
                if (mw == nullptr) return;
            }

            HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFOEXW mi{};
            mi.cbSize = sizeof(mi);
            if (!GetMonitorInfoW(mon, &mi)) return;

            const UINT dpi = GetDpiForWindow(hwnd);
            const int hz = GetMonitorRefreshRate(mi.szDevice);

            if (mw->DockNear(mi.rcMonitor, mi.szDevice, dpi ? dpi : 96, hz)) {
                DebugLog::Write(L"docked: %p edge=%d hz=%d class=%s proc=%s",
                                hwnd, static_cast<int>(mw->Edge()), hz,
                                GetClassNameStr(hwnd).c_str(),
                                GetProcessName(hwnd).c_str());
                SyncAnimPump();
            } else {
                Remove(hwnd);
            }
            break;
        }
        case kEvtForeground:
            break;
        case kEvtDestroy:
            Remove(hwnd);
            DebugLog::Write(L"destroy: %p", hwnd);
            break;
        default:
            break;
    }
}

ManagedWindow* Manager::Find(HWND hwnd) {
    for (auto& up : windows_) {
        if (up->Handle() == hwnd) return up.get();
    }
    return nullptr;
}

void Manager::AddForDrag(HWND hwnd) {
    if (Find(hwnd) != nullptr) return;
    windows_.push_back(std::make_unique<ManagedWindow>(hwnd, &settings_));
}

void Manager::Remove(HWND hwnd) {
    const auto it = std::find_if(
        windows_.begin(), windows_.end(),
        [hwnd](const std::unique_ptr<ManagedWindow>& p) { return p->Handle() == hwnd; });
    if (it == windows_.end()) return;

    (*it)->RestoreNow();
    windows_.erase(it);
    SyncAnimPump();
}

void Manager::PurgeInvalid() {
    const size_t before = windows_.size();
    windows_.erase(
        std::remove_if(windows_.begin(), windows_.end(),
                       [](const std::unique_ptr<ManagedWindow>& p) {
                           return !p->IsValid();
                       }),
        windows_.end());
    if (windows_.size() != before) {
        SyncAnimPump();
    }
}

bool Manager::IsEligible(HWND hwnd) const {
    if (hwnd == nullptr || !IsWindow(hwnd)) return false;
    if (hwnd == msgWnd_) return false;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId()) return false;

    if (!IsWindowVisible(hwnd)) return false;

    if (GetAncestor(hwnd, GA_ROOT) != hwnd) return false;
    if (GetParent(hwnd) != nullptr) return false;
    if (GetWindow(hwnd, GW_OWNER) != nullptr) return false;

    const LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    if ((style & WS_CHILD) != 0) return false;

    const LONG ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
    if ((ex & WS_EX_TOOLWINDOW) != 0) return false;
    if ((ex & WS_EX_NOACTIVATE) != 0) return false;

    int cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) &&
        cloaked != 0) {
        return false;
    }

    RECT r{};
    if (!GetWindowRect(hwnd, &r)) return false;
    if (r.right - r.left < kMinWindowSize || r.bottom - r.top < kMinWindowSize) return false;

    const std::wstring cls = GetClassNameStr(hwnd);
    static const wchar_t* kBanned[] = {
        L"Progman",
        L"WorkerW",
        L"Shell_TrayWnd",
        L"Shell_SecondaryTrayWnd",
        L"Shell_CharmWindow",
        L"Windows.UI.Core.CoreWindow",
        L"ApplicationFrameWindow",
        L"Windows.Internal.Shell.TabProxyWindow",
        L"XamlExplorerHostIslandWindow",
        L"ForegroundStaging",
    };
    for (const wchar_t* b : kBanned) {
        if (_wcsicmp(cls.c_str(), b) == 0) return false;
    }

    return true;
}

bool Manager::IsBlacklisted(HWND hwnd) const {
    const std::wstring proc = GetProcessName(hwnd);
    const std::wstring cls = GetClassNameStr(hwnd);

    for (const auto& p : settings_.blacklistProcesses) {
        if (EqualsI(p, proc)) return true;
    }
    for (const auto& c : settings_.blacklistClasses) {
        if (EqualsI(c, cls)) return true;
    }
    return false;
}

bool Manager::IsIgnored(HWND hwnd) const {
    const std::wstring proc = GetProcessName(hwnd);
    const std::wstring cls = GetClassNameStr(hwnd);

    for (const auto& e : settings_.ignoredWindows) {
        if (EqualsI(e.process, proc) && EqualsI(e.className, cls)) return true;
    }
    return false;
}

bool Manager::IsFullscreenForeground() const {
    const HWND fg = GetForegroundWindow();
    if (fg == nullptr || !IsWindow(fg)) return false;

    const std::wstring cls = GetClassNameStr(fg);
    if (_wcsicmp(cls.c_str(), L"Progman") == 0 ||
        _wcsicmp(cls.c_str(), L"WorkerW") == 0 ||
        _wcsicmp(cls.c_str(), L"Shell_TrayWnd") == 0) {
        return false;
    }

    RECT r{};
    if (!GetWindowRect(fg, &r)) return false;

    HMONITOR mon = MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi)) return false;

    return r.left <= mi.rcMonitor.left && r.top <= mi.rcMonitor.top &&
           r.right >= mi.rcMonitor.right && r.bottom >= mi.rcMonitor.bottom;
}

void CALLBACK Manager::WinEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd,
                                    LONG idObject, LONG idChild, DWORD, DWORD) {
    if (s_instance_ == nullptr || s_instance_->msgWnd_ == nullptr) return;
    if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF) return;

    WPARAM evt = 0;
    switch (event) {
        case EVENT_SYSTEM_MOVESIZESTART: evt = kEvtMoveStart; break;
        case EVENT_SYSTEM_MOVESIZEEND:   evt = kEvtMoveEnd; break;
        case EVENT_SYSTEM_FOREGROUND:    evt = kEvtForeground; break;
        case EVENT_OBJECT_DESTROY:       evt = kEvtDestroy; break;
        default: return;
    }

    // 回调只做投递，具体逻辑回主线程处理
    PostMessageW(s_instance_->msgWnd_, WM_DOCK_EVENT, evt, reinterpret_cast<LPARAM>(hwnd));
}
