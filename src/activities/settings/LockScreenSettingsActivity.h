#pragma once

#include <GfxRenderer.h>

#include <functional>
#include <string>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class LockScreenSettingsActivity final : public Activity {
 public:
  explicit LockScreenSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("LockScreenSettings", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static constexpr int ROW_PIN = 0;
  static constexpr int ROW_LINE1 = 1;
  static constexpr int ROW_LINE2 = 2;
  static constexpr int ROW_COUNT = 3;

  void handleSelection();
  void promptCurrentPin();
  void promptNewPin();
  void promptConfirmPin();
  void savePin();
  void promptMessageLine1();
  void promptMessageLine2();

  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  std::string pendingNewPin;
};
