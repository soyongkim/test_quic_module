// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/network/metrics/connection_info_metrics_logger.h"

#include <memory>

#include "base/run_loop.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/task_environment.h"
#include "chromeos/dbus/shill/shill_service_client.h"
#include "chromeos/network/metrics/shill_connect_result.h"
#include "chromeos/network/network_handler_test_helper.h"
#include "chromeos/network/network_state_handler.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

namespace chromeos {

namespace {

// Note: The actual network types used does not matter as networks are kept
// track of by GUID. This test uses Cellular and Wifi, but any combination of
// network type may be used.
const char kCellularConnectResultAllHistogram[] =
    "Network.Ash.Cellular.ConnectionResult.All";
const char kWifiConnectResultAllHistogram[] =
    "Network.Ash.WiFi.ConnectionResult.All";

const char kCellularGuid[] = "test_guid";
const char kCellularServicePath[] = "/service/network";
const char kCellularName[] = "network_name";

const char kWifiGuid[] = "test_guid2";
const char kWifiServicePath[] = "/service/network2";
const char kWifiName[] = "network_name2";

}  // namespace

class ConnectionInfoMetricsLoggerTest : public testing::Test {
 public:
  ConnectionInfoMetricsLoggerTest() = default;
  ConnectionInfoMetricsLoggerTest(const ConnectionInfoMetricsLoggerTest&) =
      delete;
  ConnectionInfoMetricsLoggerTest& operator=(
      const ConnectionInfoMetricsLoggerTest&) = delete;
  ~ConnectionInfoMetricsLoggerTest() override = default;

  void SetUp() override {
    network_handler_test_helper_ = std::make_unique<NetworkHandlerTestHelper>();
    histogram_tester_ = std::make_unique<base::HistogramTester>();

    shill_service_client_ = ShillServiceClient::Get()->GetTestInterface();
    shill_service_client_->ClearServices();
    base::RunLoop().RunUntilIdle();

    network_handler_test_helper_->RegisterPrefs(profile_prefs_.registry(),
                                                local_state_.registry());

    network_handler_test_helper_->InitializePrefs(&profile_prefs_,
                                                  &local_state_);
  }

  void TearDown() override {
    shill_service_client_->ClearServices();
    network_handler_test_helper_.reset();
  }

  void SetUpGenericCellularNetwork() {
    shill_service_client_->AddService(kCellularServicePath, kCellularGuid,
                                      kCellularName, shill::kTypeCellular,
                                      shill::kStateIdle,
                                      /*visible=*/true);
    base::RunLoop().RunUntilIdle();
  }

  void SetUpGenericWifiNetwork() {
    shill_service_client_->AddService(kWifiServicePath, kWifiGuid, kWifiName,
                                      shill::kTypeWifi, shill::kStateIdle,
                                      /*visible=*/true);
    base::RunLoop().RunUntilIdle();
  }

  void SetShillState(const std::string& service_path,
                     const std::string& shill_state) {
    shill_service_client_->SetServiceProperty(
        service_path, shill::kStateProperty, base::Value(shill_state));
    base::RunLoop().RunUntilIdle();
  }

  void RequestDisconnect(const std::string& service_path) {
    chromeos::NetworkHandler::Get()
        ->connection_info_metrics_logger_->DisconnectRequested(service_path);
  }

 protected:
  base::test::TaskEnvironment task_environment_;
  std::unique_ptr<base::HistogramTester> histogram_tester_;
  std::unique_ptr<NetworkHandlerTestHelper> network_handler_test_helper_;
  ShillServiceClient::TestInterface* shill_service_client_;
  TestingPrefServiceSimple profile_prefs_;
  TestingPrefServiceSimple local_state_;
};

TEST_F(ConnectionInfoMetricsLoggerTest, AutoStatusTransitions) {
  SetUpGenericCellularNetwork();

  // Successful connect from disconnected to connected.
  SetShillState(kCellularServicePath, shill::kStateIdle);
  histogram_tester_->ExpectTotalCount(kCellularConnectResultAllHistogram, 0);
  SetShillState(kCellularServicePath, shill::kStateOnline);
  histogram_tester_->ExpectTotalCount(kCellularConnectResultAllHistogram, 1);
  histogram_tester_->ExpectBucketCount(kCellularConnectResultAllHistogram,
                                       ShillConnectResult::kSuccess, 1);

  // Successful connect from connecting to connected.
  SetShillState(kCellularServicePath, shill::kStateAssociation);
  histogram_tester_->ExpectTotalCount(kCellularConnectResultAllHistogram, 1);
  SetShillState(kCellularServicePath, shill::kStateOnline);
  histogram_tester_->ExpectTotalCount(kCellularConnectResultAllHistogram, 2);
  histogram_tester_->ExpectBucketCount(kCellularConnectResultAllHistogram,
                                       ShillConnectResult::kSuccess, 2);

  // Fail to connect from connecting to disconnected without disconnection
  // request.
  SetShillState(kCellularServicePath, shill::kStateAssociation);
  histogram_tester_->ExpectTotalCount(kCellularConnectResultAllHistogram, 2);
  SetShillState(kCellularServicePath, shill::kStateIdle);
  histogram_tester_->ExpectTotalCount(kCellularConnectResultAllHistogram, 3);
  histogram_tester_->ExpectBucketCount(kCellularConnectResultAllHistogram,
                                       ShillConnectResult::kUnknown, 1);
}

TEST_F(ConnectionInfoMetricsLoggerTest, MultipleNetworksStatusRecorded) {
  SetUpGenericCellularNetwork();
  SetUpGenericWifiNetwork();

  SetShillState(kCellularServicePath, shill::kStateIdle);
  histogram_tester_->ExpectTotalCount(kCellularConnectResultAllHistogram, 0);
  histogram_tester_->ExpectTotalCount(kWifiConnectResultAllHistogram, 0);

  SetShillState(kCellularServicePath, shill::kStateOnline);
  histogram_tester_->ExpectTotalCount(kCellularConnectResultAllHistogram, 1);
  histogram_tester_->ExpectTotalCount(kWifiConnectResultAllHistogram, 0);
  histogram_tester_->ExpectBucketCount(kCellularConnectResultAllHistogram,
                                       ShillConnectResult::kSuccess, 1);

  SetShillState(kWifiServicePath, shill::kStateOnline);
  histogram_tester_->ExpectTotalCount(kCellularConnectResultAllHistogram, 1);
  histogram_tester_->ExpectTotalCount(kWifiConnectResultAllHistogram, 1);
  histogram_tester_->ExpectBucketCount(kWifiConnectResultAllHistogram,
                                       ShillConnectResult::kSuccess, 1);

  SetShillState(kWifiServicePath, shill::kStateAssociation);
  SetShillState(kWifiServicePath, shill::kStateIdle);
  histogram_tester_->ExpectTotalCount(kCellularConnectResultAllHistogram, 1);
  histogram_tester_->ExpectTotalCount(kWifiConnectResultAllHistogram, 2);
  histogram_tester_->ExpectBucketCount(kWifiConnectResultAllHistogram,
                                       ShillConnectResult::kSuccess, 1);
}

TEST_F(ConnectionInfoMetricsLoggerTest, DisconnectRequested) {
  SetUpGenericCellularNetwork();

  // Disconnect Requested while connected.
  SetShillState(kCellularServicePath, shill::kStateOnline);
  histogram_tester_->ExpectTotalCount(kCellularConnectResultAllHistogram, 1);
  RequestDisconnect(kCellularServicePath);
  histogram_tester_->ExpectTotalCount(kCellularConnectResultAllHistogram, 1);
  SetShillState(kCellularServicePath, shill::kStateReady);
  histogram_tester_->ExpectTotalCount(kCellularConnectResultAllHistogram, 1);

  // Disconnect Requested while disconnected.
  SetShillState(kCellularServicePath, shill::kStateIdle);
  histogram_tester_->ExpectTotalCount(kCellularConnectResultAllHistogram, 1);
  RequestDisconnect(kCellularServicePath);
  SetShillState(kCellularServicePath, shill::kStateDisconnect);
  histogram_tester_->ExpectTotalCount(kCellularConnectResultAllHistogram, 1);

  // Disconnect Requested while connecting.
  SetShillState(kCellularServicePath, shill::kStateAssociation);
  histogram_tester_->ExpectTotalCount(kCellularConnectResultAllHistogram, 1);
  RequestDisconnect(kCellularServicePath);
  SetShillState(kCellularServicePath, shill::kStateConfiguration);
  histogram_tester_->ExpectTotalCount(kCellularConnectResultAllHistogram, 1);

  // Cancel connect attempt while connecting state.
  SetShillState(kCellularServicePath, shill::kStateAssociation);
  histogram_tester_->ExpectTotalCount(kCellularConnectResultAllHistogram, 1);
  SetShillState(kCellularServicePath, shill::kStateAssociation);
  RequestDisconnect(kCellularServicePath);
  SetShillState(kCellularServicePath, shill::kStateIdle);
  histogram_tester_->ExpectTotalCount(kCellularConnectResultAllHistogram, 1);
}

}  // namespace chromeos