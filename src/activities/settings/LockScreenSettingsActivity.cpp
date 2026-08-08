#include "LockScreenSettingsActivity.h"

#include <I18n.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "activities/util/PinEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

void LockScreenSettingsActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  requestUpdate();
}

void LockScreenSettingsActivity::onExit() { Activity::onExit(); }

void LockScreenSettingsActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }

  buttonNavigator.onNextRelease([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, ROW_COUNT);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, ROW_COUNT);
    requestUpdate();
  });
}

void LockScreenSettingsActivity::handleSelection() {
  // PIN / message lines can only be set once Lock Screen is turned on
  // (from the main Settings list) — same no-op-when-disabled convention
  // used by SettingType::INFO rows there.
  if (!SETTINGS.lockEnabled) {
    return;
  }

  switch (selectedIndex) {
    case ROW_PIN:
      pendingNewPin.clear();
      if (SETTINGS.lockPin[0] != '\0') {
        promptCurrentPin();
      } else {
        promptNewPin();
      }
      break;
    case ROW_LINE1:
      promptMessageLine1();
      break;
    case ROW_LINE2:
      promptMessageLine2();
      break;
    default:
      break;
  }
}

void LockScreenSettingsActivity::promptCurrentPin() {
  startActivityForResult(
      std::make_unique<PinEntryActivity>(renderer, mappedInput, tr(STR_ENTER_CURRENT_PIN)),
      [this](const ActivityResult& result) {
        if (result.isCancelled) {
          requestUpdate();
          return;
        }
        const auto& pin = std::get<KeyboardResult>(result.data).text;
        if (!SETTINGS.verifyLockPin(pin.c_str())) {
          promptCurrentPin();
          return;
        }
        promptNewPin();
      });
}

void LockScreenSettingsActivity::promptNewPin() {
  startActivityForResult(std::make_unique<PinEntryActivity>(renderer, mappedInput, tr(STR_ENTER_NEW_PIN)),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) {
                             requestUpdate();
                             return;
                           }
                           const auto& pin = std::get<KeyboardResult>(result.data).text;
                           if (!SETTINGS.isValidLockPin(pin.c_str())) {
                             promptNewPin();
                             return;
                           }
                           pendingNewPin = pin;
                           promptConfirmPin();
                         });
}

void LockScreenSettingsActivity::promptConfirmPin() {
  startActivityForResult(std::make_unique<PinEntryActivity>(renderer, mappedInput, tr(STR_CONFIRM_PIN)),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) {
                             requestUpdate();
                             return;
                           }
                           const auto& pin = std::get<KeyboardResult>(result.data).text;
                           if (pin != pendingNewPin) {
                             startActivityForResult(
                                 std::make_unique<PinEntryActivity>(renderer, mappedInput, tr(STR_PIN_MISMATCH)),
                                 [this](const ActivityResult&) { promptNewPin(); });
                             return;
                           }
                           savePin();
                         });
}

void LockScreenSettingsActivity::savePin() {
  strncpy(SETTINGS.lockPin, pendingNewPin.c_str(), sizeof(SETTINGS.lockPin) - 1);
  SETTINGS.lockPin[sizeof(SETTINGS.lockPin) - 1] = '\0';
  SETTINGS.saveToFile();
  requestUpdate();
}

void LockScreenSettingsActivity::promptMessageLine1() {
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_LOCK_MESSAGE_LINE1),
                                              SETTINGS.lockMessageLine1, 63, false),
      [this](const ActivityResult& result) {
        if (!result.isCancelled) {
          const auto& text = std::get<KeyboardResult>(result.data).text;
          strncpy(SETTINGS.lockMessageLine1, text.c_str(), sizeof(SETTINGS.lockMessageLine1) - 1);
          SETTINGS.lockMessageLine1[sizeof(SETTINGS.lockMessageLine1) - 1] = '\0';
          SETTINGS.saveToFile();
        }
        requestUpdate();
      });
}

void LockScreenSettingsActivity::promptMessageLine2() {
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_LOCK_MESSAGE_LINE2),
                                              SETTINGS.lockMessageLine2, 63, false),
      [this](const ActivityResult& result) {
        if (!result.isCancelled) {
          const auto& text = std::get<KeyboardResult>(result.data).text;
          strncpy(SETTINGS.lockMessageLine2, text.c_str(), sizeof(SETTINGS.lockMessageLine2) - 1);
          SETTINGS.lockMessageLine2[sizeof(SETTINGS.lockMessageLine2) - 1] = '\0';
          SETTINGS.saveToFile();
        }
        requestUpdate();
      });
}

void LockScreenSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_LOCK_SCREEN_SETUP));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, ROW_COUNT, selectedIndex,
      [](int index) {
        switch (index) {
          case ROW_PIN:
            return tr(STR_PIN);
          case ROW_LINE1:
            return tr(STR_LOCK_MESSAGE_LINE1);
          default:
            return tr(STR_LOCK_MESSAGE_LINE2);
        }
      },
      nullptr, nullptr,
      [](int index) -> std::string {
        switch (index) {
          case ROW_PIN:
            return SETTINGS.lockPin[0] != '\0' ? "****" : tr(STR_NOT_SET);
          case ROW_LINE1:
            return SETTINGS.lockMessageLine1[0] != '\0' ? SETTINGS.lockMessageLine1 : tr(STR_NOT_SET);
          default:
            return SETTINGS.lockMessageLine2[0] != '\0' ? SETTINGS.lockMessageLine2 : tr(STR_NOT_SET);
        }
      },
      true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
