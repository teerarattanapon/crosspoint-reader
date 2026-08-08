#pragma once

#include "../Activity.h"
#include "util/ButtonNavigator.h"

/**
 * TRETIS — falling-block puzzle (Tetris anagram).
 * Tuned for e-ink: sparse redraws, ~700 ms gravity base.
 * Supports pause (Back) and sleep resume via TretisSaveStore.
 */
class TretisActivity final : public Activity {
 public:
  static constexpr int COLS = 10;
  static constexpr int ROWS = 16;

 private:
  ButtonNavigator buttonNavigator{200, 300};

  uint8_t board[ROWS][COLS] = {};
  int pieceType = 0;
  int nextPieceType = 0;
  int rotation = 0;
  int pieceX = 0;
  int pieceY = 0;
  int score = 0;
  int lines = 0;
  int level = 1;
  bool gameOver = false;
  bool scoreSubmitted = false;
  bool resumeFromSave = false;
  uint32_t lastFallMs = 0;
  uint32_t gameStartMs = 0;
  uint32_t frozenElapsedMs = 0;
  uint32_t lastDurationSec = 0;

  void resetGame();
  void loadFromSave();
  void saveProgress() const;
  void spawnPiece();
  void onGameOver();
  bool fits(int type, int rot, int x, int y) const;
  void lockPiece();
  int clearLines();
  void hardDrop();
  void softDrop();
  void rotateCw();
  void tryMove(int dx, int dy);
  uint32_t fallIntervalMs() const;
  void tickGravity();

 public:
  explicit TretisActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool resumeFromSaveIn = false)
      : Activity("Tretis", renderer, mappedInput), resumeFromSave(resumeFromSaveIn) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

  bool skipLoopDelay() override { return true; }
  // Allow auto-sleep / power-button sleep so the player can resume later.
  bool preventAutoSleep() override { return false; }
  bool isGameActivity() const override { return !gameOver; }
  GameKind getGameKind() const override { return GameKind::Tretis; }
};
