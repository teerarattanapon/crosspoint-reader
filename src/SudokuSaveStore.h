#pragma once

#include <cstddef>
#include <cstdint>

class SudokuSaveStore;
namespace JsonSettingsIO {
bool saveSudokuSave(const SudokuSaveStore& store, const char* path);
bool loadSudokuSave(SudokuSaveStore& store, const char* json);
}  // namespace JsonSettingsIO

/**
 * Single in-progress Sudoku game snapshot (fixed-size, no heap).
 * Survives pause (Back) and deep sleep via SPIFFS JSON.
 */
class SudokuSaveStore {
  static SudokuSaveStore instance;

  friend bool JsonSettingsIO::saveSudokuSave(const SudokuSaveStore&, const char*);
  friend bool JsonSettingsIO::loadSudokuSave(SudokuSaveStore&, const char*);

 public:
  static constexpr int SIZE = 9;

 private:
  bool valid = false;
  uint8_t given[SIZE][SIZE] = {};
  uint8_t board[SIZE][SIZE] = {};
  int8_t cursorRow = 0;
  int8_t cursorCol = 0;
  uint32_t elapsedMs = 0;

  SudokuSaveStore() = default;

 public:
  SudokuSaveStore(const SudokuSaveStore&) = delete;
  SudokuSaveStore& operator=(const SudokuSaveStore&) = delete;

  static SudokuSaveStore& getInstance() { return instance; }

  bool hasSave() const { return valid; }

  const uint8_t* getGiven() const { return &given[0][0]; }
  const uint8_t* getBoard() const { return &board[0][0]; }
  int getCursorRow() const { return cursorRow; }
  int getCursorCol() const { return cursorCol; }
  uint32_t getElapsedMs() const { return elapsedMs; }

  void set(const uint8_t givenIn[SIZE][SIZE], const uint8_t boardIn[SIZE][SIZE], int cursorRowIn, int cursorColIn,
           uint32_t elapsedMsIn);

  void clear();

  bool saveToFile() const;
  bool loadFromFile();
  bool clearFile();
};

#define SUDOKU_SAVE SudokuSaveStore::getInstance()
