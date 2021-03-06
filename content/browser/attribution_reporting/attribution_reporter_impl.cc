// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/attribution_reporting/attribution_reporter_impl.h"

#include <string>

#include "base/bind.h"
#include "base/callback.h"
#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "base/rand_util.h"
#include "base/time/clock.h"
#include "base/time/time.h"
#include "content/browser/attribution_reporting/attribution_manager.h"
#include "content/browser/attribution_reporting/attribution_network_sender_impl.h"
#include "content/browser/attribution_reporting/attribution_report.h"
#include "content/browser/attribution_reporting/attribution_utils.h"
#include "content/browser/attribution_reporting/send_result.h"
#include "content/browser/storage_partition_impl.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/network_service_instance.h"
#include "content/public/common/content_client.h"
#include "services/network/public/cpp/network_connection_tracker.h"
#include "url/gurl.h"

namespace content {

namespace {

// Called when |report| is to be sent over network, for logging metrics.
void LogMetricsOnReportSend(const AttributionReport& report) {
  // Reports sent from the WebUI should not log metrics.
  if (report.report_time() == base::Time::Min())
    return;

  // Use a large time range to capture users that might not open the browser for
  // a long time while a conversion report is pending. Revisit this range if it
  // is non-ideal for real world data.
  base::Time now = base::Time::Now();
  base::Time original_report_time =
      ComputeReportTime(report.source(), report.conversion_time());
  base::TimeDelta time_since_original_report_time = now - original_report_time;
  base::UmaHistogramCustomTimes(
      "Conversions.ExtraReportDelay2", time_since_original_report_time,
      base::Seconds(1), base::Days(24), /*buckets=*/100);

  base::TimeDelta time_from_conversion_to_report_send =
      report.report_time() - report.conversion_time();
  UMA_HISTOGRAM_COUNTS_1000("Conversions.TimeFromConversionToReportSend",
                            time_from_conversion_to_report_send.InHours());
}

}  // namespace

AttributionReporterImpl::AttributionReporterImpl(
    StoragePartitionImpl* storage_partition,
    const base::Clock* clock,
    Callback callback)
    : clock_(clock),
      callback_(std::move(callback)),
      partition_(storage_partition),
      network_sender_(
          std::make_unique<AttributionNetworkSenderImpl>(storage_partition)) {
  DCHECK(clock_);
  DCHECK(partition_);
}

AttributionReporterImpl::~AttributionReporterImpl() {
  if (network_connection_tracker_)
    network_connection_tracker_->RemoveNetworkConnectionObserver(this);
}

void AttributionReporterImpl::AddReportsToQueue(
    std::vector<AttributionReport> reports) {
  DCHECK(!reports.empty());

  // Shuffle new reports to provide plausible deniability on the ordering of
  // reports that share the same |report_time|. This is important because
  // multiple conversions for the same impression share the same report time if
  // they are within the same reporting window, and we do not want to allow
  // ordering on their conversion metadata bits.
  base::RandomShuffle(reports.begin(), reports.end());

  for (AttributionReport& report : reports) {
    report_queue_.push(std::move(report));
  }
  MaybeScheduleNextReport();
}

void AttributionReporterImpl::RemoveAllReportsFromQueue() {
  while (!report_queue_.empty()) {
    AttributionReport report = report_queue_.top();
    report_queue_.pop();
    callback_.Run(std::move(report),
                  SendResult(SendResult::Status::kRemovedFromQueue,
                             /*http_response_code=*/0));
  }
}

void AttributionReporterImpl::SetNetworkSenderForTesting(
    std::unique_ptr<NetworkSender> network_sender) {
  network_sender_ = std::move(network_sender);
}

void AttributionReporterImpl::SetNetworkConnectionTracker(
    network::NetworkConnectionTracker* network_connection_tracker) {
  DCHECK(network_connection_tracker);
  DCHECK(!network_connection_tracker_);

  network_connection_tracker_ = network_connection_tracker;
  network_connection_tracker_->AddNetworkConnectionObserver(this);
}

void AttributionReporterImpl::OnConnectionChanged(
    network::mojom::ConnectionType connection_type) {
  offline_ = connection_type == network::mojom::ConnectionType::CONNECTION_NONE;
}

void AttributionReporterImpl::SendNextReport() {
  if (report_queue_.empty())
    return;

  // Send the next report and remove it from the queue.
  AttributionReport report = report_queue_.top();
  report_queue_.pop();
  if (GetContentClient()->browser()->IsConversionMeasurementOperationAllowed(
          partition_->browser_context(),
          ContentBrowserClient::ConversionMeasurementOperation::kReport,
          &report.source().impression_origin(),
          &report.source().conversion_origin(),
          &report.source().reporting_origin())) {
    if (!network_connection_tracker_)
      SetNetworkConnectionTracker(content::GetNetworkConnectionTracker());

    // If there's no network connection, drop the report and tell the manager to
    // retry it later.
    if (offline_) {
      callback_.Run(std::move(report), SendResult(SendResult::Status::kOffline,
                                                  /*http_response_code=*/0));
    } else {
      LogMetricsOnReportSend(report);

      GURL report_url = report.ReportURL();
      std::string report_body = report.ReportBody();
      network_sender_->SendReport(std::move(report_url), std::move(report_body),
                                  base::BindOnce(callback_, std::move(report)));
    }
  } else {
    // If measurement is disallowed, just drop the report on the floor. We need
    // to make sure we forward that the report was "sent" to ensure it is
    // deleted from storage, etc. This simulates sending the report through a
    // null channel.
    callback_.Run(std::move(report), SendResult(SendResult::Status::kDropped,
                                                /*http_response_code=*/0));
  }
  MaybeScheduleNextReport();
}

void AttributionReporterImpl::MaybeScheduleNextReport() {
  if (report_queue_.empty())
    return;

  send_report_timer_.Stop();
  base::Time current_time = clock_->Now();
  base::Time report_time = report_queue_.top().report_time();

  // Start a timer to wait until the next report is ready to be sent. This
  // purposefully yields the thread for every report that gets scheduled.
  // Unretained is safe because the task should never actually be posted if the
  // timer itself is destroyed
  send_report_timer_.Start(
      FROM_HERE,
      (report_time < current_time) ? base::TimeDelta()
                                   : report_time - current_time,
      base::BindOnce(&AttributionReporterImpl::SendNextReport,
                     base::Unretained(this)));
}

bool AttributionReporterImpl::ReportComparator::operator()(
    const AttributionReport& a,
    const AttributionReport& b) const {
  // Returns whether a should appear before b in ordering. Because
  // std::priority_queue is max priority queue, we used greater then to make a
  // min priority queue.
  return a.report_time() > b.report_time();
}

}  // namespace content
