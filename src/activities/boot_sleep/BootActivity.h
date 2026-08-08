#pragma once
#include <functional>
#include <utility>

#include "../Activity.h"

class BootActivity final : public Activity {
 public:
  explicit BootActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                        std::function<void()> onContinueCallback = nullptr)
      : Activity("Boot", renderer, mappedInput), onContinue(std::move(onContinueCallback)) {}
  void onEnter() override;

 private:
  std::function<void()> onContinue;
};
