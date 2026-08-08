#pragma once

#include <cstddef>
#include <cstdint>

class CatRunSaveStore;
namespace JsonSettingsIO {
bool saveCatRunSave(const CatRunSaveStore& store, const char* path);
bool loadCatRunSave(CatRunSaveStore& store, const char* json);
}  // namespace JsonSettingsIO

/**
 * Single in-progress Cat Run snapshot (fixed-size, no heap).
 * Survives pause (Back) and deep sleep via SPIFFS JSON.
 */
class CatRunSaveStore {
  static CatRunSaveStore instance;

  friend bool JsonSettingsIO::saveCatRunSave(const CatRunSaveStore&, const char*);
  friend bool JsonSettingsIO::loadCatRunSave(CatRunSaveStore&, const char*);

 public:
  static constexpr int MAX_ENTITIES = 10;

  enum class EntityKind : uint8_t {
    Hole = 0,
    Grass,
    Tree,
    Fence,
    Crate,
    Brick,
    QuestionBlock,
    Platform,
    Water,
    Puppy,
    Dog,
    RunningDog,
    CoolCat,
    BullyCat,
    CatFood,
    Fish,
    Flag,
    Puddle,
    Frog,
    Snake,
    Crow,
    Rocket,
    Mine,
    Count
  };

  struct Entity {
    EntityKind kind = EntityKind::Hole;
    int32_t worldX = 0;
    int16_t worldY = 0;
    uint8_t w = 0;
    uint8_t h = 0;
    bool solid = false;
    bool active = false;
  };

 private:
  bool valid = false;
  uint8_t catColor = 0;
  uint8_t retriesUsed = 0;
  uint8_t jumpPhase = 0;
  uint8_t walkFrame = 0;
  bool ducking = false;
  int32_t score = 0;
  uint32_t elapsedMs = 0;
  int32_t cameraX = 0;
  int32_t nextSpawnX = 0;
  uint32_t rngState = 1;
  Entity entities[MAX_ENTITIES] = {};

  CatRunSaveStore() = default;

 public:
  CatRunSaveStore(const CatRunSaveStore&) = delete;
  CatRunSaveStore& operator=(const CatRunSaveStore&) = delete;

  static CatRunSaveStore& getInstance() { return instance; }

  bool hasSave() const { return valid; }

  uint8_t getCatColor() const { return catColor; }
  uint8_t getRetriesUsed() const { return retriesUsed; }
  uint8_t getJumpPhase() const { return jumpPhase; }
  uint8_t getWalkFrame() const { return walkFrame; }
  bool getDucking() const { return ducking; }
  int getScore() const { return score; }
  uint32_t getElapsedMs() const { return elapsedMs; }
  int32_t getCameraX() const { return cameraX; }
  int32_t getNextSpawnX() const { return nextSpawnX; }
  uint32_t getRngState() const { return rngState; }
  const Entity* getEntities() const { return entities; }

  void set(uint8_t catColorIn, uint8_t retriesUsedIn, uint8_t jumpPhaseIn, uint8_t walkFrameIn, bool duckingIn,
           int scoreIn, uint32_t elapsedMsIn, int32_t cameraXIn, int32_t nextSpawnXIn, uint32_t rngStateIn,
           const Entity entitiesIn[MAX_ENTITIES]);

  void clear();

  bool saveToFile() const;
  bool loadFromFile();
  bool clearFile();
};

#define CATRUN_SAVE CatRunSaveStore::getInstance()
