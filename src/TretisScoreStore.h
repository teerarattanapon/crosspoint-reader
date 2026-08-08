#pragma once

#include <cstddef>
#include <cstdint>

struct TretisScoreEntry {
  uint32_t score = 0;
  uint32_t durationSec = 0;
};

class TretisScoreStore;
namespace JsonSettingsIO {
bool saveTretisScores(const TretisScoreStore& store, const char* path);
bool loadTretisScores(TretisScoreStore& store, const char* json);
}  // namespace JsonSettingsIO

/**
 * Fixed-size top-3 TRETIS high scores (no heap).
 * Sorted by score descending. Duration tie-break: shorter duration ranks higher.
 */
class TretisScoreStore {
  static TretisScoreStore instance;

  friend bool JsonSettingsIO::saveTretisScores(const TretisScoreStore&, const char*);
  friend bool JsonSettingsIO::loadTretisScores(TretisScoreStore&, const char*);

 public:
  static constexpr int MAX = 3;

 private:
  TretisScoreEntry entries[MAX] = {};
  uint8_t count = 0;

  TretisScoreStore() = default;

 public:
  TretisScoreStore(const TretisScoreStore&) = delete;
  TretisScoreStore& operator=(const TretisScoreStore&) = delete;

  static TretisScoreStore& getInstance() { return instance; }

  int getCount() const { return count; }
  const TretisScoreEntry& getEntry(int index) const { return entries[index]; }

  /** Insert if score qualifies for top 3. Returns true if ranking changed. */
  bool submit(uint32_t score, uint32_t durationSec);

  bool saveToFile() const;
  bool loadFromFile();

  /** Format duration as "d hh:mm:ss" into buf. Returns buf. */
  static char* formatDuration(uint32_t durationSec, char* buf, size_t bufSize);
};

#define TRETIS_SCORES TretisScoreStore::getInstance()
