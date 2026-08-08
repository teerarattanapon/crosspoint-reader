#pragma once

#include "../Activity.h"

/** Read-only view of TRETIS top-3 high scores. */
class TretisHighScoresActivity final : public Activity {
 public:
  explicit TretisHighScoresActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("TretisHighScores", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
