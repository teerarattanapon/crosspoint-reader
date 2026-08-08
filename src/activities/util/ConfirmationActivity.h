#pragma once
#include <functional>
#include <string>

#include "../../fontIds.h"
#include "../Activity.h"

class ConfirmationActivity : public Activity {
 private:
  // Input data
  std::string heading;
  std::string body;
  std::string leftLabel;
  std::string rightLabel;

  const int margin = 20;
  const int spacing = 30;
  const int fontId = UI_10_FONT_ID;

  std::string safeHeading;
  std::string safeBody;
  int startY = 0;
  int lineHeight = 0;

 public:
  // leftLabel/rightLabel default to Cancel/Confirm when empty.
  ConfirmationActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& heading,
                       const std::string& body, const std::string& leftLabel = "",
                       const std::string& rightLabel = "");

  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;
};
