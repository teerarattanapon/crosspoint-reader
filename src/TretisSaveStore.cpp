#include "TretisSaveStore.h"

#include <HalStorage.h>
#include <JsonSettingsIO.h>
#include <Logging.h>

#include <cstring>

namespace {
constexpr char TRETIS_SAVE_FILE_JSON[] = "/.crosspoint/tretis_save.json";
}  // namespace

TretisSaveStore TretisSaveStore::instance;

void TretisSaveStore::set(const uint8_t boardIn[ROWS][COLS], int pieceTypeIn, int nextPieceTypeIn, int rotationIn,
                          int pieceXIn, int pieceYIn, int scoreIn, int linesIn, int levelIn, uint32_t elapsedMsIn) {
  memcpy(board, boardIn, sizeof(board));
  pieceType = static_cast<int8_t>(pieceTypeIn);
  nextPieceType = static_cast<int8_t>(nextPieceTypeIn);
  rotation = static_cast<int8_t>(rotationIn);
  pieceX = static_cast<int8_t>(pieceXIn);
  pieceY = static_cast<int8_t>(pieceYIn);
  score = scoreIn;
  lines = linesIn;
  level = levelIn;
  elapsedMs = elapsedMsIn;
  valid = true;
}

void TretisSaveStore::clear() {
  memset(board, 0, sizeof(board));
  pieceType = 0;
  nextPieceType = 0;
  rotation = 0;
  pieceX = 0;
  pieceY = 0;
  score = 0;
  lines = 0;
  level = 1;
  elapsedMs = 0;
  valid = false;
}

bool TretisSaveStore::saveToFile() const {
  if (!valid) {
    if (Storage.exists(TRETIS_SAVE_FILE_JSON)) {
      return Storage.remove(TRETIS_SAVE_FILE_JSON);
    }
    return true;
  }
  Storage.mkdir("/.crosspoint");
  return JsonSettingsIO::saveTretisSave(*this, TRETIS_SAVE_FILE_JSON);
}

bool TretisSaveStore::loadFromFile() {
  if (!Storage.exists(TRETIS_SAVE_FILE_JSON)) {
    clear();
    return false;
  }
  String json = Storage.readFile(TRETIS_SAVE_FILE_JSON);
  if (json.isEmpty()) {
    clear();
    return false;
  }
  return JsonSettingsIO::loadTretisSave(*this, json.c_str());
}

bool TretisSaveStore::clearFile() {
  clear();
  if (Storage.exists(TRETIS_SAVE_FILE_JSON)) {
    return Storage.remove(TRETIS_SAVE_FILE_JSON);
  }
  return true;
}
