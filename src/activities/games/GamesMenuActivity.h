#pragma once

#include "../Activity.h"
#include "util/ButtonNavigator.h"

/**
 * Extensible games catalog. Add a new GameId + catalog entry + launch case
 * when introducing another mini-game.
 */
enum class GameId : uint8_t { Tretis };

class GamesMenuActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;

  void launchSelected();

 public:
  explicit GamesMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("GamesMenu", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
