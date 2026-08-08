#pragma once

#include <GfxRenderer.h>

#include <string>
#include <utility>

#include "../Activity.h"
#include "fontIds.h"
#include "util/ButtonNavigator.h"

/**
 * Numeric PIN entry activity (4 digits) with a 3x4 keypad.
 * Result: KeyboardResult via startActivityForResult().
 */
class PinEntryActivity : public Activity {
 public:
  static constexpr size_t PIN_LENGTH = 4;
  static constexpr int NUM_ROWS = 4;
  static constexpr int NUM_COLS = 3;
  static constexpr int SPECIAL_ROW = 3;
  static constexpr int BACKSPACE_COL = 0;
  static constexpr int ZERO_COL = 1;
  static constexpr int DONE_COL = 2;
  // Shared with LockScreenActivity — larger than UI_12 for readable masks.
  static constexpr int PIN_MASK_FONT_ID = NOTOSANS_18_FONT_ID;

  struct KeypadLayout {
    int keyWidth;
    int keyHeight;
    int keySpacing;
    int leftMargin;
    int rowWidth;
  };

  // Half-screen-wide centered keypad (shared with LockScreenActivity).
  static KeypadLayout computeKeypadLayout(int pageWidth);

  explicit PinEntryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title,
                            std::string initialText = "")
      : Activity("PinEntry", renderer, mappedInput),
        title(std::move(title)),
        text(std::move(initialText)),
        buttonNavigator() {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::string title;
  std::string text;
  ButtonNavigator buttonNavigator;

  int selectedRow = 0;
  int selectedCol = 0;

  void onComplete(std::string pin);
  void onCancel();
  bool handleKeyPress();
  char getKeyLabel(int row, int col) const;
};
