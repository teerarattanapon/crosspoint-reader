#include "JsonSettingsIO.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <ObfuscationUtils.h>

#include <cstring>
#include <cstdio>
#include <string>

#include "CatRunSaveStore.h"
#include "CatRunScoreStore.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "KOReaderCredentialStore.h"
#include "RecentBooksStore.h"
#include "SettingsList.h"
#include "SudokuSaveStore.h"
#include "SudokuScoreStore.h"
#include "TretisSaveStore.h"
#include "TretisScoreStore.h"
#include "WifiCredentialStore.h"

// Convert legacy settings.
void applyLegacyStatusBarSettings(CrossPointSettings& settings) {
  switch (static_cast<CrossPointSettings::STATUS_BAR_MODE>(settings.statusBar)) {
    case CrossPointSettings::NONE:
      settings.statusBarChapterPageCount = 0;
      settings.statusBarBookProgressPercentage = 0;
      settings.statusBarProgressBar = CrossPointSettings::HIDE_PROGRESS;
      settings.statusBarTitle = CrossPointSettings::HIDE_TITLE;
      settings.statusBarBattery = 0;
      break;
    case CrossPointSettings::NO_PROGRESS:
      settings.statusBarChapterPageCount = 0;
      settings.statusBarBookProgressPercentage = 0;
      settings.statusBarProgressBar = CrossPointSettings::HIDE_PROGRESS;
      settings.statusBarTitle = CrossPointSettings::CHAPTER_TITLE;
      settings.statusBarBattery = 1;
      break;
    case CrossPointSettings::BOOK_PROGRESS_BAR:
      settings.statusBarChapterPageCount = 1;
      settings.statusBarBookProgressPercentage = 0;
      settings.statusBarProgressBar = CrossPointSettings::BOOK_PROGRESS;
      settings.statusBarTitle = CrossPointSettings::CHAPTER_TITLE;
      settings.statusBarBattery = 1;
      break;
    case CrossPointSettings::ONLY_BOOK_PROGRESS_BAR:
      settings.statusBarChapterPageCount = 1;
      settings.statusBarBookProgressPercentage = 0;
      settings.statusBarProgressBar = CrossPointSettings::BOOK_PROGRESS;
      settings.statusBarTitle = CrossPointSettings::HIDE_TITLE;
      settings.statusBarBattery = 0;
      break;
    case CrossPointSettings::CHAPTER_PROGRESS_BAR:
      settings.statusBarChapterPageCount = 0;
      settings.statusBarBookProgressPercentage = 1;
      settings.statusBarProgressBar = CrossPointSettings::CHAPTER_PROGRESS;
      settings.statusBarTitle = CrossPointSettings::CHAPTER_TITLE;
      settings.statusBarBattery = 1;
      break;
    case CrossPointSettings::FULL:
    default:
      settings.statusBarChapterPageCount = 1;
      settings.statusBarBookProgressPercentage = 1;
      settings.statusBarProgressBar = CrossPointSettings::HIDE_PROGRESS;
      settings.statusBarTitle = CrossPointSettings::CHAPTER_TITLE;
      settings.statusBarBattery = 1;
      break;
  }
}

// ---- CrossPointState ----

bool JsonSettingsIO::saveState(const CrossPointState& s, const char* path) {
  JsonDocument doc;
  doc["openEpubPath"] = s.openEpubPath;
  doc["lastSleepImage"] = s.lastSleepImage;
  doc["readerActivityLoadCount"] = s.readerActivityLoadCount;
  doc["lastSleepFromReader"] = s.lastSleepFromReader;
  doc["lastSleepFromGame"] = s.lastSleepFromGame;
  doc["autoResumeTretis"] = s.autoResumeTretis;
  doc["autoResumeSudoku"] = s.autoResumeSudoku;
  doc["autoResumeCatRun"] = s.autoResumeCatRun;

  String json;
  serializeJson(doc, json);
  return Storage.writeFile(path, json);
}

bool JsonSettingsIO::loadState(CrossPointState& s, const char* json) {
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("CPS", "JSON parse error: %s", error.c_str());
    return false;
  }

  s.openEpubPath = doc["openEpubPath"] | std::string("");
  s.lastSleepImage = doc["lastSleepImage"] | (uint8_t)UINT8_MAX;
  s.readerActivityLoadCount = doc["readerActivityLoadCount"] | (uint8_t)0;
  s.lastSleepFromReader = doc["lastSleepFromReader"] | false;
  s.lastSleepFromGame = doc["lastSleepFromGame"] | false;
  s.autoResumeTretis = doc["autoResumeTretis"] | false;
  s.autoResumeSudoku = doc["autoResumeSudoku"] | false;
  s.autoResumeCatRun = doc["autoResumeCatRun"] | false;
  return true;
}

// ---- CrossPointSettings ----

bool JsonSettingsIO::saveSettings(const CrossPointSettings& s, const char* path) {
  JsonDocument doc;

  for (const auto& info : getSettingsList()) {
    if (!info.key) continue;
    // Dynamic entries (KOReader etc.) are stored in their own files — skip.
    if (!info.valuePtr && !info.stringOffset) continue;

    if (info.stringOffset) {
      const char* strPtr = (const char*)&s + info.stringOffset;
      if (info.obfuscated) {
        doc[std::string(info.key) + "_obf"] = obfuscation::obfuscateToBase64(strPtr);
      } else {
        doc[info.key] = strPtr;
      }
    } else {
      doc[info.key] = s.*(info.valuePtr);
    }
  }

  // Front button remap — managed by RemapFrontButtons sub-activity, not in SettingsList.
  doc["frontButtonBack"] = s.frontButtonBack;
  doc["frontButtonConfirm"] = s.frontButtonConfirm;
  doc["frontButtonLeft"] = s.frontButtonLeft;
  doc["frontButtonRight"] = s.frontButtonRight;

  // Lock screen fields (also exposed via web SettingsList under STR_LOCK_SCREEN).
  doc["lockPin_obf"] = obfuscation::obfuscateToBase64(s.lockPin);
  doc["lockMessageLine1"] = s.lockMessageLine1;
  doc["lockMessageLine2"] = s.lockMessageLine2;

  doc["settingsSchemaVersion"] = 4;

  String json;
  serializeJson(doc, json);
  return Storage.writeFile(path, json);
}

bool JsonSettingsIO::loadSettings(CrossPointSettings& s, const char* json, bool* needsResave) {
  if (needsResave) *needsResave = false;
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("CPS", "JSON parse error: %s", error.c_str());
    return false;
  }

  const uint32_t settingsSchemaVersion = doc["settingsSchemaVersion"] | 0;

  auto clamp = [](uint8_t val, uint8_t maxVal, uint8_t def) -> uint8_t { return val < maxVal ? val : def; };

  // Legacy migration: if statusBarChapterPageCount is absent this is a pre-refactor settings file.
  // Populate s with migrated values now so the generic loop below picks them up as defaults and clamps them.
  if (doc["statusBarChapterPageCount"].isNull()) {
    applyLegacyStatusBarSettings(s);
  }

  for (const auto& info : getSettingsList()) {
    if (!info.key) continue;
    // Dynamic entries (KOReader etc.) are stored in their own files — skip.
    if (!info.valuePtr && !info.stringOffset) continue;

    if (info.stringOffset) {
      const char* strPtr = (const char*)&s + info.stringOffset;
      const std::string fieldDefault = strPtr;  // current buffer = struct-initializer default
      std::string val;
      if (info.obfuscated) {
        bool ok = false;
        val = obfuscation::deobfuscateFromBase64(doc[std::string(info.key) + "_obf"] | "", &ok);
        if (!ok || val.empty()) {
          val = doc[info.key] | fieldDefault;
          if (val != fieldDefault && needsResave) *needsResave = true;
        }
      } else {
        val = doc[info.key] | fieldDefault;
      }
      char* destPtr = (char*)&s + info.stringOffset;
      if (info.stringMaxLen == 0) {
        LOG_ERR("CPS", "Misconfigured SettingInfo: stringMaxLen is 0 for key '%s'", info.key);
        destPtr[0] = '\0';
        if (needsResave) *needsResave = true;
        continue;
      }
      strncpy(destPtr, val.c_str(), info.stringMaxLen - 1);
      destPtr[info.stringMaxLen - 1] = '\0';
    } else {
      const uint8_t fieldDefault = s.*(info.valuePtr);  // struct-initializer default, read before we overwrite it
      uint8_t v = doc[info.key] | fieldDefault;
      if (info.type == SettingType::ENUM) {
        if (info.key && std::strcmp(info.key, "fontFamily") == 0) {
          if (settingsSchemaVersion >= 3) {
            v = clamp(v, (uint8_t)info.enumValues.size(), fieldDefault);
          } else {
            v = CrossPointSettings::normalizeFontFamilyStoredValue(v);
          }
        } else {
          v = clamp(v, (uint8_t)info.enumValues.size(), fieldDefault);
        }
      } else if (info.type == SettingType::TOGGLE) {
        v = clamp(v, (uint8_t)2, fieldDefault);
      } else if (info.type == SettingType::VALUE) {
        if (v < info.valueRange.min)
          v = info.valueRange.min;
        else if (v > info.valueRange.max)
          v = info.valueRange.max;
      }
      s.*(info.valuePtr) = v;
    }
  }

  // Front button remap — managed by RemapFrontButtons sub-activity, not in SettingsList.
  using S = CrossPointSettings;
  s.frontButtonBack =
      clamp(doc["frontButtonBack"] | (uint8_t)S::FRONT_HW_BACK, S::FRONT_BUTTON_HARDWARE_COUNT, S::FRONT_HW_BACK);
  s.frontButtonConfirm = clamp(doc["frontButtonConfirm"] | (uint8_t)S::FRONT_HW_CONFIRM, S::FRONT_BUTTON_HARDWARE_COUNT,
                               S::FRONT_HW_CONFIRM);
  s.frontButtonLeft =
      clamp(doc["frontButtonLeft"] | (uint8_t)S::FRONT_HW_LEFT, S::FRONT_BUTTON_HARDWARE_COUNT, S::FRONT_HW_LEFT);
  s.frontButtonRight =
      clamp(doc["frontButtonRight"] | (uint8_t)S::FRONT_HW_RIGHT, S::FRONT_BUTTON_HARDWARE_COUNT, S::FRONT_HW_RIGHT);
  CrossPointSettings::validateFrontButtonMapping(s);

  if (!doc["fontFamily"].isNull() && settingsSchemaVersion < 2) {
    const uint8_t raw = static_cast<uint8_t>(doc["fontFamily"].as<unsigned int>());
    s.fontFamily = CrossPointSettings::migrateFontFamilyFromLegacy(raw);
    if (needsResave) *needsResave = true;
  }

  if (needsResave != nullptr && settingsSchemaVersion < 3) {
    *needsResave = true;
  }

  {
    bool pinOk = false;
    const std::string pinVal = obfuscation::deobfuscateFromBase64(doc["lockPin_obf"] | "", &pinOk);
    if (pinOk && !pinVal.empty()) {
      strncpy(s.lockPin, pinVal.c_str(), sizeof(s.lockPin) - 1);
      s.lockPin[sizeof(s.lockPin) - 1] = '\0';
    }
    const std::string line1Default = s.lockMessageLine1;
    const std::string line2Default = s.lockMessageLine2;
    const std::string line1 = doc["lockMessageLine1"] | line1Default;
    const std::string line2 = doc["lockMessageLine2"] | line2Default;
    strncpy(s.lockMessageLine1, line1.c_str(), sizeof(s.lockMessageLine1) - 1);
    s.lockMessageLine1[sizeof(s.lockMessageLine1) - 1] = '\0';
    strncpy(s.lockMessageLine2, line2.c_str(), sizeof(s.lockMessageLine2) - 1);
    s.lockMessageLine2[sizeof(s.lockMessageLine2) - 1] = '\0';
  }

  if (needsResave != nullptr && settingsSchemaVersion < 4) {
    *needsResave = true;
  }

  LOG_DBG("CPS", "Settings loaded from file");

  return true;
}

// ---- KOReaderCredentialStore ----

bool JsonSettingsIO::saveKOReader(const KOReaderCredentialStore& store, const char* path) {
  JsonDocument doc;
  doc["username"] = store.getUsername();
  doc["password_obf"] = obfuscation::obfuscateToBase64(store.getPassword());
  doc["serverUrl"] = store.getServerUrl();
  doc["matchMethod"] = static_cast<uint8_t>(store.getMatchMethod());

  String json;
  serializeJson(doc, json);
  return Storage.writeFile(path, json);
}

bool JsonSettingsIO::loadKOReader(KOReaderCredentialStore& store, const char* json, bool* needsResave) {
  if (needsResave) *needsResave = false;
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("KRS", "JSON parse error: %s", error.c_str());
    return false;
  }

  store.username = doc["username"] | std::string("");
  bool ok = false;
  store.password = obfuscation::deobfuscateFromBase64(doc["password_obf"] | "", &ok);
  if (!ok || store.password.empty()) {
    store.password = doc["password"] | std::string("");
    if (!store.password.empty() && needsResave) *needsResave = true;
  }
  store.serverUrl = doc["serverUrl"] | std::string("");
  uint8_t method = doc["matchMethod"] | (uint8_t)0;
  store.matchMethod = static_cast<DocumentMatchMethod>(method);

  LOG_DBG("KRS", "Loaded KOReader credentials for user: %s", store.username.c_str());
  return true;
}

// ---- WifiCredentialStore ----

bool JsonSettingsIO::saveWifi(const WifiCredentialStore& store, const char* path) {
  JsonDocument doc;
  doc["lastConnectedSsid"] = store.getLastConnectedSsid();

  JsonArray arr = doc["credentials"].to<JsonArray>();
  for (const auto& cred : store.getCredentials()) {
    JsonObject obj = arr.add<JsonObject>();
    obj["ssid"] = cred.ssid;
    obj["password_obf"] = obfuscation::obfuscateToBase64(cred.password);
  }

  String json;
  serializeJson(doc, json);
  return Storage.writeFile(path, json);
}

bool JsonSettingsIO::loadWifi(WifiCredentialStore& store, const char* json, bool* needsResave) {
  if (needsResave) *needsResave = false;
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("WCS", "JSON parse error: %s", error.c_str());
    return false;
  }

  store.lastConnectedSsid = doc["lastConnectedSsid"] | std::string("");

  store.credentials.clear();
  JsonArray arr = doc["credentials"].as<JsonArray>();
  for (JsonObject obj : arr) {
    if (store.credentials.size() >= store.MAX_NETWORKS) break;
    WifiCredential cred;
    cred.ssid = obj["ssid"] | std::string("");
    bool ok = false;
    cred.password = obfuscation::deobfuscateFromBase64(obj["password_obf"] | "", &ok);
    if (!ok || cred.password.empty()) {
      cred.password = obj["password"] | std::string("");
      if (!cred.password.empty() && needsResave) *needsResave = true;
    }
    store.credentials.push_back(cred);
  }

  LOG_DBG("WCS", "Loaded %zu WiFi credentials from file", store.credentials.size());
  return true;
}

// ---- RecentBooksStore ----

bool JsonSettingsIO::saveRecentBooks(const RecentBooksStore& store, const char* path) {
  JsonDocument doc;
  JsonArray arr = doc["books"].to<JsonArray>();
  for (const auto& book : store.getBooks()) {
    JsonObject obj = arr.add<JsonObject>();
    obj["path"] = book.path;
    obj["title"] = book.title;
    obj["author"] = book.author;
    obj["coverBmpPath"] = book.coverBmpPath;
  }

  String json;
  serializeJson(doc, json);
  return Storage.writeFile(path, json);
}

bool JsonSettingsIO::loadRecentBooks(RecentBooksStore& store, const char* json) {
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("RBS", "JSON parse error: %s", error.c_str());
    return false;
  }

  store.recentBooks.clear();
  JsonArray arr = doc["books"].as<JsonArray>();
  for (JsonObject obj : arr) {
    if (store.getCount() >= RecentBooksStore::MAX_RECENT_BOOKS) break;
    RecentBook book;
    book.path = obj["path"] | std::string("");
    book.title = obj["title"] | std::string("");
    book.author = obj["author"] | std::string("");
    book.coverBmpPath = obj["coverBmpPath"] | std::string("");
    store.recentBooks.push_back(book);
  }

  LOG_DBG("RBS", "Recent books loaded from file (%d entries)", store.getCount());
  return true;
}

// ---- TretisScoreStore ----

bool JsonSettingsIO::saveTretisScores(const TretisScoreStore& store, const char* path) {
  JsonDocument doc;
  JsonArray arr = doc["scores"].to<JsonArray>();
  for (int i = 0; i < store.getCount(); ++i) {
    const TretisScoreEntry& e = store.getEntry(i);
    JsonObject obj = arr.add<JsonObject>();
    obj["score"] = e.score;
    obj["durationSec"] = e.durationSec;
  }

  String json;
  serializeJson(doc, json);
  return Storage.writeFile(path, json);
}

bool JsonSettingsIO::loadTretisScores(TretisScoreStore& store, const char* json) {
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("TSS", "JSON parse error: %s", error.c_str());
    return false;
  }

  store.count = 0;
  JsonArray arr = doc["scores"].as<JsonArray>();
  for (JsonObject obj : arr) {
    if (store.count >= TretisScoreStore::MAX) {
      break;
    }
    store.entries[store.count].score = obj["score"] | (uint32_t)0;
    store.entries[store.count].durationSec = obj["durationSec"] | (uint32_t)0;
    ++store.count;
  }

  LOG_DBG("TSS", "TRETIS scores loaded (%d entries)", store.getCount());
  return true;
}

// ---- TretisSaveStore ----

bool JsonSettingsIO::saveTretisSave(const TretisSaveStore& store, const char* path) {
  JsonDocument doc;
  doc["valid"] = store.valid;
  doc["pieceType"] = store.pieceType;
  doc["nextPieceType"] = store.nextPieceType;
  doc["rotation"] = store.rotation;
  doc["pieceX"] = store.pieceX;
  doc["pieceY"] = store.pieceY;
  doc["score"] = store.score;
  doc["lines"] = store.lines;
  doc["level"] = store.level;
  doc["elapsedMs"] = store.elapsedMs;

  // Compact board: hex string of 160 nibbles (one digit per cell 0-7) — 160 chars, no heap array growth.
  char boardHex[TretisSaveStore::ROWS * TretisSaveStore::COLS + 1];
  size_t idx = 0;
  for (int r = 0; r < TretisSaveStore::ROWS; ++r) {
    for (int c = 0; c < TretisSaveStore::COLS; ++c) {
      boardHex[idx++] = static_cast<char>('0' + (store.board[r][c] & 0x0F));
    }
  }
  boardHex[idx] = '\0';
  doc["board"] = boardHex;

  String json;
  serializeJson(doc, json);
  return Storage.writeFile(path, json);
}

bool JsonSettingsIO::loadTretisSave(TretisSaveStore& store, const char* json) {
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("TSV", "JSON parse error: %s", error.c_str());
    store.clear();
    return false;
  }

  store.valid = doc["valid"] | false;
  if (!store.valid) {
    store.clear();
    return true;
  }

  store.pieceType = doc["pieceType"] | (int8_t)0;
  store.nextPieceType = doc["nextPieceType"] | (int8_t)0;
  store.rotation = doc["rotation"] | (int8_t)0;
  store.pieceX = doc["pieceX"] | (int8_t)0;
  store.pieceY = doc["pieceY"] | (int8_t)0;
  store.score = doc["score"] | (int32_t)0;
  store.lines = doc["lines"] | (int32_t)0;
  store.level = doc["level"] | (int32_t)1;
  store.elapsedMs = doc["elapsedMs"] | (uint32_t)0;

  const char* boardHex = doc["board"] | "";
  const size_t expected = static_cast<size_t>(TretisSaveStore::ROWS * TretisSaveStore::COLS);
  if (strlen(boardHex) != expected) {
    LOG_ERR("TSV", "Invalid board length: %u", static_cast<unsigned>(strlen(boardHex)));
    store.clear();
    return false;
  }

  size_t idx = 0;
  for (int r = 0; r < TretisSaveStore::ROWS; ++r) {
    for (int c = 0; c < TretisSaveStore::COLS; ++c) {
      const char ch = boardHex[idx++];
      if (ch < '0' || ch > '7') {
        LOG_ERR("TSV", "Invalid board cell at %u", static_cast<unsigned>(idx - 1));
        store.clear();
        return false;
      }
      store.board[r][c] = static_cast<uint8_t>(ch - '0');
    }
  }

  // Validate piece indices
  if (store.pieceType < 0 || store.pieceType > 6 || store.nextPieceType < 0 || store.nextPieceType > 6 ||
      store.rotation < 0 || store.rotation > 3) {
    LOG_ERR("TSV", "Invalid piece state");
    store.clear();
    return false;
  }

  LOG_DBG("TSV", "TRETIS save loaded (score=%d level=%d)", store.score, store.level);
  return true;
}

// ---- SudokuScoreStore ----

bool JsonSettingsIO::saveSudokuScores(const SudokuScoreStore& store, const char* path) {
  JsonDocument doc;
  JsonArray arr = doc["scores"].to<JsonArray>();
  for (int i = 0; i < store.getCount(); ++i) {
    const SudokuScoreEntry& e = store.getEntry(i);
    JsonObject obj = arr.add<JsonObject>();
    obj["durationSec"] = e.durationSec;
  }

  String json;
  serializeJson(doc, json);
  return Storage.writeFile(path, json);
}

bool JsonSettingsIO::loadSudokuScores(SudokuScoreStore& store, const char* json) {
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("SDS", "JSON parse error: %s", error.c_str());
    return false;
  }

  store.count = 0;
  JsonArray arr = doc["scores"].as<JsonArray>();
  for (JsonObject obj : arr) {
    if (store.count >= SudokuScoreStore::MAX) {
      break;
    }
    store.entries[store.count].durationSec = obj["durationSec"] | (uint32_t)0;
    ++store.count;
  }

  LOG_DBG("SDS", "Sudoku scores loaded (%d entries)", store.getCount());
  return true;
}

// ---- SudokuSaveStore ----

bool JsonSettingsIO::saveSudokuSave(const SudokuSaveStore& store, const char* path) {
  JsonDocument doc;
  doc["valid"] = store.valid;
  doc["cursorRow"] = store.cursorRow;
  doc["cursorCol"] = store.cursorCol;
  doc["elapsedMs"] = store.elapsedMs;

  // Compact grids: 81 digit chars each (0-9).
  char givenDigits[SudokuSaveStore::SIZE * SudokuSaveStore::SIZE + 1];
  char boardDigits[SudokuSaveStore::SIZE * SudokuSaveStore::SIZE + 1];
  size_t idx = 0;
  for (int r = 0; r < SudokuSaveStore::SIZE; ++r) {
    for (int c = 0; c < SudokuSaveStore::SIZE; ++c) {
      givenDigits[idx] = static_cast<char>('0' + (store.given[r][c] % 10));
      boardDigits[idx] = static_cast<char>('0' + (store.board[r][c] % 10));
      ++idx;
    }
  }
  givenDigits[idx] = '\0';
  boardDigits[idx] = '\0';
  doc["given"] = givenDigits;
  doc["board"] = boardDigits;

  String json;
  serializeJson(doc, json);
  return Storage.writeFile(path, json);
}

bool JsonSettingsIO::loadSudokuSave(SudokuSaveStore& store, const char* json) {
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("SDV", "JSON parse error: %s", error.c_str());
    store.clear();
    return false;
  }

  store.valid = doc["valid"] | false;
  if (!store.valid) {
    store.clear();
    return true;
  }

  store.cursorRow = doc["cursorRow"] | (int8_t)0;
  store.cursorCol = doc["cursorCol"] | (int8_t)0;
  store.elapsedMs = doc["elapsedMs"] | (uint32_t)0;

  const char* givenDigits = doc["given"] | "";
  const char* boardDigits = doc["board"] | "";
  const size_t expected = static_cast<size_t>(SudokuSaveStore::SIZE * SudokuSaveStore::SIZE);
  if (strlen(givenDigits) != expected || strlen(boardDigits) != expected) {
    LOG_ERR("SDV", "Invalid grid length");
    store.clear();
    return false;
  }

  size_t idx = 0;
  for (int r = 0; r < SudokuSaveStore::SIZE; ++r) {
    for (int c = 0; c < SudokuSaveStore::SIZE; ++c) {
      const char gch = givenDigits[idx];
      const char bch = boardDigits[idx];
      ++idx;
      if (gch < '0' || gch > '9' || bch < '0' || bch > '9') {
        LOG_ERR("SDV", "Invalid grid cell at %u", static_cast<unsigned>(idx - 1));
        store.clear();
        return false;
      }
      store.given[r][c] = static_cast<uint8_t>(gch - '0');
      store.board[r][c] = static_cast<uint8_t>(bch - '0');
    }
  }

  if (store.cursorRow < 0 || store.cursorRow >= SudokuSaveStore::SIZE || store.cursorCol < 0 ||
      store.cursorCol >= SudokuSaveStore::SIZE) {
    LOG_ERR("SDV", "Invalid cursor");
    store.clear();
    return false;
  }

  LOG_DBG("SDV", "Sudoku save loaded");
  return true;
}

// ---- CatRunScoreStore ----

bool JsonSettingsIO::saveCatRunScores(const CatRunScoreStore& store, const char* path) {
  JsonDocument doc;
  JsonArray arr = doc["scores"].to<JsonArray>();
  for (int i = 0; i < store.getCount(); ++i) {
    const CatRunScoreEntry& e = store.getEntry(i);
    JsonObject obj = arr.add<JsonObject>();
    obj["score"] = e.score;
    obj["durationSec"] = e.durationSec;
    obj["retries"] = e.retries;
  }

  String json;
  serializeJson(doc, json);
  return Storage.writeFile(path, json);
}

bool JsonSettingsIO::loadCatRunScores(CatRunScoreStore& store, const char* json) {
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("CRS", "JSON parse error: %s", error.c_str());
    return false;
  }

  store.count = 0;
  JsonArray arr = doc["scores"].as<JsonArray>();
  for (JsonObject obj : arr) {
    if (store.count >= CatRunScoreStore::MAX) {
      break;
    }
    store.entries[store.count].score = obj["score"] | (uint32_t)0;
    store.entries[store.count].durationSec = obj["durationSec"] | (uint32_t)0;
    store.entries[store.count].retries = obj["retries"] | (uint8_t)0;
    ++store.count;
  }

  LOG_DBG("CRS", "Cat Run scores loaded (%d entries)", store.getCount());
  return true;
}

// ---- CatRunSaveStore ----

bool JsonSettingsIO::saveCatRunSave(const CatRunSaveStore& store, const char* path) {
  JsonDocument doc;
  doc["valid"] = store.valid;
  doc["catColor"] = store.catColor;
  doc["retriesUsed"] = store.retriesUsed;
  doc["jumpPhase"] = store.jumpPhase;
  doc["walkFrame"] = store.walkFrame;
  doc["ducking"] = store.ducking;
  doc["score"] = store.score;
  doc["elapsedMs"] = store.elapsedMs;
  doc["cameraX"] = store.cameraX;
  doc["nextSpawnX"] = store.nextSpawnX;
  doc["rngState"] = store.rngState;

  // Compact entities: kind(2) worldX(8) worldY(4) w(2) h(2) solid(1) active(1) = 20 chars × 10.
  char ents[CatRunSaveStore::MAX_ENTITIES * 20 + 1];
  size_t idx = 0;
  for (int i = 0; i < CatRunSaveStore::MAX_ENTITIES; ++i) {
    const auto& e = store.entities[i];
    snprintf(ents + idx, 21, "%02X%08lX%04hX%02X%02X%c%c", static_cast<unsigned>(e.kind) & 0xFF,
             static_cast<unsigned long>(static_cast<uint32_t>(e.worldX)), static_cast<unsigned short>(e.worldY),
             static_cast<unsigned>(e.w) & 0xFF, static_cast<unsigned>(e.h) & 0xFF, e.solid ? '1' : '0',
             e.active ? '1' : '0');
    idx += 20;
  }
  ents[idx] = '\0';
  doc["ents"] = ents;

  String json;
  serializeJson(doc, json);
  return Storage.writeFile(path, json);
}

bool JsonSettingsIO::loadCatRunSave(CatRunSaveStore& store, const char* json) {
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("CRV", "JSON parse error: %s", error.c_str());
    store.clear();
    return false;
  }

  store.valid = doc["valid"] | false;
  if (!store.valid) {
    store.clear();
    return true;
  }

  store.catColor = doc["catColor"] | (uint8_t)0;
  store.retriesUsed = doc["retriesUsed"] | (uint8_t)0;
  store.jumpPhase = doc["jumpPhase"] | (uint8_t)0;
  store.walkFrame = doc["walkFrame"] | (uint8_t)0;
  store.ducking = doc["ducking"] | false;
  store.score = doc["score"] | (int32_t)0;
  store.elapsedMs = doc["elapsedMs"] | (uint32_t)0;
  store.cameraX = doc["cameraX"] | (int32_t)0;
  store.nextSpawnX = doc["nextSpawnX"] | (int32_t)0;
  store.rngState = doc["rngState"] | (uint32_t)1;

  const char* ents = doc["ents"] | "";
  const size_t expected = static_cast<size_t>(CatRunSaveStore::MAX_ENTITIES * 20);
  if (strlen(ents) != expected) {
    LOG_ERR("CRV", "Invalid ents length: %u", static_cast<unsigned>(strlen(ents)));
    store.clear();
    return false;
  }

  for (int i = 0; i < CatRunSaveStore::MAX_ENTITIES; ++i) {
    const char* p = ents + i * 20;
    unsigned kind = 0, w = 0, h = 0;
    unsigned long wx = 0;
    unsigned short wy = 0;
    char solidCh = '0';
    char activeCh = '0';
    if (sscanf(p, "%02X%08lX%04hX%02X%02X%c%c", &kind, &wx, &wy, &w, &h, &solidCh, &activeCh) != 7) {
      LOG_ERR("CRV", "Invalid entity at %d", i);
      store.clear();
      return false;
    }
    if (kind >= static_cast<unsigned>(CatRunSaveStore::EntityKind::Count)) {
      LOG_ERR("CRV", "Invalid entity kind %u", kind);
      store.clear();
      return false;
    }
    store.entities[i].kind = static_cast<CatRunSaveStore::EntityKind>(kind);
    store.entities[i].worldX = static_cast<int32_t>(wx);
    store.entities[i].worldY = static_cast<int16_t>(wy);
    store.entities[i].w = static_cast<uint8_t>(w);
    store.entities[i].h = static_cast<uint8_t>(h);
    store.entities[i].solid = solidCh == '1';
    store.entities[i].active = activeCh == '1';
  }

  LOG_DBG("CRV", "Cat Run save loaded (score=%d)", store.score);
  return true;
}
