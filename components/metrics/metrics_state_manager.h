// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_METRICS_METRICS_STATE_MANAGER_H_
#define COMPONENTS_METRICS_METRICS_STATE_MANAGER_H_

#include <memory>
#include <string>

#include "base/callback.h"
#include "base/files/file_path.h"
#include "base/gtest_prod_util.h"
#include "base/memory/raw_ptr.h"
#include "base/metrics/field_trial.h"
#include "build/chromeos_buildflags.h"
#include "components/metrics/clean_exit_beacon.h"
#include "components/metrics/client_info.h"
#include "components/metrics/cloned_install_detector.h"
#include "components/metrics/entropy_state.h"
#include "components/version_info/channel.h"

#if BUILDFLAG(IS_CHROMEOS_ASH)
#include "components/metrics/structured/neutrino_logging.h"  // nogncheck
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

class PrefService;
class PrefRegistrySimple;

namespace metrics {

class EnabledStateProvider;
class MetricsProvider;

// Denotes whether this session is a background or foreground session at
// startup. May be unknown. A background session refers to the situation in
// which the browser process starts; does some work, e.g. servicing a sync; and
// ends without ever becoming visible. Note that the point in startup at which
// this value is determined is likely before the UI is visible.
enum class StartupVisibility {
  kUnknown = 0,
  kBackground = 1,
  kForeground = 2,
};

// Denotes the type of EntropyProvider to use for one-time randomization.
enum class EntropyProviderType {
  kDefault = 0,  // Use CreateDefaultEntropyProvider().
  kLow = 1,      // Use CreateLowEntropyProvider().
};

// Responsible for managing MetricsService state prefs, specifically the UMA
// client id and low entropy source. Code outside the metrics directory should
// not be instantiating or using this class directly.
class MetricsStateManager final {
 public:
  // A callback that can be invoked to store client info to persistent storage.
  // Storing an empty client_id will resulted in the backup being voided.
  typedef base::RepeatingCallback<void(const ClientInfo& client_info)>
      StoreClientInfoCallback;

  // A callback that can be invoked to load client info stored through the
  // StoreClientInfoCallback.
  typedef base::RepeatingCallback<std::unique_ptr<ClientInfo>(void)>
      LoadClientInfoCallback;

  MetricsStateManager(const MetricsStateManager&) = delete;
  MetricsStateManager& operator=(const MetricsStateManager&) = delete;

  ~MetricsStateManager();

  std::unique_ptr<MetricsProvider> GetProvider();

  // Returns true if the user has consented to sending metric reports, and there
  // is no other reason to disable reporting. One such reason is client
  // sampling, and this client isn't in the sample.
  bool IsMetricsReportingEnabled();

  // Returns the client ID for this client, or the empty string if the user is
  // not opted in to metrics reporting.
  const std::string& client_id() const { return client_id_; }

  // Returns the low entropy source for this client.
  int GetLowEntropySource();

  // The CleanExitBeacon, used to determine whether the previous Chrome browser
  // session terminated gracefully.
  CleanExitBeacon* clean_exit_beacon() { return &clean_exit_beacon_; }
  const CleanExitBeacon* clean_exit_beacon() const {
    return &clean_exit_beacon_;
  }

  // Returns true if the session was deemed a background session during startup.
  // Note that this is not equivalent to !is_foreground_session() because the
  // type of session may be unknown.
  bool is_background_session() const {
    return startup_visibility_ == StartupVisibility::kBackground;
  }

  // Returns true if the session was deemed a foreground session during startup.
  // Note that this is not equivalent to !is_background_session() because the
  // type of session may be unknown.
  bool is_foreground_session() const {
    return startup_visibility_ == StartupVisibility::kForeground;
  }

  // Instantiates the FieldTrialList. Uses |enable_gpu_benchmarking_switch| to
  // set up the FieldTrialList for benchmarking runs. Uses
  // |entropy_provider_type| to determine the type of EntropyProvider to use for
  // one-time randomization. See CreateLowEntropyProvider() and
  // CreateDefaultEntropyProvider() for more details.
  //
  // Side effect: Initializes |clean_exit_beacon_|.
  void InstantiateFieldTrialList(
      const char* enable_gpu_benchmarking_switch = nullptr,
      EntropyProviderType entropy_provider_type =
          EntropyProviderType::kDefault);

  // Signals whether the session has shutdown cleanly. Passing `false` for
  // |has_session_shutdown_cleanly| means that Chrome has launched and has not
  // yet shut down safely. Passing `true` signals that Chrome has shut down
  // safely.
  //
  // Seeing a call with `false` without a matching call with `true` suggests
  // that Chrome crashed or otherwise did not shut down cleanly, e.g. maybe the
  // OS crashed.
  //
  // If |write_synchronously| is true, then |has_session_shutdown_cleanly| is
  // written to disk synchronously. If false, a write is scheduled, and for
  // clients in the Extended Variations Safe Mode experiment, a synchronous
  // write is done, too.
  //
  // Note: |write_synchronously| should be true only for the Extended Variations
  // Safe Mode experiment.
  void LogHasSessionShutdownCleanly(bool has_session_shutdown_cleanly,
                                    bool write_synchronously = false);

  // Forces the client ID to be generated. This is useful in case it's needed
  // before recording.
  void ForceClientIdCreation();

  // Checks if this install was cloned or imaged from another machine. If a
  // clone is detected, resets the client id and low entropy source. This
  // should not be called more than once.
  void CheckForClonedInstall();

  // Checks if the cloned install detector says that client ids should be reset.
  bool ShouldResetClientIdsOnClonedInstall();

  // Returns the preferred entropy provider used to seed persistent activities
  // based on whether or not metrics reporting is permitted on this client.
  //
  // If there's consent to report metrics or this is the first run of Chrome,
  // this method returns an entropy provider that has a high source of entropy,
  // partially based on the client ID or provisional client ID. Otherwise, it
  // returns an entropy provider that is based on a low entropy source.
  std::unique_ptr<const base::FieldTrial::EntropyProvider>
  CreateDefaultEntropyProvider();

  // Returns an entropy provider that is based on a low entropy source. This
  // provider is the same type of provider returned by
  // CreateDefaultEntropyProvider() when there's no consent to report metrics,
  // but will be a new instance.
  std::unique_ptr<const base::FieldTrial::EntropyProvider>
  CreateLowEntropyProvider();

  // Creates the MetricsStateManager, enforcing that only a single instance
  // of the class exists at a time. Returns nullptr if an instance exists
  // already.
  //
  // On Windows, |backup_registry_key| is used to store a backup of the clean
  // exit beacon. It is ignored on other platforms.
  //
  // |user_data_dir| is the path to the client's user data directory. If empty,
  // a separate file will not be used for Variations Safe Mode prefs.
  //
  // |startup_visibility| denotes whether this session is expected to come to
  // the foreground.
  //
  // TODO(crbug/1241702): Remove |channel| at the end of the Extended Variations
  // Safe Mode experiment. |channel| is used to enable the experiment on only
  // certain channels.
  static std::unique_ptr<MetricsStateManager> Create(
      PrefService* local_state,
      EnabledStateProvider* enabled_state_provider,
      const std::wstring& backup_registry_key,
      const base::FilePath& user_data_dir,
      StartupVisibility startup_visibility = StartupVisibility::kUnknown,
      version_info::Channel channel = version_info::Channel::UNKNOWN,
      StoreClientInfoCallback store_client_info = StoreClientInfoCallback(),
      LoadClientInfoCallback load_client_info = LoadClientInfoCallback(),
      base::StringPiece external_client_id = base::StringPiece());

  // Registers local state prefs used by this class.
  static void RegisterPrefs(PrefRegistrySimple* registry);

 private:
  FRIEND_TEST_ALL_PREFIXES(MetricsStateManagerTest, CheckProviderResetIds);
  FRIEND_TEST_ALL_PREFIXES(MetricsStateManagerTest, CheckProviderLogNormal);
  FRIEND_TEST_ALL_PREFIXES(MetricsStateManagerTest,
                           CheckProviderLogNormalWithParams);
  FRIEND_TEST_ALL_PREFIXES(
      MetricsStateManagerTest,
      CheckProviderResetIds_PreviousIdOnlyReportInResetSession);
  FRIEND_TEST_ALL_PREFIXES(MetricsStateManagerTest, EntropySourceUsed_Low);
  FRIEND_TEST_ALL_PREFIXES(MetricsStateManagerTest, EntropySourceUsed_High);
  FRIEND_TEST_ALL_PREFIXES(MetricsStateManagerTest,
                           ProvisionalClientId_PromotedToClientId);
  FRIEND_TEST_ALL_PREFIXES(MetricsStateManagerTest,
                           ProvisionalClientId_NotPersisted);
  FRIEND_TEST_ALL_PREFIXES(MetricsStateManagerTest, ResetBackup);
  FRIEND_TEST_ALL_PREFIXES(MetricsStateManagerTest, ResetMetricsIDs);

  // Designates which entropy source was returned from this class.
  // This is used for testing to validate that we return the correct source
  // depending on the state of the service.
  enum EntropySourceType {
    ENTROPY_SOURCE_NONE,
    ENTROPY_SOURCE_LOW,
    ENTROPY_SOURCE_HIGH,
    ENTROPY_SOURCE_ENUM_SIZE,
  };

  // These values are persisted to logs. Entries should not be renumbered and
  // numerical values should never be reused.
  enum class ClientIdSource {
    // Recorded when the client ID in Local State matches the cached copy.
    kClientIdMatches = 0,
    // Recorded when we are somehow missing the cached client ID and we are
    // able to recover it from the Local State.
    kClientIdFromLocalState = 1,
    // Recorded when we are somehow missing the client ID stored in Local State
    // yet are able to recover it from a backup location.
    kClientIdBackupRecovered = 2,
    // Recorded when we are somehow missing the client ID in Local State, cache
    // and backup and there is no provisional client ID, so a new client ID is
    // generated.
    kClientIdNew = 3,
    // Recorded when we are somehow missing the client ID in Local State, cache
    // and backup, so we promote the provisional client ID.
    kClientIdFromProvisionalId = 4,
    // Recorded when the client ID is passed in from external source.
    // This is needed for Lacros since the client id is passed in from
    // ash chrome.
    kClientIdFromExternal = 5,
    kMaxValue = kClientIdFromExternal,
  };

  // Creates the MetricsStateManager with the given |local_state|. Uses
  // |enabled_state_provider| to query whether there is consent for metrics
  // reporting, and if it is enabled. Clients should instead use Create(), which
  // enforces that a single instance of this class be alive at any given time.
  // |store_client_info| should back up client info to persistent storage such
  // that it is later retrievable by |load_client_info|.
  MetricsStateManager(PrefService* local_state,
                      EnabledStateProvider* enabled_state_provider,
                      const std::wstring& backup_registry_key,
                      const base::FilePath& user_data_dir,
                      StartupVisibility startup_visibility,
                      version_info::Channel channel,
                      StoreClientInfoCallback store_client_info,
                      LoadClientInfoCallback load_client_info,
                      base::StringPiece external_client_id);

  // Returns a MetricsStateManagerProvider instance and sets its
  // |log_normal_metric_state_.gen| with the provided random seed.
  std::unique_ptr<MetricsProvider> GetProviderAndSetRandomSeedForTesting(
      int64_t seed);

  // Backs up the current client info via |store_client_info_|.
  void BackUpCurrentClientInfo();

  // Loads the client info via |load_client_info_|.
  std::unique_ptr<ClientInfo> LoadClientInfo();

  // Returns the high entropy source for this client, which is composed of a
  // client ID and the low entropy source. This is intended to be unique for
  // each install. UMA must be enabled (and |client_id_| must be set) or
  // |provisional_client_id_| must be set before calling this.
  std::string GetHighEntropySource();

  // Returns the old low entropy source for this client.
  int GetOldLowEntropySource();

  // Updates |entropy_source_returned_| with |type| iff the current value is
  // ENTROPY_SOURCE_NONE and logs the new value in a histogram.
  void UpdateEntropySourceReturnedValue(EntropySourceType type);

  // Returns the first entropy source that was returned by this service since
  // start up, or NONE if neither was returned yet. This is exposed for testing
  // only.
  EntropySourceType entropy_source_returned() const {
    return entropy_source_returned_;
  }

  std::string initial_client_id_for_testing() const {
    return initial_client_id_;
  }

  // Reset the client id and low entropy source if the kMetricsResetMetricIDs
  // pref is true.
  void ResetMetricsIDsIfNecessary();

#if BUILDFLAG(IS_CHROMEOS_ASH)
  // Log to structured metrics when the client id is changed.
  void LogClientIdChanged(metrics::structured::NeutrinoDevicesLocation location,
                          std::string previous_client_id);
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

  // Whether an instance of this class exists. Used to enforce that there aren't
  // multiple instances of this class at a given time.
  static bool instance_exists_;

  // Weak pointer to the local state prefs store.
  const raw_ptr<PrefService> local_state_;

  // Weak pointer to an enabled state provider. Used to know whether the user
  // has consented to reporting, and if reporting should be done.
  raw_ptr<EnabledStateProvider> enabled_state_provider_;

  // A callback run during client id creation so this MetricsStateManager can
  // store a backup of the newly generated ID.
  const StoreClientInfoCallback store_client_info_;

  // A callback run if this MetricsStateManager can't get the client id from
  // its typical location and wants to attempt loading it from this backup.
  const LoadClientInfoCallback load_client_info_;

  // A beacon used to determine whether the previous Chrome browser session
  // terminated gracefully.
  CleanExitBeacon clean_exit_beacon_;

  // The identifier that's sent to the server with the log reports.
  std::string client_id_;

  // A provisional client id that's generated at start up before we know whether
  // metrics consent has been received from the client. This id becomes the
  // |client_id_| if consent is given within the same session, or is cleared
  // otherwise. Does not control transmission of UMA metrics, only used for the
  // high entropy source used for field trial randomization so that field
  // trials don't toggle state between first and second run.
  std::string provisional_client_id_;

  // The client id that was used do field trial randomization. This field should
  // only be changed when we need to do group assignment. |initial_client_id|
  // should left blank iff a client id was not used to do field trial
  // randomization.
  std::string initial_client_id_;

  // If not empty, use an external client id passed in from another browser as
  // |client_id_|. This is needed for the Lacros browser where client id needs
  // be passed in from ash chrome.
  std::string external_client_id_;

  // An instance of EntropyState for getting the entropy source values.
  EntropyState entropy_state_;

  // The last entropy source returned by this service, used for testing.
  EntropySourceType entropy_source_returned_;

  // The value of prefs::kMetricsResetIds seen upon startup, i.e., the value
  // that was appropriate in the previous session. Used when reporting previous
  // session (stability) data.
  bool metrics_ids_were_reset_;

  // The value of the metrics id before reseting. Only possibly valid if the
  // metrics id was reset. May be blank if the metrics id was reset but Chrome
  // has no record of what the previous metrics id was.
  std::string previous_client_id_;

  // The detector for understanding the cloned nature of the install so that we
  // can reset client ids.
  ClonedInstallDetector cloned_install_detector_;

  // The type of session, e.g. a foreground session, at startup. This value is
  // used only during startup. On Android WebLayer, Android WebView, and iOS,
  // the visibility is unknown at this point in startup.
  const StartupVisibility startup_visibility_;
};

}  // namespace metrics

#endif  // COMPONENTS_METRICS_METRICS_STATE_MANAGER_H_