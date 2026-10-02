#include "anim_pump.h"

namespace {
constexpr LONGLONG kMinInterval100ns = 1000;  // 0.1ms 下限
}

AnimPump::~AnimPump() { Stop(); }

bool AnimPump::Start(HWND msgWnd, UINT tickMessage) {
    if (thread_ != nullptr) return true;

    msgWnd_ = msgWnd;
    tickMsg_ = tickMessage;

    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    wakeEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (stopEvent_ == nullptr || wakeEvent_ == nullptr) { Stop(); return false; }

    // 优先高分辨率 waitable timer（Win10 1803+），无需 timeBeginPeriod
    timer_ = CreateWaitableTimerExW(nullptr, nullptr,
                                    CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                    TIMER_ALL_ACCESS);
    if (timer_ == nullptr) {
        // 退化：普通 waitable timer
        timer_ = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    }
    if (timer_ == nullptr) { Stop(); return false; }

    thread_ = CreateThread(nullptr, 0, &AnimPump::ThreadProc, this, 0, nullptr);
    if (thread_ == nullptr) { Stop(); return false; }
    return true;
}

void AnimPump::Stop() {
    quit_.store(true);
    if (stopEvent_ != nullptr) SetEvent(stopEvent_);

    if (thread_ != nullptr) {
        WaitForSingleObject(thread_, 2000);
        CloseHandle(thread_);
        thread_ = nullptr;
    }
    if (timer_ != nullptr) { CloseHandle(timer_); timer_ = nullptr; }
    if (stopEvent_ != nullptr) { CloseHandle(stopEvent_); stopEvent_ = nullptr; }
    if (wakeEvent_ != nullptr) { CloseHandle(wakeEvent_); wakeEvent_ = nullptr; }
    animating_.store(false);
    interval100ns_.store(0);
}

void AnimPump::SetAnimating(bool active, LONGLONG interval100ns) {
    interval100ns_.store(interval100ns);
    animating_.store(active);
    if (wakeEvent_ != nullptr) SetEvent(wakeEvent_);
}

DWORD WINAPI AnimPump::ThreadProc(LPVOID param) {
    static_cast<AnimPump*>(param)->Loop();
    return 0;
}

void AnimPump::Loop() {
    HANDLE outer[2] = { stopEvent_, wakeEvent_ };
    HANDLE inner[3] = { stopEvent_, wakeEvent_, timer_ };

    while (!quit_.load()) {
        if (!animating_.load()) {
            const DWORD r = WaitForMultipleObjects(2, outer, FALSE, INFINITE);
            if (r == WAIT_OBJECT_0) break;  // stop
            continue;                        // wake
        }

        LONGLONG iv = interval100ns_.load();
        if (iv < kMinInterval100ns) iv = kMinInterval100ns;

        LARGE_INTEGER due;
        due.QuadPart = -iv;  // 负值 = 相对时间
        if (!SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE)) {
            Sleep(static_cast<DWORD>(iv / 10000));
            if (animating_.load()) PostMessageW(msgWnd_, tickMsg_, 0, 0);
            continue;
        }

        const DWORD r = WaitForMultipleObjects(3, inner, FALSE, INFINITE);
        if (r == WAIT_OBJECT_0) break;         // stop
        if (r == WAIT_OBJECT_0 + 1) continue;  // wake：重查状态

        if (animating_.load()) {
            PostMessageW(msgWnd_, tickMsg_, 0, 0);
        }
    }
}
