#include "CatRunActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>

#include "../util/ConfirmationActivity.h"
#include "CatRunSaveStore.h"
#include "CatRunScoreStore.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

// Snappy jump: short air time; land-grace finishes clearing ground hazards.
// Reshaped from the old 8-phase {0,-96,-124,-124,-96,-64,-32,-12}: that curve
// spent phases 6/7 (-32/-12px) airborne but below kJumpClearMinHeight, a real
// 2-tick (900ms) dead zone where the cat visibly hadn't landed yet but hazard
// clearance had already been revoked — "jump looks like it cleared, dies
// anyway." This 6-phase curve keeps every airborne tick at or above the height
// gate below, so there's no tick that's airborne-but-uncounted-as-clear.
//
// Rapid-prototyping A/B/C for on-device game-feel comparison — change
// CATRUN_JUMP_PROFILE below and rebuild to switch. All three verified via
// simulation to produce IDENTICAL collision outcomes to each other (every
// tick still clears kJumpClearMinHeight below) — only the visual trajectory
// differs, not fairness. Collision, hitboxes, and world speed untouched.
//   0 = A: Ballistic  — decelerating rise, ACCELERATING fall (real-gravity shape)
//   1 = B: Linear     — constant ~24px/tick rate, both rise and fall
//   2 = C: Preferred  — lower peak (90 vs 124; still every tick >=48, so zero
//                       fairness change), immediate apex, even fall. Peak
//                       height was never load-bearing for fairness — only the
//                       48px floor is — so this is "spend the same tick count
//                       on a shorter, tighter arc" rather than a taller one.
#define CATRUN_JUMP_PROFILE 2

#if CATRUN_JUMP_PROFILE == 0
constexpr int kJumpOffsets[6] = {0, -88, -124, -108, -84, -52};    // A: Ballistic
#elif CATRUN_JUMP_PROFILE == 1
constexpr int kJumpOffsets[6] = {0, -100, -124, -100, -76, -52};   // B: Linear
#elif CATRUN_JUMP_PROFILE == 2
constexpr int kJumpOffsets[6] = {0, -90, -90, -76, -62, -48};      // C: Preferred
#else
#error "CATRUN_JUMP_PROFILE must be 0 (Ballistic), 1 (Linear), or 2 (Preferred)"
#endif
constexpr int kJumpPhaseMax = 5;  // phases 1..5 in air; 0 = ground
// Was 8 (8 * TICK_MS=450ms = 3.6s of post-landing hazard immunity — way too
// generous, root cause of "fell on a hazard and didn't die"). Short coyote-time
// buffer for tick-quantization only, not a free-pass window.
constexpr uint8_t kJumpLandGraceTicks = 3;
// Ground-hazard height single source of truth: applyEntitySize() assigns this
// same constant to every jump-type ground hazard (Grass/Crate/Brick/QBlock/
// Puppy/Dog/RunningDog/Snake/Mine), so kJumpClearMinHeight below can never
// silently drift out of sync with it the way the old hardcoded 64 did once
// hazards got unified to a shorter height in an earlier pass.
constexpr int kGroundHazardHeight = 40;
// Real margin above the geometric zero-overlap point (not "barely off the
// ground") — still demands genuine timing, just not 2x more than necessary.
constexpr int kJumpClearMargin = 8;
// Minimum |kJumpOffsets[phase]| (px) required to count as "high enough" to
// clear a jump-hazard.
constexpr int kJumpClearMinHeight = kGroundHazardHeight + kJumpClearMargin;

// Ticks (out of kJumpOffsets[1..kJumpPhaseMax]) that count as "high enough" per
// kJumpClearMinHeight — self-derived so it can't drift out of sync with the
// tuned curve above. Used by CatRunActivity::maxSafeHazardWidth() to keep
// spawn-time hazard width honest about what a single jump can guarantee-clear.
constexpr int countSafeJumpTicks() {
  int count = 0;
  for (int phase = 1; phase <= kJumpPhaseMax; ++phase) {
    if (-kJumpOffsets[phase] >= kJumpClearMinHeight) {
      ++count;
    }
  }
  return count;
}

constexpr int kFoodScore = 10;  // game.pdf: อาหารแมว 10
constexpr int kFishScore = 20;  // game.pdf: ปลา 20
// World hitboxes match CatRunSprites::SPRITE_DRAW (32×2) unless noted.
constexpr int kSprite = CatRunSprites::SPRITE_DRAW;

// Boss fire pose held briefly after a rocket is script-spawned.
constexpr uint32_t kBossFireHoldMs = 700u;
// Min gap between a jump hazard (bush/crate) and a boss rocket in either order.
constexpr int kBossRocketJumpClear = 280;
constexpr int kRocketSpawnInset = 120;

static bool hazardClearedByJump(CatRunActivity::EntityKind kind) {
  return kind != CatRunActivity::EntityKind::Crow && kind != CatRunActivity::EntityKind::Rocket &&
         kind != CatRunActivity::EntityKind::BullyCat && kind != CatRunActivity::EntityKind::CoolCat;
}

// #region agent log
// Bypass LOG_INF 256-byte cap (hazard_death JSON was truncated → bridge missed deaths).
#if LOG_LEVEL >= 2
static void agentJumpLog(const char* hypothesisId, const char* message, const char* dataJson) {
  char line[384];
  snprintf(line, sizeof(line),
           "JMPDBG {\"sessionId\":\"64bf1c\",\"hypothesisId\":\"%s\",\"location\":\"CatRunActivity\","
           "\"message\":\"%s\",\"data\":%s,\"timestamp\":%lu}\n",
           hypothesisId, message, dataJson, static_cast<unsigned long>(millis()));
  logSerial.print(line);
}
#endif  // LOG_LEVEL >= 2
// #endregion

}  // namespace

void CatRunActivity::onEnter() {
  Activity::onEnter();
  if (resumeFromSave && CATRUN_SAVE.hasSave()) {
    loadFromSave();
    phase = Phase::Playing;
  } else {
    phase = Phase::SelectColor;
    colorSelectIndex = 0;
    catColor = CatRunSprites::CatColor::White;
  }
  requestUpdate();
}

void CatRunActivity::onExit() {
  if (phase == Phase::Playing) {
    saveProgress();
  }
  // Dense FAST_REFRESH gameplay leaves e-ink residue; wipe before Games menu
  // FAST redraw (same pattern as SleepActivity for lastSleepFromGame).
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  Activity::onExit();
}

uint32_t CatRunActivity::nextRandom() {
  rngState ^= rngState << 13;
  rngState ^= rngState >> 17;
  rngState ^= rngState << 5;
  return rngState;
}

int CatRunActivity::scrollSpeed() const {
  // Gentler pace for e-ink (~1–3 FPS): max ~11 px/tick.
  // Continuous ramp over the run instead of a stepped jump every 18s — same
  // range as before, but speed no longer jolts at segment boundaries.
  // Base 6->7 (game-feel option A): closes part of the jump/obstacle
  // clearance-margin gap measured via tick-accurate simulation (24px exposed
  // -> 14px at the common 40px-wide ground hazard) without touching the jump
  // curve, hitboxes, or collision logic. Isolated change — see conversation
  // history for the fuller investigation before re-tuning further.
  constexpr int kBaseSpeed = 7;
  constexpr int kMaxSpeed = 11;
  const uint32_t now = millis();
  const uint32_t elapsed = (now >= gameStartMs) ? (now - gameStartMs) : 0;
  uint32_t sec = elapsed / 1000u;
  if (sec > GAME_DURATION_SEC) {
    sec = GAME_DURATION_SEC;
  }
  return kBaseSpeed + static_cast<int>((static_cast<uint32_t>(kMaxSpeed - kBaseSpeed) * sec) / GAME_DURATION_SEC);
}

int CatRunActivity::groundY() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Raised ~48px so ground hazards sit on a visible floor band and duck stays on-screen.
  return renderer.getScreenHeight() - metrics.buttonHintsHeight - 64;
}

int CatRunActivity::catScreenX() const {
  return 56;
}

int CatRunActivity::catWorldX() const {
  return cameraX + catScreenX();
}

int CatRunActivity::catTopY() const {
  const int base = groundY() - CAT_H;
  const int phaseIdx = jumpPhase <= kJumpPhaseMax ? jumpPhase : 0;
  return base + kJumpOffsets[phaseIdx] + (ducking && jumpPhase == 0 ? 8 : 0);
}

void CatRunActivity::catHitbox(int& outX, int& outY, int& outW, int& outH) const {
  // Narrower/shorter body than the 64px art so jump timing stays fair on slow
  // e-ink ticks (~13% shorter than art height — standard endless-runner
  // "hitbox smaller than sprite" forgiveness, matches every hazard already
  // getting a tighter hitbox than its own art).
  outX = catScreenX() + 16;
  outY = catTopY() + (ducking && jumpPhase == 0 ? 12 : 8);
  outW = CAT_W - 32;
  outH = ducking && jumpPhase == 0 ? 28 : CAT_H - 22;
}

bool CatRunActivity::aabbOverlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) const {
  return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

bool CatRunActivity::entityIsHazard(EntityKind kind) const {
  switch (kind) {
    case EntityKind::Hole:
    case EntityKind::Water:
    case EntityKind::Grass:
    // Tree is scenery — not a hazard
    case EntityKind::Fence:
    case EntityKind::Crate:
    case EntityKind::Brick:
    case EntityKind::QuestionBlock:
    case EntityKind::Puppy:
    case EntityKind::Dog:
    case EntityKind::RunningDog:
    case EntityKind::CoolCat:
    case EntityKind::BullyCat:
    case EntityKind::Puddle:
    case EntityKind::Frog:
    case EntityKind::Snake:
    case EntityKind::Crow:
    case EntityKind::Rocket:
    case EntityKind::Mine:
      return true;
    default:
      return false;
  }
}

bool CatRunActivity::entityIsCollectible(EntityKind kind) const {
  return kind == EntityKind::CatFood || kind == EntityKind::Fish;
}

uint8_t CatRunActivity::hazardAction(EntityKind kind) const {
  if (!entityIsHazard(kind)) {
    return HAZARD_ACTION_NONE;
  }
  if (kind == EntityKind::Crow || kind == EntityKind::Rocket) {
    return HAZARD_ACTION_DUCK;
  }
  return HAZARD_ACTION_JUMP;
}

int CatRunActivity::maxSafeHazardWidth(int scrollSpeedNow) const {
  // Endless-Runner spawn safety: a jump-type ground hazard must never be wider
  // than what the existing, untouched jump arc can guarantee-clear at the given
  // speed (countSafeJumpTicks() ticks of "high enough" elevation, each worth
  // scrollSpeedNow world-px, minus the cat's own hitbox width). Floored at the
  // 40px baseline every ground hazard was already tuned to (applyEntitySize())
  // so this only ever restricts something WIDER than that proven-fair size,
  // never second-guesses it.
  constexpr int kCatHitboxWidth = CAT_W - 32;  // matches catHitbox()
  const int dynamic = countSafeJumpTicks() * scrollSpeedNow - kCatHitboxWidth;
  return dynamic > 40 ? dynamic : 40;
}

void CatRunActivity::applyEntitySize(Entity& e) const {
  switch (e.kind) {
    case EntityKind::Hole:
    case EntityKind::Water:
      // Narrower pits so a normal jump clears them reliably.
      e.w = static_cast<uint8_t>(kSprite + 16);
      e.h = static_cast<uint8_t>(kSprite / 2);
      e.solid = false;
      break;
    case EntityKind::Grass:
    case EntityKind::Brick:
    case EntityKind::QuestionBlock:
      // game-v2: bush shares the same jump band as dog.
      e.w = 40;
      e.h = kGroundHazardHeight;
      e.solid = false;
      break;
    case EntityKind::Crate:
      // Half of prior 40×40 jump-band footprint.
      e.w = 20;
      e.h = 20;
      e.solid = false;
      break;
    case EntityKind::Fence:
      e.w = static_cast<uint8_t>(kSprite + 16);
      e.h = static_cast<uint8_t>(kSprite);
      e.solid = false;
      break;
    case EntityKind::Tree:
      // Scenery footprint matches BG_TREE_A (40×56).
      e.w = 40;
      e.h = 56;
      e.solid = false;
      break;
    case EntityKind::Platform:
      e.w = static_cast<uint8_t>(kSprite * 2);
      e.h = static_cast<uint8_t>(kSprite / 2);
      e.solid = true;
      break;
    case EntityKind::Puppy:
    case EntityKind::Dog:
    case EntityKind::RunningDog:
    case EntityKind::Snake:
    case EntityKind::Mine:
      // Unified jump-hazard band (game-v2 size balance).
      e.w = 40;
      e.h = kGroundHazardHeight;
      e.solid = false;
      break;
    case EntityKind::Frog:
      // Width capped at maxSafeHazardWidth (40); taller than ground band for frog art.
      e.w = 40;
      e.h = 48;
      e.solid = false;
      break;
    case EntityKind::CoolCat:
    case EntityKind::BullyCat:
      e.w = static_cast<uint8_t>(kSprite);
      e.h = static_cast<uint8_t>(kSprite);
      e.solid = false;
      break;
    case EntityKind::Puddle:
      // Tight band matching 40×20 packed art (was 24 — felt bigger than the oval).
      e.w = 40;
      e.h = 16;
      e.solid = false;
      break;
    case EntityKind::Crow:
      // Aerial — duck under; 20% larger than prior 56×40.
      e.w = 67;
      e.h = 48;
      e.solid = false;
      break;
    case EntityKind::Rocket:
      // High duck threat; 75% of prior 36×30 hitbox to match scaled art (45px).
      e.w = 27;
      e.h = 22;
      e.solid = false;
      break;
    case EntityKind::CatFood:
      // Half of prior 48×48 collectible footprint.
      e.w = 24;
      e.h = 24;
      e.solid = false;
      break;
    case EntityKind::Fish:
      // 75% of prior 48×48 collectible footprint (matches −25% art).
      e.w = 36;
      e.h = 36;
      e.solid = false;
      break;
    case EntityKind::Flag:
      e.w = static_cast<uint8_t>(kSprite);
      e.h = static_cast<uint8_t>(kSprite + 24);
      e.solid = false;
      break;
    default:
      e.w = static_cast<uint8_t>(kSprite);
      e.h = static_cast<uint8_t>(kSprite);
      e.solid = false;
      break;
  }
}

int CatRunActivity::freeEntitySlot() const {
  for (int i = 0; i < MAX_ENTITIES; ++i) {
    if (!entities[i].active) {
      return i;
    }
  }
  return -1;
}

void CatRunActivity::resetRun() {
  memset(entities, 0, sizeof(entities));
  cameraX = 0;
  nextSpawnX = 220;
  rngState = millis() | 1u;
  score = 0;
  retriesUsed = 0;
  jumpPhase = 0;
  jumpLandGrace = 0;
  jumpJustStarted = false;
  walkFrame = 0;
  walkAnimAccumPx = 0;
  ducking = false;
  scoreSubmitted = false;
  finishedClean = false;
  lastDurationSec = 0;
  lastBossAttackMs = 0;
  bossPose = 0;
  lastHazardAction = HAZARD_ACTION_NONE;
  lastHazardWorldX = 0;
  spawnSlotIndex = 0;
  hazardScriptIndex = 0;
  spawnPhaseId = 0xFF;
  gameStartMs = millis();
  lastTickMs = gameStartMs;
  catColor = static_cast<CatRunSprites::CatColor>(colorSelectIndex);
  CATRUN_SAVE.clearFile();
  spawnAhead();
}

void CatRunActivity::loadFromSave() {
  catColor = static_cast<CatRunSprites::CatColor>(CATRUN_SAVE.getCatColor());
  colorSelectIndex = static_cast<int>(CATRUN_SAVE.getCatColor());
  retriesUsed = CATRUN_SAVE.getRetriesUsed();
  jumpPhase = CATRUN_SAVE.getJumpPhase();
  jumpJustStarted = false;  // resuming mid-jump: treat as already in progress, not fresh
  walkFrame = CATRUN_SAVE.getWalkFrame();
  walkAnimAccumPx = 0;  // sub-frame timing only; resetting on resume is imperceptible
  ducking = CATRUN_SAVE.getDucking();
  score = CATRUN_SAVE.getScore();
  cameraX = CATRUN_SAVE.getCameraX();
  nextSpawnX = CATRUN_SAVE.getNextSpawnX();
  rngState = CATRUN_SAVE.getRngState() | 1u;
  memcpy(entities, CATRUN_SAVE.getEntities(), sizeof(entities));
  scoreSubmitted = false;
  finishedClean = false;
  lastDurationSec = 0;
  lastHazardAction = HAZARD_ACTION_NONE;
  lastHazardWorldX = 0;
  spawnSlotIndex = 0;
  hazardScriptIndex = 0;
  spawnPhaseId = 0xFF;
  const uint32_t elapsed = CATRUN_SAVE.getElapsedMs();
  const uint32_t now = millis();
  gameStartMs = (now > elapsed) ? (now - elapsed) : now;
  lastTickMs = now;
  // Rebuild spacing state from farthest active hazard still ahead of camera.
  for (int i = 0; i < MAX_ENTITIES; ++i) {
    if (!entities[i].active || !entityIsHazard(entities[i].kind)) {
      continue;
    }
    if (entities[i].worldX >= lastHazardWorldX) {
      lastHazardWorldX = entities[i].worldX;
      lastHazardAction = hazardAction(entities[i].kind);
    }
  }
}

void CatRunActivity::saveProgress() const {
  const uint32_t now = millis();
  const uint32_t elapsed = (now >= gameStartMs) ? (now - gameStartMs) : 0;
  CATRUN_SAVE.set(static_cast<uint8_t>(catColor), retriesUsed, jumpPhase, walkFrame, ducking, score, elapsed, cameraX,
                   nextSpawnX, rngState, entities);
  CATRUN_SAVE.saveToFile();
}

void CatRunActivity::onRoundEnd(bool cleanFinish) {
  if (scoreSubmitted) {
    return;
  }
  scoreSubmitted = true;
  finishedClean = cleanFinish;
  const uint32_t elapsedMs = millis() - gameStartMs;
  lastDurationSec = elapsedMs / 1000u;
  if (lastDurationSec > GAME_DURATION_SEC) {
    lastDurationSec = GAME_DURATION_SEC;
  }
  CATRUN_SAVE.clearFile();
  if (CATRUN_SCORES.submit(static_cast<uint32_t>(score), lastDurationSec, retriesUsed)) {
    CATRUN_SCORES.saveToFile();
  }
}

void CatRunActivity::clearNearbyHazards() {
  const int cx = catWorldX();
  for (int i = 0; i < MAX_ENTITIES; ++i) {
    if (!entities[i].active) {
      continue;
    }
    if (entityIsHazard(entities[i].kind) && entities[i].worldX < cx + 80 && entities[i].worldX + entities[i].w > cx - 20) {
      entities[i].active = false;
    }
  }
  jumpPhase = 0;
  jumpLandGrace = 0;
  jumpJustStarted = false;
  ducking = false;
}

void CatRunActivity::spawnAhead() {
  const int screenW = renderer.getScreenWidth();
  const int ground = groundY();
  const uint32_t now = millis();
  const uint32_t sec = (now >= gameStartMs) ? ((now - gameStartMs) / 1000u) : 0;
  const bool bossPhase = (sec + BOSS_PHASE_SEC >= GAME_DURATION_SEC);

  // Scripted cadence (food / hazard / gap) for non-boss windows.
  enum class SpawnSlot : uint8_t { Hazard = 0, CatFood, Gap, Fish };
  static constexpr SpawnSlot kSlotPattern[] = {
      SpawnSlot::Hazard, SpawnSlot::Hazard, SpawnSlot::CatFood, SpawnSlot::Hazard,
      SpawnSlot::Hazard, SpawnSlot::Fish,   SpawnSlot::Hazard,  SpawnSlot::Gap,
  };
  static constexpr int kSlotPatternLen = 8;

  // Round-robin hazard kinds per time window (flash-resident; no heap).
  // Windows match game-v5.pdf: intro / dog 21–45 / snake 46–65 / frog 66–90 /
  // crow 91–119 / boss from 02:00 (rocket+grass script).
  static constexpr EntityKind kIntroSeq[] = {EntityKind::Grass, EntityKind::Crate};
  static constexpr EntityKind kDogSeq[] = {EntityKind::Dog, EntityKind::Dog, EntityKind::Grass,
                                           EntityKind::Dog};
  static constexpr EntityKind kSnakeSeq[] = {EntityKind::Snake, EntityKind::Snake, EntityKind::Snake,
                                             EntityKind::Crate};
  static constexpr EntityKind kFrogSeq[] = {EntityKind::Frog, EntityKind::Frog, EntityKind::Frog,
                                            EntityKind::Grass};
  static constexpr EntityKind kCrowSeq[] = {EntityKind::Crow, EntityKind::Crow, EntityKind::Crow,
                                            EntityKind::Grass};
  // Boss: จรวด จรวด กอหญ้า จรวด กอหญ้า กอหญ้า จรวด จรวด จรวด กอหญ้า จรวด
  static constexpr EntityKind kBossEncounter[] = {
      EntityKind::Rocket, EntityKind::Rocket, EntityKind::Grass, EntityKind::Rocket,
      EntityKind::Grass,  EntityKind::Grass,  EntityKind::Rocket, EntityKind::Rocket,
      EntityKind::Rocket, EntityKind::Grass,  EntityKind::Rocket};
  static constexpr int kBossEncounterLen = 11;

  constexpr int kBossGroundGap = 220;
  constexpr int kBossBaseGap = 200;

  while (nextSpawnX < cameraX + screenW + 120) {
    const int slot = freeEntitySlot();
    if (slot < 0) {
      break;
    }

    Entity& e = entities[slot];
    e = {};
    e.active = true;
    e.worldX = nextSpawnX;

    if (bossPhase) {
      if (spawnPhaseId != 6) {
        spawnPhaseId = 6;
        hazardScriptIndex = 0;
      }
      e.kind = kBossEncounter[hazardScriptIndex % static_cast<uint8_t>(kBossEncounterLen)];
      hazardScriptIndex = static_cast<uint8_t>(hazardScriptIndex + 1u);
      const uint8_t action = hazardAction(e.kind);

      if (lastHazardAction != HAZARD_ACTION_NONE) {
        int32_t minGap = kBossBaseGap;
        if (lastHazardAction != action) {
          minGap = kBossRocketJumpClear;
        } else if (action == HAZARD_ACTION_JUMP) {
          minGap = kBossGroundGap;
        }
        const int32_t minX = lastHazardWorldX + minGap;
        if (e.worldX < minX) {
          e.worldX = minX;
          nextSpawnX = minX;
        }
      }

      applyEntitySize(e);
      if (e.kind == EntityKind::Rocket) {
        e.worldY = static_cast<int16_t>(ground - e.h - 48);  // high — duck
        bossPose = 3;  // ROBOT_MINE_POSE while releasing rocket
        lastBossAttackMs = now;
      } else {
        e.worldY = static_cast<int16_t>(ground - e.h);
      }

      lastHazardAction = action;
      lastHazardWorldX = e.worldX;
      const int gap = (action == HAZARD_ACTION_JUMP) ? kBossGroundGap : kBossBaseGap;
      nextSpawnX = e.worldX + gap;
      continue;
    }

    const SpawnSlot spawnSlot = kSlotPattern[spawnSlotIndex % kSlotPatternLen];
    spawnSlotIndex = static_cast<uint8_t>(spawnSlotIndex + 1u);

    if (spawnSlot == SpawnSlot::CatFood) {
      e.kind = EntityKind::CatFood;
      applyEntitySize(e);
      e.worldY = static_cast<int16_t>(ground - e.h - 28);
      nextSpawnX += 120;
      continue;
    }
    if (spawnSlot == SpawnSlot::Fish) {
      e.kind = EntityKind::Fish;
      applyEntitySize(e);
      e.worldY = static_cast<int16_t>(ground - e.h - 44);
      nextSpawnX += 130;
      continue;
    }
    if (spawnSlot == SpawnSlot::Gap) {
      e.active = false;
      nextSpawnX += 120;
      continue;
    }

    // Hazard slot — pick kind from phase sequence (deterministic round-robin).
    uint8_t phaseId = 0;
    const EntityKind* seq = kIntroSeq;
    int seqLen = 2;  // kIntroSeq: grass / crate
    if (sec >= 91) {
      phaseId = 4;
      seq = kCrowSeq;
      seqLen = 4;
    } else if (sec >= 66) {
      phaseId = 3;
      seq = kFrogSeq;
      seqLen = 4;
    } else if (sec >= 46) {
      phaseId = 2;
      seq = kSnakeSeq;
      seqLen = 4;
    } else if (sec >= 21) {
      phaseId = 1;
      seq = kDogSeq;
      seqLen = 4;
    }
    if (phaseId != spawnPhaseId) {
      spawnPhaseId = phaseId;
      hazardScriptIndex = 0;
    }

    e.kind = seq[hazardScriptIndex % static_cast<uint8_t>(seqLen)];
    hazardScriptIndex = static_cast<uint8_t>(hazardScriptIndex + 1u);
    uint8_t action = hazardAction(e.kind);

    // Width safety: never spawn a jump hazard wider than one jump can clear.
    if (action == HAZARD_ACTION_JUMP) {
      const int safeWidth = maxSafeHazardWidth(scrollSpeed());
      Entity probe{};
      probe.kind = e.kind;
      applyEntitySize(probe);
      if (probe.w > safeWidth) {
        e.active = false;
        nextSpawnX += 120;
        continue;
      }
    }

    if (lastHazardAction != HAZARD_ACTION_NONE) {
      const int32_t minX = lastHazardWorldX + MIN_HAZARD_GAP;
      if (e.worldX < minX) {
        e.worldX = minX;
        nextSpawnX = minX;
      }
    }

    applyEntitySize(e);
    if (e.kind == EntityKind::Crow) {
      e.worldY = static_cast<int16_t>(ground - e.h - 40);
    } else {
      e.worldY = static_cast<int16_t>(ground - e.h);
    }

    int gap = 160;
    if (lastHazardAction != HAZARD_ACTION_NONE && action == lastHazardAction) {
      const bool creatureStreak = (e.kind == EntityKind::Snake || e.kind == EntityKind::Frog);
      gap = creatureStreak ? 180 : 240;
    }
    lastHazardAction = action;
    lastHazardWorldX = e.worldX;
    nextSpawnX = e.worldX + gap;
  }
}

bool CatRunActivity::inBossPhase() const {
  const uint32_t now = millis();
  const uint32_t sec = (now >= gameStartMs) ? ((now - gameStartMs) / 1000u) : 0;
  return sec + BOSS_PHASE_SEC >= GAME_DURATION_SEC;
}

void CatRunActivity::maybeBossAttack() {
  // Pose-only: rockets are spawned by the boss encounter script in spawnAhead().
  // ROBOT_MINE_POSE (pose 3) covers both rocket telegraph and fire hold.
  if (!inBossPhase()) {
    bossPose = 0;
    return;
  }
  const uint32_t now = millis();
  if (bossPose == 3 && lastBossAttackMs != 0 && (now - lastBossAttackMs) < kBossFireHoldMs) {
    return;  // hold mine-pose briefly after a rocket spawn
  }

  // Telegraph when an on-path rocket is near the right edge (about to confront the cat).
  const int pageWidth = renderer.getScreenWidth();
  const int32_t warnLo = cameraX + pageWidth - kRocketSpawnInset - 80;
  const int32_t warnHi = cameraX + pageWidth - 20;
  for (int i = 0; i < MAX_ENTITIES; ++i) {
    const Entity& e = entities[i];
    if (!e.active || e.kind != EntityKind::Rocket) {
      continue;
    }
    if (e.worldX >= warnLo && e.worldX <= warnHi) {
      bossPose = 3;  // ROBOT_MINE_POSE
      return;
    }
  }
  bossPose = 0;
}

void CatRunActivity::drawBossRobot() {
  if (!inBossPhase()) {
    return;
  }
  const int pageWidth = renderer.getScreenWidth();
  const int ground = groundY();
  // Bottom-right align to pose-specific size so feet stay on the ground line.
  const int draw = CatRunSprites::bossDrawSize(bossPose);
  const int x = pageWidth - draw - 4;
  const int y = ground - draw;
  CatRunSprites::drawBossRobot(renderer, x, y, bossPose, false);
}

void CatRunActivity::tickWorld() {
  const int speed = scrollSpeed();
  // Gait cadence tracks ground speed instead of a fixed 1-frame-per-tick clip:
  // faster scroll covers kWalkFramePx sooner, so legs cycle more often at
  // higher speed and slower at low speed, matching real running.
  constexpr int kWalkFramePx = 12;
  walkAnimAccumPx += speed;
  while (walkAnimAccumPx >= kWalkFramePx) {
    walkAnimAccumPx -= kWalkFramePx;
    walkFrame = static_cast<uint8_t>((walkFrame + 1) & 3u);
  }

  if (jumpPhase > 0) {
    if (jumpJustStarted) {
      // Phase 1 was set on button press, outside this tick loop. Consume this
      // tick to actually evaluate phase 1's hitbox once instead of silently
      // skipping straight to phase 2 — previously every jump's ascent lost one
      // full collision sample.
      jumpJustStarted = false;
    } else {
      ++jumpPhase;
      if (jumpPhase > kJumpPhaseMax) {
        jumpPhase = 0;
        jumpLandGrace = kJumpLandGraceTicks;
      }
    }
  } else if (jumpLandGrace > 0) {
    --jumpLandGrace;
  }

  // Jump-conditional horizontal clearance boost (game-feel option C): while
  // actually airborne this tick, cameraX advances a bit faster than normal
  // walking speed, closing the measured "lands on the obstacle" gap (verified
  // via tick-accurate simulation: +5px/tick fully closes it for both the 40px
  // baseline hazard and the 56px Frog outlier, across the whole 7-11
  // reachable speed range) without lengthening the jump or touching baseline
  // world speed, hitboxes, or collision rules. Uses the phase value AFTER the
  // advance above so the boost applies exactly on ticks the collision check
  // below will treat as airborne — not one tick early or late.
  constexpr int kJumpSpeedBoost = 5;
  cameraX += speed + (jumpPhase > 0 ? kJumpSpeedBoost : 0);

#if LOG_LEVEL >= 2
  // #region render-sync investigation (temporary, diagnostic-only, no gameplay
  // effect) — logs every COMPUTED tick. Compare timestamps against the
  // RENDERDBG lines in render() below: if two or more TICKDBG lines appear
  // between consecutive RENDERDBG lines, that tick's state was never actually
  // shown on screen (coalesced by the render task) — direct evidence for or
  // against the render/tick desync hypothesis. Strip this block once answered.
  LOG_DBG("TICKDBG", "t=%lu phase=%u camX=%ld", static_cast<unsigned long>(millis()),
          static_cast<unsigned>(jumpPhase), static_cast<long>(cameraX));
  // #endregion
#endif

  // Despawn off-screen left
  for (int i = 0; i < MAX_ENTITIES; ++i) {
    if (!entities[i].active) {
      continue;
    }
    if (entities[i].worldX + entities[i].w < cameraX - 20) {
      entities[i].active = false;
    }
  }

  spawnAhead();
  maybeBossAttack();

  int hx = 0, hy = 0, hw = 0, hh = 0;
  catHitbox(hx, hy, hw, hh);

  for (int i = 0; i < MAX_ENTITIES; ++i) {
    Entity& e = entities[i];
    if (!e.active) {
      continue;
    }
    const int ex = e.worldX - cameraX;
    const int ey = e.worldY;
    if (!aabbOverlap(hx, hy, hw, hh, ex, ey, e.w, e.h)) {
      continue;
    }

    if (entityIsCollectible(e.kind)) {
      score += (e.kind == EntityKind::Fish) ? kFishScore : kFoodScore;
      e.active = false;
      continue;
    }

    if (e.kind == EntityKind::Flag) {
      onRoundEnd(true);
      gameOverShownMs = millis();
      gameOverNeedsHintRefresh = true;
      phase = Phase::GameOverFinal;
      return;
    }

    if (e.kind == EntityKind::BullyCat || e.kind == EntityKind::CoolCat || e.kind == EntityKind::Crow ||
        e.kind == EntityKind::Rocket) {
      // Duck under tall / aerial enemies when grounded.
      if (ducking && jumpPhase == 0) {
        continue;
      }
    }

    if (e.kind == EntityKind::Platform) {
      // Platforms are solid only when landing from above — simplify: ignore if ducking under
      if (jumpPhase == 0 && hy + hh <= ey + 4) {
        continue;
      }
      if (jumpPhase > 0 && hy + hh <= ey + 6) {
        jumpPhase = 0;
        continue;
      }
    }

    if (entityIsHazard(e.kind)) {
      // Jump only clears ground hazards once the cat is actually elevated
      // (kJumpClearMinHeight). Previously any jumpPhase>0 counted, including
      // phases 6/7 (-32px/-12px — nearly ground level), so a hazard still
      // geometrically overlapping the hitbox near takeoff/landing was forgiven
      // just for being mid-jump. Land-grace still covers tick-quantization
      // right at touchdown; rockets/crows/bully still need an actual duck.
      const int jumpHeight = (jumpPhase >= 1 && jumpPhase <= kJumpPhaseMax) ? -kJumpOffsets[jumpPhase] : 0;
      const bool highEnough = jumpHeight >= kJumpClearMinHeight;
      const bool jumpClear = (highEnough || jumpLandGrace > 0) && hazardClearedByJump(e.kind);
      if (jumpClear) {
        continue;
      }
      if (ducking && jumpPhase == 0 && e.kind == EntityKind::Grass) {
        continue;
      }

      // #region agent log
#if LOG_LEVEL >= 2
      {
        char data[160];
        const int catBottom = hy + hh;
        // gapX: >0 hazard still right of cat; <0 overlapping/past
        const int gapX = ex - (hx + hw);
        snprintf(data, sizeof(data),
                 "{\"k\":%u,\"jp\":%u,\"lg\":%u,\"hy\":%d,\"hh\":%d,\"ey\":%d,\"eh\":%d,\"ew\":%d,"
                 "\"cb\":%d,\"gx\":%d,\"sc\":%d,\"dk\":%d}",
                 static_cast<unsigned>(e.kind), static_cast<unsigned>(jumpPhase),
                 static_cast<unsigned>(jumpLandGrace), hy, hh, ey, static_cast<int>(e.h),
                 static_cast<int>(e.w), catBottom, gapX, speed, ducking ? 1 : 0);
        agentJumpLog("A-E", "hazard_death", data);
      }
#endif  // LOG_LEVEL >= 2
      // #endregion

      // Continue only if score can pay CONTINUE_SCORE_COST; otherwise must restart.
      gameOverShownMs = millis();
      gameOverNeedsHintRefresh = true;
      if (score < CONTINUE_SCORE_COST) {
        onRoundEnd(false);
        phase = Phase::GameOverFinal;
      } else {
        phase = Phase::GameOverRetry;
      }
      return;
    }
  }

  const uint32_t elapsed = millis() - gameStartMs;
  if (elapsed >= GAME_DURATION_MS) {
    onRoundEnd(true);
    gameOverShownMs = millis();
    gameOverNeedsHintRefresh = true;
    phase = Phase::GameOverFinal;
  }
}

void CatRunActivity::loop() {
  if (phase == Phase::SelectColor) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      finish();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Left) ||
        mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      colorSelectIndex = (colorSelectIndex + 2) % 3;
      requestUpdate();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Right) ||
        mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      colorSelectIndex = (colorSelectIndex + 1) % 3;
      requestUpdate();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      resetRun();
      phase = Phase::Playing;
      requestUpdate();
    }
    return;
  }

  if (phase == Phase::GameOverRetry || phase == Phase::GameOverFinal) {
    const uint32_t now = millis();
    // Ignore mashed jump/confirm for 2s after the dialog appears.
    if (now - gameOverShownMs < GAME_OVER_INPUT_DELAY_MS) {
      return;
    }
    if (gameOverNeedsHintRefresh) {
      gameOverNeedsHintRefresh = false;
      requestUpdate();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      if (phase == Phase::GameOverRetry) {
        onRoundEnd(false);
      }
      finish();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (phase == Phase::GameOverRetry) {
        score -= CONTINUE_SCORE_COST;
        if (score < 0) {
          score = 0;
        }
        ++retriesUsed;
        clearNearbyHazards();
        phase = Phase::Playing;
        lastTickMs = millis();
        requestUpdate();
        return;
      }
      // Final: play again → color select
      phase = Phase::SelectColor;
      scoreSubmitted = false;
      requestUpdate();
    }
    return;
  }

  // Playing
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
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

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) ||
      mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    if (jumpPhase == 0) {
      jumpPhase = 1;
      jumpLandGrace = 0;
      jumpJustStarted = true;  // phase 1 must survive one full tick before tickWorld() advances it
      ducking = false;
      // #region agent log
#if LOG_LEVEL >= 2
      {
        char data[96];
        snprintf(data, sizeof(data), "{\"jumpPhase\":1,\"cameraX\":%ld,\"scroll\":%d}",
                 static_cast<long>(cameraX), scrollSpeed());
        agentJumpLog("E", "jump_start", data);
      }
#endif  // LOG_LEVEL >= 2
      // #endregion
      requestUpdate();
    }
  }

  {
    const bool wantDuck =
        jumpPhase == 0 &&
        (mappedInput.isPressed(MappedInputManager::Button::Down) ||
         mappedInput.isPressed(MappedInputManager::Button::Right));
    if (wantDuck != ducking) {
      ducking = wantDuck;
      requestUpdate();
    }
  }

  const uint32_t now = millis();
  if (now - lastTickMs >= TICK_MS) {
    lastTickMs = now;
    tickWorld();
    requestUpdate();
  }
}

void CatRunActivity::renderSelectColor() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_CAT_RUN));

  const int y = metrics.topPadding + metrics.headerHeight + 40;
  renderer.drawCenteredText(UI_12_FONT_ID, y, tr(STR_CAT_SELECT_COLOR), true, EpdFontFamily::BOLD);

  static const StrId kColorNames[3] = {StrId::STR_CAT_WHITE, StrId::STR_CAT_BLACK, StrId::STR_CAT_CALICO};
  const int previewY = y + 36;
  const int spacing = CAT_W + 24;
  const int startX = pageWidth / 2 - (spacing * 3) / 2 + 8;
  for (int i = 0; i < 3; ++i) {
    const int x = startX + i * spacing;
    CatRunSprites::drawCat(renderer, x, previewY, static_cast<CatRunSprites::CatColor>(i), 0, false, false);
    if (i == colorSelectIndex) {
      renderer.drawRect(x - 4, previewY - 4, CAT_W + 8, CAT_H + 8, true);
    }
  }

  renderer.drawCenteredText(UI_10_FONT_ID, previewY + CAT_H + 24, I18N.get(kColorNames[colorSelectIndex]), true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_CONFIRM), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void CatRunActivity::renderPlaying() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int ground = groundY();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_CAT_RUN));

  // Layers: far parallax → midground trees → ground → hazards → cat.
  const int hudY = metrics.topPadding + metrics.headerHeight + 14;
  const int parallaxY = hudY + 36;
  const uint32_t elapsedSec = (millis() - gameStartMs) / 1000u;
  const CatRunSprites::ScenePhase scene =
      CatRunSprites::scenePhaseFromSec(elapsedSec, GAME_DURATION_SEC, BOSS_PHASE_SEC);

  for (int i = 0; i < 4; ++i) {
    const int mx = ((i * 180) - ((cameraX / 4) % 180) + pageWidth) % pageWidth;
    CatRunSprites::drawParallax(renderer, mx, parallaxY, i);
  }

  // Midground trees sit under the mountain band (above jump apex, below peaks).
  // Mountain bottoms ~ y 116; jump apex ~ ground-124 → feet near ground-130.
  const int treeBaseY = ground - 180;
  for (int i = 0; i < 4; ++i) {
    const int tx = ((i * 180) - ((cameraX / 2) % 180) + pageWidth) % pageWidth;
    CatRunSprites::drawMidgroundTrees(renderer, tx, treeBaseY, i, scene);
  }

  // Ground: top edge + light hatch (not solid fill) so duck sprites stay visible.
  const int floorBottom = renderer.getScreenHeight() - metrics.buttonHintsHeight;
  renderer.fillRect(0, ground, pageWidth, 3, true);
  for (int y = ground + 6; y < floorBottom; y += 6) {
    for (int x = (y & 1) ? 0 : 4; x < pageWidth; x += 8) {
      renderer.fillRect(x, y, 2, 2, true);
    }
  }

  for (int i = 0; i < MAX_ENTITIES; ++i) {
    const Entity& e = entities[i];
    if (!e.active) {
      continue;
    }
    const int ex = e.worldX - cameraX;
    if (ex + e.w < 0 || ex > pageWidth) {
      continue;
    }
    // Cosmetic bob for airborne/floating enemies — draw position only, e.worldY
    // (used for collision in tickWorld()) is untouched so hitboxes stay exact.
    int drawYOffset = 0;
    if (e.kind == EntityKind::Crow) {
      drawYOffset = (walkFrame & 1u) ? -3 : 3;  // wingbeat bob
    } else if (e.kind == EntityKind::Frog) {
      drawYOffset = (walkFrame % 3u == 1u) ? -2 : 0;  // croak bob
    }

    // Snake/frog scroll in like other hazards (always visible); animate frames.
    uint8_t entityAnimFrame = walkFrame;
    if (e.kind == EntityKind::Snake) {
      static constexpr uint8_t kSnakeCycle[] = {0u, 1u, 2u, 3u};
      entityAnimFrame = kSnakeCycle[walkFrame % 4u];
    } else if (e.kind == EntityKind::Frog) {
      entityAnimFrame = static_cast<uint8_t>(walkFrame % 3u);
    }
    CatRunSprites::drawEntity(renderer, e.kind, ex, e.worldY + drawYOffset, e.w, e.h, entityAnimFrame, scene,
                              false);
  }

  const int catX = catScreenX();
  // Landing squash: for the single tick right after touchdown (jumpLandGrace
  // freshly set to its max), briefly show the crouch pose at the duck's
  // ground offset — reuses the existing duck sprite/offset, no new art and no
  // change to the hitbox (that's still driven by the real `ducking` flag).
  const bool landingSquash = jumpPhase == 0 && !ducking && jumpLandGrace == kJumpLandGraceTicks;
  // Draw-only: cancel catTopY()'s ducking +8 so feet sit on the ground line.
  // Hitbox / crow-rocket dodge still use catTopY()+catHitbox() unchanged.
  const int catY = landingSquash ? (ground - CAT_H + 8)
                                 : (catTopY() - ((ducking && jumpPhase == 0) ? 8 : 0));
  CatRunSprites::drawCat(renderer, catX, catY, catColor, walkFrame, jumpPhase > 0,
                        (ducking && jumpPhase == 0) || landingSquash);
  drawBossRobot();

  char buf[48];
  char timeBuf[16];
  const uint32_t remain = (elapsedSec < GAME_DURATION_SEC) ? (GAME_DURATION_SEC - elapsedSec) : 0;
  CatRunScoreStore::formatDuration(remain, timeBuf, sizeof(timeBuf));
  snprintf(buf, sizeof(buf), "%s: %d", tr(STR_SCORE), score);
  renderer.drawText(UI_10_FONT_ID, 8, hudY, buf, true);
  snprintf(buf, sizeof(buf), "%s: %s", tr(STR_DURATION), timeBuf);
  renderer.drawText(UI_10_FONT_ID, pageWidth / 2, hudY, buf, true);

  const auto labels = mappedInput.mapLabels(tr(STR_PAUSE), tr(STR_JUMP), "", tr(STR_DUCK));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void CatRunActivity::renderGameOver() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_CAT_RUN));

  char buf[64];
  char timeBuf[16];
  const uint32_t showSec =
      scoreSubmitted ? lastDurationSec : ((millis() - gameStartMs) / 1000u);
  CatRunScoreStore::formatDuration(showSec > GAME_DURATION_SEC ? GAME_DURATION_SEC : showSec, timeBuf, sizeof(timeBuf));

  if (finishedClean && phase == Phase::GameOverFinal) {
    // Victory: defeated boss art + win title + score.
    constexpr int kBossW = ROBOT_DEFEATED_W;
    constexpr int kBossH = ROBOT_DEFEATED_H;
    const int bossX = (pageWidth - kBossW) / 2;
    const int bossY = metrics.topPadding + metrics.headerHeight + 28;
    CatRunSprites::drawBossDefeated(renderer, bossX, bossY);

    const int textY = bossY + kBossH + 16;
    renderer.drawCenteredText(UI_12_FONT_ID, textY, tr(STR_CAT_YOU_WIN), true, EpdFontFamily::BOLD);
    snprintf(buf, sizeof(buf), "%s: %d", tr(STR_SCORE), score);
    renderer.drawCenteredText(UI_12_FONT_ID, textY + 28, buf, true);
    snprintf(buf, sizeof(buf), "%s: %s", tr(STR_DURATION), timeBuf);
    renderer.drawCenteredText(UI_10_FONT_ID, textY + 52, buf, true);
    snprintf(buf, sizeof(buf), "%s: %u", tr(STR_RETRIES), static_cast<unsigned>(retriesUsed));
    renderer.drawCenteredText(UI_10_FONT_ID, textY + 72, buf, true);

    const bool inputReady = (millis() - gameOverShownMs) >= GAME_OVER_INPUT_DELAY_MS;
    if (inputReady) {
      const auto labels = mappedInput.mapLabels(tr(STR_QUIT_GAME), tr(STR_PLAY_AGAIN), "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    }
    return;
  }

  const int boxW = pageWidth - 80;
  const int boxH = 160;
  const int boxX = 40;
  const int boxY = pageHeight / 2 - boxH / 2;
  renderer.fillRect(boxX, boxY, boxW, boxH, false);
  renderer.drawRect(boxX, boxY, boxW, boxH, true);

  const char* title = tr(STR_GAME_OVER);
  renderer.drawCenteredText(UI_12_FONT_ID, boxY + 20, title, true, EpdFontFamily::BOLD);

  snprintf(buf, sizeof(buf), "%s: %d", tr(STR_SCORE), score);
  renderer.drawCenteredText(UI_12_FONT_ID, boxY + 48, buf, true);
  snprintf(buf, sizeof(buf), "%s: %s", tr(STR_DURATION), timeBuf);
  renderer.drawCenteredText(UI_10_FONT_ID, boxY + 72, buf, true);
  snprintf(buf, sizeof(buf), "%s: %u", tr(STR_RETRIES), static_cast<unsigned>(retriesUsed));
  renderer.drawCenteredText(UI_10_FONT_ID, boxY + 92, buf, true);

  if (phase == Phase::GameOverRetry) {
    renderer.drawCenteredText(UI_10_FONT_ID, boxY + 116, tr(STR_CAT_CONTINUE_COST), true);
    const bool inputReady = (millis() - gameOverShownMs) >= GAME_OVER_INPUT_DELAY_MS;
    if (inputReady) {
      const auto labels = mappedInput.mapLabels(tr(STR_QUIT_GAME), tr(STR_CONTINUE), "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    }
  } else {
    const bool inputReady = (millis() - gameOverShownMs) >= GAME_OVER_INPUT_DELAY_MS;
    if (inputReady) {
      const auto labels = mappedInput.mapLabels(tr(STR_QUIT_GAME), tr(STR_PLAY_AGAIN), "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    }
  }
}

void CatRunActivity::render(RenderLock&&) {
  renderer.clearScreen();
  switch (phase) {
    case Phase::SelectColor:
      renderSelectColor();
      break;
    case Phase::Playing:
      renderPlaying();
      break;
    case Phase::GameOverRetry:
    case Phase::GameOverFinal:
      renderGameOver();
      break;
  }
#if LOG_LEVEL >= 2
  // #region render-sync investigation (temporary, diagnostic-only) — see the
  // matching TICKDBG block in tickWorld(). Logs the state actually about to be
  // displayed, plus the real wall-clock duration of the blocking FAST_REFRESH
  // call, so an overrun past TICK_MS (450ms) is directly visible in the log.
  const uint32_t renderPreMs = millis();
  LOG_DBG("RENDERDBG", "pre  t=%lu phase=%u camX=%ld", static_cast<unsigned long>(renderPreMs),
          static_cast<unsigned>(jumpPhase), static_cast<long>(cameraX));
#endif
  renderer.displayBuffer(HalDisplay::RefreshMode::FAST_REFRESH);
#if LOG_LEVEL >= 2
  LOG_DBG("RENDERDBG", "post t=%lu durationMs=%lu", static_cast<unsigned long>(millis()),
          static_cast<unsigned long>(millis() - renderPreMs));
  // #endregion
#endif
}
