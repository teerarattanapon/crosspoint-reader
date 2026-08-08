#include "SudokuSaveStore.h"

#include <HalStorage.h>
#include <JsonSettingsIO.h>
#include <Logging.h>

#include <cstring>

namespace {
constexpr char SUDOKU_SAVE_FILE_JSON[] = "/.crosspoint/sudoku_save.json";
}  // namespace

SudokuSaveStore SudokuSaveStore::instance;

void SudokuSaveStore::set(const uint8_t givenIn[SIZE][SIZE], const uint8_t boardIn[SIZE][SIZE], int cursorRowIn,
                          int cursorColIn, uint32_t elapsedMsIn) {
  memcpy(given, givenIn, sizeof(given));
  memcpy(board, boardIn, sizeof(board));
  cursorRow = static_cast<int8_t>(cursorRowIn);
  cursorCol = static_cast<int8_t>(cursorColIn);
  elapsedMs = elapsedMsIn;
  valid = true;
}

void SudokuSaveStore::clear() {
  memset(given, 0, sizeof(given));
  memset(board, 0, sizeof(board));
  cursorRow = 0;
  cursorCol = 0;
  elapsedMs = 0;
  valid = false;
}

bool SudokuSaveStore::saveToFile() const {
  if (!valid) {
    if (Storage.exists(SUDOKU_SAVE_FILE_JSON)) {
      return Storage.remove(SUDOKU_SAVE_FILE_JSON);
    }
    return true;
  }
  Storage.mkdir("/.crosspoint");
  return JsonSettingsIO::saveSudokuSave(*this, SUDOKU_SAVE_FILE_JSON);
}

bool SudokuSaveStore::loadFromFile() {
  if (!Storage.exists(SUDOKU_SAVE_FILE_JSON)) {
    clear();
    return false;
  }
  String json = Storage.readFile(SUDOKU_SAVE_FILE_JSON);
  if (json.isEmpty()) {
    clear();
    return false;
  }
  return JsonSettingsIO::loadSudokuSave(*this, json.c_str());
}

bool SudokuSaveStore::clearFile() {
  clear();
  if (Storage.exists(SUDOKU_SAVE_FILE_JSON)) {
    return Storage.remove(SUDOKU_SAVE_FILE_JSON);
  }
  return true;
}
