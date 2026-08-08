#include "SudokuScoreStore.h"

#include <HalStorage.h>
#include <JsonSettingsIO.h>
#include <Logging.h>

#include <cstdio>

namespace {
constexpr char SUDOKU_SCORES_FILE_JSON[] = "/.crosspoint/sudoku_scores.json";
}  // namespace

SudokuScoreStore SudokuScoreStore::instance;

bool SudokuScoreStore::submit(uint32_t durationSec) {
  // Find insert position: shorter duration ranks higher.
  int insertAt = count;
  for (int i = 0; i < count; ++i) {
    if (durationSec < entries[i].durationSec) {
      insertAt = i;
      break;
    }
  }

  if (insertAt >= MAX) {
    return false;  // Does not qualify
  }

  // Shift slower times down
  const int last = (count < MAX) ? count : (MAX - 1);
  for (int i = last; i > insertAt; --i) {
    entries[i] = entries[i - 1];
  }
  entries[insertAt].durationSec = durationSec;
  if (count < MAX) {
    ++count;
  }
  return true;
}

bool SudokuScoreStore::saveToFile() const {
  Storage.mkdir("/.crosspoint");
  return JsonSettingsIO::saveSudokuScores(*this, SUDOKU_SCORES_FILE_JSON);
}

bool SudokuScoreStore::loadFromFile() {
  if (!Storage.exists(SUDOKU_SCORES_FILE_JSON)) {
    return false;
  }
  String json = Storage.readFile(SUDOKU_SCORES_FILE_JSON);
  if (json.isEmpty()) {
    return false;
  }
  return JsonSettingsIO::loadSudokuScores(*this, json.c_str());
}

char* SudokuScoreStore::formatDuration(uint32_t durationSec, char* buf, size_t bufSize) {
  const uint32_t days = durationSec / 86400u;
  const uint32_t hours = (durationSec / 3600u) % 24u;
  const uint32_t mins = (durationSec / 60u) % 60u;
  const uint32_t secs = durationSec % 60u;
  snprintf(buf, bufSize, "%u %02u:%02u:%02u", days, hours, mins, secs);
  return buf;
}
