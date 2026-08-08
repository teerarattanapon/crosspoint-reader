#pragma once

#include "../Activity.h"
#include "CatRunSaveStore.h"
#include "CatRunSprites.h"

/**
 * Cat Run — e-ink auto-runner (side view).
 * Cat runs automatically; Jump / Duck. Unlimited continues (-10 score each).
 */
class CatRunActivity final : public Activity {
 public:
  using Entity = CatRunSaveStore::Entity;
  using EntityKind = CatRunSaveStore::EntityKind;
  static constexpr int MAX_ENTITIES = CatRunSaveStore::MAX_ENTITIES;
  static constexpr int CONTINUE_SCORE_COST = 10;
  static constexpr uint32_t GAME_DURATION_MS = 210000u;  // 3:30
  static constexpr uint32_t GAME_DURATION_SEC = 210u;
  static constexpr uint32_t BOSS_PHASE_SEC = 90u;  // final 90s from 02:00 → 03:30
  static constexpr uint32_t TICK_MS = 450u;
  static constexpr uint32_t GAME_OVER_INPUT_DELAY_MS = 2000u;
  static constexpr int CAT_W = CatRunSprites::CAT_DRAW;
  static constexpr int CAT_H = CatRunSprites::CAT_DRAW;
  static constexpr uint8_t HAZARD_ACTION_NONE = 0;
  static constexpr uint8_t HAZARD_ACTION_JUMP = 1;
  static constexpr uint8_t HAZARD_ACTION_DUCK = 2;
  static constexpr int MIN_HAZARD_GAP = 200;

 private:
  enum class Phase : uint8_t { SelectColor, Playing, GameOverRetry, GameOverFinal };

  Phase phase = Phase::SelectColor;
  bool resumeFromSave = false;

  CatRunSprites::CatColor catColor = CatRunSprites::CatColor::White;
  int colorSelectIndex = 0;

  Entity entities[MAX_ENTITIES] = {};
  int32_t cameraX = 0;
  int32_t nextSpawnX = 0;
  uint32_t rngState = 1;

  int score = 0;
  uint8_t retriesUsed = 0;
  uint8_t jumpPhase = 0;  // 0 ground, 1..kJumpPhaseMax air
  uint8_t jumpLandGrace = 0;  // ticks after landing that still clear jump-hazards
  bool jumpJustStarted = false;  // true for the tick right after jump input; lets phase 1
                                  // get one collision check before tickWorld() advances it
  uint8_t walkFrame = 0;
  int32_t walkAnimAccumPx = 0;  // distance-based accumulator so gait speed tracks scrollSpeed()
  bool ducking = false;
  bool scoreSubmitted = false;
  bool finishedClean = false;  // time/flag vs death

  uint32_t gameStartMs = 0;
  uint32_t frozenElapsedMs = 0;
  uint32_t lastTickMs = 0;
  uint32_t lastDurationSec = 0;
  uint32_t lastBossAttackMs = 0;
  uint32_t gameOverShownMs = 0;
  bool gameOverNeedsHintRefresh = false;
  uint8_t bossPose = 0;  // 0 stand, 3 ROBOT_MINE_POSE (rocket warn / fire)
  uint8_t lastHazardAction = HAZARD_ACTION_NONE;
  int32_t lastHazardWorldX = 0;
  uint8_t spawnSlotIndex = 0;    // scripted food/hazard/gap cadence
  uint8_t hazardScriptIndex = 0;  // round-robin kind within current phase seq
  uint8_t spawnPhaseId = 0xFF;   // last phase id; reset hazardScriptIndex on change

  void resetRun();
  void loadFromSave();
  void saveProgress() const;
  void onRoundEnd(bool cleanFinish);
  void tickWorld();
  void spawnAhead();
  void maybeBossAttack();
  bool inBossPhase() const;
  void clearNearbyHazards();
  int freeEntitySlot() const;
  int scrollSpeed() const;
  int groundY() const;
  int catScreenX() const;
  int catWorldX() const;
  int catTopY() const;
  void catHitbox(int& outX, int& outY, int& outW, int& outH) const;
  bool aabbOverlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) const;
  bool entityIsHazard(EntityKind kind) const;
  bool entityIsCollectible(EntityKind kind) const;
  uint8_t hazardAction(EntityKind kind) const;
  int maxSafeHazardWidth(int scrollSpeedNow) const;
  void applyEntitySize(Entity& e) const;
  uint32_t nextRandom();
  void drawBossRobot();

  void renderSelectColor();
  void renderPlaying();
  void renderGameOver();

 public:
  explicit CatRunActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool resumeFromSaveIn = false)
      : Activity("CatRun", renderer, mappedInput), resumeFromSave(resumeFromSaveIn) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

  bool skipLoopDelay() override { return true; }
  bool preventAutoSleep() override { return false; }
  bool isGameActivity() const override { return phase == Phase::Playing; }
  GameKind getGameKind() const override { return GameKind::CatRun; }
};
