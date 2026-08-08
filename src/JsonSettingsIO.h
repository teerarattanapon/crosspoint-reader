#pragma once

class CrossPointSettings;
class CrossPointState;
class WifiCredentialStore;
class KOReaderCredentialStore;
class RecentBooksStore;
class TretisScoreStore;
class TretisSaveStore;
class SudokuScoreStore;
class SudokuSaveStore;
class CatRunScoreStore;
class CatRunSaveStore;

namespace JsonSettingsIO {

// CrossPointSettings
bool saveSettings(const CrossPointSettings& s, const char* path);
bool loadSettings(CrossPointSettings& s, const char* json, bool* needsResave = nullptr);

// CrossPointState
bool saveState(const CrossPointState& s, const char* path);
bool loadState(CrossPointState& s, const char* json);

// WifiCredentialStore
bool saveWifi(const WifiCredentialStore& store, const char* path);
bool loadWifi(WifiCredentialStore& store, const char* json, bool* needsResave = nullptr);

// KOReaderCredentialStore
bool saveKOReader(const KOReaderCredentialStore& store, const char* path);
bool loadKOReader(KOReaderCredentialStore& store, const char* json, bool* needsResave = nullptr);

// RecentBooksStore
bool saveRecentBooks(const RecentBooksStore& store, const char* path);
bool loadRecentBooks(RecentBooksStore& store, const char* json);

// TretisScoreStore
bool saveTretisScores(const TretisScoreStore& store, const char* path);
bool loadTretisScores(TretisScoreStore& store, const char* json);

// TretisSaveStore
bool saveTretisSave(const TretisSaveStore& store, const char* path);
bool loadTretisSave(TretisSaveStore& store, const char* json);

// SudokuScoreStore
bool saveSudokuScores(const SudokuScoreStore& store, const char* path);
bool loadSudokuScores(SudokuScoreStore& store, const char* json);

// SudokuSaveStore
bool saveSudokuSave(const SudokuSaveStore& store, const char* path);
bool loadSudokuSave(SudokuSaveStore& store, const char* json);

// CatRunScoreStore
bool saveCatRunScores(const CatRunScoreStore& store, const char* path);
bool loadCatRunScores(CatRunScoreStore& store, const char* json);

// CatRunSaveStore
bool saveCatRunSave(const CatRunSaveStore& store, const char* path);
bool loadCatRunSave(CatRunSaveStore& store, const char* json);

}  // namespace JsonSettingsIO
