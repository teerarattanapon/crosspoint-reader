#include "GamesMenuActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "../util/ConfirmationActivity.h"
#include "CatRunActivity.h"
#include "CatRunSaveStore.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "SudokuActivity.h"
#include "SudokuSaveStore.h"
#include "TretisActivity.h"
#include "TretisHighScoresActivity.h"
#include "TretisSaveStore.h"
#include "components/UITheme.h"

namespace {

enum class MenuAction : uint8_t { PlayTretis, PlaySudoku, PlayCatRun, HighScores };

struct MenuEntry {
  MenuAction action;
  StrId title;
  StrId description;
  UIIcon icon;
};

// Games first, then High Scores — append new games before HighScores.
static constexpr MenuEntry kMenu[] = {
    {MenuAction::PlayTretis, StrId::STR_TRETIS, StrId::STR_TRETIS_DESC, UIIcon::Tretis},
    {MenuAction::PlaySudoku, StrId::STR_SUDOKU, StrId::STR_SUDOKU_DESC, UIIcon::Sudoku},
    {MenuAction::PlayCatRun, StrId::STR_CAT_RUN, StrId::STR_CAT_RUN_DESC, UIIcon::CatRun},
    {MenuAction::HighScores, StrId::STR_HIGH_SCORES, StrId::STR_HIGH_SCORES_DESC, UIIcon::Recent},
};

static constexpr int kMenuCount = static_cast<int>(sizeof(kMenu) / sizeof(kMenu[0]));

}  // namespace

void GamesMenuActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;

  // After wake-from-sleep during a game, open the matching resume prompt immediately.
  if (APP_STATE.autoResumeTretis) {
    APP_STATE.autoResumeTretis = false;
    APP_STATE.saveToFile();
    selectedIndex = 0;  // Play Tretis
    launchSelected();
    return;
  }
  if (APP_STATE.autoResumeSudoku) {
    APP_STATE.autoResumeSudoku = false;
    APP_STATE.saveToFile();
    selectedIndex = 1;  // Play Sudoku
    launchSelected();
    return;
  }
  if (APP_STATE.autoResumeCatRun) {
    APP_STATE.autoResumeCatRun = false;
    APP_STATE.saveToFile();
    selectedIndex = 2;  // Play Cat Run
    launchSelected();
    return;
  }

  requestUpdate();
}

void GamesMenuActivity::launchSelected() {
  if (selectedIndex < 0 || selectedIndex >= kMenuCount) {
    return;
  }
  switch (kMenu[selectedIndex].action) {
    case MenuAction::PlayTretis:
      if (TRETIS_SAVE.hasSave()) {
        startActivityForResult(
            std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_RESUME_GAME),
                                                   tr(STR_RESUME_GAME_BODY)),
            [this](const ActivityResult& result) {
              if (result.isCancelled) {
                // Cancel = dismiss only; keep save so the resume prompt returns next time.
                requestUpdate();
                return;
              }
              startActivityForResult(std::make_unique<TretisActivity>(renderer, mappedInput, /*resumeFromSave=*/true),
                                     [this](const ActivityResult&) { requestUpdate(); });
            });
      } else {
        startActivityForResult(std::make_unique<TretisActivity>(renderer, mappedInput, /*resumeFromSave=*/false),
                               [this](const ActivityResult&) { requestUpdate(); });
      }
      break;
    case MenuAction::PlaySudoku:
      if (SUDOKU_SAVE.hasSave()) {
        startActivityForResult(
            std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_RESUME_GAME),
                                                   tr(STR_RESUME_SUDOKU_BODY)),
            [this](const ActivityResult& result) {
              if (result.isCancelled) {
                // Cancel = dismiss only; keep save so the resume prompt returns next time.
                requestUpdate();
                return;
              }
              startActivityForResult(std::make_unique<SudokuActivity>(renderer, mappedInput, /*resumeFromSave=*/true),
                                     [this](const ActivityResult&) { requestUpdate(); });
            });
      } else {
        startActivityForResult(std::make_unique<SudokuActivity>(renderer, mappedInput, /*resumeFromSave=*/false),
                               [this](const ActivityResult&) { requestUpdate(); });
      }
      break;
    case MenuAction::PlayCatRun:
      if (CATRUN_SAVE.hasSave()) {
        startActivityForResult(
            std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_RESUME_GAME),
                                                   tr(STR_RESUME_CAT_RUN_BODY)),
            [this](const ActivityResult& result) {
              if (result.isCancelled) {
                requestUpdate();
                return;
              }
              startActivityForResult(std::make_unique<CatRunActivity>(renderer, mappedInput, /*resumeFromSave=*/true),
                                     [this](const ActivityResult&) { requestUpdate(); });
            });
      } else {
        startActivityForResult(std::make_unique<CatRunActivity>(renderer, mappedInput, /*resumeFromSave=*/false),
                               [this](const ActivityResult&) { requestUpdate(); });
      }
      break;
    case MenuAction::HighScores:
      startActivityForResult(std::make_unique<TretisHighScoresActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) { requestUpdate(); });
      break;
  }
}

void GamesMenuActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    activityManager.goHome();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    launchSelected();
    return;
  }

  buttonNavigator.onNext([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, kMenuCount);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, kMenuCount);
    requestUpdate();
  });
}

void GamesMenuActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_GAMES));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, kMenuCount, selectedIndex,
      [](int index) { return std::string(I18N.get(kMenu[index].title)); },
      [](int index) { return std::string(I18N.get(kMenu[index].description)); },
      [](int index) { return kMenu[index].icon; });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
