// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ASH_APP_MODE_STARTUP_APP_LAUNCHER_H_
#define CHROME_BROWSER_ASH_APP_MODE_STARTUP_APP_LAUNCHER_H_

#include <memory>
#include <string>

#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ash/app_mode/chrome_app_kiosk_app_installer.h"
#include "chrome/browser/ash/app_mode/kiosk_app_launcher.h"
#include "chrome/browser/ash/app_mode/kiosk_app_manager.h"
#include "chrome/browser/ash/app_mode/kiosk_app_manager_observer.h"
#include "chrome/browser/extensions/install_observer.h"
#include "chrome/browser/extensions/install_tracker.h"
#include "extensions/browser/app_window/app_window_registry.h"

class Profile;

namespace extensions {
class AppWindowRegistry;
}

namespace ash {

class StartupAppLauncherUpdateChecker;

// Responsible for the startup of the app for Chrome App kiosk.
class StartupAppLauncher : public KioskAppLauncher,
                           public KioskAppManagerObserver,
                           public extensions::AppWindowRegistry::Observer {
 public:
  StartupAppLauncher(Profile* profile,
                     const std::string& app_id,
                     Delegate* delegate);
  StartupAppLauncher(const StartupAppLauncher&) = delete;
  StartupAppLauncher& operator=(const StartupAppLauncher&) = delete;
  ~StartupAppLauncher() override;

 private:
  // Class used to watch for app window creation.
  class AppWindowWatcher;

  // KioskAppLauncher:
  void Initialize() override;
  void ContinueWithNetworkReady() override;
  void LaunchApp() override;
  void RestartLauncher() override;

  void OnLaunchSuccess();
  void OnLaunchFailure(KioskAppLaunchError::Error error);

  void BeginInstall();
  void OnInstallComplete(KioskAppLaunchError::Error error);

  void OnReadyToLaunch();
  void MaybeUpdateAppData();

  void MaybeInitializeNetwork();
  void SetSecondaryAppsEnabledState(const extensions::Extension* primary_app);
  void MaybeLaunchApp();

  void OnKioskAppDataLoadStatusChanged(const std::string& app_id);

  // AppWindowRegistry::Observer:
  void OnAppWindowAdded(extensions::AppWindow* app_window) override;

  // Returns true if all secondary apps have been installed.
  bool AreSecondaryAppsInstalled() const;

  // Returns true if the primary app has a pending update.
  bool PrimaryAppHasPendingUpdate() const;

  const extensions::Extension* GetPrimaryAppExtension() const;

  // KioskAppManagerObserver overrides.
  void OnKioskExtensionLoadedInCache(const std::string& app_id) override;
  void OnKioskExtensionDownloadFailed(const std::string& app_id) override;

  Profile* const profile_;
  const std::string app_id_;
  bool network_ready_handled_ = false;
  int launch_attempt_ = 0;
  bool wait_for_crx_update_ = false;
  bool waiting_for_window_ = false;

  ChromeAppKioskAppInstaller installer_;

  extensions::AppWindowRegistry* window_registry_;

  base::ScopedObservation<KioskAppManagerBase, KioskAppManagerObserver>
      kiosk_app_manager_observation_{this};

  base::WeakPtrFactory<StartupAppLauncher> weak_ptr_factory_{this};
};

}  // namespace ash

// TODO(https://crbug.com/1164001): remove when the //chrome/browser/chromeos
// migration is finished.
namespace chromeos {
using ::ash::StartupAppLauncher;
}

#endif  // CHROME_BROWSER_ASH_APP_MODE_STARTUP_APP_LAUNCHER_H_
