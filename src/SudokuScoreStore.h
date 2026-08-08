#pragma once

#include <cstddef>
#include <cstdint>

struct SudokuScoreEntry {
  uint32_t durationSec = 0;
};

class SudokuScoreStore;
namespace JsonSettingsIO {
bool saveSudokuScores(const SudokuScoreStore& store, const char* path);
bool loadSudokuScores(SudokuScoreStore& store, const char* json);
}  // namespace JsonSettingsIO

/**
 * Fixed-size top-3 Sudoku high scores (no heap).
 * Sorted by duration ascending — shortest completion time ranks highest.
 */
class SudokuScoreStore {
  static SudokuScoreStore instance;

  friend bool JsonSettingsIO::saveSudokuScores(const SudokuScoreStore&, const char*);
  friend bool JsonSettingsIO::loadSudokuScores(SudokuScoreStore&, const char*);

 public:
  static constexpr int MAX = 3;

 private:
  SudokuScoreEntry entries[MAX] = {};
  uint8_t count = 0;

  SudokuScoreStore() = default;

 public:
  SudokuScoreStore(const SudokuScoreStore&) = delete;
  SudokuScoreStore& operator=(const SudokuScoreStore&) = delete;

  static SudokuScoreStore& getInstance() { return instance; }

  int getCount() const { return count; }
  const SudokuScoreEntry& getEntry(int index) const { return entries[index]; }

  /** Insert if duration qualifies for top 3. Returns true if ranking changed. */
  bool submit(uint32_t durationSec);

  bool saveToFile() const;
  bool loadFromFile();

  /** Format duration as "d hh:mm:ss" into buf. Returns buf. */
  static char* formatDuration(uint32_t durationSec, char* buf, size_t bufSize);
};

#define SUDOKU_SCORES SudokuScoreStore::getInstance()
