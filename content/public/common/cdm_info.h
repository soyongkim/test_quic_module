// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_COMMON_CDM_INFO_H_
#define CONTENT_PUBLIC_COMMON_CDM_INFO_H_

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/token.h"
#include "base/version.h"
#include "build/build_config.h"
#include "content/common/content_export.h"
#include "media/base/content_decryption_module.h"
#include "media/base/encryption_scheme.h"
#include "media/base/video_codecs.h"
#include "media/cdm/cdm_capability.h"
#include "media/cdm/cdm_type.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace content {

#if defined(OS_CHROMEOS) || BUILDFLAG(USE_CHROMEOS_PROTECTED_MEDIA)
// TODO(crbug.com/1231162): Remove the string identifier once we've migrated off
// of the PluginPrivateFileSystem.
// CdmType for Chrome OS.
const CONTENT_EXPORT media::CdmType kChromeOsCdmType{
    base::Token{0xa6ecd3fc63b3ded2ull, 0x9306d3270227ce5full},
    "application_chromeos-cdm-factory-daemon"};
#endif  // defined(OS_CHROMEOS) || BUILDFLAG(USE_CHROMEOS_PROTECTED_MEDIA)

// Represents a Content Decryption Module implementation and its capabilities.
struct CONTENT_EXPORT CdmInfo {
  enum class Robustness {
    kHardwareSecure,
    kSoftwareSecure,
  };

  CdmInfo(const std::string& key_system,
          Robustness robustness,
          absl::optional<media::CdmCapability> capability,
          bool supports_sub_key_systems,
          const std::string& name,
          const media::CdmType& type,
          const base::Version& version,
          const base::FilePath& path);
  CdmInfo(const std::string& key_system,
          Robustness robustness,
          absl::optional<media::CdmCapability> capability,
          const media::CdmType& type);
  CdmInfo(const CdmInfo& other);
  ~CdmInfo();

  // The key system supported by this CDM.
  std::string key_system;

  // Whether this CdmInfo is for the hardware secure pipeline. Even for the
  // same `key_system`, the software and hardware secure pipeline (specified as
  // `robustness` in EME) could be supported by different CDMs, or having
  // different CDM capabilities. Therefore, we use this flag to differentiate
  // between the software and hardware secure pipelines.
  Robustness robustness;

  // CDM capability, e.g. video codecs, encryption schemes and session types.
  // Optional to allow lazy initialization, i.e. to populate the capability
  // after registration.
  absl::optional<media::CdmCapability> capability;

  // Whether we also support sub key systems of the `key_system`.
  // A sub key system to a key system is like a sub domain to a domain.
  // For example, com.example.somekeysystem.a and com.example.somekeysystem.b
  // are both sub key systems of com.example.somekeysystem.
  bool supports_sub_key_systems = false;

  // Display name of the CDM (e.g. Widevine Content Decryption Module).
  std::string name;

  // An object to uniquely identify the type of the CDM. Used for per-CDM-type
  // isolation, e.g. for running different CDMs in different child processes,
  // and per-CDM-type storage.
  media::CdmType type;

  // Version of the CDM. May be empty if the version is not known.
  base::Version version;

  // Path to the library implementing the CDM. May be empty if the
  // CDM is not a separate library (e.g. Widevine on Android).
  base::FilePath path;
};

}  // namespace content

#endif  // CONTENT_PUBLIC_COMMON_CDM_INFO_H_