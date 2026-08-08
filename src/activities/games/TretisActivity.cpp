#include "TretisActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>

#include <cstdio>
#include <cstring>

#include "../util/ConfirmationActivity.h"
#include "MappedInputManager.h"
#include "TretisSaveStore.h"
#include "TretisScoreStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

// 7 tetrominoes × 4 rotations × 4 cells as (row,col) within a 4×4 box.
// Layout: shapes[type][rotation][cell][0]=dy, [1]=dx
static constexpr int8_t kShapes[7][4][4][2] = {
    // I
    {{{1, 0}, {1, 1}, {1, 2}, {1, 3}},
     {{0, 2}, {1, 2}, {2, 2}, {3, 2}},
     {{2, 0}, {2, 1}, {2, 2}, {2, 3}},
     {{0, 1}, {1, 1}, {2, 1}, {3, 1}}},
    // O
    {{{0, 1}, {0, 2}, {1, 1}, {1, 2}},
     {{0, 1}, {0, 2}, {1, 1}, {1, 2}},
     {{0, 1}, {0, 2}, {1, 1}, {1, 2}},
     {{0, 1}, {0, 2}, {1, 1}, {1, 2}}},
    // T
    {{{0, 1}, {1, 0}, {1, 1}, {1, 2}},
     {{0, 1}, {1, 1}, {1, 2}, {2, 1}},
     {{1, 0}, {1, 1}, {1, 2}, {2, 1}},
     {{0, 1}, {1, 0}, {1, 1}, {2, 1}}},
    // S
    {{{0, 1}, {0, 2}, {1, 0}, {1, 1}},
     {{0, 1}, {1, 1}, {1, 2}, {2, 2}},
     {{1, 1}, {1, 2}, {2, 0}, {2, 1}},
     {{0, 0}, {1, 0}, {1, 1}, {2, 1}}},
    // Z
    {{{0, 0}, {0, 1}, {1, 1}, {1, 2}},
     {{0, 2}, {1, 1}, {1, 2}, {2, 1}},
     {{1, 0}, {1, 1}, {2, 1}, {2, 2}},
     {{0, 1}, {1, 0}, {1, 1}, {2, 0}}},
    // J
    {{{0, 0}, {1, 0}, {1, 1}, {1, 2}},
     {{0, 1}, {0, 2}, {1, 1}, {2, 1}},
     {{1, 0}, {1, 1}, {1, 2}, {2, 2}},
     {{0, 1}, {1, 1}, {2, 0}, {2, 1}}},
    // L
    {{{0, 2}, {1, 0}, {1, 1}, {1, 2}},
     {{0, 1}, {1, 1}, {2, 1}, {2, 2}},
     {{1, 0}, {1, 1}, {1, 2}, {2, 0}},
     {{0, 0}, {0, 1}, {1, 1}, {2, 1}}},
};

static constexpr int kLineScores[5] = {0, 100, 300, 500, 800};

uint32_t nextRandom(uint32_t& state) {
  // xorshift32 — no heap, no std::mt19937
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

}  // namespace

void TretisActivity::onEnter() {
  Activity::onEnter();
  if (resumeFromSave && TRETIS_SAVE.hasSave()) {
    loadFromSave();
  } else {
    resetGame();
  }
  requestUpdate();
}

void TretisActivity::onExit() {
  // Persist in-progress game so Back (pause) and deep sleep can resume later.
  if (!gameOver) {
    saveProgress();
  }
  Activity::onExit();
}

void TretisActivity::resetGame() {
  memset(board, 0, sizeof(board));
  score = 0;
  lines = 0;
  level = 1;
  gameOver = false;
  scoreSubmitted = false;
  lastDurationSec = 0;
  gameStartMs = millis();
  uint32_t seed = millis() | 1u;
  nextPieceType = static_cast<int>(nextRandom(seed) % 7);
  spawnPiece();
  lastFallMs = millis();
  // Fresh run replaces any paused snapshot.
  TRETIS_SAVE.clearFile();
}

void TretisActivity::loadFromSave() {
  static_assert(COLS == TretisSaveStore::COLS && ROWS == TretisSaveStore::ROWS, "board size mismatch");
  memcpy(board, TRETIS_SAVE.getBoard(), sizeof(board));
  pieceType = TRETIS_SAVE.getPieceType();
  nextPieceType = TRETIS_SAVE.getNextPieceType();
  rotation = TRETIS_SAVE.getRotation();
  pieceX = TRETIS_SAVE.getPieceX();
  pieceY = TRETIS_SAVE.getPieceY();
  score = TRETIS_SAVE.getScore();
  lines = TRETIS_SAVE.getLines();
  level = TRETIS_SAVE.getLevel();
  gameOver = false;
  scoreSubmitted = false;
  lastDurationSec = 0;
  // Continue wall-clock duration from elapsed time at pause.
  const uint32_t elapsed = TRETIS_SAVE.getElapsedMs();
  const uint32_t now = millis();
  gameStartMs = (now > elapsed) ? (now - elapsed) : now;
  lastFallMs = now;
}

void TretisActivity::saveProgress() const {
  const uint32_t now = millis();
  const uint32_t elapsed = (now >= gameStartMs) ? (now - gameStartMs) : 0;
  TRETIS_SAVE.set(board, pieceType, nextPieceType, rotation, pieceX, pieceY, score, lines, level, elapsed);
  TRETIS_SAVE.saveToFile();
}

void TretisActivity::onGameOver() {
  if (scoreSubmitted) {
    return;
  }
  scoreSubmitted = true;
  const uint32_t elapsedMs = millis() - gameStartMs;
  lastDurationSec = elapsedMs / 1000u;
  // Round ended — discard paused snapshot.
  TRETIS_SAVE.clearFile();
  if (TRETIS_SCORES.submit(static_cast<uint32_t>(score), lastDurationSec)) {
    TRETIS_SCORES.saveToFile();
  }
}

void TretisActivity::spawnPiece() {
  pieceType = nextPieceType;
  uint32_t seed = millis() + static_cast<uint32_t>(score + 1);
  nextPieceType = static_cast<int>(nextRandom(seed) % 7);
  rotation = 0;
  pieceX = COLS / 2 - 2;
  pieceY = 0;
  if (!fits(pieceType, rotation, pieceX, pieceY)) {
    gameOver = true;
    onGameOver();
  }
}

bool TretisActivity::fits(int type, int rot, int x, int y) const {
  for (int i = 0; i < 4; ++i) {
    const int r = y + kShapes[type][rot][i][0];
    const int c = x + kShapes[type][rot][i][1];
    if (c < 0 || c >= COLS || r >= ROWS) {
      return false;
    }
    if (r >= 0 && board[r][c] != 0) {
      return false;
    }
  }
  return true;
}

void TretisActivity::lockPiece() {
  for (int i = 0; i < 4; ++i) {
    const int r = pieceY + kShapes[pieceType][rotation][i][0];
    const int c = pieceX + kShapes[pieceType][rotation][i][1];
    if (r >= 0 && r < ROWS && c >= 0 && c < COLS) {
      board[r][c] = static_cast<uint8_t>(pieceType + 1);
    }
  }
  const int cleared = clearLines();
  if (cleared > 0) {
    score += kLineScores[cleared] * level;
    lines += cleared;
    level = 1 + lines / 10;
  }
  spawnPiece();
}

int TretisActivity::clearLines() {
  int cleared = 0;
  for (int r = ROWS - 1; r >= 0; --r) {
    bool full = true;
    for (int c = 0; c < COLS; ++c) {
      if (board[r][c] == 0) {
        full = false;
        break;
      }
    }
    if (full) {
      ++cleared;
      for (int row = r; row > 0; --row) {
        memcpy(board[row], board[row - 1], COLS);
      }
      memset(board[0], 0, COLS);
      ++r;  // re-check same row after shift
    }
  }
  return cleared;
}

void TretisActivity::tryMove(int dx, int dy) {
  if (gameOver) {
    return;
  }
  if (fits(pieceType, rotation, pieceX + dx, pieceY + dy)) {
    pieceX += dx;
    pieceY += dy;
    requestUpdate();
  }
}

void TretisActivity::rotateCw() {
  if (gameOver) {
    return;
  }
  const int nextRot = (rotation + 1) % 4;
  // Simple wall kicks: try 0, -1, +1, -2, +2
  static constexpr int kKicks[] = {0, -1, 1, -2, 2};
  for (int kick : kKicks) {
    if (fits(pieceType, nextRot, pieceX + kick, pieceY)) {
      rotation = nextRot;
      pieceX += kick;
      requestUpdate();
      return;
    }
  }
}

void TretisActivity::softDrop() {
  if (gameOver) {
    return;
  }
  if (fits(pieceType, rotation, pieceX, pieceY + 1)) {
    ++pieceY;
    score += 1;
    lastFallMs = millis();
    requestUpdate();
  } else {
    lockPiece();
    requestUpdate();
  }
}

void TretisActivity::hardDrop() {
  if (gameOver) {
    return;
  }
  int dropped = 0;
  while (fits(pieceType, rotation, pieceX, pieceY + 1)) {
    ++pieceY;
    ++dropped;
  }
  score += dropped * 2;
  lockPiece();
  requestUpdate();
}

uint32_t TretisActivity::fallIntervalMs() const {
  // E-ink friendly: start ~800 ms, floor at 250 ms
  const int interval = 800 - (level - 1) * 50;
  return static_cast<uint32_t>(interval > 250 ? interval : 250);
}

void TretisActivity::tickGravity() {
  if (gameOver) {
    return;
  }
  const uint32_t now = millis();
  if (now - lastFallMs < fallIntervalMs()) {
    return;
  }
  lastFallMs = now;
  if (fits(pieceType, rotation, pieceX, pieceY + 1)) {
    ++pieceY;
  } else {
    lockPiece();
  }
  requestUpdate();
}

void TretisActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (gameOver) {
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

  if (gameOver) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      resetGame();
      requestUpdate();
    }
    return;
  }

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this] { tryMove(-1, 0); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this] { tryMove(1, 0); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down}, [this] { softDrop(); });

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    rotateCw();
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    hardDrop();
  }

  tickGravity();
}

void TretisActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_TRETIS));

  // Layout: board on left/center, stats on right
  int top = 0;
  int right = 0;
  int bottom = 0;
  int left = 0;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int hintsH = metrics.buttonHintsHeight;
  const int availH = pageHeight - contentTop - hintsH - metrics.verticalSpacing - bottom;
  const int sidePanelW = 110;
  const int boardAreaW = pageWidth - left - right - sidePanelW - metrics.verticalSpacing * 2;
  const int cell = (availH / ROWS < boardAreaW / COLS) ? (availH / ROWS) : (boardAreaW / COLS);
  const int boardW = cell * COLS;
  const int boardH = cell * ROWS;
  const int boardX = left + metrics.verticalSpacing;
  const int boardY = contentTop + (availH - boardH) / 2;

  // Board frame
  renderer.drawRect(boardX - 1, boardY - 1, boardW + 2, boardH + 2, true);

  // Settled cells
  for (int r = 0; r < ROWS; ++r) {
    for (int c = 0; c < COLS; ++c) {
      if (board[r][c] != 0) {
        const int x = boardX + c * cell;
        const int y = boardY + r * cell;
        renderer.fillRect(x + 1, y + 1, cell - 2, cell - 2, true);
      }
    }
  }

  // Active piece
  if (!gameOver) {
    for (int i = 0; i < 4; ++i) {
      const int r = pieceY + kShapes[pieceType][rotation][i][0];
      const int c = pieceX + kShapes[pieceType][rotation][i][1];
      if (r >= 0 && r < ROWS && c >= 0 && c < COLS) {
        const int x = boardX + c * cell;
        const int y = boardY + r * cell;
        renderer.fillRect(x + 1, y + 1, cell - 2, cell - 2, true);
      }
    }
  }

  // Side panel: score / lines / level / next
  const int panelX = boardX + boardW + metrics.verticalSpacing;
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  int textY = boardY;

  char buf[64];
  char durationBuf[24];
  renderer.drawText(UI_10_FONT_ID, panelX, textY, tr(STR_SCORE), true, EpdFontFamily::BOLD);
  textY += lineH;
  snprintf(buf, sizeof(buf), "%d", score);
  renderer.drawText(UI_10_FONT_ID, panelX, textY, buf, true);
  textY += lineH + 6;

  renderer.drawText(UI_10_FONT_ID, panelX, textY, tr(STR_LINES), true, EpdFontFamily::BOLD);
  textY += lineH;
  snprintf(buf, sizeof(buf), "%d", lines);
  renderer.drawText(UI_10_FONT_ID, panelX, textY, buf, true);
  textY += lineH + 6;

  renderer.drawText(UI_10_FONT_ID, panelX, textY, tr(STR_LEVEL), true, EpdFontFamily::BOLD);
  textY += lineH;
  snprintf(buf, sizeof(buf), "%d", level);
  renderer.drawText(UI_10_FONT_ID, panelX, textY, buf, true);
  textY += lineH + 10;

  // Next piece preview (4×4 mini grid)
  const int previewCell = 8;
  const int previewX = panelX;
  const int previewY = textY;
  renderer.drawRect(previewX, previewY, previewCell * 4 + 2, previewCell * 4 + 2, true);
  for (int i = 0; i < 4; ++i) {
    const int pr = kShapes[nextPieceType][0][i][0];
    const int pc = kShapes[nextPieceType][0][i][1];
    renderer.fillRect(previewX + 1 + pc * previewCell, previewY + 1 + pr * previewCell, previewCell - 1,
                      previewCell - 1, true);
  }

  if (gameOver) {
    const int scoreCount = TRETIS_SCORES.getCount();
    const int titleH = renderer.getLineHeight(UI_12_FONT_ID);
    const int rowH = lineH;
    // title + score + duration + optional high-scores header + up to 3 rows
    const int extraRows = 2 + (scoreCount > 0 ? 1 + scoreCount : 0);
    const int boxW = 280;
    const int boxH = 16 + titleH + 4 + extraRows * rowH + 12;
    const int boxX = (pageWidth - boxW) / 2;
    const int boxY = (pageHeight - boxH) / 2;
    renderer.fillRect(boxX - 2, boxY - 2, boxW + 4, boxH + 4, true);
    renderer.fillRect(boxX, boxY, boxW, boxH, false);

    int y = boxY + 10;
    renderer.drawCenteredText(UI_12_FONT_ID, y, tr(STR_GAME_OVER), true, EpdFontFamily::BOLD);
    y += titleH + 4;

    snprintf(buf, sizeof(buf), "%s: %d", tr(STR_SCORE), score);
    renderer.drawCenteredText(UI_10_FONT_ID, y, buf, true);
    y += rowH;

    TretisScoreStore::formatDuration(lastDurationSec, durationBuf, sizeof(durationBuf));
    snprintf(buf, sizeof(buf), "%s: %s", tr(STR_DURATION), durationBuf);
    renderer.drawCenteredText(UI_10_FONT_ID, y, buf, true);
    y += rowH;

    if (scoreCount > 0) {
      renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_HIGH_SCORES), true, EpdFontFamily::BOLD);
      y += rowH;
      for (int i = 0; i < scoreCount; ++i) {
        const TretisScoreEntry& e = TRETIS_SCORES.getEntry(i);
        TretisScoreStore::formatDuration(e.durationSec, durationBuf, sizeof(durationBuf));
        snprintf(buf, sizeof(buf), "%d. %lu  %s", i + 1, static_cast<unsigned long>(e.score), durationBuf);
        renderer.drawCenteredText(UI_10_FONT_ID, y, buf, true);
        y += rowH;
      }
    }
  }

  const auto labels =
      gameOver ? mappedInput.mapLabels(tr(STR_QUIT_GAME), tr(STR_PLAY_AGAIN), "", "")
               : mappedInput.mapLabels(tr(STR_PAUSE), tr(STR_ROTATE), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer(HalDisplay::RefreshMode::FAST_REFRESH);
}
