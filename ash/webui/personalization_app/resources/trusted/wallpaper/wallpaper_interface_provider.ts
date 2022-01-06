// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview a singleton getter for the mojom interface used in
 * the Personalization SWA. Also contains utility functions around fetching
 * mojom data and mocking out the implementation for testing.
 */

import 'chrome://resources/mojo/mojo/public/js/bindings.js';
import 'chrome://resources/mojo/url/mojom/url.mojom-webui.js';

import {WallpaperProvider, WallpaperProviderInterface} from '../personalization_app.mojom-webui.js';

let wallpaperProvider: WallpaperProviderInterface|null = null;

/**
 * @param {!WallpaperProviderInterface}
 *     testProvider
 */
export function setWallpaperProviderForTesting(
    testProvider: WallpaperProviderInterface): void {
  wallpaperProvider = testProvider;
}

/** Returns a singleton for the WallpaperProvider mojom interface. */
export function getWallpaperProvider(): WallpaperProviderInterface {
  if (!wallpaperProvider) {
    wallpaperProvider = WallpaperProvider.getRemote();
    wallpaperProvider.makeTransparent();
  }
  return wallpaperProvider;
}