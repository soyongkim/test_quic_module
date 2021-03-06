// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/wm/haptics_util.h"
#include "ui/events/devices/haptic_touchpad_effects.h"
#include "ui/ozone/public/input_controller.h"
#include "ui/ozone/public/ozone_platform.h"

namespace ash {

namespace haptics_util {

namespace {

ui::InputController* g_test_input_controller = nullptr;

}  // namespace

void SetInputControllerForTesting(ui::InputController* input_controller) {
  g_test_input_controller = input_controller;
}

void PlayHapticTouchpadEffect(ui::HapticTouchpadEffect effect,
                              ui::HapticTouchpadEffectStrength strength) {
  ui::InputController* input_controller =
      g_test_input_controller
          ? g_test_input_controller
          : ui::OzonePlatform::GetInstance()->GetInputController();
  DCHECK(input_controller);
  input_controller->PlayHapticTouchpadEffect(effect, strength);
}

}  // namespace haptics_util
}  // namespace ash
