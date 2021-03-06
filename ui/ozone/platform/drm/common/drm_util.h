// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_DRM_COMMON_DRM_UTIL_H_
#define UI_OZONE_PLATFORM_DRM_COMMON_DRM_UTIL_H_

#include <stddef.h>

#include <memory>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "ui/display/types/display_constants.h"
#include "ui/display/types/display_snapshot.h"
#include "ui/ozone/platform/drm/common/display_types.h"
#include "ui/ozone/platform/drm/common/scoped_drm_types.h"

typedef struct _drmModeModeInfo drmModeModeInfo;

namespace display {
class DisplayMode;
}  // namespace display

namespace gfx {
class Point;
}

namespace ui {

// It is safe to assume there will be no more than 256 connected DRM devices.
constexpr int kMaxDrmCount = 256u;

// It is safe to assume there will be no more than 256 connectors per DRM.
constexpr int kMaxDrmConnectors = 256u;

// DRM property names.
const char kContentProtection[] = "Content Protection";
const char kHdcpContentType[] = "HDCP Content Type";

constexpr char kPrivacyScreenPropertyNameLegacy[] = "privacy-screen";
constexpr char kPrivacyScreenHwStatePropertyName[] = "privacy-screen hw-state";
constexpr char kPrivacyScreenSwStatePropertyName[] = "privacy-screen sw-state";

// DRM property enum to internal type mappings.
template <typename InternalType>
struct DrmPropertyEnumToInternalTypeMapping {
  const char* drm_enum;
  const InternalType& internal_state;
};

const DrmPropertyEnumToInternalTypeMapping<display::ContentProtectionMethod>
    kHdcpContentTypeStates[] = {
        {"HDCP Type0", display::CONTENT_PROTECTION_METHOD_HDCP_TYPE_0},
        {"HDCP Type1", display::CONTENT_PROTECTION_METHOD_HDCP_TYPE_1}};

const DrmPropertyEnumToInternalTypeMapping<display::HDCPState>
    kContentProtectionStates[] = {{"Undesired", display::HDCP_STATE_UNDESIRED},
                                  {"Desired", display::HDCP_STATE_DESIRED},
                                  {"Enabled", display::HDCP_STATE_ENABLED}};

const DrmPropertyEnumToInternalTypeMapping<display::PrivacyScreenState>
    kPrivacyScreenStates[] = {{"Disabled", display::kDisabled},
                              {"Enabled", display::kEnabled},
                              {"Disabled-locked", display::kDisabledLocked},
                              {"Enabled-locked", display::kEnabledLocked}};

// Representation of the information required to initialize and configure a
// native display. |index| is the position of the connection and will be
// used to generate a unique identifier for the display.
class HardwareDisplayControllerInfo {
 public:
  HardwareDisplayControllerInfo(ScopedDrmConnectorPtr connector,
                                ScopedDrmCrtcPtr crtc,
                                uint8_t index);

  HardwareDisplayControllerInfo(const HardwareDisplayControllerInfo&) = delete;
  HardwareDisplayControllerInfo& operator=(
      const HardwareDisplayControllerInfo&) = delete;

  ~HardwareDisplayControllerInfo();

  drmModeConnector* connector() const { return connector_.get(); }
  drmModeCrtc* crtc() const { return crtc_.get(); }
  uint8_t index() const { return index_; }

 private:
  ScopedDrmConnectorPtr connector_;
  ScopedDrmCrtcPtr crtc_;
  uint8_t index_;
};

using HardwareDisplayControllerInfoList =
    std::vector<std::unique_ptr<HardwareDisplayControllerInfo>>;

// Looks-up and parses the native display configurations returning all available
// displays and CRTCs that weren't picked as best CRTC for each connector.
// TODO(markyacoub): Create unit tests that tests the different bits and pieces
// that this function goes through.
std::pair<HardwareDisplayControllerInfoList, std::vector<uint32_t>>
GetDisplayInfosAndInvalidCrtcs(int fd);

// Returns the display infos parsed in |GetDisplayInfosAndInvalidCrtcs|
HardwareDisplayControllerInfoList GetAvailableDisplayControllerInfos(int fd);

bool SameMode(const drmModeModeInfo& lhs, const drmModeModeInfo& rhs);

std::unique_ptr<display::DisplayMode> CreateDisplayMode(
    const drmModeModeInfo& mode);

// Extracts the display modes list from |info| as well as the current and native
// display modes given the |active_pixel_size| which is retrieved from the first
// detailed timing descriptor in the EDID.
display::DisplaySnapshot::DisplayModeList ExtractDisplayModes(
    HardwareDisplayControllerInfo* info,
    const gfx::Size& active_pixel_size,
    const display::DisplayMode** out_current_mode,
    const display::DisplayMode** out_native_mode);

// |info| provides the DRM information related to the display, |fd| is the
// connection to the DRM device.
std::unique_ptr<display::DisplaySnapshot> CreateDisplaySnapshot(
    HardwareDisplayControllerInfo* info,
    int fd,
    const base::FilePath& sys_path,
    uint8_t device_index,
    const gfx::Point& origin);

int GetFourCCFormatForOpaqueFramebuffer(gfx::BufferFormat format);

gfx::Size GetMaximumCursorSize(int fd);

ScopedDrmPropertyPtr FindDrmProperty(int fd,
                                     drmModeObjectProperties* properties,
                                     const char* name);

bool HasColorCorrectionMatrix(int fd, drmModeCrtc* crtc);

bool MatchMode(const display::DisplayMode& display_mode,
               const drmModeModeInfo& m);

const gfx::Size ModeSize(const drmModeModeInfo& mode);

float ModeRefreshRate(const drmModeModeInfo& mode);

bool ModeIsInterlaced(const drmModeModeInfo& mode);

uint64_t GetEnumValueForName(int fd, int property_id, const char* str);

std::vector<uint64_t> ParsePathBlob(const drmModePropertyBlobRes& path_blob);

display::PrivacyScreenState GetPrivacyScreenStateFromEnumValue(
    const std::string& enum_value);

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_DRM_COMMON_DRM_UTIL_H_
