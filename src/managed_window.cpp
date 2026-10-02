#include "managed_window.h"

#include <cmath>
#include <climits>

namespace {

LONGLONG QpcFreq() {
    static const LONGLONG f = [] {
        LARGE_INTEGER li{};
        QueryPerformanceFrequency(&li);
        return li.QuadPart > 0 ? li.QuadPart : 10000000LL;
    }();
    return f;
}

// 展开：EaseOutCubic
double EaseOutCubic(double t) {
    const double u = 1.0 - t;
    return 1.0 - u * u * u;
}

// 隐藏：EaseInOutCubic
double EaseInOutCubic(double t) {
    if (t < 0.5) return 4.0 * t * t * t;
    const double u = -2.0 * t + 2.0;
    return 1.0 - (u * u * u) / 2.0;
}

}  // namespace

ManagedWindow::ManagedWindow(HWND hwnd, const Settings* settings)
    : hwnd_(hwnd), settings_(settings) {}

void ManagedWindow::SetDragging(bool on) {
    if (on) {
        if (state_ == State::DockedVisible || state_ == State::Hidden ||
            state_ == State::WaitingHide) {
            Undock();
        }
        state_ = State::Dragging;
        anim_.active = false;
        waitValid_ = false;
    } else if (state_ == State::Dragging) {
        state_ = State::Normal;
    }
}

bool ManagedWindow::DockNear(const RECT& monitorRect, const std::wstring& device,
                             UINT dpi, int refreshHz) {
    RECT r{};
    if (!GetWindowRect(hwnd_, &r)) return false;

    monitorRect_ = monitorRect;
    monitorDevice_ = device;
    dpi_ = dpi;
    monitorHz_ = (refreshHz > 1) ? refreshHz : 60;

    const int magnet = MulDiv(settings_->magnetDistanceDip, static_cast<int>(dpi_), 96);

    DockEdge best = DockEdge::None;
    int bestDist = INT_MAX;

    if (settings_->edgeLeft) {
        const int d = r.left - monitorRect_.left;
        if (d <= magnet && d < bestDist) { bestDist = d; best = DockEdge::Left; }
    }
    if (settings_->edgeRight) {
        const int d = monitorRect_.right - r.right;
        if (d <= magnet && d < bestDist) { bestDist = d; best = DockEdge::Right; }
    }
    if (settings_->edgeTop) {
        const int d = r.top - monitorRect_.top;
        if (d <= magnet && d < bestDist) { bestDist = d; best = DockEdge::Top; }
    }
    if (settings_->edgeBottom) {
        const int d = monitorRect_.bottom - r.bottom;
        if (d <= magnet && d < bestDist) { bestDist = d; best = DockEdge::Bottom; }
    }

    if (best == DockEdge::None) return false;

    edge_ = best;
    CacheRects();

    // 平滑对齐边缘（磁吸收束），而不是瞬移
    StartAnim(dockedRect_, /*toHidden=*/false, settings_->animationTimeMs);
    state_ = State::Showing;
    waitValid_ = false;
    return true;
}

void ManagedWindow::Undock() {
    edge_ = DockEdge::None;
    state_ = State::Normal;
    anim_.active = false;
    waitValid_ = false;
}

void ManagedWindow::RestoreNow() {
    if (edge_ == DockEdge::None) return;

    const RECT target = dockedRect_;
    SetWindowPos(hwnd_, nullptr, target.left, target.top, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    anim_.active = false;
    state_ = State::DockedVisible;
    waitValid_ = false;
}

void ManagedWindow::BeginHide() {
    if (edge_ == DockEdge::None) return;
    if (anim_.active) return;

    StartAnim(hiddenRect_, /*toHidden=*/true, settings_->animationTimeMs);
    state_ = State::Hiding;
    waitValid_ = false;
}

void ManagedWindow::BeginShow() {
    if (edge_ == DockEdge::None) return;
    if (anim_.active && !anim_.toHidden) return;

    StartAnim(dockedRect_, /*toHidden=*/false, settings_->animationTimeMs);
    state_ = State::Showing;
    waitValid_ = false;
}

bool ManagedWindow::Tick(LONGLONG nowQpc) {
    if (!anim_.active) return false;

    LONGLONG elapsed = nowQpc - anim_.startQpc;
    if (elapsed < 0) elapsed = 0;

    double t = (anim_.durationQpc > 0)
                   ? static_cast<double>(elapsed) / static_cast<double>(anim_.durationQpc)
                   : 1.0;
    if (t > 1.0) t = 1.0;

    const bool done = (t >= 1.0);

    int x = 0;
    int y = 0;
    if (done) {
        // 末帧强制精确落到目标，消除累计误差
        x = anim_.to.left;
        y = anim_.to.top;
    } else {
        const double e = anim_.toHidden ? EaseInOutCubic(t) : EaseOutCubic(t);
        const double fx = static_cast<double>(anim_.from.left) +
                          static_cast<double>(anim_.to.left - anim_.from.left) * e;
        const double fy = static_cast<double>(anim_.from.top) +
                          static_cast<double>(anim_.to.top - anim_.from.top) * e;
        x = static_cast<int>(std::lround(fx));
        y = static_cast<int>(std::lround(fy));
    }

    SetWindowPos(hwnd_, nullptr, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    // 诊断采样：整数累加，无 IO
    if (anim_.lastFrameQpc != 0) {
        const LONGLONG dt = nowQpc - anim_.lastFrameQpc;
        anim_.sumIntervalQpc += dt;
        anim_.intervalCount++;
        if (dt > anim_.maxIntervalQpc) anim_.maxIntervalQpc = dt;
        if (anim_.expectedIntervalQpc > 0 && dt > anim_.expectedIntervalQpc * 3 / 2) {
            anim_.droppedFrames++;
        }
    }
    anim_.lastFrameQpc = nowQpc;
    anim_.frameCount++;

    if (done) {
        anim_.active = false;
        state_ = anim_.toHidden ? State::Hidden : State::DockedVisible;
        PopulateStats();
    }
    return true;
}

RECT ManagedWindow::HotZone() const {
    if (edge_ == DockEdge::None) return RECT{};

    RECT cur{};
    GetWindowRect(hwnd_, &cur);

    const int rz = settings_->revealZonePx;
    constexpr int kPad = 12;

    RECT hz{};
    switch (edge_) {
        case DockEdge::Left:
            hz.left = monitorRect_.left;
            hz.right = monitorRect_.left + rz;
            hz.top = cur.top - kPad;
            hz.bottom = cur.bottom + kPad;
            break;
        case DockEdge::Right:
            hz.left = monitorRect_.right - rz;
            hz.right = monitorRect_.right;
            hz.top = cur.top - kPad;
            hz.bottom = cur.bottom + kPad;
            break;
        case DockEdge::Top:
            hz.top = monitorRect_.top;
            hz.bottom = monitorRect_.top + rz;
            hz.left = cur.left - kPad;
            hz.right = cur.right + kPad;
            break;
        case DockEdge::Bottom:
            hz.top = monitorRect_.bottom - rz;
            hz.bottom = monitorRect_.bottom;
            hz.left = cur.left - kPad;
            hz.right = cur.right + kPad;
            break;
        default:
            return RECT{};
    }
    return hz;
}

void ManagedWindow::NotifyMouseOutside(DWORD nowMs) {
    if (!waitValid_) {
        waitValid_ = true;
        waitStart_ = nowMs;
    }
    if (state_ == State::DockedVisible) {
        state_ = State::WaitingHide;
    }
}

bool ManagedWindow::ShouldHide(DWORD nowMs, int hideDelayMs) const {
    return waitValid_ && (nowMs - waitStart_) >= static_cast<DWORD>(hideDelayMs);
}

void ManagedWindow::Recompute(const RECT& monitorRect, const std::wstring& device, UINT dpi) {
    if (edge_ == DockEdge::None) return;
    monitorRect_ = monitorRect;
    monitorDevice_ = device;
    dpi_ = dpi;
    CacheRects();
}

void ManagedWindow::CacheRects() {
    RECT r{};
    if (!GetWindowRect(hwnd_, &r)) return;

    const int w = r.right - r.left;
    const int h = r.bottom - r.top;
    const int vis = settings_->visiblePixels;

    dockedRect_ = r;
    hiddenRect_ = r;

    switch (edge_) {
        case DockEdge::Left:
            dockedRect_.left = monitorRect_.left;
            dockedRect_.right = monitorRect_.left + w;
            hiddenRect_.left = monitorRect_.left - w + vis;
            hiddenRect_.right = hiddenRect_.left + w;
            break;
        case DockEdge::Right:
            dockedRect_.left = monitorRect_.right - w;
            dockedRect_.right = monitorRect_.right;
            hiddenRect_.left = monitorRect_.right - vis;
            hiddenRect_.right = hiddenRect_.left + w;
            break;
        case DockEdge::Top:
            dockedRect_.top = monitorRect_.top;
            dockedRect_.bottom = monitorRect_.top + h;
            hiddenRect_.top = monitorRect_.top - h + vis;
            hiddenRect_.bottom = hiddenRect_.top + h;
            break;
        case DockEdge::Bottom:
            dockedRect_.top = monitorRect_.bottom - h;
            dockedRect_.bottom = monitorRect_.bottom;
            hiddenRect_.top = monitorRect_.bottom - vis;
            hiddenRect_.bottom = hiddenRect_.top + h;
            break;
        default:
            break;
    }
}

void ManagedWindow::StartAnim(const RECT& target, bool toHidden, int durationMs) {
    RECT cur{};
    if (!GetWindowRect(hwnd_, &cur)) return;

    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);

    const LONGLONG freq = QpcFreq();

    LONGLONG dur = (freq * static_cast<LONGLONG>(durationMs)) / 1000;
    if (dur < 1) dur = 1;

    LONGLONG expected = 0;
    if (monitorHz_ > 1) {
        expected = freq / monitorHz_;
        if (expected < 1) expected = 1;
    }

    anim_.active = true;
    anim_.from = cur;
    anim_.to = target;
    anim_.startQpc = now.QuadPart;
    anim_.durationQpc = dur;
    anim_.toHidden = toHidden;

    anim_.lastFrameQpc = 0;
    anim_.expectedIntervalQpc = expected;
    anim_.sumIntervalQpc = 0;
    anim_.maxIntervalQpc = 0;
    anim_.frameCount = 0;
    anim_.intervalCount = 0;
    anim_.droppedFrames = 0;
}

void ManagedWindow::PopulateStats() {
    const LONGLONG freq = QpcFreq();
    if (freq <= 0) return;

    AnimStats s;
    s.monitorHz = monitorHz_;
    s.durationMs = static_cast<int>((anim_.durationQpc * 1000) / freq);
    s.frames = anim_.frameCount;
    if (anim_.intervalCount > 0) {
        s.avgFrameMs = static_cast<double>(anim_.sumIntervalQpc) * 1000.0 /
                       static_cast<double>(freq) /
                       static_cast<double>(anim_.intervalCount);
    }
    s.maxFrameMs = static_cast<double>(anim_.maxIntervalQpc) * 1000.0 /
                   static_cast<double>(freq);
    s.droppedFrames = anim_.droppedFrames;
    s.valid = true;

    lastStats_ = s;
}
