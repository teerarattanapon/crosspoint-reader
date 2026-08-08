#pragma once

#include <GfxRenderer.h>

#include <functional>
#include <string>

#include "activities/Activity.h"
#include "activities/util/PinEntryActivity.h"
#include "util/ButtonNavigator.h"

class LockScreenActivity final : public Activity {
 public:
  explicit LockScreenActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                              std::function<void()> onUnlockCallback)
      : Activity("LockScreen", renderer, mappedInput),
        onUnlock(std::move(onUnlockCallback)),
        buttonNavigator() {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::function<void()> onUnlock;
  ButtonNavigator buttonNavigator;

  std::string pinText;
  int selectedRow = 0;
  int selectedCol = 0;
  bool showErrorPopup = false;
  bool pendingUnlock = false;

  bool handleKeyPress();
  char getKeyLabel(int row, int col) const;
  void verifyPin();
  void completeUnlock();
  void renderKeypad(int keyboardStartY, int pageWidth) const;
  void renderMessages(int messageBottomY) const;
};
