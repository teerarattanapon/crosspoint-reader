#include "TretisScoreStore.h"

#include <HalStorage.h>
#include <JsonSettingsIO.h>
#include <Logging.h>

#include <cstdio>

namespace {
constexpr char TRETIS_SCORES_FILE_JSON[] = "/.crosspoint/tretis_scores.json";
}  // namespace

TretisScoreStore TretisScoreStore::instance;

bool TretisScoreStore::submit(uint32_t score, uint32_t durationSec) {
  // Find insert position: higher score first; on tie, shorter duration ranks higher.
  int insertAt = count;
  for (int i = 0; i < count; ++i) {
    if (score > entries[i].score || (score == entries[i].score && durationSec < entries[i].durationSec)) {
      insertAt = i;
      break;
    }
  }

  if (insertAt >= MAX) {
    return false;  // Does not qualify
  }

  // Shift lower scores down
  const int last = (count < MAX) ? count : (MAX - 1);
  for (int i = last; i > insertAt; --i) {
    entries[i] = entries[i - 1];
  }
  entries[insertAt].score = score;
  entries[insertAt].durationSec = durationSec;
  if (count < MAX) {
    ++count;
  }
  return true;
}

bool TretisScoreStore::saveToFile() const {
  Storage.mkdir("/.crosspoint");
  return JsonSettingsIO::saveTretisScores(*this, TRETIS_SCORES_FILE_JSON);
}

bool TretisScoreStore::loadFromFile() {
  if (!Storage.exists(TRETIS_SCORES_FILE_JSON)) {
    return false;
  }
  String json = Storage.readFile(TRETIS_SCORES_FILE_JSON);
  if (json.isEmpty()) {
    return false;
  }
  return JsonSettingsIO::loadTretisScores(*this, json.c_str());
}

char* TretisScoreStore::formatDuration(uint32_t durationSec, char* buf, size_t bufSize) {
  const uint32_t days = durationSec / 86400u;
  const uint32_t hours = (durationSec / 3600u) % 24u;
  const uint32_t mins = (durationSec / 60u) % 60u;
  const uint32_t secs = durationSec % 60u;
  snprintf(buf, bufSize, "%u %02u:%02u:%02u", days, hours, mins, secs);
  return buf;
}
