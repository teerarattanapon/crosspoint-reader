#include "CatRunScoreStore.h"

#include <HalStorage.h>
#include <JsonSettingsIO.h>
#include <Logging.h>

#include <cstdio>

namespace {
constexpr char CATRUN_SCORES_FILE_JSON[] = "/.crosspoint/catrun_scores.json";

bool ranksHigher(uint32_t score, uint32_t durationSec, uint8_t retries, const CatRunScoreEntry& other) {
  if (score != other.score) {
    return score > other.score;
  }
  if (durationSec != other.durationSec) {
    return durationSec < other.durationSec;
  }
  return retries < other.retries;
}
}  // namespace

CatRunScoreStore CatRunScoreStore::instance;

bool CatRunScoreStore::submit(uint32_t score, uint32_t durationSec, uint8_t retries) {
  int insertAt = count;
  for (int i = 0; i < count; ++i) {
    if (ranksHigher(score, durationSec, retries, entries[i])) {
      insertAt = i;
      break;
    }
  }

  if (insertAt >= MAX) {
    return false;
  }

  const int last = (count < MAX) ? count : (MAX - 1);
  for (int i = last; i > insertAt; --i) {
    entries[i] = entries[i - 1];
  }
  entries[insertAt].score = score;
  entries[insertAt].durationSec = durationSec;
  entries[insertAt].retries = retries;
  if (count < MAX) {
    ++count;
  }
  return true;
}

bool CatRunScoreStore::saveToFile() const {
  Storage.mkdir("/.crosspoint");
  return JsonSettingsIO::saveCatRunScores(*this, CATRUN_SCORES_FILE_JSON);
}

bool CatRunScoreStore::loadFromFile() {
  if (!Storage.exists(CATRUN_SCORES_FILE_JSON)) {
    return false;
  }
  String json = Storage.readFile(CATRUN_SCORES_FILE_JSON);
  if (json.isEmpty()) {
    return false;
  }
  return JsonSettingsIO::loadCatRunScores(*this, json.c_str());
}

char* CatRunScoreStore::formatDuration(uint32_t durationSec, char* buf, size_t bufSize) {
  const uint32_t mins = durationSec / 60u;
  const uint32_t secs = durationSec % 60u;
  snprintf(buf, bufSize, "%02u:%02u", mins, secs);
  return buf;
}
