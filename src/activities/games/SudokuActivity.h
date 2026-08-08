#pragma once

#include "../Activity.h"
#include "util/ButtonNavigator.h"

/**
 * Sudoku — Medium difficulty number puzzle.
 * Tuned for e-ink: redraw only on input.
 * Supports pause (Back) and sleep resume via SudokuSaveStore.
 * Ranking: shortest completion time.
 */
class SudokuActivity final : public Activity {
 public:
  static constexpr int SIZE = 9;

 private:
  ButtonNavigator buttonNavigator{200, 300};

  uint8_t given[SIZE][SIZE] = {};
  uint8_t board[SIZE][SIZE] = {};
  int cursorRow = 0;
  int cursorCol = 0;
  bool completed = false;
  bool scoreSubmitted = false;
  bool resumeFromSave = false;
  uint32_t gameStartMs = 0;
  uint32_t frozenElapsedMs = 0;
  uint32_t lastDurationSec = 0;

  void resetGame();
  void loadFromSave();
  void saveProgress() const;
  void onComplete();
  void moveCursor(int dRow, int dCol);
  void cycleCell();
  bool isConflict(int row, int col, uint8_t digit) const;
  bool isBoardFull() const;
  void applyPuzzle(const char* puzzle, uint32_t seed);

 public:
  explicit SudokuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool resumeFromSaveIn = false)
      : Activity("Sudoku", renderer, mappedInput), resumeFromSave(resumeFromSaveIn) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

  bool skipLoopDelay() override { return true; }
  bool preventAutoSleep() override { return false; }
  bool isGameActivity() const override { return !completed; }
  GameKind getGameKind() const override { return GameKind::Sudoku; }
};
