#include "TretisHighScoresActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>

#include <cstdio>

#include "CatRunScoreStore.h"
#include "MappedInputManager.h"
#include "SudokuScoreStore.h"
#include "TretisScoreStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

void TretisHighScoresActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void TretisHighScoresActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
  }
}

void TretisHighScoresActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_HIGH_SCORES));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 2;
  const int lineH12 = renderer.getLineHeight(UI_12_FONT_ID);
  const int lineH10 = renderer.getLineHeight(UI_10_FONT_ID);

  char buf[64];
  char durationBuf[24];
  int y = contentTop;

  // --- TRETIS section ---
  renderer.drawCenteredText(UI_12_FONT_ID, y, tr(STR_TRETIS), true, EpdFontFamily::BOLD);
  y += lineH12 + 8;

  const int tretisCount = TRETIS_SCORES.getCount();
  if (tretisCount == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_NO_HIGH_SCORES), true);
    y += lineH10 + 16;
  } else {
    for (int i = 0; i < tretisCount; ++i) {
      const TretisScoreEntry& e = TRETIS_SCORES.getEntry(i);
      TretisScoreStore::formatDuration(e.durationSec, durationBuf, sizeof(durationBuf));

      snprintf(buf, sizeof(buf), "%d.  %s: %lu", i + 1, tr(STR_SCORE), static_cast<unsigned long>(e.score));
      renderer.drawCenteredText(UI_12_FONT_ID, y, buf, true);
      y += lineH12 + 4;

      snprintf(buf, sizeof(buf), "%s: %s", tr(STR_DURATION), durationBuf);
      renderer.drawCenteredText(UI_10_FONT_ID, y, buf, true);
      y += lineH10 + 8;
    }
    y += 4;
  }

  // --- Sudoku section ---
  renderer.drawCenteredText(UI_12_FONT_ID, y, tr(STR_SUDOKU), true, EpdFontFamily::BOLD);
  y += lineH12 + 8;

  const int sudokuCount = SUDOKU_SCORES.getCount();
  if (sudokuCount == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_NO_HIGH_SCORES), true);
    y += lineH10 + 16;
  } else {
    for (int i = 0; i < sudokuCount; ++i) {
      const SudokuScoreEntry& e = SUDOKU_SCORES.getEntry(i);
      SudokuScoreStore::formatDuration(e.durationSec, durationBuf, sizeof(durationBuf));

      snprintf(buf, sizeof(buf), "%d.  %s: %s", i + 1, tr(STR_DURATION), durationBuf);
      renderer.drawCenteredText(UI_12_FONT_ID, y, buf, true);
      y += lineH12 + 8;
    }
    y += 4;
  }

  // --- Cat Run section ---
  renderer.drawCenteredText(UI_12_FONT_ID, y, tr(STR_CAT_RUN), true, EpdFontFamily::BOLD);
  y += lineH12 + 8;

  const int catRunCount = CATRUN_SCORES.getCount();
  if (catRunCount == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_NO_HIGH_SCORES), true);
  } else {
    for (int i = 0; i < catRunCount; ++i) {
      const CatRunScoreEntry& e = CATRUN_SCORES.getEntry(i);
      CatRunScoreStore::formatDuration(e.durationSec, durationBuf, sizeof(durationBuf));

      snprintf(buf, sizeof(buf), "%d.  %s: %lu", i + 1, tr(STR_SCORE), static_cast<unsigned long>(e.score));
      renderer.drawCenteredText(UI_12_FONT_ID, y, buf, true);
      y += lineH12 + 4;

      snprintf(buf, sizeof(buf), "%s: %s  %s: %u", tr(STR_DURATION), durationBuf, tr(STR_RETRIES),
               static_cast<unsigned>(e.retries));
      renderer.drawCenteredText(UI_10_FONT_ID, y, buf, true);
      y += lineH10 + 8;
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer(HalDisplay::RefreshMode::FAST_REFRESH);
}
