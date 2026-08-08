#include "LockScreenActivity.h"

#include <I18n.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

extern void requestDeepSleepFromActivity();

namespace {
constexpr char DIGIT_KEYS[9] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};

// Draw PIN-error banner into the framebuffer only (no mid-render displayBuffer).
void drawErrorBanner(const GfxRenderer& renderer, const char* message) {
  constexpr int margin = 15;
  constexpr int y = 60;
  const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, message, EpdFontFamily::BOLD);
  const int textHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int w = textWidth + margin * 2;
  const int h = textHeight + margin * 2;
  const int x = (renderer.getScreenWidth() - w) / 2;
  renderer.fillRect(x - 2, y - 2, w + 4, h + 4, true);
  renderer.fillRect(x, y, w, h, false);
  renderer.drawText(UI_12_FONT_ID, x + (w - textWidth) / 2, y + margin - 2, message, true, EpdFontFamily::BOLD);
}
}  // namespace

char LockScreenActivity::getKeyLabel(const int row, const int col) const {
  if (row < 0 || row >= PinEntryActivity::NUM_ROWS || col < 0 || col >= PinEntryActivity::NUM_COLS) {
    return '\0';
  }
  if (row < 3) {
    return DIGIT_KEYS[row * PinEntryActivity::NUM_COLS + col];
  }
  if (col == PinEntryActivity::BACKSPACE_COL) {
    return '\b';
  }
  if (col == PinEntryActivity::ZERO_COL) {
    return '0';
  }
  if (col == PinEntryActivity::DONE_COL) {
    return '\n';
  }
  return '\0';
}

void LockScreenActivity::onEnter() {
  Activity::onEnter();
  pinText.clear();
  selectedRow = 0;
  selectedCol = 0;
  showErrorPopup = false;
  pendingUnlock = false;
  requestUpdateAndWait();
}

void LockScreenActivity::onExit() { Activity::onExit(); }

void LockScreenActivity::completeUnlock() {
  if (onUnlock) {
    onUnlock();
  }
}

void LockScreenActivity::verifyPin() {
  if (SETTINGS.verifyLockPin(pinText.c_str())) {
    // Defer navigation until Confirm is released — otherwise Home sees wasReleased(Confirm)
    // and opens the focused recent book.
    pendingUnlock = true;
    return;
  }
  showErrorPopup = true;
  pinText.clear();
  requestUpdate();
}

bool LockScreenActivity::handleKeyPress() {
  // Dismiss sticky error popup as soon as the user starts a new attempt.
  showErrorPopup = false;

  if (selectedRow == PinEntryActivity::SPECIAL_ROW) {
    if (selectedCol == PinEntryActivity::BACKSPACE_COL) {
      if (!pinText.empty()) {
        pinText.pop_back();
      }
      return true;
    }
    if (selectedCol == PinEntryActivity::ZERO_COL) {
      if (pinText.length() < PinEntryActivity::PIN_LENGTH) {
        pinText += '0';
      }
      if (pinText.length() == PinEntryActivity::PIN_LENGTH) {
        verifyPin();
        return false;
      }
      return true;
    }
    if (selectedCol == PinEntryActivity::DONE_COL) {
      if (pinText.length() == PinEntryActivity::PIN_LENGTH) {
        verifyPin();
        return false;
      }
      return true;
    }
  }

  const char c = getKeyLabel(selectedRow, selectedCol);
  if (c >= '1' && c <= '9' && pinText.length() < PinEntryActivity::PIN_LENGTH) {
    pinText += c;
    if (pinText.length() == PinEntryActivity::PIN_LENGTH) {
      verifyPin();
      return false;
    }
  }
  return true;
}

void LockScreenActivity::loop() {
  if (pendingUnlock) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      pendingUnlock = false;
      completeUnlock();
    }
    return;
  }

  auto onNav = [this] {
    // Dismiss error on first D-pad move so later nav does not re-paint the banner.
    showErrorPopup = false;
    requestUpdate();
  };

  // Press-only (no continuous): avoids e-ink refresh storms while a direction is held.
  buttonNavigator.onPress({MappedInputManager::Button::Up}, [this, onNav] {
    selectedRow = ButtonNavigator::previousIndex(selectedRow, PinEntryActivity::NUM_ROWS);
    onNav();
  });
  buttonNavigator.onPress({MappedInputManager::Button::Down}, [this, onNav] {
    selectedRow = ButtonNavigator::nextIndex(selectedRow, PinEntryActivity::NUM_ROWS);
    onNav();
  });
  buttonNavigator.onPress({MappedInputManager::Button::Left}, [this, onNav] {
    selectedCol = ButtonNavigator::previousIndex(selectedCol, PinEntryActivity::NUM_COLS);
    onNav();
  });
  buttonNavigator.onPress({MappedInputManager::Button::Right}, [this, onNav] {
    selectedCol = ButtonNavigator::nextIndex(selectedCol, PinEntryActivity::NUM_COLS);
    onNav();
  });

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (handleKeyPress()) {
      requestUpdate();
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    // Defer sleep to main::loop — calling enterDeepSleep() here would destroy this activity
    // while LockScreenActivity::loop() is still running.
    requestDeepSleepFromActivity();
  }
}

void LockScreenActivity::renderKeypad(const int keyboardStartY, const int pageWidth) const {
  const auto keypad = PinEntryActivity::computeKeypadLayout(pageWidth);

  for (int row = 0; row < PinEntryActivity::NUM_ROWS; row++) {
    const int rowY = keyboardStartY + row * (keypad.keyHeight + keypad.keySpacing);
    for (int col = 0; col < PinEntryActivity::NUM_COLS; col++) {
      const int keyX = keypad.leftMargin + col * (keypad.keyWidth + keypad.keySpacing);
      const bool isSelected = row == selectedRow && col == selectedCol;
      const char keyChar = getKeyLabel(row, col);
      const char* label = "";
      char digitLabel[2] = {};
      if (keyChar == '\b') {
        label = "<-";
      } else if (keyChar == '\n') {
        label = tr(STR_OK_BUTTON);
      } else if (keyChar >= '0' && keyChar <= '9') {
        digitLabel[0] = keyChar;
        label = digitLabel;
      }
      GUI.drawKeyboardKey(renderer, Rect{keyX, rowY, keypad.keyWidth, keypad.keyHeight}, label, isSelected);
    }
  }
}

void LockScreenActivity::renderMessages(const int messageBottomY) const {
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  int y = messageBottomY;

  if (SETTINGS.lockMessageLine2[0] != '\0') {
    renderer.drawCenteredText(UI_12_FONT_ID, y, SETTINGS.lockMessageLine2);
    y -= lineHeight + 4;
  }
  if (SETTINGS.lockMessageLine1[0] != '\0') {
    renderer.drawCenteredText(UI_12_FONT_ID, y, SETTINGS.lockMessageLine1);
  }
}

void LockScreenActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_ENTER_PIN));

  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int pinLineHeight = renderer.getLineHeight(PinEntryActivity::PIN_MASK_FONT_ID);
  const int inputStartY =
      metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + metrics.verticalSpacing * 2;
  const std::string displayText = std::string(pinText.length(), '*') + "_";
  renderer.drawCenteredText(PinEntryActivity::PIN_MASK_FONT_ID, inputStartY, displayText.c_str());
  const int textWidth = renderer.getTextWidth(PinEntryActivity::PIN_MASK_FONT_ID, displayText.c_str());
  GUI.drawTextField(renderer, Rect{0, inputStartY, pageWidth, pinLineHeight}, textWidth);

  const auto keypad = PinEntryActivity::computeKeypadLayout(pageWidth);
  const int messageAreaHeight = lineHeight * 2 + 8;
  const int keyboardBlockHeight =
      PinEntryActivity::NUM_ROWS * (keypad.keyHeight + keypad.keySpacing) - keypad.keySpacing;
  const int keyboardStartY = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - messageAreaHeight -
                             metrics.verticalSpacing - keyboardBlockHeight;

  renderKeypad(keyboardStartY, pageWidth);

  const int messageBottomY =
      pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - lineHeight;
  renderMessages(messageBottomY);

  if (showErrorPopup) {
    drawErrorBanner(renderer, tr(STR_PIN_INCORRECT));
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  GUI.drawSideButtonHints(renderer, ">", "<");

  renderer.displayBuffer();
}
