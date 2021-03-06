// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let lazyLoadPromise: Promise<void[]>|null = null;

/**
 * @return Resolves when the lazy load module is imported.
 */
export function ensureLazyLoaded(): Promise<void[]> {
  if (!lazyLoadPromise) {
    const script = document.createElement('script');
    script.type = 'module';
    script.src = './lazy_load.js';
    document.body.appendChild(script);

    lazyLoadPromise = Promise.all([
      'profile-type-choice', 'local-profile-customization', 'profile-switch',
      // <if expr="lacros">
      'account-selection-lacros',
      // </if>
    ].map(name => customElements.whenDefined(name)));
  }
  return lazyLoadPromise;
}
