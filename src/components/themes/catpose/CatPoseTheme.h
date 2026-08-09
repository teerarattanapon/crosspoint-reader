#pragma once

#include "components/themes/lyra/LyraTheme.h"

class GfxRenderer;

// CatPose home metrics — Welcome + recent strip + 2x2 menu (fit ~480px height)
// Unselected cover = homeCoverHeight; selected = homeCoverHeight * 5/4
namespace CatPoseMetrics {
constexpr int kSectionTopPad = 10;
constexpr int kSectionHeaderH = 36;
constexpr int kCoverMetaH = 58;
constexpr int kBaseCoverH = 120;  // sized for ~3 visible covers
constexpr int kFocusCoverH = (kBaseCoverH * 5) / 4;  // +25% of base = 150
constexpr int kCoverTileH = kSectionTopPad + kSectionHeaderH + kFocusCoverH + kCoverMetaH;

constexpr ThemeMetrics values = {.batteryWidth = 16,
                                 .batteryHeight = 12,
                                 .topPadding = 5,
                                 .batteryBarHeight = 28,
                                 .headerHeight = 84,
                                 .verticalSpacing = 28,
                                 .contentSidePadding = 16,
                                 .listRowHeight = 40,
                                 .listWithSubtitleRowHeight = 60,
                                 .menuRowHeight = 136,
                                 .menuSpacing = 8,
                                 .tabSpacing = 8,
                                 .tabBarHeight = 40,
                                 .scrollBarWidth = 4,
                                 .scrollBarRightOffset = 5,
                                 .homeTopPadding = 124,  // battery row + 96px cat below
                                 .homeCoverHeight = kBaseCoverH,
                                 .homeCoverTileHeight = kCoverTileH,
                                 .homeRecentBooksCount = 5,
                                 .buttonHintsHeight = 32,
                                 .sideButtonHintsWidth = 30,
                                 .progressBarHeight = 16,
                                 .progressBarMarginTop = 1,
                                 .statusBarHorizontalMargin = 5,
                                 .statusBarVerticalMargin = 19,
                                 .keyboardKeyWidth = 31,
                                 .keyboardKeyHeight = 50,
                                 .keyboardKeySpacing = 0,
                                 .keyboardBottomAligned = true,
                                 .keyboardCenteredText = true};
}

class CatPoseTheme : public LyraTheme {
 public:
  void drawHomeWelcome(const GfxRenderer& renderer, Rect rect) const override;

  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer, int carouselLayoutFocusIndex = -1) const override;

  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon,
                      const std::function<std::string(int index)>& buttonSubtitle = nullptr) const override;
};
