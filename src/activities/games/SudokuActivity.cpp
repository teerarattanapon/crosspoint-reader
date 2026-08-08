#include "SudokuActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>

#include "../util/ConfirmationActivity.h"
#include "MappedInputManager.h"
#include "SudokuPuzzles.h"
#include "SudokuSaveStore.h"
#include "SudokuScoreStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

uint32_t nextRandom(uint32_t& state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

}  // namespace

void SudokuActivity::onEnter() {
  Activity::onEnter();
  if (resumeFromSave && SUDOKU_SAVE.hasSave()) {
    loadFromSave();
  } else {
    resetGame();
  }
  requestUpdate();
}

void SudokuActivity::onExit() {
  if (!completed) {
    saveProgress();
  }
  Activity::onExit();
}

void SudokuActivity::applyPuzzle(const char* puzzle, uint32_t seed) {
  // Digit remapping (1–9) for variety; 0 stays empty.
  uint8_t map[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  for (int i = 9; i >= 2; --i) {
    const int j = 1 + static_cast<int>(nextRandom(seed) % static_cast<uint32_t>(i));
    const uint8_t tmp = map[i];
    map[i] = map[j];
    map[j] = tmp;
  }
  const bool transpose = (nextRandom(seed) & 1u) != 0;

  memset(given, 0, sizeof(given));
  memset(board, 0, sizeof(board));
  for (int i = 0; i < SudokuPuzzles::CELLS; ++i) {
    const char ch = puzzle[i];
    uint8_t digit = 0;
    if (ch >= '1' && ch <= '9') {
      digit = map[static_cast<uint8_t>(ch - '0')];
    }
    int r = i / SIZE;
    int c = i % SIZE;
    if (transpose) {
      const int t = r;
      r = c;
      c = t;
    }
    given[r][c] = digit;
    board[r][c] = digit;
  }
}

void SudokuActivity::resetGame() {
  completed = false;
  scoreSubmitted = false;
  lastDurationSec = 0;
  cursorRow = 0;
  cursorCol = 0;
  gameStartMs = millis();

  uint32_t seed = millis() | 1u;
  const int idx = static_cast<int>(nextRandom(seed) % static_cast<uint32_t>(SudokuPuzzles::kCount));
  applyPuzzle(SudokuPuzzles::kPuzzles[idx], seed);

  // Prefer cursor on first empty cell.
  bool found = false;
  for (int r = 0; r < SIZE && !found; ++r) {
    for (int c = 0; c < SIZE; ++c) {
      if (given[r][c] == 0) {
        cursorRow = r;
        cursorCol = c;
        found = true;
        break;
      }
    }
  }

  SUDOKU_SAVE.clearFile();
}

void SudokuActivity::loadFromSave() {
  static_assert(SIZE == SudokuSaveStore::SIZE, "board size mismatch");
  memcpy(given, SUDOKU_SAVE.getGiven(), sizeof(given));
  memcpy(board, SUDOKU_SAVE.getBoard(), sizeof(board));
  cursorRow = SUDOKU_SAVE.getCursorRow();
  cursorCol = SUDOKU_SAVE.getCursorCol();
  completed = false;
  scoreSubmitted = false;
  lastDurationSec = 0;
  const uint32_t elapsed = SUDOKU_SAVE.getElapsedMs();
  const uint32_t now = millis();
  gameStartMs = (now > elapsed) ? (now - elapsed) : now;
}

void SudokuActivity::saveProgress() const {
  const uint32_t now = millis();
  const uint32_t elapsed = (now >= gameStartMs) ? (now - gameStartMs) : 0;
  SUDOKU_SAVE.set(given, board, cursorRow, cursorCol, elapsed);
  SUDOKU_SAVE.saveToFile();
}

void SudokuActivity::onComplete() {
  if (scoreSubmitted) {
    return;
  }
  scoreSubmitted = true;
  completed = true;
  const uint32_t elapsedMs = millis() - gameStartMs;
  lastDurationSec = elapsedMs / 1000u;
  SUDOKU_SAVE.clearFile();
  if (SUDOKU_SCORES.submit(lastDurationSec)) {
    SUDOKU_SCORES.saveToFile();
  }
}

bool SudokuActivity::isConflict(int row, int col, uint8_t digit) const {
  if (digit == 0) {
    return false;
  }
  for (int c = 0; c < SIZE; ++c) {
    if (c != col && board[row][c] == digit) {
      return true;
    }
  }
  for (int r = 0; r < SIZE; ++r) {
    if (r != row && board[r][col] == digit) {
      return true;
    }
  }
  const int br = (row / 3) * 3;
  const int bc = (col / 3) * 3;
  for (int r = br; r < br + 3; ++r) {
    for (int c = bc; c < bc + 3; ++c) {
      if ((r != row || c != col) && board[r][c] == digit) {
        return true;
      }
    }
  }
  return false;
}

bool SudokuActivity::isBoardFull() const {
  for (int r = 0; r < SIZE; ++r) {
    for (int c = 0; c < SIZE; ++c) {
      if (board[r][c] == 0) {
        return false;
      }
    }
  }
  return true;
}

void SudokuActivity::moveCursor(int dRow, int dCol) {
  cursorRow = (cursorRow + dRow + SIZE) % SIZE;
  cursorCol = (cursorCol + dCol + SIZE) % SIZE;
  requestUpdate();
}

void SudokuActivity::cycleCell() {
  if (given[cursorRow][cursorCol] != 0) {
    return;  // Locked clue
  }
  uint8_t next = board[cursorRow][cursorCol];
  for (int attempt = 0; attempt < 10; ++attempt) {
    next = static_cast<uint8_t>((next + 1) % 10);  // 0..9
    if (!isConflict(cursorRow, cursorCol, next)) {
      board[cursorRow][cursorCol] = next;
      if (isBoardFull()) {
        onComplete();
      }
      requestUpdate();
      return;
    }
  }
  // All digits conflict — clear cell.
  board[cursorRow][cursorCol] = 0;
  requestUpdate();
}

void SudokuActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (completed) {
      finish();
      return;
    }
    // Freeze wall-clock duration while the pause/restart dialog is open.
    const uint32_t now = millis();
    frozenElapsedMs = (now >= gameStartMs) ? (now - gameStartMs) : 0;
    startActivityForResult(
        std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_PAUSE_GAME),
                                               tr(STR_PAUSE_OR_RESUME_BODY), tr(STR_RESUME), tr(STR_PAUSE)),
        [this](const ActivityResult& result) {
          const uint32_t resumeNow = millis();
          gameStartMs = (resumeNow > frozenElapsedMs) ? (resumeNow - frozenElapsedMs) : resumeNow;
          if (result.isCancelled) {
            // Left = Resume: continue playing.
            requestUpdate();
            return;
          }
          // Right = Pause: save on onExit and return to games menu.
          finish();
        });
    return;
  }

  if (completed) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      resetGame();
      requestUpdate();
    }
    return;
  }

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this] { moveCursor(0, -1); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this] { moveCursor(0, 1); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up}, [this] { moveCursor(-1, 0); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down}, [this] { moveCursor(1, 0); });

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    cycleCell();
  }
}

void SudokuActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_SUDOKU));

  int top = 0;
  int right = 0;
  int bottom = 0;
  int left = 0;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int hintsH = metrics.buttonHintsHeight;
  const int availH = pageHeight - contentTop - hintsH - metrics.verticalSpacing - bottom;
  const int sidePanelW = 100;
  const int boardAreaW = pageWidth - left - right - sidePanelW - metrics.verticalSpacing * 2;
  const int cell = (availH / SIZE < boardAreaW / SIZE) ? (availH / SIZE) : (boardAreaW / SIZE);
  const int boardW = cell * SIZE;
  const int boardH = cell * SIZE;
  const int boardX = left + metrics.verticalSpacing;
  const int boardY = contentTop + (availH - boardH) / 2;

  // Outer frame
  renderer.drawRect(boardX - 1, boardY - 1, boardW + 2, boardH + 2, true);

  // Grid lines (thick every 3)
  for (int i = 0; i <= SIZE; ++i) {
    const int x = boardX + i * cell;
    const int y = boardY + i * cell;
    renderer.drawLine(x, boardY, x, boardY + boardH, true);
    renderer.drawLine(boardX, y, boardX + boardW, y, true);
    if (i % 3 == 0) {
      if (i > 0 && i < SIZE) {
        renderer.drawLine(x - 1, boardY, x - 1, boardY + boardH, true);
        renderer.drawLine(x + 1, boardY, x + 1, boardY + boardH, true);
        renderer.drawLine(boardX, y - 1, boardX + boardW, y - 1, true);
        renderer.drawLine(boardX, y + 1, boardX + boardW, y + 1, true);
      }
    }
  }

  const int fontId = (cell >= 28) ? UI_12_FONT_ID : UI_10_FONT_ID;
  const int lineH = renderer.getLineHeight(fontId);
  char digitBuf[2] = {'0', '\0'};

  for (int r = 0; r < SIZE; ++r) {
    for (int c = 0; c < SIZE; ++c) {
      const int x = boardX + c * cell;
      const int y = boardY + r * cell;
      const bool isCursor = (!completed && r == cursorRow && c == cursorCol);
      if (isCursor) {
        renderer.fillRect(x + 2, y + 2, cell - 4, cell - 4, true);
      }
      if (board[r][c] != 0) {
        digitBuf[0] = static_cast<char>('0' + board[r][c]);
        const int tw = renderer.getTextWidth(fontId, digitBuf,
                                             given[r][c] != 0 ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
        const int tx = x + (cell - tw) / 2;
        // drawText baseline = ty + ascender; center the line box in the cell.
        const int ty = y + (cell - lineH) / 2;
        // Invert text on cursor (filled cell).
        renderer.drawText(fontId, tx, ty, digitBuf, !isCursor,
                          given[r][c] != 0 ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
      }
    }
  }

  // Side panel: elapsed time
  const int panelX = boardX + boardW + metrics.verticalSpacing;
  const int panelLineH = renderer.getLineHeight(UI_10_FONT_ID);
  int textY = boardY;
  char buf[64];
  char durationBuf[24];

  renderer.drawText(UI_10_FONT_ID, panelX, textY, tr(STR_DURATION), true, EpdFontFamily::BOLD);
  textY += panelLineH;
  const uint32_t elapsedSec =
      completed ? lastDurationSec : ((millis() - gameStartMs) / 1000u);
  SudokuScoreStore::formatDuration(elapsedSec, durationBuf, sizeof(durationBuf));
  renderer.drawText(UI_10_FONT_ID, panelX, textY, durationBuf, true);

  if (completed) {
    const int scoreCount = SUDOKU_SCORES.getCount();
    const int titleH = renderer.getLineHeight(UI_12_FONT_ID);
    const int rowH = panelLineH;
    const int extraRows = 2 + (scoreCount > 0 ? 1 + scoreCount : 0);
    const int boxW = 280;
    const int boxH = 16 + titleH + 4 + extraRows * rowH + 12;
    const int boxX = (pageWidth - boxW) / 2;
    const int boxY = (pageHeight - boxH) / 2;
    renderer.fillRect(boxX - 2, boxY - 2, boxW + 4, boxH + 4, true);
    renderer.fillRect(boxX, boxY, boxW, boxH, false);

    int y = boxY + 10;
    renderer.drawCenteredText(UI_12_FONT_ID, y, tr(STR_SUDOKU_COMPLETE), true, EpdFontFamily::BOLD);
    y += titleH + 4;

    snprintf(buf, sizeof(buf), "%s: %s", tr(STR_DURATION), durationBuf);
    renderer.drawCenteredText(UI_10_FONT_ID, y, buf, true);
    y += rowH;

    if (scoreCount > 0) {
      renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_HIGH_SCORES), true, EpdFontFamily::BOLD);
      y += rowH;
      for (int i = 0; i < scoreCount; ++i) {
        const SudokuScoreEntry& e = SUDOKU_SCORES.getEntry(i);
        SudokuScoreStore::formatDuration(e.durationSec, durationBuf, sizeof(durationBuf));
        snprintf(buf, sizeof(buf), "%d. %s", i + 1, durationBuf);
        renderer.drawCenteredText(UI_10_FONT_ID, y, buf, true);
        y += rowH;
      }
    }
  }

  const auto labels =
      completed ? mappedInput.mapLabels(tr(STR_QUIT_GAME), tr(STR_PLAY_AGAIN), "", "")
                : mappedInput.mapLabels(tr(STR_PAUSE), tr(STR_NUMBER), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer(HalDisplay::RefreshMode::FAST_REFRESH);
}
