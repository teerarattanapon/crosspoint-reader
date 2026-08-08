#pragma once

#include <cstddef>
#include <cstdint>

class TretisSaveStore;
namespace JsonSettingsIO {
bool saveTretisSave(const TretisSaveStore& store, const char* path);
bool loadTretisSave(TretisSaveStore& store, const char* json);
}  // namespace JsonSettingsIO

/**
 * Single in-progress TRETIS game snapshot (fixed-size, no heap).
 * Survives pause (Back) and deep sleep via SPIFFS JSON.
 */
class TretisSaveStore {
  static TretisSaveStore instance;

  friend bool JsonSettingsIO::saveTretisSave(const TretisSaveStore&, const char*);
  friend bool JsonSettingsIO::loadTretisSave(TretisSaveStore&, const char*);

 public:
  static constexpr int COLS = 10;
  static constexpr int ROWS = 16;

 private:
  bool valid = false;
  uint8_t board[ROWS][COLS] = {};
  int8_t pieceType = 0;
  int8_t nextPieceType = 0;
  int8_t rotation = 0;
  int8_t pieceX = 0;
  int8_t pieceY = 0;
  int32_t score = 0;
  int32_t lines = 0;
  int32_t level = 1;
  uint32_t elapsedMs = 0;

  TretisSaveStore() = default;

 public:
  TretisSaveStore(const TretisSaveStore&) = delete;
  TretisSaveStore& operator=(const TretisSaveStore&) = delete;

  static TretisSaveStore& getInstance() { return instance; }

  bool hasSave() const { return valid; }

  const uint8_t* getBoard() const { return &board[0][0]; }
  int getPieceType() const { return pieceType; }
  int getNextPieceType() const { return nextPieceType; }
  int getRotation() const { return rotation; }
  int getPieceX() const { return pieceX; }
  int getPieceY() const { return pieceY; }
  int getScore() const { return score; }
  int getLines() const { return lines; }
  int getLevel() const { return level; }
  uint32_t getElapsedMs() const { return elapsedMs; }

  void set(const uint8_t boardIn[ROWS][COLS], int pieceTypeIn, int nextPieceTypeIn, int rotationIn, int pieceXIn,
           int pieceYIn, int scoreIn, int linesIn, int levelIn, uint32_t elapsedMsIn);

  void clear();

  bool saveToFile() const;
  bool loadFromFile();
  bool clearFile();
};

#define TRETIS_SAVE TretisSaveStore::getInstance()
