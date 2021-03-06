// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if defined(OS_WIN)
#define MAYBE_WebAppIntegration_ChngAMode1_ChckAMode1_ChckBMode1 \
  DISABLED_WebAppIntegration_ChngAMode1_ChckAMode1_ChckBMode1
#else
#define MAYBE_WebAppIntegration_ChngAMode1_ChckAMode1_ChckBMode1 \
  WebAppIntegration_ChngAMode1_ChckAMode1_ChckBMode1
#endif
IN_PROC_BROWSER_TEST_F(
    WebAppIntegrationBrowserTest,
    MAYBE_WebAppIntegration_ChngAMode1_ChckAMode1_ChckBMode1) {
  // Test contents are generated by script. Please do not modify!
  // See `chrome/test/webapps/README.md` for more info.
  // Sheriffs: Disabling this test is supported.
  StateChangeA("Mode1");
  CheckA("Mode1");
  CheckB("Mode1");
}
