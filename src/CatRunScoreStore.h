#pragma once

#include <cstddef>
#include <cstdint>

struct CatRunScoreEntry {
  uint32_t score = 0;
  uint32_t durationSec = 0;
  uint8_t retries = 0;
};

class CatRunScoreStore;
namespace JsonSettingsIO {
bool saveCatRunScores(const CatRunScoreStore& store, const char* path);
bool loadCatRunScores(CatRunScoreStore& store, const char* json);
}  // namespace JsonSettingsIO

/**
 * Fixed-size top-3 Cat Run high scores (no heap).
 * Rank: score desc, then duration asc, then retries asc.
 */
class CatRunScoreStore {
  static CatRunScoreStore instance;

  friend bool JsonSettingsIO::saveCatRunScores(const CatRunScoreStore&, const char*);
  friend bool JsonSettingsIO::loadCatRunScores(CatRunScoreStore&, const char*);

 public:
  static constexpr int MAX = 3;

 private:
  CatRunScoreEntry entries[MAX] = {};
  uint8_t count = 0;

  CatRunScoreStore() = default;

 public:
  CatRunScoreStore(const CatRunScoreStore&) = delete;
  CatRunScoreStore& operator=(const CatRunScoreStore&) = delete;

  static CatRunScoreStore& getInstance() { return instance; }

  int getCount() const { return count; }
  const CatRunScoreEntry& getEntry(int index) const { return entries[index]; }

  /** Insert if run qualifies for top 3. Returns true if ranking changed. */
  bool submit(uint32_t score, uint32_t durationSec, uint8_t retries);

  bool saveToFile() const;
  bool loadFromFile();

  static char* formatDuration(uint32_t durationSec, char* buf, size_t bufSize);
};

#define CATRUN_SCORES CatRunScoreStore::getInstance()
