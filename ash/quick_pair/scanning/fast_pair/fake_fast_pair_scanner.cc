// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/quick_pair/scanning/fast_pair/fake_fast_pair_scanner.h"

namespace ash {
namespace quick_pair {

FakeFastPairScanner::FakeFastPairScanner() = default;

FakeFastPairScanner::~FakeFastPairScanner() = default;

void FakeFastPairScanner::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void FakeFastPairScanner::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void FakeFastPairScanner::NotifyDeviceFound(device::BluetoothDevice* device) {
  for (auto& obs : observers_)
    obs.OnDeviceFound(device);
}

void FakeFastPairScanner::NotifyDeviceLost(device::BluetoothDevice* device) {
  for (auto& obs : observers_)
    obs.OnDeviceLost(device);
}

}  // namespace quick_pair
}  // namespace ash
