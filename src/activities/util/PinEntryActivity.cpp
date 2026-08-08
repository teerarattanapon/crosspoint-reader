#include "PinEntryActivity.h"

#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace {
constexpr char DIGIT_KEYS[9] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
}  // namespace

PinEntryActivity::KeypadLayout PinEntryActivity::computeKeypadLayout(const int pageWidth) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  KeypadLayout layout;
  layout.rowWidth = pageWidth / 2;
  layout.keySpacing = metrics.keyboardKeySpacing;
  layout.keyWidth = (layout.rowWidth - (NUM_COLS - 1) * layout.keySpacing) / NUM_COLS;
  layout.keyHeight = metrics.keyboardKeyHeight;
  layout.leftMargin = (pageWidth - layout.rowWidth) / 2;
  return layout;
}

char PinEntryActivity::getKeyLabel(const int row, const int col) const {
  if (row < 0 || row >= NUM_ROWS || col < 0 || col >= NUM_COLS) {
    return '\0';
  }
  if (row < 3) {
    return DIGIT_KEYS[row * NUM_COLS + col];
  }
  if (col == BACKSPACE_COL) {
    return '\b';
  }
  if (col == ZERO_COL) {
    return '0';
  }
  if (col == DONE_COL) {
    return '\n';
  }
  return '\0';
}

void PinEntryActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void PinEntryActivity::onExit() { Activity::onExit(); }

bool PinEntryActivity::handleKeyPress() {
  if (selectedRow == SPECIAL_ROW) {
    if (selectedCol == BACKSPACE_COL) {
      if (!text.empty()) {
        text.pop_back();
      }
      return true;
    }
    if (selectedCol == ZERO_COL) {
      if (text.length() < PIN_LENGTH) {
        text += '0';
      }
      if (text.length() == PIN_LENGTH) {
        onComplete(text);
        return false;
      }
      return true;
    }
    if (selectedCol == DONE_COL) {
      if (text.length() == PIN_LENGTH) {
        onComplete(text);
        return false;
      }
      return true;
    }
  }

  const char c = getKeyLabel(selectedRow, selectedCol);
  if (c >= '1' && c <= '9' && text.length() < PIN_LENGTH) {
    text += c;
    if (text.length() == PIN_LENGTH) {
      onComplete(text);
      return false;
    }
  }
  return true;
}

void PinEntryActivity::loop() {
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up}, [this] {
    selectedRow = ButtonNavigator::previousIndex(selectedRow, NUM_ROWS);
    requestUpdate();
  });

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down}, [this] {
    selectedRow = ButtonNavigator::nextIndex(selectedRow, NUM_ROWS);
    requestUpdate();
  });

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this] {
    selectedCol = ButtonNavigator::previousIndex(selectedCol, NUM_COLS);
    requestUpdate();
  });

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this] {
    selectedCol = ButtonNavigator::nextIndex(selectedCol, NUM_COLS);
    requestUpdate();
  });

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (handleKeyPress()) {
      requestUpdate();
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onCancel();
  }
}

void PinEntryActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title.c_str());

  const int lineHeight = renderer.getLineHeight(PIN_MASK_FONT_ID);
  const int inputStartY =
      metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + metrics.verticalSpacing * 2;
  const std::string displayText = std::string(text.length(), '*') + "_";
  renderer.drawCenteredText(PIN_MASK_FONT_ID, inputStartY, displayText.c_str());
  const int inputHeight = lineHeight;
  const int textWidth = renderer.getTextWidth(PIN_MASK_FONT_ID, displayText.c_str());
  GUI.drawTextField(renderer, Rect{0, inputStartY, pageWidth, inputHeight}, textWidth);

  const auto keypad = computeKeypadLayout(pageWidth);
  const int keyboardStartY = metrics.keyboardBottomAligned
                                 ? pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing -
                                       (keypad.keyHeight + keypad.keySpacing) * NUM_ROWS
                                 : inputStartY + inputHeight + metrics.verticalSpacing * 4;

  for (int row = 0; row < NUM_ROWS; row++) {
    const int rowY = keyboardStartY + row * (keypad.keyHeight + keypad.keySpacing);
    for (int col = 0; col < NUM_COLS; col++) {
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

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  GUI.drawSideButtonHints(renderer, ">", "<");

  renderer.displayBuffer();
}

void PinEntryActivity::onComplete(std::string pin) {
  setResult(KeyboardResult{std::move(pin)});
  finish();
}

void PinEntryActivity::onCancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}
