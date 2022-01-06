// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/metrics/structured/external_metrics.h"

#include <memory>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/logging.h"
#include "base/test/task_environment.h"
#include "components/metrics/structured/storage.pb.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace metrics {
namespace structured {
namespace {

using testing::UnorderedElementsAre;

// Make a simple testing proto with one |uma_events| message for each id in
// |ids|.
EventsProto MakeTestingProto(const std::vector<uint64_t>& ids) {
  EventsProto proto;

  for (const auto id : ids) {
    auto* event = proto.add_uma_events();
    event->set_profile_event_id(id);
  }

  return proto;
}

// Check that |proto| is consistent with the proto that would be generated by
// MakeTestingProto(ids).
void AssertEqualsTestingProto(const EventsProto& proto,
                              const std::vector<uint64_t>& ids) {
  ASSERT_EQ(proto.uma_events().size(), static_cast<int>(ids.size()));
  ASSERT_TRUE(proto.non_uma_events().empty());

  for (size_t i = 0; i < ids.size(); ++i) {
    const auto& event = proto.uma_events(i);
    ASSERT_EQ(event.profile_event_id(), ids[i]);
    ASSERT_FALSE(event.has_event_name_hash());
    ASSERT_TRUE(event.metrics().empty());
  }
}

}  // namespace

class ExternalMetricsTest : public testing::Test {
 public:
  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }

  void Init() {
    // We don't use the scheduling feature when testing ExternalMetrics, instead
    // we just call CollectMetrics directly. So make up a time interval here
    // that we'll never reach in a test.
    const auto one_hour = base::Hours(1);
    external_metrics_ = std::make_unique<ExternalMetrics>(
        temp_dir_.GetPath(), one_hour,
        base::BindRepeating(&ExternalMetricsTest::OnEventsCollected,
                            base::Unretained(this)));
  }

  void CollectEvents() {
    external_metrics_->CollectEvents();
    Wait();
    CHECK(proto_.has_value());
  }

  void OnEventsCollected(const EventsProto& proto) {
    proto_ = std::move(proto);
  }

  void WriteToDisk(const std::string& name, const EventsProto& proto) {
    CHECK(base::WriteFile(temp_dir_.GetPath().Append(name),
                          proto.SerializeAsString()));
  }

  void WriteToDisk(const std::string& name, const std::string& str) {
    CHECK(base::WriteFile(temp_dir_.GetPath().Append(name), str));
  }

  void Wait() { task_environment_.RunUntilIdle(); }

  base::ScopedTempDir temp_dir_;
  std::unique_ptr<ExternalMetrics> external_metrics_;
  absl::optional<EventsProto> proto_;

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::MainThreadType::UI,
      base::test::TaskEnvironment::ThreadPoolExecutionMode::QUEUED};
};

TEST_F(ExternalMetricsTest, ReadOneFile) {
  // Make one proto with three events.
  WriteToDisk("myproto", MakeTestingProto({111, 222, 333}));
  Init();

  CollectEvents();

  // We should have correctly picked up the three events.
  AssertEqualsTestingProto(proto_.value(), {111, 222, 333});
  // And the directory should now be empty.
  ASSERT_TRUE(base::IsDirectoryEmpty(temp_dir_.GetPath()));
}

TEST_F(ExternalMetricsTest, ReadManyFiles) {
  // Make three protos with three events each.
  WriteToDisk("first", MakeTestingProto({111, 222, 333}));
  WriteToDisk("second", MakeTestingProto({444, 555, 666}));
  WriteToDisk("third", MakeTestingProto({777, 888, 999}));
  Init();

  CollectEvents();

  // We should have correctly picked up the nine events. Don't check for order,
  // because we can't guarantee the files will be read from disk in any
  // particular order.
  std::vector<int64_t> ids;
  for (const auto& event : proto_.value().uma_events()) {
    ids.push_back(event.profile_event_id());
  }
  ASSERT_THAT(
      ids, UnorderedElementsAre(111, 222, 333, 444, 555, 666, 777, 888, 999));

  // The directory should be empty after reading.
  ASSERT_TRUE(base::IsDirectoryEmpty(temp_dir_.GetPath()));
}

TEST_F(ExternalMetricsTest, ReadZeroFiles) {
  Init();
  CollectEvents();
  // We should have an empty proto.
  AssertEqualsTestingProto(proto_.value(), {});
  // And the directory should be empty too.
  ASSERT_TRUE(base::IsDirectoryEmpty(temp_dir_.GetPath()));
}

TEST_F(ExternalMetricsTest, CollectTwice) {
  Init();
  WriteToDisk("first", MakeTestingProto({111, 222, 333}));
  CollectEvents();
  AssertEqualsTestingProto(proto_.value(), {111, 222, 333});

  WriteToDisk("first", MakeTestingProto({444}));
  CollectEvents();
  AssertEqualsTestingProto(proto_.value(), {444});
}

TEST_F(ExternalMetricsTest, HandleCorruptFile) {
  Init();

  WriteToDisk("invalid", "surprise i'm not a proto");
  WriteToDisk("valid", MakeTestingProto({111, 222, 333}));

  CollectEvents();
  AssertEqualsTestingProto(proto_.value(), {111, 222, 333});
  // Should have deleted the invalid file too.
  ASSERT_TRUE(base::IsDirectoryEmpty(temp_dir_.GetPath()));
}

// TODO(b/181724341): Remove this when the bluetooth metrics feature is enabled
// by default.
TEST_F(ExternalMetricsTest, FilterBluetoothEvents) {
  // Event name hash for cros's BluetoothPairingStateChanged event.
  const uint64_t event_hash = UINT64_C(11839023048095184048);

  Init();

  // Use the profile_event_id as an marker of which event is which, and assign a
  // bluetooth event hash to ids > 100.
  EventsProto proto;
  for (const auto id : {101, 1, 2, 102, 103, 3, 104}) {
    auto* event = proto.add_uma_events();
    event->set_profile_event_id(id);
    if (id > 100)
      event->set_event_name_hash(event_hash);
  }
  WriteToDisk("proto", proto);

  CollectEvents();
  AssertEqualsTestingProto(proto_.value(), {1, 2, 3});
}

// TODO(crbug.com/1148168): Add a test for concurrent reading and writing here
// once we know the specifics of how the lock in cros is performed.

}  // namespace structured
}  // namespace metrics