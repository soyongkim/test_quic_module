// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/http_request_info.h"

#include "net/dns/public/secure_dns_policy.h"

namespace net {

HttpRequestInfo::HttpRequestInfo()
    : is_subframe_document_resource(false),
      upload_data_stream(nullptr),
      load_flags(0),
      privacy_mode(PRIVACY_MODE_DISABLED),
      secure_dns_policy(SecureDnsPolicy::kAllow),
      reporting_upload_depth(0),
      idempotency(net::DEFAULT_IDEMPOTENCY) {}

HttpRequestInfo::HttpRequestInfo(const HttpRequestInfo& other) = default;

HttpRequestInfo::~HttpRequestInfo() = default;

}  // namespace net
