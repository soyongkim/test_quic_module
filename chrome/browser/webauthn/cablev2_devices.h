// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_WEBAUTHN_CABLEV2_DEVICES_H_
#define CHROME_BROWSER_WEBAUTHN_CABLEV2_DEVICES_H_

#include <memory>
#include <vector>

#include "device/fido/fido_constants.h"
#include "third_party/abseil-cpp/absl/types/variant.h"
#include "third_party/icu/source/common/unicode/locid.h"

class PrefService;
class Profile;

namespace user_prefs {
class PrefRegistrySyncable;
}

namespace device {
namespace cablev2 {
struct Pairing;
}  // namespace cablev2
}  // namespace device

namespace cablev2 {

// RegisterProfilePrefs registers any preferences used by these functions. This
// must be called at browser startup otherwise the preferences won't be
// usable.
void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

// KnownDevices reflects the browser's knowledge of known caBLEv2 devices.
// caBLEv2 is the protocol used when phones are acting as security keys. (Except
// for the case where accounts.google.com is using caBLEv1, but Chrome doesn't
// store any state for caBLEv1.)
//
// There are two sources of caBLEv2 pairing information: DeviceInfo entries in
// Sync, and information sent by the phone during a QR-initiated transaction.
// DeviceInfo state is managed by Sync. Information from phones is kept in
// preferences. These sources are referred to in variable names as "synced"
// and "linked" devices.
//
// (Preferences may be synced, and thus information about "linked" phones may be
// synced around. But that is separate from learning about phones because they
// are signed into the same account and are publishing DeviceInfo records into
// Sync.)
//
// Call `FromProfile` to load the information from sync data and preferences.
// Alternatively, for testing, information can be added directly to the
// vectors.
struct KnownDevices {
  KnownDevices();
  ~KnownDevices();
  KnownDevices(const KnownDevices&) = delete;
  KnownDevices(const KnownDevices&&) = delete;
  KnownDevices& operator=(const KnownDevices&) = delete;

  // FromProfile returns a `KnownDevices` by extracting them from the
  // Sync data and preferencs of `browser_context`.
  static std::unique_ptr<KnownDevices> FromProfile(Profile* profile);

  std::vector<std::unique_ptr<device::cablev2::Pairing>> synced_devices;
  std::vector<std::unique_ptr<device::cablev2::Pairing>> linked_devices;
};

// MergeDevices returns a merged and sorted list of pairings, suitable for
// display in UI. There may be sequential `Pairing`s with the same name.
// These should be merged before display and are returned in priority order for
// connections. Devices are sorted by name based on the given locale, which
// should be `icu::Locale::getDefault` in real code but should be a fixed locale
// in tests.
std::vector<std::unique_ptr<device::cablev2::Pairing>> MergeDevices(
    std::unique_ptr<KnownDevices>,
    const icu::Locale* locale);

// AddPairing records `pairing` in `pref_service`, displacing any existing
// pairing with the same public key.
void AddPairing(PrefService* pref_service,
                std::unique_ptr<device::cablev2::Pairing> pairing);

// DeletePairingByPublicKey erases a pairing from `pref_service` by public key.
void DeletePairingByPublicKey(
    PrefService* pref_service,
    std::array<uint8_t, device::kP256X962Length> public_key);

}  // namespace cablev2

#endif  // CHROME_BROWSER_WEBAUTHN_CABLEV2_DEVICES_H_
