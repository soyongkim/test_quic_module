// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SYSTEM_BLUETOOTH_BLUETOOTH_DEVICE_STATUS_UI_HANDLER_H_
#define ASH_SYSTEM_BLUETOOTH_BLUETOOTH_DEVICE_STATUS_UI_HANDLER_H_

#include "ash/ash_export.h"
#include "ash/public/cpp/toast_manager.h"
#include "chromeos/services/bluetooth_config/public/mojom/cros_bluetooth_config.mojom.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"

namespace ash {

// Listens for changes in Bluetooth device status, like a when a new device is
// paired, a device is disconnected or connected. It shows a toast when these
// events occur.
class ASH_EXPORT BluetoothDeviceStatusUiHandler
    : public chromeos::bluetooth_config::mojom::BluetoothDeviceStatusObserver {
 public:
  BluetoothDeviceStatusUiHandler();
  BluetoothDeviceStatusUiHandler(const BluetoothDeviceStatusUiHandler&) =
      delete;
  BluetoothDeviceStatusUiHandler& operator=(
      const BluetoothDeviceStatusUiHandler&) = delete;
  ~BluetoothDeviceStatusUiHandler() override;

 private:
  // chromeos::bluetooth_config::mojom::BluetoothDeviceStatusObserver:
  void OnDevicePaired(
      chromeos::bluetooth_config::mojom::PairedBluetoothDevicePropertiesPtr
          device) override;
  void OnDeviceConnected(
      chromeos::bluetooth_config::mojom::PairedBluetoothDevicePropertiesPtr
          device) override;
  void OnDeviceDisconnected(
      chromeos::bluetooth_config::mojom::PairedBluetoothDevicePropertiesPtr
          device) override;

  virtual void ShowToast(const ash::ToastData& toast_data);

  // Returns a string which represents a toast id. Id is created from a
  // constant string prefix concatenated to |paired_device_properties| id.
  std::string GetToastId(
      const chromeos::bluetooth_config::mojom::PairedBluetoothDeviceProperties*
          paired_device_properties);

  mojo::Remote<chromeos::bluetooth_config::mojom::CrosBluetoothConfig>
      remote_cros_bluetooth_config_;
  mojo::Receiver<
      chromeos::bluetooth_config::mojom::BluetoothDeviceStatusObserver>
      cros_bluetooth_device_status_observer_receiver_{this};
};

}  // namespace ash

#endif  // ASH_SYSTEM_BLUETOOTH_BLUETOOTH_DEVICE_STATUS_UI_HANDLER_H_