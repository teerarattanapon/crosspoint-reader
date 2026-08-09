#include "CatPoseTheme.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/book.h"
#include "components/icons/clock.h"
#include "components/icons/cover.h"
#include "components/icons/folder.h"
#include "components/icons/games.h"
#include "components/icons/library.h"
#include "components/icons/recent.h"
#include "components/icons/settings2.h"
#include "components/icons/sleepcat.h"
#include "components/icons/transfer.h"
#include "fontIds.h"

namespace {
constexpr int hPaddingInSelection = 6;
constexpr int cornerRadius = 6;
constexpr int kCarouselGap = 8;
constexpr int kVisibleCovers = 3;
constexpr float kCoverAspect = 3.0f / 4.0f;  // wider than 2:3 for longer titles
constexpr int kMenuIconSize = 32;
constexpr int kMenuCols = 2;
constexpr int kFocusScaleNum = 5;
constexpr int kFocusScaleDen = 4;  // selected = base * 5/4 (+25%)
constexpr int kSleepCatSize = 96;  // 64 * 1.5 (+50%)
constexpr int kWelcomeTitleGap = 8;

const uint8_t* catPoseIcon(UIIcon icon) {
  switch (icon) {
    case UIIcon::Folder:
      return FolderIcon;
    case UIIcon::Book:
      return BookIcon;
    case UIIcon::Transfer:
      return TransferIcon;
    case UIIcon::Games:
      return GamesIcon;
    case UIIcon::Settings:
      return Settings2Icon;
    case UIIcon::Library:
      return LibraryIcon;
    case UIIcon::Recent:
      return RecentIcon;
    default:
      return nullptr;
  }
}

void drawCoverCell(GfxRenderer& renderer, const RecentBook& book, int tileX, int tileY, int tileW, int tileH,
                   int thumbHeight) {
  const int innerX = tileX + hPaddingInSelection;
  const int innerY = tileY + hPaddingInSelection;
  const int innerW = tileW - 2 * hPaddingInSelection;
  const int innerH = tileH - 2 * hPaddingInSelection;

  // White fill so letterboxing / empty covers are not black
  renderer.fillRect(innerX, innerY, innerW, innerH, false);

  bool hasCover = true;
  if (book.coverBmpPath.empty()) {
    hasCover = false;
  } else {
    const std::string coverBmpPath = UITheme::getCoverThumbPath(book.coverBmpPath, thumbHeight);
    FsFile file;
    if (Storage.openFileForRead("HOME", coverBmpPath, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        const int bmpW = bitmap.getWidth();
        const int bmpH = bitmap.getHeight();
        // Fit inside cell and center (no crop-fill; keep white letterbox)
        const float scale =
            std::min(static_cast<float>(innerW) / static_cast<float>(bmpW),
                     static_cast<float>(innerH) / static_cast<float>(bmpH));
        const int drawW = std::max(1, static_cast<int>(static_cast<float>(bmpW) * scale + 0.5f));
        const int drawH = std::max(1, static_cast<int>(static_cast<float>(bmpH) * scale + 0.5f));
        const int drawX = innerX + (innerW - drawW) / 2;
        const int drawY = innerY + (innerH - drawH) / 2;
        renderer.drawBitmap(bitmap, drawX, drawY, drawW, drawH, 0.0f);
      } else {
        hasCover = false;
      }
      file.close();
    } else {
      hasCover = false;
    }
  }

  if (!hasCover) {
    constexpr int kIcon = 32;
    renderer.drawIcon(CoverIcon, innerX + (innerW - kIcon) / 2, innerY + (innerH - kIcon) / 2, kIcon, kIcon);
  }
}

void drawCoverMeta(GfxRenderer& renderer, const RecentBook& book, int x, int y, int maxW, bool selected) {
  const auto titleStyle = selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  auto title = renderer.truncatedText(SMALL_FONT_ID, book.title.c_str(), maxW, titleStyle);
  renderer.drawText(SMALL_FONT_ID, x, y, title.c_str(), true, titleStyle);

  int lineY = y + renderer.getLineHeight(SMALL_FONT_ID);
  if (!book.author.empty()) {
    auto author = renderer.truncatedText(SMALL_FONT_ID, book.author.c_str(), maxW);
    renderer.drawText(SMALL_FONT_ID, x, lineY, author.c_str(), true);
    lineY += renderer.getLineHeight(SMALL_FONT_ID);
  }

  char pctBuf[8];
  snprintf(pctBuf, sizeof(pctBuf), "%u%%", static_cast<unsigned>(book.progressPercent));
  renderer.drawText(SMALL_FONT_ID, x, lineY, pctBuf, true);
}
}  // namespace

void CatPoseTheme::drawHomeWelcome(const GfxRenderer& renderer, Rect rect) const {
  const int pad = CatPoseMetrics::values.contentSidePadding;
  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  const int batteryX = rect.x + rect.width - 12 - CatPoseMetrics::values.batteryWidth;
  const int batteryY = rect.y + 4;
  drawBatteryRight(renderer,
                   Rect{batteryX, batteryY, CatPoseMetrics::values.batteryWidth, CatPoseMetrics::values.batteryHeight},
                   showBatteryPercentage);

  // Cat + welcome sit below the battery/% row (not vertically centered with it)
  const int batteryBandBottom = batteryY + renderer.getLineHeight(SMALL_FONT_ID) + 2;
  // drawImageTransparent only places icons on 8px X boundaries (x/8); misaligned Y maps to
  // that axis after orientation transform and clips/shifts the cat's right side.
  const int catX = (rect.x + pad) & ~7;
  const int catY = (batteryBandBottom + 7) & ~7;
  renderer.drawIcon(SleepCatIcon, catX, catY, kSleepCatSize, kSleepCatSize);

  const int titleFont = NOTOSANS_18_FONT_ID;  // larger than prior 16; largest Noto Sans available
  const int titleX = catX + kSleepCatSize + kWelcomeTitleGap;
  // Center text ink (ascender) on the cat icon — lineHeight includes extra leading and looks high
  const int titleH = renderer.getTextHeight(titleFont);
  const int titleY = catY + (kSleepCatSize - titleH) / 2;
  renderer.drawText(titleFont, titleX, titleY, tr(STR_WELCOME_BACK), true, EpdFontFamily::BOLD);
}

void CatPoseTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                       const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                       bool& bufferRestored, std::function<bool()> storeCoverBuffer,
                                       int carouselLayoutFocusIndex) const {
  const int pad = CatPoseMetrics::values.contentSidePadding;
  const int innerLeft = rect.x + pad;
  const int innerWidth = rect.width - 2 * pad;
  const int n = static_cast<int>(recentBooks.size());
  const bool viewAllSelected = (selectorIndex == n);
  const int sectionHeaderH = CatPoseMetrics::kSectionHeaderH;
  const int sectionTopPad = CatPoseMetrics::kSectionTopPad;

  // Section header: Recent Books + View All (ClockIcon is 32x32) — always redraw (selection can change)
  const int headerY = rect.y + sectionTopPad;
  constexpr int kRecentIconSize = 32;
  renderer.drawIcon(ClockIcon, innerLeft, headerY + (sectionHeaderH - kRecentIconSize) / 2, kRecentIconSize,
                    kRecentIconSize);
  const int headerTextY = headerY + (sectionHeaderH - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
  renderer.drawText(UI_12_FONT_ID, innerLeft + kRecentIconSize + 4, headerTextY, tr(STR_MENU_RECENT_BOOKS), true,
                    EpdFontFamily::REGULAR);

  const char* viewAllLabel = tr(STR_VIEW_ALL);
  char viewAllBuf[32];
  snprintf(viewAllBuf, sizeof(viewAllBuf), "%s >", viewAllLabel);
  const int viewAllW = renderer.getTextWidth(UI_12_FONT_ID, viewAllBuf, EpdFontFamily::REGULAR);
  const int viewAllX = rect.x + rect.width - pad - viewAllW;
  if (viewAllSelected) {
    renderer.fillRoundedRect(viewAllX - 6, headerY + 2, viewAllW + 12, sectionHeaderH - 4, cornerRadius,
                             Color::LightGray);
  }
  renderer.drawText(UI_12_FONT_ID, viewAllX, headerTextY, viewAllBuf, true, EpdFontFamily::REGULAR);

  const int tileYBase = rect.y + sectionTopPad + sectionHeaderH;
  // Size for ~3 visible slots across innerWidth; clamp focusH but keep slot widths for longer titles
  const int maxFocusH = CatPoseMetrics::kFocusCoverH;
  const int slotW =
      std::max(1, (innerWidth - (kVisibleCovers - 1) * kCarouselGap) / kVisibleCovers);
  int baseW = slotW;
  int baseH = std::max(1, static_cast<int>(static_cast<float>(baseW) / kCoverAspect + 0.5f));
  int focusH = (baseH * kFocusScaleNum) / kFocusScaleDen;
  int focusW = std::min(innerWidth, slotW + slotW / 4);  // +25% width when focused
  if (focusH > maxFocusH) {
    focusH = maxFocusH;
    baseH = (focusH * kFocusScaleDen) / kFocusScaleNum;
    // Keep 3-slot widths after height clamp so meta titles stay wide
    baseW = slotW;
    focusW = std::min(innerWidth, slotW + slotW / 4);
  }

  if (n == 0) {
    renderer.drawText(UI_12_FONT_ID, innerLeft, tileYBase + 20, tr(STR_NO_RECENT_BOOKS), true);
    coverRendered = false;
    coverBufferStored = false;
    return;
  }

  int layoutFocus = 0;
  if (selectorIndex >= 0 && selectorIndex < n) {
    layoutFocus = selectorIndex;
  } else if (carouselLayoutFocusIndex >= 0) {
    layoutFocus = std::clamp(carouselLayoutFocusIndex, 0, n - 1);
  }

  std::vector<int> widths(static_cast<size_t>(n));
  std::vector<int> heights(static_cast<size_t>(n));
  for (int i = 0; i < n; i++) {
    const bool isFocus = (i == layoutFocus);
    widths[static_cast<size_t>(i)] = isFocus ? focusW : baseW;
    heights[static_cast<size_t>(i)] = isFocus ? focusH : baseH;
  }

  std::vector<int> leftEdge(static_cast<size_t>(n));
  int stripW = 0;
  for (int i = 0; i < n; i++) {
    leftEdge[static_cast<size_t>(i)] = stripW;
    stripW += widths[static_cast<size_t>(i)];
    if (i + 1 < n) {
      stripW += kCarouselGap;
    }
  }

  const int focusCenter = leftEdge[static_cast<size_t>(layoutFocus)] + widths[static_cast<size_t>(layoutFocus)] / 2;
  const int idealScroll = focusCenter - innerWidth / 2;
  const int scrollMax = std::max(0, stripW - innerWidth);
  const int scroll = std::clamp(idealScroll, 0, scrollMax);
  // Generate thumbs at focus height so selected cover stays sharp
  const int thumbH = focusH;

  // Lyra-style cache: decode BMPs once per layout focus, then restore framebuffer on later frames.
  // Store BEFORE selection so restore yields clean covers; selection/meta are redrawn every frame.
  const bool needDecode = !coverRendered || !bufferRestored;
  if (needDecode) {
    for (int i = 0; i < n; i++) {
      const int w = widths[static_cast<size_t>(i)];
      const int h = heights[static_cast<size_t>(i)];
      const int lx = innerLeft + leftEdge[static_cast<size_t>(i)] - scroll;
      const int ly = tileYBase + focusH - h;
      // Skip off-screen covers (same viewport test as meta)
      if (lx + w <= innerLeft || lx >= innerLeft + innerWidth) {
        continue;
      }
      drawCoverCell(renderer, recentBooks[static_cast<size_t>(i)], lx, ly, w, h, thumbH);
    }
    if (storeCoverBuffer) {
      coverBufferStored = storeCoverBuffer();
      coverRendered = coverBufferStored;
    } else {
      coverRendered = false;
      coverBufferStored = false;
    }
  }

  // Selection highlight + meta every frame (overlay on cached covers).
  // Paint only the padding ring so restored cover pixels stay intact (no SD reload).
  for (int i = 0; i < n; i++) {
    const int w = widths[static_cast<size_t>(i)];
    const int h = heights[static_cast<size_t>(i)];
    const int lx = innerLeft + leftEdge[static_cast<size_t>(i)] - scroll;
    const int ly = tileYBase + focusH - h;
    if (lx + w <= innerLeft || lx >= innerLeft + innerWidth) {
      continue;
    }

    const bool selected = (selectorIndex == i);
    if (selected) {
      const int p = hPaddingInSelection;
      const int midH = h - 2 * p;
      renderer.fillRectDither(lx, ly, w, p, Color::LightGray);                  // top
      renderer.fillRectDither(lx, ly + h - p, w, p + 4, Color::LightGray);      // bottom + pad
      if (midH > 0) {
        renderer.fillRectDither(lx, ly + p, p, midH, Color::LightGray);         // left
        renderer.fillRectDither(lx + w - p, ly + p, p, midH, Color::LightGray); // right
      }
    }

    const int metaY = tileYBase + focusH + 4;
    const int metaW = std::max(1, w - 2);
    drawCoverMeta(renderer, recentBooks[static_cast<size_t>(i)], lx + 2, metaY, metaW, selected);
  }
}

void CatPoseTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                                  const std::function<std::string(int index)>& buttonLabel,
                                  const std::function<UIIcon(int index)>& rowIcon,
                                  const std::function<std::string(int index)>& buttonSubtitle) const {
  const int pad = CatPoseMetrics::values.contentSidePadding;
  const int gap = CatPoseMetrics::values.menuSpacing;
  const int rows = (buttonCount + kMenuCols - 1) / kMenuCols;
  const int available = std::max(1, rect.height - 8);
  const int maxRowH = (rows > 0) ? (available - (rows - 1) * gap) / rows : CatPoseMetrics::values.menuRowHeight;
  // Prefer configured 2x height; clamp so landscape 480 does not overflow hints
  const int rowH = std::min(CatPoseMetrics::values.menuRowHeight, std::max(68, maxRowH));
  const int innerW = rect.width - 2 * pad;
  const int tileW = (innerW - gap) / kMenuCols;

  renderer.drawLine(rect.x + pad, rect.y, rect.x + rect.width - pad - 1, rect.y, 1, true);

  for (int i = 0; i < buttonCount; ++i) {
    const int col = i % kMenuCols;
    const int row = i / kMenuCols;
    const int tileX = rect.x + pad + col * (tileW + gap);
    const int tileY = rect.y + 8 + row * (rowH + gap);
    const bool selected = selectedIndex == i;

    renderer.drawRoundedRect(tileX, tileY, tileW, rowH, 1, cornerRadius, true);
    if (selected) {
      renderer.fillRoundedRect(tileX, tileY, tileW, rowH, cornerRadius, Color::LightGray);
      renderer.drawRoundedRect(tileX, tileY, tileW, rowH, 1, cornerRadius, true);
    }

    int textX = tileX + 12;
    const int titleLineH = renderer.getLineHeight(UI_12_FONT_ID);
    const int subLineH = renderer.getLineHeight(SMALL_FONT_ID);
    constexpr int kSubMaxLines = 2;
    constexpr int kSubGap = 4;
    std::string sub;
    if (buttonSubtitle) {
      sub = buttonSubtitle(i);
    }
    const int subLinesBudget = sub.empty() ? 0 : kSubMaxLines;
    const int textBlockH = titleLineH + (subLinesBudget > 0 ? (kSubGap + subLinesBudget * subLineH) : 0);
    const int titleY = tileY + (rowH - textBlockH) / 2;

    if (rowIcon) {
      UIIcon icon = rowIcon(i);
      const uint8_t* bmp = catPoseIcon(icon);
      if (bmp) {
        renderer.drawIcon(bmp, textX, tileY + (rowH - kMenuIconSize) / 2, kMenuIconSize, kMenuIconSize);
        textX += kMenuIconSize + 10;
      }
    }

    std::string labelStr = buttonLabel(i);
    renderer.drawText(UI_12_FONT_ID, textX, titleY, labelStr.c_str(), true, EpdFontFamily::BOLD);

    if (!sub.empty()) {
      const int subMaxW = tileW - (textX - tileX) - 20;
      // Wrap up to 2 lines; ellipsis only if the last line still overflows
      auto lines = renderer.wrappedText(SMALL_FONT_ID, sub.c_str(), subMaxW, kSubMaxLines);
      int lineY = titleY + titleLineH + kSubGap;
      for (const auto& line : lines) {
        renderer.drawText(SMALL_FONT_ID, textX, lineY, line.c_str(), true);
        lineY += subLineH;
      }
    }

    const char* chevron = ">";
    const int chevW = renderer.getTextWidth(UI_12_FONT_ID, chevron);
    renderer.drawText(UI_12_FONT_ID, tileX + tileW - chevW - 12,
                      tileY + (rowH - renderer.getLineHeight(UI_12_FONT_ID)) / 2, chevron, true);
  }
}
