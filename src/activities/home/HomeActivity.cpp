#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Utf8.h>
#include <Xtc.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int kCatPoseMenuCols = 2;

uint8_t clampPercentU8(int percent) {
  if (percent < 0) {
    return 0;
  }
  if (percent > 100) {
    return 100;
  }
  return static_cast<uint8_t>(percent);
}
}  // namespace

bool HomeActivity::isCatPose() const { return SETTINGS.uiTheme == CrossPointSettings::UI_THEME::CAT_POSE; }

bool HomeActivity::isLyra3() const { return SETTINGS.uiTheme == CrossPointSettings::UI_THEME::LYRA_3_COVERS; }

bool HomeActivity::isCarouselTheme() const { return isLyra3() || isCatPose(); }

int HomeActivity::getCatPoseViewAllIndex() const { return static_cast<int>(recentBooks.size()); }

int HomeActivity::getCatPoseMenuStart() const { return getCatPoseViewAllIndex() + 1; }

int HomeActivity::getCatPoseMenuCount() const {
  int count = 4;  // Browse, Transfer, Games, Settings
  if (hasOpdsUrl) {
    count++;
  }
  return count;
}

int HomeActivity::getMenuItemCount() const {
  if (isCatPose()) {
    return getCatPoseMenuStart() + getCatPoseMenuCount();
  }

  int count = 5;  // File Browser, Recents, File transfer, Games, Settings
  if (!recentBooks.empty()) {
    count += static_cast<int>(recentBooks.size());
  }
  if (hasOpdsUrl) {
    count++;
  }
  return count;
}

void HomeActivity::backfillProgressIfNeeded(RecentBook& book) {
  if (book.progressPercent > 0) {
    return;
  }

  uint8_t percent = 0;
  if (FsHelpers::hasEpubExtension(book.path)) {
    Epub epub(book.path, "/.crosspoint");
    if (!epub.load(false, true)) {
      return;
    }
    FsFile f;
    if (!Storage.openFileForRead("HOME", epub.getCachePath() + "/progress.bin", f)) {
      return;
    }
    uint8_t data[6];
    const int dataSize = f.read(data, 6);
    f.close();
    if (dataSize < 4) {
      return;
    }
    const int spineIndex = data[0] | (data[1] << 8);
    const int page = data[2] | (data[3] << 8);
    const int pageCount = (dataSize >= 6) ? (data[4] | (data[5] << 8)) : 0;
    const float chapterProg =
        (pageCount > 0) ? (static_cast<float>(page) / static_cast<float>(pageCount)) : 0.0f;
    percent = clampPercentU8(static_cast<int>(epub.calculateProgress(spineIndex, chapterProg) * 100.0f + 0.5f));
  } else if (FsHelpers::hasXtcExtension(book.path)) {
    Xtc xtc(book.path, "/.crosspoint");
    if (!xtc.load()) {
      return;
    }
    FsFile f;
    if (!Storage.openFileForRead("HOME", xtc.getCachePath() + "/progress.bin", f)) {
      return;
    }
    uint8_t data[4];
    if (f.read(data, 4) != 4) {
      f.close();
      return;
    }
    f.close();
    const uint32_t page = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
    percent = xtc.calculateProgress(page);
  } else {
    return;
  }

  if (percent == 0) {
    return;
  }
  book.progressPercent = percent;
  RECENT_BOOKS.updateProgress(book.path, percent);
}

void HomeActivity::loadRecentBooks(int maxBooks) {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(static_cast<int>(books.size()), maxBooks));

  for (const RecentBook& book : books) {
    if (static_cast<int>(recentBooks.size()) >= maxBooks) {
      break;
    }
    if (!Storage.exists(book.path.c_str())) {
      continue;
    }
    recentBooks.push_back(book);
  }
}

void HomeActivity::loadRecentCovers(int coverHeight) {
  recentsLoading = true;
  bool showingLoading = false;
  Rect popupRect;

  int progress = 0;
  for (RecentBook& book : recentBooks) {
    if (isCatPose()) {
      backfillProgressIfNeeded(book);
    }

    if (!book.coverBmpPath.empty()) {
      std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
      if (!Storage.exists(coverPath.c_str())) {
        if (FsHelpers::hasEpubExtension(book.path)) {
          Epub epub(book.path, "/.crosspoint");
          epub.load(false, true);

          if (!showingLoading) {
            showingLoading = true;
            popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
          }
          GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / std::max(1, static_cast<int>(recentBooks.size()))));
          bool success = epub.generateThumbBmp(coverHeight);
          if (!success) {
            RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
            book.coverBmpPath = "";
          }
          coverRendered = false;
          requestUpdate();
        } else if (FsHelpers::hasXtcExtension(book.path)) {
          Xtc xtc(book.path, "/.crosspoint");
          if (xtc.load()) {
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect,
                                  10 + progress * (90 / std::max(1, static_cast<int>(recentBooks.size()))));
            bool success = xtc.generateThumbBmp(coverHeight);
            if (!success) {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
              book.coverBmpPath = "";
            }
            coverRendered = false;
            requestUpdate();
          }
        }
      }
    }
    progress++;
  }

  recentsLoaded = true;
  recentsLoading = false;
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  hasOpdsUrl = strlen(SETTINGS.opdsServerUrl) > 0;

  selectorIndex = 0;
  carouselFocus = 0;
  ignoreConfirmRelease = true;

  const auto& metrics = UITheme::getInstance().getMetrics();
  loadRecentBooks(metrics.homeRecentBooksCount);

  // CatPose with no recents: start on View All
  if (isCatPose() && recentBooks.empty()) {
    selectorIndex = getCatPoseViewAllIndex();
  }

  requestUpdate();
}

void HomeActivity::onExit() {
  Activity::onExit();
  freeCoverBuffer();
}

bool HomeActivity::storeCoverBuffer() {
  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  freeCoverBuffer();

  const size_t bufferSize = renderer.getBufferSize();
  coverBuffer = static_cast<uint8_t*>(malloc(bufferSize));
  if (!coverBuffer) {
    return false;
  }

  memcpy(coverBuffer, frameBuffer, bufferSize);
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer) {
    return false;
  }

  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  const size_t bufferSize = renderer.getBufferSize();
  memcpy(frameBuffer, coverBuffer, bufferSize);
  return true;
}

void HomeActivity::freeCoverBuffer() {
  if (coverBuffer) {
    free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferStored = false;
}

void HomeActivity::loop() {
  const int menuCount = getMenuItemCount();
  const int recentCount = static_cast<int>(recentBooks.size());

  if (isCatPose()) {
    const int viewAllIdx = getCatPoseViewAllIndex();
    const int menuStart = getCatPoseMenuStart();
    const int menuItemCount = getCatPoseMenuCount();

    if (selectorIndex < recentCount) {
      carouselFocus = selectorIndex;
      buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this] {
        if (selectorIndex > 0) {
          selectorIndex--;
          carouselFocus = selectorIndex;
          coverRendered = false;  // layout focus changed — redecode covers
          requestUpdate();
        }
      });
      buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this, recentCount, viewAllIdx] {
        if (selectorIndex < recentCount - 1) {
          selectorIndex++;
          carouselFocus = selectorIndex;
          coverRendered = false;  // layout focus changed — redecode covers
        } else {
          // View All: cover layout focus unchanged (still carouselFocus)
          selectorIndex = viewAllIdx;
        }
        requestUpdate();
      });
      buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up}, [this, viewAllIdx] {
        selectorIndex = viewAllIdx;
        requestUpdate();
      });
      buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down}, [this, menuStart] {
        selectorIndex = menuStart;
        requestUpdate();
      });
    } else if (selectorIndex == viewAllIdx) {
      buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this, recentCount] {
        if (recentCount > 0) {
          selectorIndex = std::clamp(carouselFocus, 0, recentCount - 1);
          requestUpdate();
        }
      });
      buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down}, [this, recentCount] {
        if (recentCount > 0) {
          selectorIndex = std::clamp(carouselFocus, 0, recentCount - 1);
        } else {
          selectorIndex = getCatPoseMenuStart();
        }
        requestUpdate();
      });
    } else {
      // 2-column menu grid — cover strip layout unchanged; keep cover cache
      buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this, menuStart, menuItemCount] {
        const int menuIdx = selectorIndex - menuStart;
        if (menuIdx % kCatPoseMenuCols == 1) {
          selectorIndex = menuStart + menuIdx - 1;
          requestUpdate();
        }
      });
      buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this, menuStart, menuItemCount] {
        const int menuIdx = selectorIndex - menuStart;
        if ((menuIdx % kCatPoseMenuCols == 0) && (menuIdx + 1 < menuItemCount)) {
          selectorIndex = menuStart + menuIdx + 1;
          requestUpdate();
        }
      });
      buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up}, [this, menuStart] {
        const int menuIdx = selectorIndex - menuStart;
        if (menuIdx >= kCatPoseMenuCols) {
          selectorIndex = menuStart + menuIdx - kCatPoseMenuCols;
        } else {
          selectorIndex = getCatPoseViewAllIndex();
        }
        requestUpdate();
      });
      buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down}, [this, menuStart, menuItemCount] {
        const int menuIdx = selectorIndex - menuStart;
        if (menuIdx + kCatPoseMenuCols < menuItemCount) {
          selectorIndex = menuStart + menuIdx + kCatPoseMenuCols;
          requestUpdate();
        }
      });
    }
  } else if (isLyra3() && recentCount > 0) {
    if (selectorIndex < recentCount) {
      carouselFocus = selectorIndex;
      buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this] {
        if (selectorIndex > 0) {
          selectorIndex--;
          carouselFocus = selectorIndex;
          coverRendered = false;
          requestUpdate();
        }
      });
      buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this, recentCount] {
        if (selectorIndex < recentCount - 1) {
          selectorIndex++;
          carouselFocus = selectorIndex;
          coverRendered = false;
          requestUpdate();
        }
      });
      buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down}, [this, recentCount] {
        selectorIndex = recentCount;
        coverRendered = false;
        requestUpdate();
      });
    } else {
      buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up}, [this, recentCount, menuCount] {
        if (selectorIndex == recentCount) {
          selectorIndex = carouselFocus;
        } else {
          selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
        }
        coverRendered = false;
        requestUpdate();
      });
      buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down}, [this, menuCount] {
        selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
        coverRendered = false;
        requestUpdate();
      });
    }
  } else {
    buttonNavigator.onNext([this, menuCount] {
      selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
      requestUpdate();
    });

    buttonNavigator.onPrevious([this, menuCount] {
      selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
      requestUpdate();
    });
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (ignoreConfirmRelease) {
      ignoreConfirmRelease = false;
      return;
    }

    if (isCatPose()) {
      const int n = static_cast<int>(recentBooks.size());
      if (selectorIndex < n) {
        onSelectBook(recentBooks[static_cast<size_t>(selectorIndex)].path);
      } else if (selectorIndex == getCatPoseViewAllIndex()) {
        onRecentsOpen();
      } else {
        int idx = 0;
        const int menuSelectedIndex = selectorIndex - getCatPoseMenuStart();
        const int fileBrowserIdx = idx++;
        const int fileTransferIdx = idx++;
        const int opdsLibraryIdx = hasOpdsUrl ? idx++ : -1;
        const int gamesIdx = idx++;
        const int settingsIdx = idx;

        if (menuSelectedIndex == fileBrowserIdx) {
          onFileBrowserOpen();
        } else if (menuSelectedIndex == fileTransferIdx) {
          onFileTransferOpen();
        } else if (menuSelectedIndex == opdsLibraryIdx) {
          onOpdsBrowserOpen();
        } else if (menuSelectedIndex == gamesIdx) {
          onGamesOpen();
        } else if (menuSelectedIndex == settingsIdx) {
          onSettingsOpen();
        }
      }
    } else {
      int idx = 0;
      int menuSelectedIndex = selectorIndex - static_cast<int>(recentBooks.size());
      const int fileBrowserIdx = idx++;
      const int recentsIdx = idx++;
      const int opdsLibraryIdx = hasOpdsUrl ? idx++ : -1;
      const int fileTransferIdx = idx++;
      const int gamesIdx = idx++;
      const int settingsIdx = idx;

      if (selectorIndex < static_cast<int>(recentBooks.size())) {
        onSelectBook(recentBooks[static_cast<size_t>(selectorIndex)].path);
      } else if (menuSelectedIndex == fileBrowserIdx) {
        onFileBrowserOpen();
      } else if (menuSelectedIndex == recentsIdx) {
        onRecentsOpen();
      } else if (menuSelectedIndex == opdsLibraryIdx) {
        onOpdsBrowserOpen();
      } else if (menuSelectedIndex == fileTransferIdx) {
        onFileTransferOpen();
      } else if (menuSelectedIndex == gamesIdx) {
        onGamesOpen();
      } else if (menuSelectedIndex == settingsIdx) {
        onSettingsOpen();
      }
    }
  } else if (ignoreConfirmRelease && !mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
    ignoreConfirmRelease = false;
  }
}

void HomeActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  const bool carousel = isCarouselTheme();
  bool bufferRestored = false;
  // CatPose uses Lyra-style cover buffer cache; Lyra3 still redraws covers every frame
  if (!isLyra3()) {
    bufferRestored = coverBufferStored && restoreCoverBuffer();
  }

  const int recentCount = static_cast<int>(recentBooks.size());
  int carouselAux = -1;
  if (carousel && recentCount > 0) {
    if (isCatPose() && selectorIndex >= recentCount) {
      carouselAux = carouselFocus;
    } else if (isLyra3() && selectorIndex >= recentCount) {
      carouselAux = carouselFocus;
    }
  }

  if (isCatPose()) {
    GUI.drawHomeWelcome(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding});
  } else {
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding}, nullptr);
  }

  GUI.drawRecentBookCover(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                          recentBooks, selectorIndex, coverRendered, coverBufferStored, bufferRestored,
                          std::bind(&HomeActivity::storeCoverBuffer, this), carouselAux);

  std::vector<const char*> menuItems;
  std::vector<UIIcon> menuIcons;
  std::vector<const char*> menuSubtitles;
  int menuSelectedIndex = 0;

  if (isCatPose()) {
    menuItems = {tr(STR_BROWSE_FILES), tr(STR_TRANSFER_FILE), tr(STR_GAMES), tr(STR_SETTINGS_TITLE)};
    menuIcons = {Folder, Transfer, Games, Settings};
    menuSubtitles = {tr(STR_BROWSE_DESC), tr(STR_TRANSFER_FILE_DESC), tr(STR_GAMES_DESC), tr(STR_SETTINGS_DESC)};
    if (hasOpdsUrl) {
      menuItems.insert(menuItems.begin() + 2, tr(STR_OPDS_BROWSER));
      menuIcons.insert(menuIcons.begin() + 2, Library);
      menuSubtitles.insert(menuSubtitles.begin() + 2, "");
    }
    menuSelectedIndex = selectorIndex - getCatPoseMenuStart();
  } else {
    menuItems = {tr(STR_BROWSE_FILES), tr(STR_MENU_RECENT_BOOKS), tr(STR_FILE_TRANSFER), tr(STR_GAMES),
                 tr(STR_SETTINGS_TITLE)};
    menuIcons = {Folder, Recent, Transfer, Games, Settings};
    if (hasOpdsUrl) {
      menuItems.insert(menuItems.begin() + 2, tr(STR_OPDS_BROWSER));
      menuIcons.insert(menuIcons.begin() + 2, Library);
    }
    menuSelectedIndex = selectorIndex - recentCount;
  }

  const int menuY = metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.verticalSpacing;
  const int menuH = pageHeight - (menuY + metrics.buttonHintsHeight + metrics.verticalSpacing);

  if (isCatPose()) {
    GUI.drawButtonMenu(
        renderer, Rect{0, menuY, pageWidth, menuH}, static_cast<int>(menuItems.size()), menuSelectedIndex,
        [&menuItems](int index) { return std::string(menuItems[static_cast<size_t>(index)]); },
        [&menuIcons](int index) { return menuIcons[static_cast<size_t>(index)]; },
        [&menuSubtitles](int index) {
          if (index < 0 || index >= static_cast<int>(menuSubtitles.size())) {
            return std::string();
          }
          return std::string(menuSubtitles[static_cast<size_t>(index)]);
        });
  } else {
    GUI.drawButtonMenu(renderer, Rect{0, menuY, pageWidth, menuH}, static_cast<int>(menuItems.size()),
                       menuSelectedIndex,
                       [&menuItems](int index) { return std::string(menuItems[static_cast<size_t>(index)]); },
                       [&menuIcons](int index) { return menuIcons[static_cast<size_t>(index)]; });
  }

  const bool onCarousel = isCatPose() ? (selectorIndex < recentCount)
                                      : (selectorIndex < static_cast<int>(recentBooks.size()));
  const bool catPoseGrid = isCatPose() && selectorIndex >= getCatPoseMenuStart();
  const auto labels =
      mappedInput.mapLabels("", tr(STR_SELECT),
                            (onCarousel || catPoseGrid) ? tr(STR_DIR_LEFT) : tr(STR_DIR_UP),
                            (onCarousel || catPoseGrid) ? tr(STR_DIR_RIGHT) : tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  } else if (!recentsLoaded && !recentsLoading) {
    recentsLoading = true;
    int coverH = metrics.homeCoverHeight;
    if (isCatPose()) {
      coverH = (coverH * 5) / 4;  // match focused cover height for sharp thumbs
    }
    loadRecentCovers(coverH);
  }
}

void HomeActivity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onRecentsOpen() { activityManager.goToRecentBooks(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }

void HomeActivity::onGamesOpen() { activityManager.goToGames(); }
