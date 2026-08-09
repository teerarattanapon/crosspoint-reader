#pragma once
#include <functional>
#include <vector>

#include "../Activity.h"
#include "./FileBrowserActivity.h"
#include "util/ButtonNavigator.h"

struct RecentBook;
struct Rect;

class HomeActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  /// Last focused recent-book index on carousel themes when View All / menu is active.
  int carouselFocus = 0;
  bool recentsLoading = false;
  bool recentsLoaded = false;
  bool firstRenderDone = false;
  bool hasOpdsUrl = false;
  bool coverRendered = false;
  bool coverBufferStored = false;
  bool ignoreConfirmRelease = true;
  uint8_t* coverBuffer = nullptr;
  std::vector<RecentBook> recentBooks;

  void onSelectBook(const std::string& path);
  void onFileBrowserOpen();
  void onRecentsOpen();
  void onSettingsOpen();
  void onFileTransferOpen();
  void onOpdsBrowserOpen();
  void onGamesOpen();

  bool isCatPose() const;
  bool isLyra3() const;
  bool isCarouselTheme() const;
  int getMenuItemCount() const;
  int getCatPoseViewAllIndex() const;
  int getCatPoseMenuStart() const;
  int getCatPoseMenuCount() const;
  void backfillProgressIfNeeded(RecentBook& book);

  bool storeCoverBuffer();
  bool restoreCoverBuffer();
  void freeCoverBuffer();
  void loadRecentBooks(int maxBooks);
  void loadRecentCovers(int coverHeight);

 public:
  explicit HomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Home", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
