// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/quick_pair/scanning/fast_pair/fast_pair_not_discoverable_scanner.h"

#include <cstdint>
#include <iomanip>
#include <memory>
#include <sstream>
#include <vector>

#include "ash/quick_pair/common/constants.h"
#include "ash/quick_pair/common/device.h"
#include "ash/quick_pair/common/fast_pair/fast_pair_decoder.h"
#include "ash/quick_pair/common/logging.h"
#include "ash/quick_pair/common/pair_failure.h"
#include "ash/quick_pair/common/protocol.h"
#include "ash/quick_pair/fast_pair_handshake/fast_pair_handshake_lookup.h"
#include "ash/quick_pair/repository/fast_pair/device_metadata.h"
#include "ash/quick_pair/repository/fast_pair/pairing_metadata.h"
#include "ash/quick_pair/repository/fast_pair_repository.h"
#include "ash/services/quick_pair/public/cpp/account_key_filter.h"
#include "ash/services/quick_pair/public/cpp/not_discoverable_advertisement.h"
#include "ash/services/quick_pair/quick_pair_process.h"
#include "ash/services/quick_pair/quick_pair_process_manager.h"
#include "base/bind.h"
#include "base/callback.h"
#include "base/callback_helpers.h"
#include "base/check.h"
#include "base/containers/flat_set.h"
#include "base/memory/scoped_refptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "device/bluetooth//bluetooth_adapter.h"
#include "device/bluetooth/bluetooth_device.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace {

constexpr int kMaxParseAdvertisementRetryCount = 5;

device::BluetoothDevice::BatteryInfo GetBatteryInfo(
    const ash::quick_pair::BatteryInfo& battery_info,
    const device::BluetoothDevice::BatteryType& battery_type) {
  return device::BluetoothDevice::BatteryInfo(
      battery_type, battery_info.percentage,
      battery_info.is_charging
          ? device::BluetoothDevice::BatteryInfo::ChargeState::kCharging
          : device::BluetoothDevice::BatteryInfo::ChargeState::kDischarging);
}

void SetBatteryInfo(
    device::BluetoothDevice* device,
    const ash::quick_pair::BatteryNotification& battery_notification) {
  device::BluetoothDevice::BatteryInfo left_bud_info =
      GetBatteryInfo(/*battery_info=*/battery_notification.left_bud_info,
                     /*battery_type=*/device::BluetoothDevice::BatteryType::
                         kLeftBudTrueWireless);
  device->SetBatteryInfo(left_bud_info);

  device::BluetoothDevice::BatteryInfo right_bud_info =
      GetBatteryInfo(/*battery_info=*/battery_notification.right_bud_info,
                     /*battery_type=*/device::BluetoothDevice::BatteryType::
                         kRightBudTrueWireless);
  device->SetBatteryInfo(right_bud_info);

  device::BluetoothDevice::BatteryInfo case_info = GetBatteryInfo(
      /*battery_info=*/battery_notification.case_info,
      /*battery_type=*/device::BluetoothDevice::BatteryType::kCaseTrueWireless);
  device->SetBatteryInfo(case_info);
}

}  // namespace

namespace ash {
namespace quick_pair {

FastPairNotDiscoverableScanner::FastPairNotDiscoverableScanner(
    scoped_refptr<FastPairScanner> scanner,
    scoped_refptr<device::BluetoothAdapter> adapter,
    DeviceCallback found_callback,
    DeviceCallback lost_callback)
    : scanner_(scanner),
      adapter_(std::move(adapter)),
      found_callback_(std::move(found_callback)),
      lost_callback_(std::move(lost_callback)) {
  observation_.Observe(scanner.get());
}

FastPairNotDiscoverableScanner::~FastPairNotDiscoverableScanner() = default;

void FastPairNotDiscoverableScanner::OnDeviceFound(
    device::BluetoothDevice* device) {
  QP_LOG(VERBOSE) << __func__ << ": " << device->GetNameForDisplay();

  const std::vector<uint8_t>* fast_pair_service_data =
      device->GetServiceDataForUUID(kFastPairBluetoothUuid);

  if (!fast_pair_service_data) {
    QP_LOG(WARNING) << __func__
                    << ": Device doesn't have any Fast Pair Service Data.";
    return;
  }

  advertisement_parse_attempts_[device->GetAddress()] = 1;

  quick_pair_process::ParseNotDiscoverableAdvertisement(
      *fast_pair_service_data,
      base::BindOnce(&FastPairNotDiscoverableScanner::OnAdvertisementParsed,
                     weak_pointer_factory_.GetWeakPtr(), device),
      base::BindOnce(&FastPairNotDiscoverableScanner::OnUtilityProcessStopped,
                     weak_pointer_factory_.GetWeakPtr(), device));
}

void FastPairNotDiscoverableScanner::OnDeviceLost(
    device::BluetoothDevice* device) {
  QP_LOG(VERBOSE) << __func__ << ": " << device->GetNameForDisplay();

  // If we have an in-progess parse attempt for this device, this will ensure
  // the result is ignored.
  advertisement_parse_attempts_.erase(device->GetAddress());

  auto it = notified_devices_.find(device->GetAddress());

  // Don't invoke callback if we didn't notify this device.
  if (it == notified_devices_.end())
    return;

  scoped_refptr<Device> notified_device = it->second;
  notified_devices_.erase(it);
  lost_callback_.Run(std::move(notified_device));
}

void FastPairNotDiscoverableScanner::OnAdvertisementParsed(
    device::BluetoothDevice* device,
    const absl::optional<NotDiscoverableAdvertisement>& advertisement) {
  auto it = advertisement_parse_attempts_.find(device->GetAddress());

  // If this check fails, the device was lost during parsing
  if (it == advertisement_parse_attempts_.end())
    return;

  advertisement_parse_attempts_.erase(it);

  if (!advertisement)
    return;

  // Set the battery notification if the advertisement contains battery
  // notification information
  if (advertisement->battery_notification)
    SetBatteryInfo(device, advertisement->battery_notification.value());

  if (!advertisement->show_ui)
    return;

  auto filter_iterator =
      account_key_filters_
          .insert_or_assign(device->GetAddress(),
                            AccountKeyFilter(advertisement.value()))
          .first;

  FastPairRepository::Get()->CheckAccountKeys(
      filter_iterator->second,
      base::BindOnce(
          &FastPairNotDiscoverableScanner::OnAccountKeyFilterCheckResult,
          weak_pointer_factory_.GetWeakPtr(), device));
}

void FastPairNotDiscoverableScanner::OnAccountKeyFilterCheckResult(
    device::BluetoothDevice* bluetooth_device,
    absl::optional<PairingMetadata> metadata) {
  account_key_filters_.erase(bluetooth_device->GetAddress());

  QP_LOG(VERBOSE) << __func__ << " Metadata: " << (metadata ? "yes" : "no");

  if (!metadata || !metadata->device_metadata)
    return;

  auto& details = metadata->device_metadata->GetDetails();

  // Convert the integer model id to uppercase hex string.
  std::stringstream model_id_stream;
  model_id_stream << std::uppercase << std::hex << details.id();
  std::string model_id = model_id_stream.str();

  QP_LOG(VERBOSE) << __func__ << ": Id: " << model_id;
  auto device = base::MakeRefCounted<Device>(
      model_id, bluetooth_device->GetAddress(), Protocol::kFastPairSubsequent);
  device->SetAdditionalData(Device::AdditionalDataType::kAccountKey,
                            metadata->account_key);

  FastPairHandshakeLookup::GetInstance()->Create(
      adapter_, std::move(device),
      base::BindOnce(&FastPairNotDiscoverableScanner::OnHandshakeComplete,
                     weak_pointer_factory_.GetWeakPtr()));
}

void FastPairNotDiscoverableScanner::OnHandshakeComplete(
    scoped_refptr<Device> device,
    absl::optional<PairFailure> failure) {
  if (failure) {
    QP_LOG(WARNING) << __func__ << ": Handshake failed with " << device
                    << " because: " << failure.value();
    return;
  }

  device::BluetoothDevice* classic_device =
      device->classic_address()
          ? adapter_->GetDevice(device->classic_address().value())
          : nullptr;

  device::BluetoothDevice* ble_device =
      adapter_->GetDevice(device->ble_address);

  bool is_already_paired =
      (classic_device != nullptr ? classic_device->IsPaired() : false) ||
      ble_device->IsPaired();

  if (is_already_paired) {
    QP_LOG(INFO) << __func__ << ": Already paired with " << device;
    return;
  }

  notified_devices_[device->ble_address] = device;
  found_callback_.Run(device);
}

void FastPairNotDiscoverableScanner::OnUtilityProcessStopped(
    device::BluetoothDevice* device,
    QuickPairProcessManager::ShutdownReason shutdown_reason) {
  int current_retry_count = advertisement_parse_attempts_[device->GetAddress()];
  if (current_retry_count > kMaxParseAdvertisementRetryCount) {
    QP_LOG(WARNING) << "Failed to parse advertisement from device more than "
                    << kMaxParseAdvertisementRetryCount << " times.";
    // Clean up the state here which enables trying again in the future if
    // this device is re-discovered.
    advertisement_parse_attempts_.erase(device->GetAddress());
    return;
  }

  const std::vector<uint8_t>* fast_pair_service_data =
      device->GetServiceDataForUUID(kFastPairBluetoothUuid);

  if (!fast_pair_service_data) {
    QP_LOG(WARNING) << "Failed to get service data for a device we previously "
                       "did get it for.";
    advertisement_parse_attempts_.erase(device->GetAddress());
    return;
  }

  advertisement_parse_attempts_[device->GetAddress()] = current_retry_count + 1;

  quick_pair_process::ParseNotDiscoverableAdvertisement(
      *fast_pair_service_data,
      base::BindOnce(&FastPairNotDiscoverableScanner::OnAdvertisementParsed,
                     weak_pointer_factory_.GetWeakPtr(), device),
      base::BindOnce(&FastPairNotDiscoverableScanner::OnUtilityProcessStopped,
                     weak_pointer_factory_.GetWeakPtr(), device));
}

}  // namespace quick_pair
}  // namespace ash