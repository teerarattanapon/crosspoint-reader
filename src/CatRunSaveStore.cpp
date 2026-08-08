#include "CatRunSaveStore.h"

#include <HalStorage.h>
#include <JsonSettingsIO.h>

#include <cstring>

namespace {
constexpr char CATRUN_SAVE_FILE_JSON[] = "/.crosspoint/catrun_save.json";
}  // namespace

CatRunSaveStore CatRunSaveStore::instance;

void CatRunSaveStore::set(uint8_t catColorIn, uint8_t retriesUsedIn, uint8_t jumpPhaseIn, uint8_t walkFrameIn,
                          bool duckingIn, int scoreIn, uint32_t elapsedMsIn, int32_t cameraXIn, int32_t nextSpawnXIn,
                          uint32_t rngStateIn, const Entity entitiesIn[MAX_ENTITIES]) {
  catColor = catColorIn;
  retriesUsed = retriesUsedIn;
  jumpPhase = jumpPhaseIn;
  walkFrame = walkFrameIn;
  ducking = duckingIn;
  score = scoreIn;
  elapsedMs = elapsedMsIn;
  cameraX = cameraXIn;
  nextSpawnX = nextSpawnXIn;
  rngState = rngStateIn;
  memcpy(entities, entitiesIn, sizeof(entities));
  valid = true;
}

void CatRunSaveStore::clear() {
  catColor = 0;
  retriesUsed = 0;
  jumpPhase = 0;
  walkFrame = 0;
  ducking = false;
  score = 0;
  elapsedMs = 0;
  cameraX = 0;
  nextSpawnX = 0;
  rngState = 1;
  memset(entities, 0, sizeof(entities));
  valid = false;
}

bool CatRunSaveStore::saveToFile() const {
  if (!valid) {
    if (Storage.exists(CATRUN_SAVE_FILE_JSON)) {
      return Storage.remove(CATRUN_SAVE_FILE_JSON);
    }
    return true;
  }
  Storage.mkdir("/.crosspoint");
  return JsonSettingsIO::saveCatRunSave(*this, CATRUN_SAVE_FILE_JSON);
}

bool CatRunSaveStore::loadFromFile() {
  if (!Storage.exists(CATRUN_SAVE_FILE_JSON)) {
    clear();
    return false;
  }
  String json = Storage.readFile(CATRUN_SAVE_FILE_JSON);
  if (json.isEmpty()) {
    clear();
    return false;
  }
  return JsonSettingsIO::loadCatRunSave(*this, json.c_str());
}

bool CatRunSaveStore::clearFile() {
  clear();
  if (Storage.exists(CATRUN_SAVE_FILE_JSON)) {
    return Storage.remove(CATRUN_SAVE_FILE_JSON);
  }
  return true;
}
