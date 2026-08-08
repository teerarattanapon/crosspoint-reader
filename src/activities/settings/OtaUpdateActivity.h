#pragma once

#include "activities/Activity.h"
#include "network/OtaUpdater.h"

class OtaUpdateActivity : public Activity {
  enum State {
    WIFI_SELECTION,
    CHECKING_FOR_UPDATE,
    WAITING_CONFIRMATION,
    UPDATE_IN_PROGRESS,
    NO_UPDATE,
    FAILED,
    FINISHED,
    SHUTTING_DOWN
  };

  // Can't initialize this to 0 or the first render doesn't happen
  static constexpr unsigned int UNINITIALIZED_PERCENTAGE = 111;
  static constexpr uint32_t kRebootDelayMs = 10000;

  State state = WIFI_SELECTION;
  unsigned int lastUpdaterPercentage = UNINITIALIZED_PERCENTAGE;
  uint32_t finishedAtMs = 0;
  OtaUpdater updater;
  OtaUpdater::OtaUpdaterError failureDetail = OtaUpdater::OK;
  bool lastFailureWasInstall = false;

  void onWifiSelectionComplete(bool success);

 public:
  explicit OtaUpdateActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("OtaUpdate", renderer, mappedInput), updater() {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override {
    return state == CHECKING_FOR_UPDATE || state == UPDATE_IN_PROGRESS || state == FINISHED ||
           state == SHUTTING_DOWN;
  }
  bool skipLoopDelay() override { return true; }  // Prevent power-saving mode
};
