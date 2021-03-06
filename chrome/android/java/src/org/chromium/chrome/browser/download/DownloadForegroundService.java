// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.download;

import org.chromium.chrome.browser.base.SplitCompatService;
import org.chromium.chrome.browser.base.SplitCompatUtils;

/** See {@link DownloadForegroundServiceImpl}. */
public class DownloadForegroundService extends SplitCompatService {
    public DownloadForegroundService() {
        super(SplitCompatUtils.getIdentifierName(
                "org.chromium.chrome.browser.download.DownloadForegroundServiceImpl"));
    }
}
