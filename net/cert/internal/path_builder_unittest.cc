// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/internal/path_builder.h"

#include "base/base_paths.h"
#include "base/callback_forward.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/test/bind.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/task_environment.h"
#include "build/build_config.h"
#include "net/cert/internal/cert_error_params.h"
#include "net/cert/internal/cert_issuer_source_static.h"
#include "net/cert/internal/common_cert_errors.h"
#include "net/cert/internal/parsed_certificate.h"
#include "net/cert/internal/simple_path_builder_delegate.h"
#include "net/cert/internal/test_helpers.h"
#include "net/cert/internal/trust_store_collection.h"
#include "net/cert/internal/trust_store_in_memory.h"
#include "net/cert/internal/verify_certificate_chain.h"
#include "net/cert/pem.h"
#include "net/der/input.h"
#include "net/net_buildflags.h"
#include "net/test/test_certificate_data.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/boringssl/src/include/openssl/pool.h"

#if defined(OS_WIN)
#include "base/ranges/algorithm.h"
#include "base/win/wincrypt_shim.h"
#include "crypto/scoped_capi_types.h"
#include "net/cert/internal/trust_store_win.h"
#endif  // OS_WIN

namespace net {

// TODO(crbug.com/634443): Assert the errors for each ResultPath.

namespace {

using ::testing::_;
using ::testing::ElementsAre;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgPointee;
using ::testing::StrictMock;

// AsyncCertIssuerSourceStatic always returns its certs asynchronously.
class AsyncCertIssuerSourceStatic : public CertIssuerSource {
 public:
  class StaticAsyncRequest : public Request {
   public:
    explicit StaticAsyncRequest(ParsedCertificateList&& issuers) {
      issuers_.swap(issuers);
      issuers_iter_ = issuers_.begin();
    }

    StaticAsyncRequest(const StaticAsyncRequest&) = delete;
    StaticAsyncRequest& operator=(const StaticAsyncRequest&) = delete;

    ~StaticAsyncRequest() override = default;

    void GetNext(ParsedCertificateList* out_certs) override {
      if (issuers_iter_ != issuers_.end())
        out_certs->push_back(std::move(*issuers_iter_++));
    }

    ParsedCertificateList issuers_;
    ParsedCertificateList::iterator issuers_iter_;
  };

  ~AsyncCertIssuerSourceStatic() override = default;

  void SetAsyncGetCallback(base::RepeatingClosure closure) {
    async_get_callback_ = std::move(closure);
  }

  void AddCert(scoped_refptr<ParsedCertificate> cert) {
    static_cert_issuer_source_.AddCert(std::move(cert));
  }

  void SyncGetIssuersOf(const ParsedCertificate* cert,
                        ParsedCertificateList* issuers) override {}
  void AsyncGetIssuersOf(const ParsedCertificate* cert,
                         std::unique_ptr<Request>* out_req) override {
    num_async_gets_++;
    ParsedCertificateList issuers;
    static_cert_issuer_source_.SyncGetIssuersOf(cert, &issuers);
    std::unique_ptr<StaticAsyncRequest> req(
        new StaticAsyncRequest(std::move(issuers)));
    *out_req = std::move(req);
    if (!async_get_callback_.is_null())
      async_get_callback_.Run();
  }
  int num_async_gets() const { return num_async_gets_; }

 private:
  CertIssuerSourceStatic static_cert_issuer_source_;

  int num_async_gets_ = 0;
  base::RepeatingClosure async_get_callback_;
};

::testing::AssertionResult ReadTestPem(const std::string& file_name,
                                       const std::string& block_name,
                                       std::string* result) {
  const PemBlockMapping mappings[] = {
      {block_name.c_str(), result},
  };

  return ReadTestDataFromPemFile(file_name, mappings);
}

::testing::AssertionResult ReadTestCert(
    const std::string& file_name,
    scoped_refptr<ParsedCertificate>* result) {
  std::string der;
  ::testing::AssertionResult r = ReadTestPem(
      "net/data/ssl/certificates/" + file_name, "CERTIFICATE", &der);
  if (!r)
    return r;
  CertErrors errors;
  *result = ParsedCertificate::Create(
      bssl::UniquePtr<CRYPTO_BUFFER>(CRYPTO_BUFFER_new(
          reinterpret_cast<const uint8_t*>(der.data()), der.size(), nullptr)),
      {}, &errors);
  if (!*result) {
    return ::testing::AssertionFailure()
           << "ParseCertificate::Create() failed:\n"
           << errors.ToDebugString();
  }
  return ::testing::AssertionSuccess();
}

const void* kKey = &kKey;
class TrustStoreThatStoresUserData : public TrustStore {
 public:
  class Data : public base::SupportsUserData::Data {
   public:
    explicit Data(int value) : value(value) {}

    int value = 0;
  };

  // TrustStore implementation:
  void SyncGetIssuersOf(const ParsedCertificate* cert,
                        ParsedCertificateList* issuers) override {}
  CertificateTrust GetTrust(const ParsedCertificate* cert,
                            base::SupportsUserData* debug_data) const override {
    debug_data->SetUserData(kKey, std::make_unique<Data>(1234));
    return CertificateTrust::ForUnspecified();
  }
};

TEST(PathBuilderResultUserDataTest, ModifyUserDataInConstructor) {
  scoped_refptr<ParsedCertificate> a_by_b;
  ASSERT_TRUE(ReadTestCert("multi-root-A-by-B.pem", &a_by_b));
  SimplePathBuilderDelegate delegate(
      1024, SimplePathBuilderDelegate::DigestPolicy::kWeakAllowSha1);
  der::GeneralizedTime verify_time = {2017, 3, 1, 0, 0, 0};
  TrustStoreThatStoresUserData trust_store;

  // |trust_store| will unconditionally store user data in the
  // CertPathBuilder::Result. This ensures that the Result object has been
  // initialized before the first GetTrust call occurs (otherwise the test will
  // crash or fail on ASAN bots).
  CertPathBuilder path_builder(
      a_by_b, &trust_store, &delegate, verify_time, KeyPurpose::ANY_EKU,
      InitialExplicitPolicy::kFalse, {AnyPolicy()},
      InitialPolicyMappingInhibit::kFalse, InitialAnyPolicyInhibit::kFalse);
  CertPathBuilder::Result result = path_builder.Run();
  auto* data = static_cast<TrustStoreThatStoresUserData::Data*>(
      result.GetUserData(kKey));
  ASSERT_TRUE(data);
  EXPECT_EQ(1234, data->value);
}

class PathBuilderMultiRootTest : public ::testing::Test {
 public:
  PathBuilderMultiRootTest()
      : delegate_(1024,
                  SimplePathBuilderDelegate::DigestPolicy::kWeakAllowSha1) {}

  void SetUp() override {
    ASSERT_TRUE(ReadTestCert("multi-root-A-by-B.pem", &a_by_b_));
    ASSERT_TRUE(ReadTestCert("multi-root-B-by-C.pem", &b_by_c_));
    ASSERT_TRUE(ReadTestCert("multi-root-B-by-F.pem", &b_by_f_));
    ASSERT_TRUE(ReadTestCert("multi-root-C-by-D.pem", &c_by_d_));
    ASSERT_TRUE(ReadTestCert("multi-root-C-by-E.pem", &c_by_e_));
    ASSERT_TRUE(ReadTestCert("multi-root-D-by-D.pem", &d_by_d_));
    ASSERT_TRUE(ReadTestCert("multi-root-E-by-E.pem", &e_by_e_));
    ASSERT_TRUE(ReadTestCert("multi-root-F-by-E.pem", &f_by_e_));
  }

 protected:
  scoped_refptr<ParsedCertificate> a_by_b_, b_by_c_, b_by_f_, c_by_d_, c_by_e_,
      d_by_d_, e_by_e_, f_by_e_;

  SimplePathBuilderDelegate delegate_;
  der::GeneralizedTime time_ = {2017, 3, 1, 0, 0, 0};

  const InitialExplicitPolicy initial_explicit_policy_ =
      InitialExplicitPolicy::kFalse;
  const std::set<der::Input> user_initial_policy_set_ = {AnyPolicy()};
  const InitialPolicyMappingInhibit initial_policy_mapping_inhibit_ =
      InitialPolicyMappingInhibit::kFalse;
  const InitialAnyPolicyInhibit initial_any_policy_inhibit_ =
      InitialAnyPolicyInhibit::kFalse;
};

// Tests when the target cert has the same name and key as a trust anchor,
// however is signed by a different trust anchor. This should successfully build
// a path, however the trust anchor will be the signer of this cert.
//
// (This test is very similar to TestEndEntityHasSameNameAndSpkiAsTrustAnchor
// but with different data; also in this test the target cert itself is in the
// trust store).
TEST_F(PathBuilderMultiRootTest, TargetHasNameAndSpkiOfTrustAnchor) {
  TrustStoreInMemory trust_store;
  trust_store.AddTrustAnchor(a_by_b_);
  trust_store.AddTrustAnchor(b_by_f_);

  CertPathBuilder path_builder(
      a_by_b_, &trust_store, &delegate_, time_, KeyPurpose::ANY_EKU,
      initial_explicit_policy_, user_initial_policy_set_,
      initial_policy_mapping_inhibit_, initial_any_policy_inhibit_);

  auto result = path_builder.Run();

  ASSERT_TRUE(result.HasValidPath());
  const auto& path = *result.GetBestValidPath();
  ASSERT_EQ(2U, path.certs.size());
  EXPECT_EQ(a_by_b_, path.certs[0]);
  EXPECT_EQ(b_by_f_, path.certs[1]);
}

// If the target cert is has the same name and key as a trust anchor, however
// is NOT itself signed by a trust anchor, it fails. Although the provided SPKI
// is trusted, the certificate contents cannot be verified.
TEST_F(PathBuilderMultiRootTest, TargetWithSameNameAsTrustAnchorFails) {
  TrustStoreInMemory trust_store;
  trust_store.AddTrustAnchor(a_by_b_);

  CertPathBuilder path_builder(
      a_by_b_, &trust_store, &delegate_, time_, KeyPurpose::ANY_EKU,
      initial_explicit_policy_, user_initial_policy_set_,
      initial_policy_mapping_inhibit_, initial_any_policy_inhibit_);

  auto result = path_builder.Run();

  EXPECT_FALSE(result.HasValidPath());
}

// Test a failed path building when the trust anchor is provided as a
// supplemental certificate. Conceptually the following paths could be built:
//
//   B(C) <- C(D) <- [Trust anchor D]
//   B(C) <- C(D) <- D(D) <- [Trust anchor D]
//
// However the second one is extraneous given the shorter path.
TEST_F(PathBuilderMultiRootTest, SelfSignedTrustAnchorSupplementalCert) {
  TrustStoreInMemory trust_store;
  trust_store.AddTrustAnchor(d_by_d_);

  // The (extraneous) trust anchor D(D) is supplied as a certificate, as is the
  // intermediate needed for path building C(D).
  CertIssuerSourceStatic sync_certs;
  sync_certs.AddCert(d_by_d_);
  sync_certs.AddCert(c_by_d_);

  // C(D) is not valid at this time, so path building will fail.
  der::GeneralizedTime expired_time = {2016, 1, 1, 0, 0, 0};

  CertPathBuilder path_builder(
      b_by_c_, &trust_store, &delegate_, expired_time, KeyPurpose::ANY_EKU,
      initial_explicit_policy_, user_initial_policy_set_,
      initial_policy_mapping_inhibit_, initial_any_policy_inhibit_);
  path_builder.AddCertIssuerSource(&sync_certs);

  auto result = path_builder.Run();

  EXPECT_FALSE(result.HasValidPath());
  ASSERT_EQ(1U, result.paths.size());

  EXPECT_FALSE(result.paths[0]->IsValid());
  const auto& path0 = *result.paths[0];
  ASSERT_EQ(3U, path0.certs.size());
  EXPECT_EQ(b_by_c_, path0.certs[0]);
  EXPECT_EQ(c_by_d_, path0.certs[1]);
  EXPECT_EQ(d_by_d_, path0.certs[2]);
}

// Test verifying a certificate that is a trust anchor.
TEST_F(PathBuilderMultiRootTest, TargetIsSelfSignedTrustAnchor) {
  TrustStoreInMemory trust_store;
  trust_store.AddTrustAnchor(e_by_e_);
  // This is not necessary for the test, just an extra...
  trust_store.AddTrustAnchor(f_by_e_);

  CertPathBuilder path_builder(
      e_by_e_, &trust_store, &delegate_, time_, KeyPurpose::ANY_EKU,
      initial_explicit_policy_, user_initial_policy_set_,
      initial_policy_mapping_inhibit_, initial_any_policy_inhibit_);

  auto result = path_builder.Run();

  ASSERT_TRUE(result.HasValidPath());

  // Verifying a trusted leaf certificate is not permitted, however this
  // certificate is self-signed, and can chain to itself.
  const auto& path = *result.GetBestValidPath();
  ASSERT_EQ(2U, path.certs.size());
  EXPECT_EQ(e_by_e_, path.certs[0]);
  EXPECT_EQ(e_by_e_, path.certs[1]);
}

// If the target cert is directly issued by a trust anchor, it should verify
// without any intermediate certs being provided.
TEST_F(PathBuilderMultiRootTest, TargetDirectlySignedByTrustAnchor) {
  TrustStoreInMemory trust_store;
  trust_store.AddTrustAnchor(b_by_f_);

  CertPathBuilder path_builder(
      a_by_b_, &trust_store, &delegate_, time_, KeyPurpose::ANY_EKU,
      initial_explicit_policy_, user_initial_policy_set_,
      initial_policy_mapping_inhibit_, initial_any_policy_inhibit_);

  auto result = path_builder.Run();

  ASSERT_TRUE(result.HasValidPath());
  const auto& path = *result.GetBestValidPath();
  ASSERT_EQ(2U, path.certs.size());
  EXPECT_EQ(a_by_b_, path.certs[0]);
  EXPECT_EQ(b_by_f_, path.certs[1]);
}

// Test that async cert queries are not made if the path can be successfully
// built with synchronously available certs.
TEST_F(PathBuilderMultiRootTest, TriesSyncFirst) {
  TrustStoreInMemory trust_store;
  trust_store.AddTrustAnchor(e_by_e_);

  CertIssuerSourceStatic sync_certs;
  sync_certs.AddCert(b_by_f_);
  sync_certs.AddCert(f_by_e_);

  AsyncCertIssuerSourceStatic async_certs;
  async_certs.AddCert(b_by_c_);
  async_certs.AddCert(c_by_e_);

  CertPathBuilder path_builder(
      a_by_b_, &trust_store, &delegate_, time_, KeyPurpose::ANY_EKU,
      initial_explicit_policy_, user_initial_policy_set_,
      initial_policy_mapping_inhibit_, initial_any_policy_inhibit_);
  path_builder.AddCertIssuerSource(&async_certs);
  path_builder.AddCertIssuerSource(&sync_certs);

  auto result = path_builder.Run();

  EXPECT_TRUE(result.HasValidPath());
  EXPECT_EQ(0, async_certs.num_async_gets());
}

// If async queries are needed, all async sources will be queried
// simultaneously.
TEST_F(PathBuilderMultiRootTest, TestAsyncSimultaneous) {
  TrustStoreInMemory trust_store;
  trust_store.AddTrustAnchor(e_by_e_);

  CertIssuerSourceStatic sync_certs;
  sync_certs.AddCert(b_by_c_);
  sync_certs.AddCert(b_by_f_);

  AsyncCertIssuerSourceStatic async_certs1;
  async_certs1.AddCert(c_by_e_);

  AsyncCertIssuerSourceStatic async_certs2;
  async_certs2.AddCert(f_by_e_);

  CertPathBuilder path_builder(
      a_by_b_, &trust_store, &delegate_, time_, KeyPurpose::ANY_EKU,
      initial_explicit_policy_, user_initial_policy_set_,
      initial_policy_mapping_inhibit_, initial_any_policy_inhibit_);
  path_builder.AddCertIssuerSource(&async_certs1);
  path_builder.AddCertIssuerSource(&async_certs2);
  path_builder.AddCertIssuerSource(&sync_certs);

  auto result = path_builder.Run();

  EXPECT_TRUE(result.HasValidPath());
  EXPECT_EQ(1, async_certs1.num_async_gets());
  EXPECT_EQ(1, async_certs2.num_async_gets());
}

// Test that PathBuilder does not generate longer paths than necessary if one of
// the supplied certs is itself a trust anchor.
TEST_F(PathBuilderMultiRootTest, TestLongChain) {
  // Both D(D) and C(D) are trusted roots.
  TrustStoreInMemory trust_store;
  trust_store.AddTrustAnchor(d_by_d_);
  trust_store.AddTrustAnchor(c_by_d_);

  // Certs B(C), and C(D) are all supplied.
  CertIssuerSourceStatic sync_certs;
  sync_certs.AddCert(b_by_c_);
  sync_certs.AddCert(c_by_d_);

  CertPathBuilder path_builder(
      a_by_b_, &trust_store, &delegate_, time_, KeyPurpose::ANY_EKU,
      initial_explicit_policy_, user_initial_policy_set_,
      initial_policy_mapping_inhibit_, initial_any_policy_inhibit_);
  path_builder.AddCertIssuerSource(&sync_certs);

  auto result = path_builder.Run();

  ASSERT_TRUE(result.HasValidPath());

  // The result path should be A(B) <- B(C) <- C(D)
  // not the longer but also valid A(B) <- B(C) <- C(D) <- D(D)
  EXPECT_EQ(3U, result.GetBestValidPath()->certs.size());
}

// Test that PathBuilder will backtrack and try a different path if the first
// one doesn't work out.
TEST_F(PathBuilderMultiRootTest, TestBacktracking) {
  // Only D(D) is a trusted root.
  TrustStoreInMemory trust_store;
  trust_store.AddTrustAnchor(d_by_d_);

  // Certs B(F) and F(E) are supplied synchronously, thus the path
  // A(B) <- B(F) <- F(E) should be built first, though it won't verify.
  CertIssuerSourceStatic sync_certs;
  sync_certs.AddCert(b_by_f_);
  sync_certs.AddCert(f_by_e_);

  // Certs B(C), and C(D) are supplied asynchronously, so the path
  // A(B) <- B(C) <- C(D) <- D(D) should be tried second.
  AsyncCertIssuerSourceStatic async_certs;
  async_certs.AddCert(b_by_c_);
  async_certs.AddCert(c_by_d_);

  CertPathBuilder path_builder(
      a_by_b_, &trust_store, &delegate_, time_, KeyPurpose::ANY_EKU,
      initial_explicit_policy_, user_initial_policy_set_,
      initial_policy_mapping_inhibit_, initial_any_policy_inhibit_);
  path_builder.AddCertIssuerSource(&sync_certs);
  path_builder.AddCertIssuerSource(&async_certs);

  auto result = path_builder.Run();

  ASSERT_TRUE(result.HasValidPath());

  // The partial path should be returned even though it didn't reach a trust
  // anchor.
  ASSERT_EQ(2U, result.paths.size());
  EXPECT_FALSE(result.paths[0]->IsValid());
  ASSERT_EQ(3U, result.paths[0]->certs.size());
  EXPECT_EQ(a_by_b_, result.paths[0]->certs[0]);
  EXPECT_EQ(b_by_f_, result.paths[0]->certs[1]);
  EXPECT_EQ(f_by_e_, result.paths[0]->certs[2]);

  // The result path should be A(B) <- B(C) <- C(D) <- D(D)
  EXPECT_EQ(1U, result.best_result_index);
  EXPECT_TRUE(result.paths[1]->IsValid());
  const auto& path = *result.GetBestValidPath();
  ASSERT_EQ(4U, path.certs.size());
  EXPECT_EQ(a_by_b_, path.certs[0]);
  EXPECT_EQ(b_by_c_, path.certs[1]);
  EXPECT_EQ(c_by_d_, path.certs[2]);
  EXPECT_EQ(d_by_d_, path.certs[3]);
}

// Test that if no path to a trust anchor was found, the partial path is
// returned.
TEST_F(PathBuilderMultiRootTest, TestOnlyPartialPathResult) {
  TrustStoreInMemory trust_store;

  // Certs B(F) and F(E) are supplied synchronously, thus the path
  // A(B) <- B(F) <- F(E) should be built first, though it won't verify.
  CertIssuerSourceStatic sync_certs;
  sync_certs.AddCert(b_by_f_);
  sync_certs.AddCert(f_by_e_);

  CertPathBuilder path_builder(
      a_by_b_, &trust_store, &delegate_, time_, KeyPurpose::ANY_EKU,
      initial_explicit_policy_, user_initial_policy_set_,
      initial_policy_mapping_inhibit_, initial_any_policy_inhibit_);
  path_builder.AddCertIssuerSource(&sync_certs);

  auto result = path_builder.Run();

  EXPECT_FALSE(result.HasValidPath());

  // The partial path should be returned even though it didn't reach a trust
  // anchor.
  ASSERT_EQ(1U, result.paths.size());
  EXPECT_FALSE(result.paths[0]->IsValid());
  ASSERT_EQ(3U, result.paths[0]->certs.size());
  EXPECT_EQ(a_by_b_, result.paths[0]->certs[0]);
  EXPECT_EQ(b_by_f_, result.paths[0]->certs[1]);
  EXPECT_EQ(f_by_e_, result.paths[0]->certs[2]);
}

// Test that if two partial paths are returned, the first is marked as the best
// path.
TEST_F(PathBuilderMultiRootTest, TestTwoPartialPathResults) {
  TrustStoreInMemory trust_store;

  // Certs B(F) and F(E) are supplied synchronously, thus the path
  // A(B) <- B(F) <- F(E) should be built first, though it won't verify.
  CertIssuerSourceStatic sync_certs;
  sync_certs.AddCert(b_by_f_);
  sync_certs.AddCert(f_by_e_);

  // Certs B(C), and C(D) are supplied asynchronously, so the path
  // A(B) <- B(C) <- C(D) <- D(D) should be tried second.
  AsyncCertIssuerSourceStatic async_certs;
  async_certs.AddCert(b_by_c_);
  async_certs.AddCert(c_by_d_);

  CertPathBuilder path_builder(
      a_by_b_, &trust_store, &delegate_, time_, KeyPurpose::ANY_EKU,
      initial_explicit_policy_, user_initial_policy_set_,
      initial_policy_mapping_inhibit_, initial_any_policy_inhibit_);
  path_builder.AddCertIssuerSource(&sync_certs);
  path_builder.AddCertIssuerSource(&async_certs);

  auto result = path_builder.Run();

  EXPECT_FALSE(result.HasValidPath());

  // First partial path found should be marked as the best one.
  EXPECT_EQ(0U, result.best_result_index);

  ASSERT_EQ(2U, result.paths.size());
  EXPECT_FALSE(result.paths[0]->IsValid());
  ASSERT_EQ(3U, result.paths[0]->certs.size());
  EXPECT_EQ(a_by_b_, result.paths[0]->certs[0]);
  EXPECT_EQ(b_by_f_, result.paths[0]->certs[1]);
  EXPECT_EQ(f_by_e_, result.paths[0]->certs[2]);

  EXPECT_FALSE(result.paths[1]->IsValid());
  ASSERT_EQ(3U, result.paths[1]->certs.size());
  EXPECT_EQ(a_by_b_, result.paths[1]->certs[0]);
  EXPECT_EQ(b_by_c_, result.paths[1]->certs[1]);
  EXPECT_EQ(c_by_d_, result.paths[1]->certs[2]);
}

// Test that if no valid path is found, and the first invalid path is a partial
// path, but the 2nd invalid path ends with a cert with a trust record, the 2nd
// path should be preferred.
TEST_F(PathBuilderMultiRootTest, TestDistrustedPathPreferredOverPartialPath) {
  // Only D(D) has a trust record, but it is distrusted.
  TrustStoreInMemory trust_store;
  trust_store.AddDistrustedCertificateForTest(d_by_d_);

  // Certs B(F) and F(E) are supplied synchronously, thus the path
  // A(B) <- B(F) <- F(E) should be built first, though it won't verify.
  CertIssuerSourceStatic sync_certs;
  sync_certs.AddCert(b_by_f_);
  sync_certs.AddCert(f_by_e_);

  // Certs B(C), and C(D) are supplied asynchronously, so the path
  // A(B) <- B(C) <- C(D) <- D(D) should be tried second.
  AsyncCertIssuerSourceStatic async_certs;
  async_certs.AddCert(b_by_c_);
  async_certs.AddCert(c_by_d_);

  CertPathBuilder path_builder(
      a_by_b_, &trust_store, &delegate_, time_, KeyPurpose::ANY_EKU,
      initial_explicit_policy_, user_initial_policy_set_,
      initial_policy_mapping_inhibit_, initial_any_policy_inhibit_);
  path_builder.AddCertIssuerSource(&sync_certs);
  path_builder.AddCertIssuerSource(&async_certs);

  auto result = path_builder.Run();

  EXPECT_FALSE(result.HasValidPath());

  // The partial path should be returned even though it didn't reach a trust
  // anchor.
  ASSERT_EQ(2U, result.paths.size());
  EXPECT_FALSE(result.paths[0]->IsValid());
  ASSERT_EQ(3U, result.paths[0]->certs.size());
  EXPECT_EQ(a_by_b_, result.paths[0]->certs[0]);
  EXPECT_EQ(b_by_f_, result.paths[0]->certs[1]);
  EXPECT_EQ(f_by_e_, result.paths[0]->certs[2]);

  // The result path should be A(B) <- B(C) <- C(D) <- D(D)
  EXPECT_EQ(1U, result.best_result_index);
  EXPECT_FALSE(result.paths[1]->IsValid());
  const auto& path = *result.GetBestPathPossiblyInvalid();
  ASSERT_EQ(4U, path.certs.size());
  EXPECT_EQ(a_by_b_, path.certs[0]);
  EXPECT_EQ(b_by_c_, path.certs[1]);
  EXPECT_EQ(c_by_d_, path.certs[2]);
  EXPECT_EQ(d_by_d_, path.certs[3]);
}

// Test that whichever order CertIssuerSource returns the issuers, the path
// building still succeeds.
TEST_F(PathBuilderMultiRootTest, TestCertIssuerOrdering) {
  // Only D(D) is a trusted root.
  TrustStoreInMemory trust_store;
  trust_store.AddTrustAnchor(d_by_d_);

  for (bool reverse_order : {false, true}) {
    SCOPED_TRACE(reverse_order);
    std::vector<scoped_refptr<ParsedCertificate>> certs = {
        b_by_c_, b_by_f_, f_by_e_, c_by_d_, c_by_e_};
    CertIssuerSourceStatic sync_certs;
    if (reverse_order) {
      for (auto it = certs.rbegin(); it != certs.rend(); ++it)
        sync_certs.AddCert(*it);
    } else {
      for (const auto& cert : certs)
        sync_certs.AddCert(cert);
    }

    CertPathBuilder path_builder(
        a_by_b_, &trust_store, &delegate_, time_, KeyPurpose::ANY_EKU,
        initial_explicit_policy_, user_initial_policy_set_,
        initial_policy_mapping_inhibit_, initial_any_policy_inhibit_);
    path_builder.AddCertIssuerSource(&sync_certs);

    auto result = path_builder.Run();

    ASSERT_TRUE(result.HasValidPath());

    // The result path should be A(B) <- B(C) <- C(D) <- D(D)
    const auto& path = *result.GetBestValidPath();
    ASSERT_EQ(4U, path.certs.size());
    EXPECT_EQ(a_by_b_, path.certs[0]);
    EXPECT_EQ(b_by_c_, path.certs[1]);
    EXPECT_EQ(c_by_d_, path.certs[2]);
    EXPECT_EQ(d_by_d_, path.certs[3]);
  }
}

TEST_F(PathBuilderMultiRootTest, TestIterationLimit) {
  // D(D) is the trust root.
  TrustStoreInMemory trust_store;
  trust_store.AddTrustAnchor(d_by_d_);

  // Certs B(C) and C(D) are supplied.
  CertIssuerSourceStatic sync_certs;
  sync_certs.AddCert(b_by_c_);
  sync_certs.AddCert(c_by_d_);

  for (const bool insufficient_limit : {true, false}) {
    SCOPED_TRACE(insufficient_limit);

    CertPathBuilder path_builder(
        a_by_b_, &trust_store, &delegate_, time_, KeyPurpose::ANY_EKU,
        initial_explicit_policy_, user_initial_policy_set_,
        initial_policy_mapping_inhibit_, initial_any_policy_inhibit_);
    path_builder.AddCertIssuerSource(&sync_certs);

    if (insufficient_limit) {
      // A limit of one is insufficient to build a path in this case. Therefore
      // building is expected to fail in this case.
      path_builder.SetIterationLimit(1);
    } else {
      // The other tests in this file exercise the case that |SetIterationLimit|
      // isn't called. Therefore set a sufficient limit for the path to be
      // found.
      path_builder.SetIterationLimit(5);
    }

    base::HistogramTester histogram_tester;
    auto result = path_builder.Run();

    EXPECT_EQ(!insufficient_limit, result.HasValidPath());
    EXPECT_EQ(insufficient_limit, result.exceeded_iteration_limit);

    if (insufficient_limit) {
      EXPECT_THAT(histogram_tester.GetAllSamples(
                      "Net.CertVerifier.PathBuilderIterationCount"),
                  ElementsAre(base::Bucket(/*sample=*/2, /*count=*/1)));
    } else {
      EXPECT_THAT(histogram_tester.GetAllSamples(
                      "Net.CertVerifier.PathBuilderIterationCount"),
                  ElementsAre(base::Bucket(/*sample=*/3, /*count=*/1)));
    }
  }
}

TEST_F(PathBuilderMultiRootTest, TestTrivialDeadline) {
  // C(D) is the trust root.
  TrustStoreInMemory trust_store;
  trust_store.AddTrustAnchor(c_by_d_);

  // Cert B(C) is supplied.
  CertIssuerSourceStatic sync_certs;
  sync_certs.AddCert(b_by_c_);

  for (const bool insufficient_limit : {true, false}) {
    SCOPED_TRACE(insufficient_limit);

    CertPathBuilder path_builder(
        a_by_b_, &trust_store, &delegate_, time_, KeyPurpose::ANY_EKU,
        initial_explicit_policy_, user_initial_policy_set_,
        initial_policy_mapping_inhibit_, initial_any_policy_inhibit_);
    path_builder.AddCertIssuerSource(&sync_certs);

    base::TimeTicks deadline;
    if (insufficient_limit) {
      // Set a deadline one millisecond in the past. Path building should fail
      // since the deadline is already past.
      deadline = base::TimeTicks::Now() - base::Milliseconds(1);
    } else {
      // The other tests in this file exercise the case that |SetDeadline|
      // isn't called. Therefore set a sufficient limit for the path to be
      // found.
      deadline = base::TimeTicks::Now() + base::Days(1);
    }
    path_builder.SetDeadline(deadline);

    auto result = path_builder.Run();

    EXPECT_EQ(!insufficient_limit, result.HasValidPath());
    EXPECT_EQ(insufficient_limit, result.exceeded_deadline);
    EXPECT_EQ(deadline, path_builder.deadline());

    if (insufficient_limit) {
      ASSERT_EQ(1U, result.paths.size());
      EXPECT_FALSE(result.paths[0]->IsValid());
      ASSERT_EQ(1U, result.paths[0]->certs.size());
      EXPECT_EQ(a_by_b_, result.paths[0]->certs[0]);
      EXPECT_TRUE(result.paths[0]->errors.ContainsError(
          cert_errors::kDeadlineExceeded));
    } else {
      ASSERT_EQ(1U, result.paths.size());
      EXPECT_TRUE(result.paths[0]->IsValid());
      ASSERT_EQ(3U, result.paths[0]->certs.size());
      EXPECT_EQ(a_by_b_, result.paths[0]->certs[0]);
      EXPECT_EQ(b_by_c_, result.paths[0]->certs[1]);
      EXPECT_EQ(c_by_d_, result.paths[0]->certs[2]);
    }
  }
}

TEST_F(PathBuilderMultiRootTest, TestDeadline) {
  base::test::TaskEnvironment task_environment{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  TrustStoreInMemory trust_store;
  trust_store.AddTrustAnchor(d_by_d_);

  // Cert B(C) is supplied statically.
  CertIssuerSourceStatic sync_certs;
  sync_certs.AddCert(b_by_c_);

  // Cert C(D) is supplied asynchronously and will advance time before returning
  // the async result.
  AsyncCertIssuerSourceStatic async_certs;
  async_certs.AddCert(c_by_d_);
  async_certs.SetAsyncGetCallback(base::BindLambdaForTesting(
      [&] { task_environment.FastForwardBy(base::Seconds(2)); }));

  CertPathBuilder path_builder(
      a_by_b_, &trust_store, &delegate_, time_, KeyPurpose::ANY_EKU,
      initial_explicit_policy_, user_initial_policy_set_,
      initial_policy_mapping_inhibit_, initial_any_policy_inhibit_);
  path_builder.AddCertIssuerSource(&sync_certs);
  path_builder.AddCertIssuerSource(&async_certs);

  base::TimeTicks deadline;
  deadline = base::TimeTicks::Now() + base::Seconds(1);
  path_builder.SetDeadline(deadline);

  auto result = path_builder.Run();

  EXPECT_FALSE(result.HasValidPath());
  EXPECT_TRUE(result.exceeded_deadline);
  EXPECT_EQ(deadline, path_builder.deadline());

  // The chain returned should end in c_by_d_, since the deadline would only be
  // checked again after the async results had been checked (since
  // AsyncCertIssuerSourceStatic makes the async results available immediately.)
  ASSERT_EQ(1U, result.paths.size());
  EXPECT_FALSE(result.paths[0]->IsValid());
  ASSERT_EQ(3U, result.paths[0]->certs.size());
  EXPECT_EQ(a_by_b_, result.paths[0]->certs[0]);
  EXPECT_EQ(b_by_c_, result.paths[0]->certs[1]);
  EXPECT_EQ(c_by_d_, result.paths[0]->certs[2]);
  EXPECT_TRUE(
      result.paths[0]->errors.ContainsError(cert_errors::kDeadlineExceeded));
}

#if defined(OS_WIN) && BUILDFLAG(CHROME_ROOT_STORE_SUPPORTED)

void AddToStoreWithEKURestriction(HCERTSTORE store,
                                  const scoped_refptr<ParsedCertificate>& cert,
                                  LPCSTR usage_identifier) {
  crypto::ScopedPCCERT_CONTEXT os_cert(CertCreateCertificateContext(
      X509_ASN_ENCODING, cert->der_cert().UnsafeData(),
      cert->der_cert().Length()));

  CERT_ENHKEY_USAGE usage;
  memset(&usage, 0, sizeof(usage));
  CertSetEnhancedKeyUsage(os_cert.get(), &usage);
  if (usage_identifier) {
    CertAddEnhancedKeyUsageIdentifier(os_cert.get(), usage_identifier);
  }
  CertAddCertificateContextToStore(store, os_cert.get(), CERT_STORE_ADD_ALWAYS,
                                   NULL);
}

bool AreCertsEq(const scoped_refptr<ParsedCertificate> cert_1,
                const scoped_refptr<ParsedCertificate> cert_2) {
  return cert_1 && cert_2 &&
         base::ranges::equal(cert_1->der_cert().AsSpan(),
                             cert_2->der_cert().AsSpan());
}

// Test to ensure that path building stops when an intermediate cert is
// encountered that is not usable for TLS because of EKU restrictions.
TEST_F(PathBuilderMultiRootTest, TrustStoreWinOnlyFindTrustedTLSPath) {
  crypto::ScopedHCERTSTORE root_store(CertOpenStore(
      CERT_STORE_PROV_MEMORY, X509_ASN_ENCODING, NULL, 0, nullptr));
  crypto::ScopedHCERTSTORE intermediate_store(CertOpenStore(
      CERT_STORE_PROV_MEMORY, X509_ASN_ENCODING, NULL, 0, nullptr));
  crypto::ScopedHCERTSTORE disallowed_store(CertOpenStore(
      CERT_STORE_PROV_MEMORY, X509_ASN_ENCODING, NULL, 0, nullptr));

  AddToStoreWithEKURestriction(root_store.get(), d_by_d_,
                               szOID_PKIX_KP_SERVER_AUTH);
  AddToStoreWithEKURestriction(root_store.get(), e_by_e_,
                               szOID_PKIX_KP_SERVER_AUTH);
  AddToStoreWithEKURestriction(intermediate_store.get(), c_by_e_,
                               szOID_PKIX_KP_SERVER_AUTH);
  AddToStoreWithEKURestriction(intermediate_store.get(), c_by_d_, nullptr);

  std::unique_ptr<TrustStoreWin> trust_store = TrustStoreWin::CreateForTesting(
      std::move(root_store), std::move(intermediate_store),
      std::move(disallowed_store));

  CertPathBuilder path_builder(
      b_by_c_, trust_store.get(), &delegate_, time_, KeyPurpose::ANY_EKU,
      initial_explicit_policy_, user_initial_policy_set_,
      initial_policy_mapping_inhibit_, initial_any_policy_inhibit_);

  // Check all paths.
  path_builder.SetExploreAllPaths(true);

  auto result = path_builder.Run();
  ASSERT_TRUE(result.HasValidPath());
  ASSERT_EQ(2U, result.paths.size());
  const auto& path = *result.GetBestValidPath();
  ASSERT_EQ(3U, path.certs.size());
  EXPECT_TRUE(AreCertsEq(b_by_c_, path.certs[0]));
  EXPECT_TRUE(AreCertsEq(c_by_e_, path.certs[1]));
  EXPECT_TRUE(AreCertsEq(e_by_e_, path.certs[2]));

  // Should only be one valid path, the one above.
  int valid_paths = 0;
  for (auto&& path : result.paths) {
    valid_paths += path->IsValid() ? 1 : 0;
  }
  ASSERT_EQ(1, valid_paths);
}

// Test that if an intermediate is disabled for TLS, and it is the only
// path, then path building should fail, even if the root is enabled for
// TLS.
TEST_F(PathBuilderMultiRootTest, TrustStoreWinNoPathEKURestrictions) {
  crypto::ScopedHCERTSTORE root_store(CertOpenStore(
      CERT_STORE_PROV_MEMORY, X509_ASN_ENCODING, NULL, 0, nullptr));
  crypto::ScopedHCERTSTORE intermediate_store(CertOpenStore(
      CERT_STORE_PROV_MEMORY, X509_ASN_ENCODING, NULL, 0, nullptr));
  crypto::ScopedHCERTSTORE disallowed_store(CertOpenStore(
      CERT_STORE_PROV_MEMORY, X509_ASN_ENCODING, NULL, 0, nullptr));

  AddToStoreWithEKURestriction(root_store.get(), d_by_d_,
                               szOID_PKIX_KP_SERVER_AUTH);
  AddToStoreWithEKURestriction(intermediate_store.get(), c_by_d_, nullptr);
  std::unique_ptr<TrustStoreWin> trust_store = TrustStoreWin::CreateForTesting(
      std::move(root_store), std::move(intermediate_store),
      std::move(disallowed_store));

  CertPathBuilder path_builder(
      b_by_c_, trust_store.get(), &delegate_, time_, KeyPurpose::ANY_EKU,
      initial_explicit_policy_, user_initial_policy_set_,
      initial_policy_mapping_inhibit_, initial_any_policy_inhibit_);

  auto result = path_builder.Run();
  ASSERT_FALSE(result.HasValidPath());
}
#endif  // defined(OS_WIN) && BUILDFLAG(CHROME_ROOT_STORE_SUPPORTED)

class PathBuilderKeyRolloverTest : public ::testing::Test {
 public:
  PathBuilderKeyRolloverTest()
      : delegate_(1024,
                  SimplePathBuilderDelegate::DigestPolicy::kWeakAllowSha1) {}

  void SetUp() override {
    ParsedCertificateList path;

    VerifyCertChainTest test;
    ASSERT_TRUE(ReadVerifyCertChainTestFromFile(
        "net/data/verify_certificate_chain_unittest/key-rollover/oldchain.test",
        &test));
    path = test.chain;
    ASSERT_EQ(3U, path.size());
    target_ = path[0];
    oldintermediate_ = path[1];
    oldroot_ = path[2];
    time_ = test.time;

    ASSERT_TRUE(target_);
    ASSERT_TRUE(oldintermediate_);

    ASSERT_TRUE(ReadVerifyCertChainTestFromFile(
        "net/data/verify_certificate_chain_unittest/"
        "key-rollover/longrolloverchain.test",
        &test));
    path = test.chain;

    ASSERT_EQ(5U, path.size());
    newintermediate_ = path[1];
    newroot_ = path[2];
    newrootrollover_ = path[3];
    ASSERT_TRUE(newintermediate_);
    ASSERT_TRUE(newroot_);
    ASSERT_TRUE(newrootrollover_);
  }

 protected:
  //    oldroot-------->newrootrollover  newroot
  //       |                      |        |
  //       v                      v        v
  // oldintermediate           newintermediate
  //       |                          |
  //       +------------+-------------+
  //                    |
  //                    v
  //                  target
  scoped_refptr<ParsedCertificate> target_;
  scoped_refptr<ParsedCertificate> oldintermediate_;
  scoped_refptr<ParsedCertificate> newintermediate_;
  scoped_refptr<ParsedCertificate> oldroot_;
  scoped_refptr<ParsedCertificate> newroot_;
  scoped_refptr<ParsedCertificate> newrootrollover_;

  SimplePathBuilderDelegate delegate_;
  der::GeneralizedTime time_;

  const InitialExplicitPolicy initial_explicit_policy_ =
      InitialExplicitPolicy::kFalse;
  const std::set<der::Input> user_initial_policy_set_ = {AnyPolicy()};
  const InitialPolicyMappingInhibit initial_policy_mapping_inhibit_ =
      InitialPolicyMappingInhibit::kFalse;
  const InitialAnyPolicyInhibit initial_any_policy_inhibit_ =
      InitialAnyPolicyInhibit::kFalse;
};

// Tests that if only the old root cert is trusted, the path builder can build a
// path through the new intermediate and rollover cert to the old root.
TEST_F(PathBuilderKeyRolloverTest, TestRolloverOnlyOldRootTrusted) {
  // Only oldroot is trusted.
  TrustStoreInMemory trust_store;
  trust_store.AddTrustAnchor(oldroot_);

  // Old intermediate cert is not provided, so the pathbuilder will need to go
  // through the rollover cert.
  CertIssuerSourceStatic sync_certs;
  sync_certs.AddCert(newintermediate_);
  sync_certs.AddCert(newrootrollover_);

  CertPathBuilder path_builder(
      target_, &trust_store, &delegate_, time_, KeyPurpose::ANY_EKU,
      initial_explicit_policy_, user_initial_policy_set_,
      initial_policy_mapping_inhibit_, initial_any_policy_inhibit_);
  path_builder.AddCertIssuerSource(&sync_certs);

  auto result = path_builder.Run();

  EXPECT_TRUE(result.HasValidPath());

  // Due to authorityKeyIdentifier prioritization, path builder will first
  // attempt: target <- newintermediate <- newrootrollover <- oldroot
  // which will succeed.
  ASSERT_EQ(1U, result.paths.size());
  const auto& path0 = *result.paths[0];
  EXPECT_EQ(0U, result.best_result_index);
  EXPECT_TRUE(path0.IsValid());
  ASSERT_EQ(4U, path0.certs.size());
  EXPECT_EQ(target_, path0.certs[0]);
  EXPECT_EQ(newintermediate_, path0.certs[1]);
  EXPECT_EQ(newrootrollover_, path0.certs[2]);
  EXPECT_EQ(oldroot_, path0.certs[3]);
}

// Tests that if both old and new roots are trusted it builds a path through
// the new intermediate.
TEST_F(PathBuilderKeyRolloverTest, TestRolloverBothRootsTrusted) {
  // Both oldroot and newroot are trusted.
  TrustStoreInMemory trust_store;
  trust_store.AddTrustAnchor(oldroot_);
  trust_store.AddTrustAnchor(newroot_);

  // Both old and new intermediates + rollover cert are provided.
  CertIssuerSourceStatic sync_certs;
  sync_certs.AddCert(oldintermediate_);
  sync_certs.AddCert(newintermediate_);
  sync_certs.AddCert(newrootrollover_);

  CertPathBuilder path_builder(
      target_, &trust_store, &delegate_, time_, KeyPurpose::ANY_EKU,
      initial_explicit_policy_, user_initial_policy_set_,
      initial_policy_mapping_inhibit_, initial_any_policy_inhibit_);
  path_builder.AddCertIssuerSource(&sync_certs);

  auto result = path_builder.Run();

  EXPECT_TRUE(result.HasValidPath());

  ASSERT_EQ(1U, result.paths.size());
  const auto& path = *result.paths[0];
  EXPECT_TRUE(result.paths[0]->IsValid());
  ASSERT_EQ(3U, path.certs.size());
  EXPECT_EQ(target_, path.certs[0]);
  // The newer intermediate should be used as newer certs are prioritized in
  // path building.
  EXPECT_EQ(newintermediate_, path.certs[1]);
  EXPECT_EQ(newroot_, path.certs[2]);
}

// If trust anchor query returned no results, and there are no issuer
// sources, path building should fail at that point.
TEST_F(PathBuilderKeyRolloverTest, TestAnchorsNoMatchAndNoIssuerSources) {
  TrustStoreInMemory trust_store;
  trust_store.AddTrustAnchor(newroot_);

  CertPathBuilder path_builder(
      target_, &trust_store, &delegate_, time_, KeyPurpose::ANY_EKU,
      initial_explicit_policy_, user_initial_policy_set_,
      initial_policy_mapping_inhibit_, initial_any_policy_inhibit_);

  auto result = path_builder.Run();

  EXPECT_FALSE(result.HasValidPath());

  ASSERT_EQ(1U, result.paths.size());
  const auto& path = *result.paths[0];
  EXPECT_FALSE(result.paths[0]->IsValid());
  ASSERT_EQ(1U, path.certs.size());
  EXPECT_EQ(target_, path.certs[0]);
}

// If a path to a trust anchor could not be found, and the last issuer(s) in
// the chain were culled by the loop checker, the partial path up to that point
// should be returned.
TEST_F(PathBuilderKeyRolloverTest, TestReturnsPartialPathEndedByLoopChecker) {
  TrustStoreInMemory trust_store;

  CertIssuerSourceStatic sync_certs;
  sync_certs.AddCert(newintermediate_);
  sync_certs.AddCert(newroot_);
  sync_certs.AddCert(newrootrollover_);

  CertPathBuilder path_builder(
      target_, &trust_store, &delegate_, time_, KeyPurpose::ANY_EKU,
      initial_explicit_policy_, user_initial_policy_set_,
      initial_policy_mapping_inhibit_, initial_any_policy_inhibit_);
  path_builder.AddCertIssuerSource(&sync_certs);

  auto result = path_builder.Run();

  EXPECT_FALSE(result.HasValidPath());
  ASSERT_EQ(2U, result.paths.size());

  // Since none of the certs are trusted, the path builder should build 4
  // candidate paths, all of which are disallowed due to the loop checker:
  //   target->newintermediate->newroot->newroot
  //   target->newintermediate->newroot->newrootrollover
  //   target->newintermediate->newrootrollover->newroot
  //   target->newintermediate->newrootrollover->newrootrollover
  // This should end up returning the 2 partial paths which are the longest
  // paths for which no acceptable issuers could be found:
  //   target->newintermediate->newroot
  //   target->newintermediate->newrootrollover

  {
    const auto& path = *result.paths[0];
    EXPECT_FALSE(path.IsValid());
    ASSERT_EQ(3U, path.certs.size());
    EXPECT_EQ(target_, path.certs[0]);
    EXPECT_EQ(newintermediate_, path.certs[1]);
    EXPECT_EQ(newroot_, path.certs[2]);
    EXPECT_TRUE(path.errors.ContainsError(cert_errors::kNoIssuersFound));
  }

  {
    const auto& path = *result.paths[1];
    EXPECT_FALSE(path.IsValid());
    ASSERT_EQ(3U, path.certs.size());
    EXPECT_EQ(target_, path.certs[0]);
    EXPECT_EQ(newintermediate_, path.certs[1]);
    EXPECT_EQ(newrootrollover_, path.certs[2]);
    EXPECT_TRUE(path.errors.ContainsError(cert_errors::kNoIssuersFound));
  }
}

// Tests that multiple trust root matches on a single path will be considered.
// Both roots have the same subject but different keys. Only one of them will
// verify.
TEST_F(PathBuilderKeyRolloverTest, TestMultipleRootMatchesOnlyOneWorks) {
  TrustStoreCollection trust_store_collection;
  TrustStoreInMemory trust_store1;
  TrustStoreInMemory trust_store2;
  trust_store_collection.AddTrustStore(&trust_store1);
  trust_store_collection.AddTrustStore(&trust_store2);
  // Add two trust anchors (newroot_ and oldroot_). Path building will attempt
  // them in this same order, as trust_store1 was added to
  // trust_store_collection first.
  trust_store1.AddTrustAnchor(newroot_);
  trust_store2.AddTrustAnchor(oldroot_);

  // Only oldintermediate is supplied, so the path with newroot should fail,
  // oldroot should succeed.
  CertIssuerSourceStatic sync_certs;
  sync_certs.AddCert(oldintermediate_);

  CertPathBuilder path_builder(
      target_, &trust_store_collection, &delegate_, time_, KeyPurpose::ANY_EKU,
      initial_explicit_policy_, user_initial_policy_set_,
      initial_policy_mapping_inhibit_, initial_any_policy_inhibit_);
  path_builder.AddCertIssuerSource(&sync_certs);

  auto result = path_builder.Run();

  EXPECT_TRUE(result.HasValidPath());
  ASSERT_EQ(1U, result.paths.size());

  // Due to authorityKeyIdentifier prioritization, path builder will first
  // attempt: target <- old intermediate <- oldroot
  // which should succeed.
  EXPECT_TRUE(result.paths[result.best_result_index]->IsValid());
  const auto& path = *result.paths[result.best_result_index];
  ASSERT_EQ(3U, path.certs.size());
  EXPECT_EQ(target_, path.certs[0]);
  EXPECT_EQ(oldintermediate_, path.certs[1]);
  EXPECT_EQ(oldroot_, path.certs[2]);
}

// Tests that the path builder doesn't build longer than necessary paths,
// by skipping certs where the same Name+SAN+SPKI is already in the current
// path.
TEST_F(PathBuilderKeyRolloverTest, TestRolloverLongChain) {
  // Only oldroot is trusted.
  TrustStoreInMemory trust_store;
  trust_store.AddTrustAnchor(oldroot_);

  // New intermediate and new root are provided synchronously.
  CertIssuerSourceStatic sync_certs;
  sync_certs.AddCert(newintermediate_);
  sync_certs.AddCert(newroot_);

  // Rollover cert is only provided asynchronously. This will force the
  // pathbuilder to first try building a longer than necessary path.
  AsyncCertIssuerSourceStatic async_certs;
  async_certs.AddCert(newrootrollover_);

  CertPathBuilder path_builder(
      target_, &trust_store, &delegate_, time_, KeyPurpose::ANY_EKU,
      initial_explicit_policy_, user_initial_policy_set_,
      initial_policy_mapping_inhibit_, initial_any_policy_inhibit_);
  path_builder.AddCertIssuerSource(&sync_certs);
  path_builder.AddCertIssuerSource(&async_certs);

  auto result = path_builder.Run();

  EXPECT_TRUE(result.HasValidPath());
  ASSERT_EQ(3U, result.paths.size());

  // Path builder will first attempt:
  // target <- newintermediate <- newroot <- oldroot
  // but it will fail since newroot is self-signed.
  EXPECT_FALSE(result.paths[0]->IsValid());
  const auto& path0 = *result.paths[0];
  ASSERT_EQ(4U, path0.certs.size());
  EXPECT_EQ(target_, path0.certs[0]);
  EXPECT_EQ(newintermediate_, path0.certs[1]);
  EXPECT_EQ(newroot_, path0.certs[2]);
  EXPECT_EQ(oldroot_, path0.certs[3]);

  // Path builder will next attempt: target <- newintermediate <- oldroot
  // but it will fail since newintermediate is signed by newroot.
  EXPECT_FALSE(result.paths[1]->IsValid());
  const auto& path1 = *result.paths[1];
  ASSERT_EQ(3U, path1.certs.size());
  EXPECT_EQ(target_, path1.certs[0]);
  EXPECT_EQ(newintermediate_, path1.certs[1]);
  EXPECT_EQ(oldroot_, path1.certs[2]);

  // Path builder will skip:
  // target <- newintermediate <- newroot <- newrootrollover <- ...
  // Since newroot and newrootrollover have the same Name+SAN+SPKI.

  // Finally path builder will use:
  // target <- newintermediate <- newrootrollover <- oldroot
  EXPECT_EQ(2U, result.best_result_index);
  EXPECT_TRUE(result.paths[2]->IsValid());
  const auto& path2 = *result.paths[2];
  ASSERT_EQ(4U, path2.certs.size());
  EXPECT_EQ(target_, path2.certs[0]);
  EXPECT_EQ(newintermediate_, path2.certs[1]);
  EXPECT_EQ(newrootrollover_, path2.certs[2]);
  EXPECT_EQ(oldroot_, path2.certs[3]);
}

// Tests that when SetExploreAllPaths is combined with SetIterationLimit the
// path builder will return all the paths that were able to be built before the
// iteration limit was reached.
TEST_F(PathBuilderKeyRolloverTest, ExploreAllPathsWithIterationLimit) {
  struct Expectation {
    int iteration_limit;
    size_t expected_num_paths;
    std::vector<scoped_refptr<ParsedCertificate>> partial_path;
  } kExpectations[] = {
      // No iteration limit. All possible paths should be built.
      {0, 4, {}},
      // Limit 1 is only enough to reach the intermediate, no complete path
      // should be built.
      {1, 0, {target_, newintermediate_}},
      // Limit 2 allows reaching the root on the first path.
      {2, 1, {target_, newintermediate_}},
      // Next iteration uses oldroot instead of newroot.
      {3, 2, {target_, newintermediate_}},
      // Backtracking to the target cert.
      {4, 2, {target_}},
      // Adding oldintermediate.
      {5, 2, {target_, oldintermediate_}},
      // Trying oldroot.
      {6, 3, {target_, oldintermediate_}},
      // Trying newroot.
      {7, 4, {target_, oldintermediate_}},
  };

  // Trust both old and new roots.
  TrustStoreInMemory trust_store;
  trust_store.AddTrustAnchor(oldroot_);
  trust_store.AddTrustAnchor(newroot_);

  // Intermediates and root rollover are all provided synchronously.
  CertIssuerSourceStatic sync_certs;
  sync_certs.AddCert(oldintermediate_);
  sync_certs.AddCert(newintermediate_);

  for (const auto& expectation : kExpectations) {
    SCOPED_TRACE(expectation.iteration_limit);

    CertPathBuilder path_builder(
        target_, &trust_store, &delegate_, time_, KeyPurpose::ANY_EKU,
        initial_explicit_policy_, user_initial_policy_set_,
        initial_policy_mapping_inhibit_, initial_any_policy_inhibit_);
    path_builder.AddCertIssuerSource(&sync_certs);

    // Explore all paths, rather than stopping at the first valid path.
    path_builder.SetExploreAllPaths(true);

    // Limit the number of iterations.
    path_builder.SetIterationLimit(expectation.iteration_limit);

    auto result = path_builder.Run();

    EXPECT_EQ(expectation.expected_num_paths > 0, result.HasValidPath());
    if (expectation.partial_path.empty()) {
      ASSERT_EQ(expectation.expected_num_paths, result.paths.size());
    } else {
      ASSERT_EQ(1 + expectation.expected_num_paths, result.paths.size());
      const auto& path = *result.paths[result.paths.size() - 1];
      EXPECT_FALSE(path.IsValid());
      EXPECT_EQ(expectation.partial_path, path.certs);
      EXPECT_TRUE(
          path.errors.ContainsError(cert_errors::kIterationLimitExceeded));
    }

    if (expectation.expected_num_paths > 0) {
      // Path builder will first build path: target <- newintermediate <-
      // newroot
      const auto& path0 = *result.paths[0];
      EXPECT_TRUE(path0.IsValid());
      ASSERT_EQ(3U, path0.certs.size());
      EXPECT_EQ(target_, path0.certs[0]);
      EXPECT_EQ(newintermediate_, path0.certs[1]);
      EXPECT_EQ(newroot_, path0.certs[2]);
    }

    if (expectation.expected_num_paths > 1) {
      // Next path:  target <- newintermediate <- oldroot
      const auto& path1 = *result.paths[1];
      EXPECT_FALSE(path1.IsValid());
      ASSERT_EQ(3U, path1.certs.size());
      EXPECT_EQ(target_, path1.certs[0]);
      EXPECT_EQ(newintermediate_, path1.certs[1]);
      EXPECT_EQ(oldroot_, path1.certs[2]);
    }

    if (expectation.expected_num_paths > 2) {
      // Next path:  target <- oldintermediate <- oldroot
      const auto& path2 = *result.paths[2];
      EXPECT_TRUE(path2.IsValid());
      ASSERT_EQ(3U, path2.certs.size());
      EXPECT_EQ(target_, path2.certs[0]);
      EXPECT_EQ(oldintermediate_, path2.certs[1]);
      EXPECT_EQ(oldroot_, path2.certs[2]);
    }

    if (expectation.expected_num_paths > 3) {
      // Final path:  target <- oldintermediate <- newroot
      const auto& path3 = *result.paths[3];
      EXPECT_FALSE(path3.IsValid());
      ASSERT_EQ(3U, path3.certs.size());
      EXPECT_EQ(target_, path3.certs[0]);
      EXPECT_EQ(oldintermediate_, path3.certs[1]);
      EXPECT_EQ(newroot_, path3.certs[2]);
    }
  }
}

// If the target cert is a trust anchor, however is not itself *signed* by a
// trust anchor, then it is not considered valid (the SPKI and name of the
// trust anchor matches the SPKI and subject of the targe certificate, but the
// rest of the certificate cannot be verified).
TEST_F(PathBuilderKeyRolloverTest, TestEndEntityIsTrustRoot) {
  // Trust newintermediate.
  TrustStoreInMemory trust_store;
  trust_store.AddTrustAnchor(newintermediate_);

  // Newintermediate is also the target cert.
  CertPathBuilder path_builder(
      newintermediate_, &trust_store, &delegate_, time_, KeyPurpose::ANY_EKU,
      initial_explicit_policy_, user_initial_policy_set_,
      initial_policy_mapping_inhibit_, initial_any_policy_inhibit_);

  auto result = path_builder.Run();

  EXPECT_FALSE(result.HasValidPath());
}

// If target has same Name+SAN+SPKI as a necessary intermediate, test if a path
// can still be built.
// Since LoopChecker will prevent the intermediate from being included, this
// currently does NOT verify. This case shouldn't occur in the web PKI.
TEST_F(PathBuilderKeyRolloverTest,
       TestEndEntityHasSameNameAndSpkiAsIntermediate) {
  // Trust oldroot.
  TrustStoreInMemory trust_store;
  trust_store.AddTrustAnchor(oldroot_);

  // New root rollover is provided synchronously.
  CertIssuerSourceStatic sync_certs;
  sync_certs.AddCert(newrootrollover_);

  // Newroot is the target cert.
  CertPathBuilder path_builder(
      newroot_, &trust_store, &delegate_, time_, KeyPurpose::ANY_EKU,
      initial_explicit_policy_, user_initial_policy_set_,
      initial_policy_mapping_inhibit_, initial_any_policy_inhibit_);
  path_builder.AddCertIssuerSource(&sync_certs);

  auto result = path_builder.Run();

  // This could actually be OK, but CertPathBuilder does not build the
  // newroot <- newrootrollover <- oldroot path.
  EXPECT_FALSE(result.HasValidPath());
}

// If target has same Name+SAN+SPKI as the trust root, test that a (trivial)
// path can still be built.
TEST_F(PathBuilderKeyRolloverTest,
       TestEndEntityHasSameNameAndSpkiAsTrustAnchor) {
  // Trust newrootrollover.
  TrustStoreInMemory trust_store;
  trust_store.AddTrustAnchor(newrootrollover_);

  // Newroot is the target cert.
  CertPathBuilder path_builder(
      newroot_, &trust_store, &delegate_, time_, KeyPurpose::ANY_EKU,
      initial_explicit_policy_, user_initial_policy_set_,
      initial_policy_mapping_inhibit_, initial_any_policy_inhibit_);

  auto result = path_builder.Run();

  ASSERT_TRUE(result.HasValidPath());

  const CertPathBuilderResultPath* best_result = result.GetBestValidPath();

  // Newroot has same name+SPKI as newrootrollover, thus the path is valid and
  // only contains newroot.
  EXPECT_TRUE(best_result->IsValid());
  ASSERT_EQ(2U, best_result->certs.size());
  EXPECT_EQ(newroot_, best_result->certs[0]);
  EXPECT_EQ(newrootrollover_, best_result->certs[1]);
}

// Test that PathBuilder will not try the same path twice if multiple
// CertIssuerSources provide the same certificate.
TEST_F(PathBuilderKeyRolloverTest, TestDuplicateIntermediates) {
  // Create a separate copy of oldintermediate.
  scoped_refptr<ParsedCertificate> oldintermediate_dupe(
      ParsedCertificate::Create(
          bssl::UniquePtr<CRYPTO_BUFFER>(CRYPTO_BUFFER_new(
              oldintermediate_->der_cert().UnsafeData(),
              oldintermediate_->der_cert().Length(), nullptr)),
          {}, nullptr));

  // Only newroot is a trusted root.
  TrustStoreInMemory trust_store;
  trust_store.AddTrustAnchor(newroot_);

  // The oldintermediate is supplied synchronously by |sync_certs1| and
  // another copy of oldintermediate is supplied synchronously by |sync_certs2|.
  // The path target <- oldintermediate <- newroot  should be built first,
  // though it won't verify. It should not be attempted again even though
  // oldintermediate was supplied twice.
  CertIssuerSourceStatic sync_certs1;
  sync_certs1.AddCert(oldintermediate_);
  CertIssuerSourceStatic sync_certs2;
  sync_certs2.AddCert(oldintermediate_dupe);

  // The newintermediate is supplied asynchronously, so the path
  // target <- newintermediate <- newroot should be tried second.
  AsyncCertIssuerSourceStatic async_certs;
  async_certs.AddCert(newintermediate_);

  CertPathBuilder path_builder(
      target_, &trust_store, &delegate_, time_, KeyPurpose::ANY_EKU,
      initial_explicit_policy_, user_initial_policy_set_,
      initial_policy_mapping_inhibit_, initial_any_policy_inhibit_);
  path_builder.AddCertIssuerSource(&sync_certs1);
  path_builder.AddCertIssuerSource(&sync_certs2);
  path_builder.AddCertIssuerSource(&async_certs);

  auto result = path_builder.Run();

  EXPECT_TRUE(result.HasValidPath());
  ASSERT_EQ(2U, result.paths.size());

  // Path builder will first attempt: target <- oldintermediate <- newroot
  // but it will fail since oldintermediate is signed by oldroot.
  EXPECT_FALSE(result.paths[0]->IsValid());
  const auto& path0 = *result.paths[0];

  ASSERT_EQ(3U, path0.certs.size());
  EXPECT_EQ(target_, path0.certs[0]);
  // Compare the DER instead of ParsedCertificate pointer, don't care which copy
  // of oldintermediate was used in the path.
  EXPECT_EQ(oldintermediate_->der_cert(), path0.certs[1]->der_cert());
  EXPECT_EQ(newroot_, path0.certs[2]);

  // Path builder will next attempt: target <- newintermediate <- newroot
  // which will succeed.
  EXPECT_EQ(1U, result.best_result_index);
  EXPECT_TRUE(result.paths[1]->IsValid());
  const auto& path1 = *result.paths[1];
  ASSERT_EQ(3U, path1.certs.size());
  EXPECT_EQ(target_, path1.certs[0]);
  EXPECT_EQ(newintermediate_, path1.certs[1]);
  EXPECT_EQ(newroot_, path1.certs[2]);
}

// Test when PathBuilder is given a cert via CertIssuerSources that has the same
// SPKI as a trust anchor.
TEST_F(PathBuilderKeyRolloverTest, TestDuplicateIntermediateAndRoot) {
  // Create a separate copy of newroot.
  scoped_refptr<ParsedCertificate> newroot_dupe(ParsedCertificate::Create(
      bssl::UniquePtr<CRYPTO_BUFFER>(
          CRYPTO_BUFFER_new(newroot_->der_cert().UnsafeData(),
                            newroot_->der_cert().Length(), nullptr)),
      {}, nullptr));

  // Only newroot is a trusted root.
  TrustStoreInMemory trust_store;
  trust_store.AddTrustAnchor(newroot_);

  // The oldintermediate and newroot are supplied synchronously by |sync_certs|.
  CertIssuerSourceStatic sync_certs;
  sync_certs.AddCert(oldintermediate_);
  sync_certs.AddCert(newroot_dupe);

  CertPathBuilder path_builder(
      target_, &trust_store, &delegate_, time_, KeyPurpose::ANY_EKU,
      initial_explicit_policy_, user_initial_policy_set_,
      initial_policy_mapping_inhibit_, initial_any_policy_inhibit_);
  path_builder.AddCertIssuerSource(&sync_certs);

  auto result = path_builder.Run();

  EXPECT_FALSE(result.HasValidPath());
  ASSERT_EQ(1U, result.paths.size());

  // Path builder attempt: target <- oldintermediate <- newroot
  // but it will fail since oldintermediate is signed by oldroot.
  EXPECT_FALSE(result.paths[0]->IsValid());
  const auto& path = *result.paths[0];
  ASSERT_EQ(3U, path.certs.size());
  EXPECT_EQ(target_, path.certs[0]);
  EXPECT_EQ(oldintermediate_, path.certs[1]);
  // Compare the DER instead of ParsedCertificate pointer, don't care which copy
  // of newroot was used in the path.
  EXPECT_EQ(newroot_->der_cert(), path.certs[2]->der_cert());
}

class MockCertIssuerSourceRequest : public CertIssuerSource::Request {
 public:
  MOCK_METHOD1(GetNext, void(ParsedCertificateList*));
};

class MockCertIssuerSource : public CertIssuerSource {
 public:
  MOCK_METHOD2(SyncGetIssuersOf,
               void(const ParsedCertificate*, ParsedCertificateList*));
  MOCK_METHOD2(AsyncGetIssuersOf,
               void(const ParsedCertificate*, std::unique_ptr<Request>*));
};

// Helper class to pass the Request to the PathBuilder when it calls
// AsyncGetIssuersOf. (GoogleMock has a ByMove helper, but it apparently can
// only be used with Return, not SetArgPointee.)
class CertIssuerSourceRequestMover {
 public:
  CertIssuerSourceRequestMover(std::unique_ptr<CertIssuerSource::Request> req)
      : request_(std::move(req)) {}
  void MoveIt(const ParsedCertificate* cert,
              std::unique_ptr<CertIssuerSource::Request>* out_req) {
    *out_req = std::move(request_);
  }

 private:
  std::unique_ptr<CertIssuerSource::Request> request_;
};

// Functor that when called with a ParsedCertificateList* will append the
// specified certificate.
class AppendCertToList {
 public:
  explicit AppendCertToList(const scoped_refptr<ParsedCertificate>& cert)
      : cert_(cert) {}

  void operator()(ParsedCertificateList* out) { out->push_back(cert_); }

 private:
  scoped_refptr<ParsedCertificate> cert_;
};

// Test that a single CertIssuerSource returning multiple async batches of
// issuers is handled correctly. Due to the StrictMocks, it also tests that path
// builder does not request issuers of certs that it shouldn't.
TEST_F(PathBuilderKeyRolloverTest, TestMultipleAsyncIssuersFromSingleSource) {
  StrictMock<MockCertIssuerSource> cert_issuer_source;

  // Only newroot is a trusted root.
  TrustStoreInMemory trust_store;
  trust_store.AddTrustAnchor(newroot_);

  CertPathBuilder path_builder(
      target_, &trust_store, &delegate_, time_, KeyPurpose::ANY_EKU,
      initial_explicit_policy_, user_initial_policy_set_,
      initial_policy_mapping_inhibit_, initial_any_policy_inhibit_);
  path_builder.AddCertIssuerSource(&cert_issuer_source);

  // Create the mock CertIssuerSource::Request...
  std::unique_ptr<StrictMock<MockCertIssuerSourceRequest>>
      target_issuers_req_owner(new StrictMock<MockCertIssuerSourceRequest>());
  // Keep a raw pointer to the Request...
  StrictMock<MockCertIssuerSourceRequest>* target_issuers_req =
      target_issuers_req_owner.get();
  // Setup helper class to pass ownership of the Request to the PathBuilder when
  // it calls AsyncGetIssuersOf.
  CertIssuerSourceRequestMover req_mover(std::move(target_issuers_req_owner));
  {
    ::testing::InSequence s;
    EXPECT_CALL(cert_issuer_source, SyncGetIssuersOf(target_.get(), _));
    EXPECT_CALL(cert_issuer_source, AsyncGetIssuersOf(target_.get(), _))
        .WillOnce(Invoke(&req_mover, &CertIssuerSourceRequestMover::MoveIt));
  }

  EXPECT_CALL(*target_issuers_req, GetNext(_))
      // First async batch: return oldintermediate_.
      .WillOnce(Invoke(AppendCertToList(oldintermediate_)))
      // Second async batch: return newintermediate_.
      .WillOnce(Invoke(AppendCertToList(newintermediate_)));
  {
    ::testing::InSequence s;
    // oldintermediate_ does not create a valid path, so both sync and async
    // lookups are expected.
    EXPECT_CALL(cert_issuer_source,
                SyncGetIssuersOf(oldintermediate_.get(), _));
    EXPECT_CALL(cert_issuer_source,
                AsyncGetIssuersOf(oldintermediate_.get(), _));
  }

  // newroot_ is in the trust store, so this path will be completed
  // synchronously. AsyncGetIssuersOf will not be called on newintermediate_.
  EXPECT_CALL(cert_issuer_source, SyncGetIssuersOf(newintermediate_.get(), _));

  // Ensure pathbuilder finished and filled result.
  auto result = path_builder.Run();

  // Note that VerifyAndClearExpectations(target_issuers_req) is not called
  // here. PathBuilder could have destroyed it already, so just let the
  // expectations get checked by the destructor.
  ::testing::Mock::VerifyAndClearExpectations(&cert_issuer_source);

  EXPECT_TRUE(result.HasValidPath());
  ASSERT_EQ(2U, result.paths.size());

  // Path builder first attempts: target <- oldintermediate <- newroot
  // but it will fail since oldintermediate is signed by oldroot.
  EXPECT_FALSE(result.paths[0]->IsValid());
  const auto& path0 = *result.paths[0];
  ASSERT_EQ(3U, path0.certs.size());
  EXPECT_EQ(target_, path0.certs[0]);
  EXPECT_EQ(oldintermediate_, path0.certs[1]);
  EXPECT_EQ(newroot_, path0.certs[2]);

  // After the second batch of async results, path builder will attempt:
  // target <- newintermediate <- newroot which will succeed.
  EXPECT_TRUE(result.paths[1]->IsValid());
  const auto& path1 = *result.paths[1];
  ASSERT_EQ(3U, path1.certs.size());
  EXPECT_EQ(target_, path1.certs[0]);
  EXPECT_EQ(newintermediate_, path1.certs[1]);
  EXPECT_EQ(newroot_, path1.certs[2]);
}

// Test that PathBuilder will not try the same path twice if CertIssuerSources
// asynchronously provide the same certificate multiple times.
TEST_F(PathBuilderKeyRolloverTest, TestDuplicateAsyncIntermediates) {
  StrictMock<MockCertIssuerSource> cert_issuer_source;

  // Only newroot is a trusted root.
  TrustStoreInMemory trust_store;
  trust_store.AddTrustAnchor(newroot_);

  CertPathBuilder path_builder(
      target_, &trust_store, &delegate_, time_, KeyPurpose::ANY_EKU,
      initial_explicit_policy_, user_initial_policy_set_,
      initial_policy_mapping_inhibit_, initial_any_policy_inhibit_);
  path_builder.AddCertIssuerSource(&cert_issuer_source);

  // Create the mock CertIssuerSource::Request...
  std::unique_ptr<StrictMock<MockCertIssuerSourceRequest>>
      target_issuers_req_owner(new StrictMock<MockCertIssuerSourceRequest>());
  // Keep a raw pointer to the Request...
  StrictMock<MockCertIssuerSourceRequest>* target_issuers_req =
      target_issuers_req_owner.get();
  // Setup helper class to pass ownership of the Request to the PathBuilder when
  // it calls AsyncGetIssuersOf.
  CertIssuerSourceRequestMover req_mover(std::move(target_issuers_req_owner));
  {
    ::testing::InSequence s;
    EXPECT_CALL(cert_issuer_source, SyncGetIssuersOf(target_.get(), _));
    EXPECT_CALL(cert_issuer_source, AsyncGetIssuersOf(target_.get(), _))
        .WillOnce(Invoke(&req_mover, &CertIssuerSourceRequestMover::MoveIt));
  }

  scoped_refptr<ParsedCertificate> oldintermediate_dupe(
      ParsedCertificate::Create(
          bssl::UniquePtr<CRYPTO_BUFFER>(CRYPTO_BUFFER_new(
              oldintermediate_->der_cert().UnsafeData(),
              oldintermediate_->der_cert().Length(), nullptr)),
          {}, nullptr));

  EXPECT_CALL(*target_issuers_req, GetNext(_))
      // First async batch: return oldintermediate_.
      .WillOnce(Invoke(AppendCertToList(oldintermediate_)))
      // Second async batch: return a different copy of oldintermediate_ again.
      .WillOnce(Invoke(AppendCertToList(oldintermediate_dupe)))
      // Third async batch: return newintermediate_.
      .WillOnce(Invoke(AppendCertToList(newintermediate_)));

  {
    ::testing::InSequence s;
    // oldintermediate_ does not create a valid path, so both sync and async
    // lookups are expected.
    EXPECT_CALL(cert_issuer_source,
                SyncGetIssuersOf(oldintermediate_.get(), _));
    EXPECT_CALL(cert_issuer_source,
                AsyncGetIssuersOf(oldintermediate_.get(), _));
  }

  // newroot_ is in the trust store, so this path will be completed
  // synchronously. AsyncGetIssuersOf will not be called on newintermediate_.
  EXPECT_CALL(cert_issuer_source, SyncGetIssuersOf(newintermediate_.get(), _));

  // Ensure pathbuilder finished and filled result.
  auto result = path_builder.Run();

  ::testing::Mock::VerifyAndClearExpectations(&cert_issuer_source);

  EXPECT_TRUE(result.HasValidPath());
  ASSERT_EQ(2U, result.paths.size());

  // Path builder first attempts: target <- oldintermediate <- newroot
  // but it will fail since oldintermediate is signed by oldroot.
  EXPECT_FALSE(result.paths[0]->IsValid());
  const auto& path0 = *result.paths[0];
  ASSERT_EQ(3U, path0.certs.size());
  EXPECT_EQ(target_, path0.certs[0]);
  EXPECT_EQ(oldintermediate_, path0.certs[1]);
  EXPECT_EQ(newroot_, path0.certs[2]);

  // The second async result does not generate any path.

  // After the third batch of async results, path builder will attempt:
  // target <- newintermediate <- newroot which will succeed.
  EXPECT_TRUE(result.paths[1]->IsValid());
  const auto& path1 = *result.paths[1];
  ASSERT_EQ(3U, path1.certs.size());
  EXPECT_EQ(target_, path1.certs[0]);
  EXPECT_EQ(newintermediate_, path1.certs[1]);
  EXPECT_EQ(newroot_, path1.certs[2]);
}

class PathBuilderSimpleChainTest : public ::testing::Test {
 public:
  PathBuilderSimpleChainTest() = default;

 protected:
  void SetUp() override {
    // Read a simple test chain comprised of a target, intermediate, and root.
    ASSERT_TRUE(ReadVerifyCertChainTestFromFile(
        "net/data/verify_certificate_chain_unittest/target-and-intermediate/"
        "main.test",
        &test_));
    ASSERT_EQ(3u, test_.chain.size());
  }

  // Runs the path builder for the target certificate while |distrusted_cert| is
  // blocked, and |delegate| if non-null.
  CertPathBuilder::Result RunPathBuilder(
      const scoped_refptr<ParsedCertificate>& distrusted_cert,
      CertPathBuilderDelegate* optional_delegate) {
    // Set up the trust store such that |distrusted_cert| is blocked, and
    // the root is trusted (except if it was |distrusted_cert|).
    TrustStoreInMemory trust_store;
    if (distrusted_cert != test_.chain.back())
      trust_store.AddTrustAnchor(test_.chain.back());
    if (distrusted_cert)
      trust_store.AddDistrustedCertificateForTest(distrusted_cert);

    // Add the single intermediate.
    CertIssuerSourceStatic intermediates;
    intermediates.AddCert(test_.chain[1]);

    SimplePathBuilderDelegate default_delegate(
        1024, SimplePathBuilderDelegate::DigestPolicy::kWeakAllowSha1);
    CertPathBuilderDelegate* delegate =
        optional_delegate ? optional_delegate : &default_delegate;

    const InitialExplicitPolicy initial_explicit_policy =
        InitialExplicitPolicy::kFalse;
    const std::set<der::Input> user_initial_policy_set = {AnyPolicy()};
    const InitialPolicyMappingInhibit initial_policy_mapping_inhibit =
        InitialPolicyMappingInhibit::kFalse;
    const InitialAnyPolicyInhibit initial_any_policy_inhibit =
        InitialAnyPolicyInhibit::kFalse;

    CertPathBuilder path_builder(
        test_.chain.front(), &trust_store, delegate, test_.time,
        KeyPurpose::ANY_EKU, initial_explicit_policy, user_initial_policy_set,
        initial_policy_mapping_inhibit, initial_any_policy_inhibit);
    path_builder.AddCertIssuerSource(&intermediates);
    return path_builder.Run();
  }

 protected:
  VerifyCertChainTest test_;
};

// Test fixture for running the path builder over a simple chain, while varying
// the trustedness of certain certificates.
class PathBuilderDistrustTest : public PathBuilderSimpleChainTest {
 public:
  PathBuilderDistrustTest() = default;

 protected:
  // Runs the path builder for the target certificate while |distrusted_cert| is
  // blocked.
  CertPathBuilder::Result RunPathBuilderWithDistrustedCert(
      const scoped_refptr<ParsedCertificate>& distrusted_cert) {
    return RunPathBuilder(distrusted_cert, nullptr);
  }
};

// Tests that path building fails when the target, intermediate, or root are
// distrusted (but the path is otherwise valid).
TEST_F(PathBuilderDistrustTest, TargetIntermediateRoot) {
  // First do a control test -- path building without any blocked
  // certificates should work.
  CertPathBuilder::Result result = RunPathBuilderWithDistrustedCert(nullptr);
  {
    ASSERT_TRUE(result.HasValidPath());
    // The built path should be identical the the one read from disk.
    const auto& path = *result.GetBestValidPath();
    ASSERT_EQ(test_.chain.size(), path.certs.size());
    for (size_t i = 0; i < test_.chain.size(); ++i)
      EXPECT_EQ(test_.chain[i], path.certs[i]);
  }

  // Try path building when only the target is blocked - should fail.
  result = RunPathBuilderWithDistrustedCert(test_.chain[0]);
  {
    EXPECT_FALSE(result.HasValidPath());
    ASSERT_LT(result.best_result_index, result.paths.size());
    const auto& best_path = result.paths[result.best_result_index];

    // The built chain has length 1 since path building stopped once
    // it encountered the blocked certificate (target).
    ASSERT_EQ(1u, best_path->certs.size());
    EXPECT_EQ(best_path->certs[0], test_.chain[0]);
    EXPECT_TRUE(best_path->errors.ContainsHighSeverityErrors());
    best_path->errors.ContainsError(cert_errors::kDistrustedByTrustStore);
  }

  // Try path building when only the intermediate is blocked - should fail.
  result = RunPathBuilderWithDistrustedCert(test_.chain[1]);
  {
    EXPECT_FALSE(result.HasValidPath());
    ASSERT_LT(result.best_result_index, result.paths.size());
    const auto& best_path = result.paths[result.best_result_index];

    // The built chain has length 2 since path building stopped once
    // it encountered the blocked certificate (intermediate).
    ASSERT_EQ(2u, best_path->certs.size());
    EXPECT_EQ(best_path->certs[0], test_.chain[0]);
    EXPECT_EQ(best_path->certs[1], test_.chain[1]);
    EXPECT_TRUE(best_path->errors.ContainsHighSeverityErrors());
    best_path->errors.ContainsError(cert_errors::kDistrustedByTrustStore);
  }

  // Try path building when only the root is blocked - should fail.
  result = RunPathBuilderWithDistrustedCert(test_.chain[2]);
  {
    EXPECT_FALSE(result.HasValidPath());
    ASSERT_LT(result.best_result_index, result.paths.size());
    const auto& best_path = result.paths[result.best_result_index];

    // The built chain has length 3 since path building stopped once
    // it encountered the blocked certificate (root).
    ASSERT_EQ(3u, best_path->certs.size());
    EXPECT_EQ(best_path->certs[0], test_.chain[0]);
    EXPECT_EQ(best_path->certs[1], test_.chain[1]);
    EXPECT_EQ(best_path->certs[2], test_.chain[2]);
    EXPECT_TRUE(best_path->errors.ContainsHighSeverityErrors());
    best_path->errors.ContainsError(cert_errors::kDistrustedByTrustStore);
  }
}

// Test fixture for running the path builder over a simple chain, while varying
// what CheckPathAfterVerification() does.
class PathBuilderCheckPathAfterVerificationTest
    : public PathBuilderSimpleChainTest {};

class CertPathBuilderDelegateBase : public SimplePathBuilderDelegate {
 public:
  CertPathBuilderDelegateBase()
      : SimplePathBuilderDelegate(
            1024,
            SimplePathBuilderDelegate::DigestPolicy::kWeakAllowSha1) {}
  void CheckPathAfterVerification(const CertPathBuilder& path_builder,
                                  CertPathBuilderResultPath* path) override {
    ADD_FAILURE() << "Tests must override this";
  }
};

class MockPathBuilderDelegate : public CertPathBuilderDelegateBase {
 public:
  MOCK_METHOD2(CheckPathAfterVerification,
               void(const CertPathBuilder& path_builder,
                    CertPathBuilderResultPath* path));
};

TEST_F(PathBuilderCheckPathAfterVerificationTest, NoOpToValidPath) {
  StrictMock<MockPathBuilderDelegate> delegate;
  // Just verify that the hook is called.
  EXPECT_CALL(delegate, CheckPathAfterVerification(_, _));

  CertPathBuilder::Result result = RunPathBuilder(nullptr, &delegate);
  EXPECT_TRUE(result.HasValidPath());
}

DEFINE_CERT_ERROR_ID(kWarningFromDelegate, "Warning from delegate");

class AddWarningPathBuilderDelegate : public CertPathBuilderDelegateBase {
 public:
  void CheckPathAfterVerification(const CertPathBuilder& path_builder,
                                  CertPathBuilderResultPath* path) override {
    path->errors.GetErrorsForCert(1)->AddWarning(kWarningFromDelegate, nullptr);
  }
};

TEST_F(PathBuilderCheckPathAfterVerificationTest, AddsWarningToValidPath) {
  AddWarningPathBuilderDelegate delegate;
  CertPathBuilder::Result result = RunPathBuilder(nullptr, &delegate);
  ASSERT_TRUE(result.HasValidPath());

  // A warning should have been added to certificate at index 1 in the path.
  const CertErrors* cert1_errors =
      result.GetBestValidPath()->errors.GetErrorsForCert(1);
  ASSERT_TRUE(cert1_errors);
  EXPECT_TRUE(cert1_errors->ContainsError(kWarningFromDelegate));
}

DEFINE_CERT_ERROR_ID(kErrorFromDelegate, "Error from delegate");

class AddErrorPathBuilderDelegate : public CertPathBuilderDelegateBase {
 public:
  void CheckPathAfterVerification(const CertPathBuilder& path_builder,
                                  CertPathBuilderResultPath* path) override {
    path->errors.GetErrorsForCert(2)->AddError(kErrorFromDelegate, nullptr);
  }
};

TEST_F(PathBuilderCheckPathAfterVerificationTest, AddsErrorToValidPath) {
  AddErrorPathBuilderDelegate delegate;
  CertPathBuilder::Result result = RunPathBuilder(nullptr, &delegate);

  // Verification failed.
  ASSERT_FALSE(result.HasValidPath());

  ASSERT_LT(result.best_result_index, result.paths.size());
  const CertPathBuilderResultPath* failed_path =
      result.paths[result.best_result_index].get();
  ASSERT_TRUE(failed_path);

  // An error should have been added to certificate at index 2 in the path.
  const CertErrors* cert2_errors = failed_path->errors.GetErrorsForCert(2);
  ASSERT_TRUE(cert2_errors);
  EXPECT_TRUE(cert2_errors->ContainsError(kErrorFromDelegate));
}

TEST_F(PathBuilderCheckPathAfterVerificationTest, NoopToAlreadyInvalidPath) {
  StrictMock<MockPathBuilderDelegate> delegate;
  // Just verify that the hook is called (on an invalid path).
  EXPECT_CALL(delegate, CheckPathAfterVerification(_, _));

  // Run the pathbuilder with certificate at index 1 actively distrusted.
  CertPathBuilder::Result result = RunPathBuilder(test_.chain[1], &delegate);
  EXPECT_FALSE(result.HasValidPath());
}

struct DelegateData : public CertPathBuilderDelegateData {
  int value = 0xB33F;
};

class SetsDelegateDataPathBuilderDelegate : public CertPathBuilderDelegateBase {
 public:
  void CheckPathAfterVerification(const CertPathBuilder& path_builder,
                                  CertPathBuilderResultPath* path) override {
    path->delegate_data = std::make_unique<DelegateData>();
  }
};

TEST_F(PathBuilderCheckPathAfterVerificationTest, SetsDelegateData) {
  SetsDelegateDataPathBuilderDelegate delegate;
  CertPathBuilder::Result result = RunPathBuilder(nullptr, &delegate);
  ASSERT_TRUE(result.HasValidPath());

  DelegateData* data = reinterpret_cast<DelegateData*>(
      result.GetBestValidPath()->delegate_data.get());

  EXPECT_EQ(0xB33F, data->value);
}

TEST(PathBuilderPrioritizationTest, DatePrioritization) {
  std::string test_dir =
      "net/data/path_builder_unittest/validity_date_prioritization/";
  scoped_refptr<ParsedCertificate> root =
      ReadCertFromFile(test_dir + "root.pem");
  ASSERT_TRUE(root);
  scoped_refptr<ParsedCertificate> int_ac =
      ReadCertFromFile(test_dir + "int_ac.pem");
  ASSERT_TRUE(int_ac);
  scoped_refptr<ParsedCertificate> int_ad =
      ReadCertFromFile(test_dir + "int_ad.pem");
  ASSERT_TRUE(int_ad);
  scoped_refptr<ParsedCertificate> int_bc =
      ReadCertFromFile(test_dir + "int_bc.pem");
  ASSERT_TRUE(int_bc);
  scoped_refptr<ParsedCertificate> int_bd =
      ReadCertFromFile(test_dir + "int_bd.pem");
  ASSERT_TRUE(int_bd);
  scoped_refptr<ParsedCertificate> target =
      ReadCertFromFile(test_dir + "target.pem");
  ASSERT_TRUE(target);

  SimplePathBuilderDelegate delegate(
      1024, SimplePathBuilderDelegate::DigestPolicy::kWeakAllowSha1);
  der::GeneralizedTime verify_time = {2017, 3, 1, 0, 0, 0};

  // Distrust the root certificate. This will force the path builder to attempt
  // all possible paths.
  TrustStoreInMemory trust_store;
  trust_store.AddDistrustedCertificateForTest(root);

  for (bool reverse_input_order : {false, true}) {
    SCOPED_TRACE(reverse_input_order);

    CertIssuerSourceStatic intermediates;
    // Test with the intermediates supplied in two different orders to ensure
    // the results don't depend on input ordering.
    if (reverse_input_order) {
      intermediates.AddCert(int_bd);
      intermediates.AddCert(int_bc);
      intermediates.AddCert(int_ad);
      intermediates.AddCert(int_ac);
    } else {
      intermediates.AddCert(int_ac);
      intermediates.AddCert(int_ad);
      intermediates.AddCert(int_bc);
      intermediates.AddCert(int_bd);
    }

    CertPathBuilder path_builder(
        target, &trust_store, &delegate, verify_time, KeyPurpose::ANY_EKU,
        InitialExplicitPolicy::kFalse, {AnyPolicy()},
        InitialPolicyMappingInhibit::kFalse, InitialAnyPolicyInhibit::kFalse);
    path_builder.AddCertIssuerSource(&intermediates);

    CertPathBuilder::Result result = path_builder.Run();
    EXPECT_FALSE(result.HasValidPath());
    ASSERT_EQ(4U, result.paths.size());

    // Path builder should have attempted paths using the intermediates in
    // order: bd, bc, ad, ac

    EXPECT_FALSE(result.paths[0]->IsValid());
    ASSERT_EQ(3U, result.paths[0]->certs.size());
    EXPECT_EQ(target, result.paths[0]->certs[0]);
    EXPECT_EQ(int_bd, result.paths[0]->certs[1]);
    EXPECT_EQ(root, result.paths[0]->certs[2]);

    EXPECT_FALSE(result.paths[1]->IsValid());
    ASSERT_EQ(3U, result.paths[1]->certs.size());
    EXPECT_EQ(target, result.paths[1]->certs[0]);
    EXPECT_EQ(int_bc, result.paths[1]->certs[1]);
    EXPECT_EQ(root, result.paths[1]->certs[2]);

    EXPECT_FALSE(result.paths[2]->IsValid());
    ASSERT_EQ(3U, result.paths[2]->certs.size());
    EXPECT_EQ(target, result.paths[2]->certs[0]);
    EXPECT_EQ(int_ad, result.paths[2]->certs[1]);
    EXPECT_EQ(root, result.paths[2]->certs[2]);

    EXPECT_FALSE(result.paths[3]->IsValid());
    ASSERT_EQ(3U, result.paths[3]->certs.size());
    EXPECT_EQ(target, result.paths[3]->certs[0]);
    EXPECT_EQ(int_ac, result.paths[3]->certs[1]);
    EXPECT_EQ(root, result.paths[3]->certs[2]);
  }
}

TEST(PathBuilderPrioritizationTest, KeyIdPrioritization) {
  std::string test_dir =
      "net/data/path_builder_unittest/key_id_prioritization/";
  scoped_refptr<ParsedCertificate> root =
      ReadCertFromFile(test_dir + "root.pem");
  ASSERT_TRUE(root);
  scoped_refptr<ParsedCertificate> int_matching_ski_a =
      ReadCertFromFile(test_dir + "int_matching_ski_a.pem");
  ASSERT_TRUE(int_matching_ski_a);
  scoped_refptr<ParsedCertificate> int_matching_ski_b =
      ReadCertFromFile(test_dir + "int_matching_ski_b.pem");
  ASSERT_TRUE(int_matching_ski_b);
  scoped_refptr<ParsedCertificate> int_no_ski_a =
      ReadCertFromFile(test_dir + "int_no_ski_a.pem");
  ASSERT_TRUE(int_no_ski_a);
  scoped_refptr<ParsedCertificate> int_no_ski_b =
      ReadCertFromFile(test_dir + "int_no_ski_b.pem");
  ASSERT_TRUE(int_no_ski_b);
  scoped_refptr<ParsedCertificate> int_different_ski_a =
      ReadCertFromFile(test_dir + "int_different_ski_a.pem");
  ASSERT_TRUE(int_different_ski_a);
  scoped_refptr<ParsedCertificate> int_different_ski_b =
      ReadCertFromFile(test_dir + "int_different_ski_b.pem");
  ASSERT_TRUE(int_different_ski_b);
  scoped_refptr<ParsedCertificate> target =
      ReadCertFromFile(test_dir + "target.pem");
  ASSERT_TRUE(target);

  SimplePathBuilderDelegate delegate(
      1024, SimplePathBuilderDelegate::DigestPolicy::kWeakAllowSha1);
  der::GeneralizedTime verify_time = {2017, 3, 1, 0, 0, 0};

  // Distrust the root certificate. This will force the path builder to attempt
  // all possible paths.
  TrustStoreInMemory trust_store;
  trust_store.AddDistrustedCertificateForTest(root);

  for (bool reverse_input_order : {false, true}) {
    SCOPED_TRACE(reverse_input_order);

    CertIssuerSourceStatic intermediates;
    // Test with the intermediates supplied in two different orders to ensure
    // the results don't depend on input ordering.
    if (reverse_input_order) {
      intermediates.AddCert(int_different_ski_b);
      intermediates.AddCert(int_different_ski_a);
      intermediates.AddCert(int_no_ski_b);
      intermediates.AddCert(int_no_ski_a);
      intermediates.AddCert(int_matching_ski_b);
      intermediates.AddCert(int_matching_ski_a);
    } else {
      intermediates.AddCert(int_matching_ski_a);
      intermediates.AddCert(int_matching_ski_b);
      intermediates.AddCert(int_no_ski_a);
      intermediates.AddCert(int_no_ski_b);
      intermediates.AddCert(int_different_ski_a);
      intermediates.AddCert(int_different_ski_b);
    }

    CertPathBuilder path_builder(
        target, &trust_store, &delegate, verify_time, KeyPurpose::ANY_EKU,
        InitialExplicitPolicy::kFalse, {AnyPolicy()},
        InitialPolicyMappingInhibit::kFalse, InitialAnyPolicyInhibit::kFalse);
    path_builder.AddCertIssuerSource(&intermediates);

    CertPathBuilder::Result result = path_builder.Run();
    EXPECT_FALSE(result.HasValidPath());
    ASSERT_EQ(6U, result.paths.size());

    // Path builder should have attempted paths using the intermediates in
    // order: matching_ski_b, matching_ski_a, no_ski_b, no_ski_a,
    // different_ski_b, different_ski_a

    EXPECT_FALSE(result.paths[0]->IsValid());
    ASSERT_EQ(3U, result.paths[0]->certs.size());
    EXPECT_EQ(target, result.paths[0]->certs[0]);
    EXPECT_EQ(int_matching_ski_b, result.paths[0]->certs[1]);
    EXPECT_EQ(root, result.paths[0]->certs[2]);

    EXPECT_FALSE(result.paths[1]->IsValid());
    ASSERT_EQ(3U, result.paths[1]->certs.size());
    EXPECT_EQ(target, result.paths[1]->certs[0]);
    EXPECT_EQ(int_matching_ski_a, result.paths[1]->certs[1]);
    EXPECT_EQ(root, result.paths[1]->certs[2]);

    EXPECT_FALSE(result.paths[2]->IsValid());
    ASSERT_EQ(3U, result.paths[2]->certs.size());
    EXPECT_EQ(target, result.paths[2]->certs[0]);
    EXPECT_EQ(int_no_ski_b, result.paths[2]->certs[1]);
    EXPECT_EQ(root, result.paths[2]->certs[2]);

    EXPECT_FALSE(result.paths[3]->IsValid());
    ASSERT_EQ(3U, result.paths[3]->certs.size());
    EXPECT_EQ(target, result.paths[3]->certs[0]);
    EXPECT_EQ(int_no_ski_a, result.paths[3]->certs[1]);
    EXPECT_EQ(root, result.paths[3]->certs[2]);

    EXPECT_FALSE(result.paths[4]->IsValid());
    ASSERT_EQ(3U, result.paths[4]->certs.size());
    EXPECT_EQ(target, result.paths[4]->certs[0]);
    EXPECT_EQ(int_different_ski_b, result.paths[4]->certs[1]);
    EXPECT_EQ(root, result.paths[4]->certs[2]);

    EXPECT_FALSE(result.paths[5]->IsValid());
    ASSERT_EQ(3U, result.paths[5]->certs.size());
    EXPECT_EQ(target, result.paths[5]->certs[0]);
    EXPECT_EQ(int_different_ski_a, result.paths[5]->certs[1]);
    EXPECT_EQ(root, result.paths[5]->certs[2]);
  }
}

TEST(PathBuilderPrioritizationTest, TrustAndKeyIdPrioritization) {
  std::string test_dir =
      "net/data/path_builder_unittest/key_id_prioritization/";
  scoped_refptr<ParsedCertificate> root =
      ReadCertFromFile(test_dir + "root.pem");
  ASSERT_TRUE(root);
  scoped_refptr<ParsedCertificate> trusted_and_matching =
      ReadCertFromFile(test_dir + "int_matching_ski_a.pem");
  ASSERT_TRUE(trusted_and_matching);
  scoped_refptr<ParsedCertificate> matching =
      ReadCertFromFile(test_dir + "int_matching_ski_b.pem");
  ASSERT_TRUE(matching);
  scoped_refptr<ParsedCertificate> distrusted_and_matching =
      ReadCertFromFile(test_dir + "int_matching_ski_c.pem");
  ASSERT_TRUE(distrusted_and_matching);
  scoped_refptr<ParsedCertificate> trusted_and_no_match_data =
      ReadCertFromFile(test_dir + "int_no_ski_a.pem");
  ASSERT_TRUE(trusted_and_no_match_data);
  scoped_refptr<ParsedCertificate> no_match_data =
      ReadCertFromFile(test_dir + "int_no_ski_b.pem");
  ASSERT_TRUE(no_match_data);
  scoped_refptr<ParsedCertificate> distrusted_and_no_match_data =
      ReadCertFromFile(test_dir + "int_no_ski_c.pem");
  ASSERT_TRUE(distrusted_and_no_match_data);
  scoped_refptr<ParsedCertificate> trusted_and_mismatch =
      ReadCertFromFile(test_dir + "int_different_ski_a.pem");
  ASSERT_TRUE(trusted_and_mismatch);
  scoped_refptr<ParsedCertificate> mismatch =
      ReadCertFromFile(test_dir + "int_different_ski_b.pem");
  ASSERT_TRUE(mismatch);
  scoped_refptr<ParsedCertificate> distrusted_and_mismatch =
      ReadCertFromFile(test_dir + "int_different_ski_c.pem");
  ASSERT_TRUE(distrusted_and_mismatch);
  scoped_refptr<ParsedCertificate> target =
      ReadCertFromFile(test_dir + "target.pem");
  ASSERT_TRUE(target);

  SimplePathBuilderDelegate delegate(
      1024, SimplePathBuilderDelegate::DigestPolicy::kWeakAllowSha1);
  der::GeneralizedTime verify_time = {2017, 3, 1, 0, 0, 0};

  for (bool reverse_input_order : {false, true}) {
    SCOPED_TRACE(reverse_input_order);

    TrustStoreInMemory trust_store;
    // Test with the intermediates supplied in two different orders to ensure
    // the results don't depend on input ordering.
    if (reverse_input_order) {
      trust_store.AddTrustAnchor(trusted_and_matching);
      trust_store.AddCertificateWithUnspecifiedTrust(matching);
      trust_store.AddDistrustedCertificateForTest(distrusted_and_matching);
      trust_store.AddTrustAnchor(trusted_and_no_match_data);
      trust_store.AddCertificateWithUnspecifiedTrust(no_match_data);
      trust_store.AddDistrustedCertificateForTest(distrusted_and_no_match_data);
      trust_store.AddTrustAnchor(trusted_and_mismatch);
      trust_store.AddCertificateWithUnspecifiedTrust(mismatch);
      trust_store.AddDistrustedCertificateForTest(distrusted_and_mismatch);
    } else {
      trust_store.AddDistrustedCertificateForTest(distrusted_and_matching);
      trust_store.AddCertificateWithUnspecifiedTrust(no_match_data);
      trust_store.AddTrustAnchor(trusted_and_no_match_data);
      trust_store.AddTrustAnchor(trusted_and_matching);
      trust_store.AddCertificateWithUnspecifiedTrust(matching);
      trust_store.AddCertificateWithUnspecifiedTrust(mismatch);
      trust_store.AddDistrustedCertificateForTest(distrusted_and_no_match_data);
      trust_store.AddTrustAnchor(trusted_and_mismatch);
      trust_store.AddDistrustedCertificateForTest(distrusted_and_mismatch);
    }
    // Also distrust the root certificate. This will force the path builder to
    // report paths that included an unspecified trust intermediate.
    trust_store.AddDistrustedCertificateForTest(root);

    CertPathBuilder path_builder(
        target, &trust_store, &delegate, verify_time, KeyPurpose::ANY_EKU,
        InitialExplicitPolicy::kFalse, {AnyPolicy()},
        InitialPolicyMappingInhibit::kFalse, InitialAnyPolicyInhibit::kFalse);
    path_builder.SetExploreAllPaths(true);

    CertPathBuilder::Result result = path_builder.Run();
    EXPECT_TRUE(result.HasValidPath());
    ASSERT_EQ(9U, result.paths.size());

    // Path builder should have attempted paths using the intermediates in
    // order: trusted_and_matching, trusted_and_no_match_data, matching,
    // no_match_data, trusted_and_mismatch, mismatch, distrusted_and_matching,
    // distrusted_and_no_match_data, distrusted_and_mismatch.

    EXPECT_TRUE(result.paths[0]->IsValid());
    ASSERT_EQ(2U, result.paths[0]->certs.size());
    EXPECT_EQ(target, result.paths[0]->certs[0]);
    EXPECT_EQ(trusted_and_matching, result.paths[0]->certs[1]);

    EXPECT_TRUE(result.paths[1]->IsValid());
    ASSERT_EQ(2U, result.paths[1]->certs.size());
    EXPECT_EQ(target, result.paths[1]->certs[0]);
    EXPECT_EQ(trusted_and_no_match_data, result.paths[1]->certs[1]);

    EXPECT_FALSE(result.paths[2]->IsValid());
    ASSERT_EQ(3U, result.paths[2]->certs.size());
    EXPECT_EQ(target, result.paths[2]->certs[0]);
    EXPECT_EQ(matching, result.paths[2]->certs[1]);
    EXPECT_EQ(root, result.paths[2]->certs[2]);

    EXPECT_FALSE(result.paths[3]->IsValid());
    ASSERT_EQ(3U, result.paths[3]->certs.size());
    EXPECT_EQ(target, result.paths[3]->certs[0]);
    EXPECT_EQ(no_match_data, result.paths[3]->certs[1]);
    EXPECT_EQ(root, result.paths[3]->certs[2]);

    // Although this intermediate is trusted, it has the wrong key, so
    // the path should not be valid.
    EXPECT_FALSE(result.paths[4]->IsValid());
    ASSERT_EQ(2U, result.paths[4]->certs.size());
    EXPECT_EQ(target, result.paths[4]->certs[0]);
    EXPECT_EQ(trusted_and_mismatch, result.paths[4]->certs[1]);

    EXPECT_FALSE(result.paths[5]->IsValid());
    ASSERT_EQ(3U, result.paths[5]->certs.size());
    EXPECT_EQ(target, result.paths[5]->certs[0]);
    EXPECT_EQ(mismatch, result.paths[5]->certs[1]);
    EXPECT_EQ(root, result.paths[5]->certs[2]);

    EXPECT_FALSE(result.paths[6]->IsValid());
    ASSERT_EQ(2U, result.paths[6]->certs.size());
    EXPECT_EQ(target, result.paths[6]->certs[0]);
    EXPECT_EQ(distrusted_and_matching, result.paths[6]->certs[1]);

    EXPECT_FALSE(result.paths[7]->IsValid());
    ASSERT_EQ(2U, result.paths[7]->certs.size());
    EXPECT_EQ(target, result.paths[7]->certs[0]);
    EXPECT_EQ(distrusted_and_no_match_data, result.paths[7]->certs[1]);

    EXPECT_FALSE(result.paths[8]->IsValid());
    ASSERT_EQ(2U, result.paths[8]->certs.size());
    EXPECT_EQ(target, result.paths[8]->certs[0]);
    EXPECT_EQ(distrusted_and_mismatch, result.paths[8]->certs[1]);
  }
}

// PathBuilder does not support prioritization based on the issuer name &
// serial in authorityKeyIdentifier, so this test just ensures that it does not
// affect prioritization order and that it is generally just ignored
// completely.
TEST(PathBuilderPrioritizationTest, KeyIdNameAndSerialPrioritization) {
  std::string test_dir =
      "net/data/path_builder_unittest/key_id_name_and_serial_prioritization/";
  scoped_refptr<ParsedCertificate> root =
      ReadCertFromFile(test_dir + "root.pem");
  ASSERT_TRUE(root);
  scoped_refptr<ParsedCertificate> root2 =
      ReadCertFromFile(test_dir + "root2.pem");
  ASSERT_TRUE(root2);
  scoped_refptr<ParsedCertificate> int_matching =
      ReadCertFromFile(test_dir + "int_matching.pem");
  ASSERT_TRUE(int_matching);
  scoped_refptr<ParsedCertificate> int_match_name_only =
      ReadCertFromFile(test_dir + "int_match_name_only.pem");
  ASSERT_TRUE(int_match_name_only);
  scoped_refptr<ParsedCertificate> int_mismatch =
      ReadCertFromFile(test_dir + "int_mismatch.pem");
  ASSERT_TRUE(int_mismatch);
  scoped_refptr<ParsedCertificate> target =
      ReadCertFromFile(test_dir + "target.pem");
  ASSERT_TRUE(target);

  SimplePathBuilderDelegate delegate(
      1024, SimplePathBuilderDelegate::DigestPolicy::kWeakAllowSha1);
  der::GeneralizedTime verify_time = {2017, 3, 1, 0, 0, 0};

  // Distrust the root certificates. This will force the path builder to attempt
  // all possible paths.
  TrustStoreInMemory trust_store;
  trust_store.AddDistrustedCertificateForTest(root);
  trust_store.AddDistrustedCertificateForTest(root2);

  for (bool reverse_input_order : {false, true}) {
    SCOPED_TRACE(reverse_input_order);

    CertIssuerSourceStatic intermediates;
    // Test with the intermediates supplied in two different orders to ensure
    // the results don't depend on input ordering.
    if (reverse_input_order) {
      intermediates.AddCert(int_mismatch);
      intermediates.AddCert(int_match_name_only);
      intermediates.AddCert(int_matching);
    } else {
      intermediates.AddCert(int_matching);
      intermediates.AddCert(int_match_name_only);
      intermediates.AddCert(int_mismatch);
    }

    CertPathBuilder path_builder(
        target, &trust_store, &delegate, verify_time, KeyPurpose::ANY_EKU,
        InitialExplicitPolicy::kFalse, {AnyPolicy()},
        InitialPolicyMappingInhibit::kFalse, InitialAnyPolicyInhibit::kFalse);
    path_builder.AddCertIssuerSource(&intermediates);

    CertPathBuilder::Result result = path_builder.Run();
    EXPECT_FALSE(result.HasValidPath());
    ASSERT_EQ(3U, result.paths.size());

    // The serial & issuer method is not used in prioritization, so the certs
    // should have been prioritized based on dates. The test certs have the
    // date priority order in the reverse of what authorityKeyIdentifier
    // prioritization would have done if it were supported.
    // Path builder should have attempted paths using the intermediates in
    // order: mismatch, match_name_only, matching

    EXPECT_FALSE(result.paths[0]->IsValid());
    ASSERT_EQ(3U, result.paths[0]->certs.size());
    EXPECT_EQ(target, result.paths[0]->certs[0]);
    EXPECT_EQ(int_mismatch, result.paths[0]->certs[1]);
    EXPECT_EQ(root2, result.paths[0]->certs[2]);

    EXPECT_FALSE(result.paths[1]->IsValid());
    ASSERT_EQ(3U, result.paths[1]->certs.size());
    EXPECT_EQ(target, result.paths[1]->certs[0]);
    EXPECT_EQ(int_match_name_only, result.paths[1]->certs[1]);
    EXPECT_EQ(root, result.paths[1]->certs[2]);

    EXPECT_FALSE(result.paths[2]->IsValid());
    ASSERT_EQ(3U, result.paths[2]->certs.size());
    EXPECT_EQ(target, result.paths[2]->certs[0]);
    EXPECT_EQ(int_matching, result.paths[2]->certs[1]);
    EXPECT_EQ(root, result.paths[2]->certs[2]);
  }
}

TEST(PathBuilderPrioritizationTest, SelfIssuedPrioritization) {
  std::string test_dir =
      "net/data/path_builder_unittest/self_issued_prioritization/";
  scoped_refptr<ParsedCertificate> root1 =
      ReadCertFromFile(test_dir + "root1.pem");
  ASSERT_TRUE(root1);
  scoped_refptr<ParsedCertificate> root1_cross =
      ReadCertFromFile(test_dir + "root1_cross.pem");
  ASSERT_TRUE(root1_cross);
  scoped_refptr<ParsedCertificate> target =
      ReadCertFromFile(test_dir + "target.pem");
  ASSERT_TRUE(target);

  SimplePathBuilderDelegate delegate(
      1024, SimplePathBuilderDelegate::DigestPolicy::kWeakAllowSha1);
  der::GeneralizedTime verify_time = {2017, 3, 1, 0, 0, 0};

  TrustStoreInMemory trust_store;
  trust_store.AddTrustAnchor(root1);
  trust_store.AddTrustAnchor(root1_cross);
  CertPathBuilder path_builder(
      target, &trust_store, &delegate, verify_time, KeyPurpose::ANY_EKU,
      InitialExplicitPolicy::kFalse, {AnyPolicy()},
      InitialPolicyMappingInhibit::kFalse, InitialAnyPolicyInhibit::kFalse);
  path_builder.SetExploreAllPaths(true);

  CertPathBuilder::Result result = path_builder.Run();
  EXPECT_TRUE(result.HasValidPath());

  // Path builder should have built paths to both trusted roots.
  ASSERT_EQ(2U, result.paths.size());

  // |root1| should have been preferred because it is self-issued, even though
  // the notBefore date is older than |root1_cross|.
  EXPECT_TRUE(result.paths[0]->IsValid());
  ASSERT_EQ(2U, result.paths[0]->certs.size());
  EXPECT_EQ(target, result.paths[0]->certs[0]);
  EXPECT_EQ(root1, result.paths[0]->certs[1]);

  EXPECT_TRUE(result.paths[1]->IsValid());
  ASSERT_EQ(2U, result.paths[1]->certs.size());
  EXPECT_EQ(target, result.paths[1]->certs[0]);
  EXPECT_EQ(root1_cross, result.paths[1]->certs[1]);
}

}  // namespace

}  // namespace net