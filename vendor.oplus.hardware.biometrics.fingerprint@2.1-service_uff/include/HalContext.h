/*
 * Copyright (C) 2026 The YAAP Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "FingerprintDriverAbi.h"
#include "FpTypes.h"
#include "PerfTrace.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace android {
namespace fingerprint {

class Device;
class FpCa;
class FingerprintCommon;
class FingerprintConfig;
class FingerprintNotify;
class IFingerprintCore;

// Process-wide owner of the fingerprint HAL subsystems.
class HalContext {
  public:
    HalContext();
    ~HalContext();

    HalContext(const HalContext&) = delete;
    HalContext& operator=(const HalContext&) = delete;

    // Brings up the CA, device and core. Returns 0 on success.
    int init();
    int deinit();

    // Waits until the touch worker is not running an operation. A new session
    // operation calls it before touching the TA so its setup cannot race an
    // operation that is still cleaning up.
    void waitForWorkIdle();

    // Arms a session operation. Drops a touch queued for the previous
    // operation, waits for one that is still running, then holds the touch
    // worker until endOperationSetup(). A touch that arrives while the
    // framework is still inside authenticate()/enroll() is kept and runs once
    // the operation has been opened in the TA: opening the session has to
    // finish first, because a capture issued before it is rejected and leaves
    // the TA with an operation no later command can close.
    void beginOperationSetup();
    void endOperationSetup();

    // Hands a display touch to the operation worker. The driver reader and the
    // AIDL pointer path both come through here.
    void postTouchDown(PerfTrace::Source source);

    // Screen-on authentication may consume the UI_READY that was posted when
    // its overlay and HBM were configured before the touch. A screen-off or
    // display-wake operation must still drop that event and wait for the panel
    // after the touch. AOSP can start authentication while the display is
    // dozing; the later context update promotes reuse once the display is up.
    // UI_DISAPPEAR clears a latch left by an older overlay.
    void beginUireadyOperation(bool allowReuse, bool displayOn);
    void updateUireadyDisplayState(bool displayOn);
    void disableUireadyReuse();

    FpCa* ca() const { return mCa.get(); }
    Device* device() const { return mDevice.get(); }
    FingerprintCommon* common() const { return mCommon.get(); }
    FingerprintConfig* config() const { return mConfig.get(); }
    FingerprintNotify* notify() const { return mNotify.get(); }
    IFingerprintCore* core() const { return mCore.get(); }
    PerfTrace& perf() { return mPerf; }

    TaName selectedTa() const { return mTaName; }
    bool isInitialized() const { return mInitialized; }

  private:
    int selectAndStartTa();
    void onDeviceEvent(const driver::FingerprintMessage& message);

    // The driver reader hands touch-downs to this thread, so a capture that
    // waits for UI_READY cannot block the thread that delivers it. UI_READY and
    // touch-up stay on the reader (the vendor's EventThd/WorkThd split).
    void startWorkThread();
    void stopWorkThread();
    void workThreadLoop();

    std::unique_ptr<FpCa> mCa;
    std::unique_ptr<Device> mDevice;
    std::unique_ptr<FingerprintCommon> mCommon;
    std::unique_ptr<FingerprintConfig> mConfig;
    std::unique_ptr<FingerprintNotify> mNotify;
    std::unique_ptr<IFingerprintCore> mCore;
    PerfTrace mPerf;

    // Written by the session thread and read by the driver reader. Allowed is
    // true for an authentication operation; reuse is the current display gate.
    std::atomic<bool> mUireadyReuseAllowed{false};
    std::atomic<bool> mReuseUiready{false};

    TaName mTaName = TaName::UffGx;
    bool mInitialized = false;

    std::thread mWorkThread;
    std::mutex mWorkMutex;
    std::condition_variable mWorkCond;
    std::condition_variable mWorkDoneCond;
    // Signalled when an operation setup finishes; the worker waits on it
    // before it issues commands to the TA.
    std::condition_variable mSetupCond;
    int mSetupCount = 0;
    bool mWorkRunning = false;
    bool mTouchPending = false;
    bool mWorkActive = false;
};

}  // namespace fingerprint
}  // namespace android
