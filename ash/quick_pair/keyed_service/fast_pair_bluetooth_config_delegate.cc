// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/quick_pair/keyed_service/fast_pair_bluetooth_config_delegate.h"

#include "chromeos/services/bluetooth_config/device_name_manager.h"

namespace ash {
namespace quick_pair {

FastPairBluetoothConfigDelegate::FastPairBluetoothConfigDelegate() = default;

FastPairBluetoothConfigDelegate::~FastPairBluetoothConfigDelegate() = default;

void FastPairBluetoothConfigDelegate::SetDeviceNameManager(
    chromeos::bluetooth_config::DeviceNameManager* device_name_manager) {
  device_name_manager_ = device_name_manager;
}

}  // namespace quick_pair
}  // namespace ash