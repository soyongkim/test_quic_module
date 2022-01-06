// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// NIST P-256 public key made available to tests. Must be an uncompressed
// point in accordance with SEC1 2.3.3.
var kApplicationServerKey = new Uint8Array([
  0x04, 0x55, 0x52, 0x6A, 0xA5, 0x6E, 0x8E, 0xAA, 0x47, 0x97, 0x36, 0x10, 0xC1,
  0x66, 0x3C, 0x1E, 0x65, 0xBF, 0xA1, 0x7B, 0xEE, 0x48, 0xC9, 0xC6, 0xBB, 0xBF,
  0x02, 0x18, 0x53, 0x72, 0x1D, 0x0C, 0x7B, 0xA9, 0xE3, 0x11, 0xB7, 0x03, 0x52,
  0x21, 0xD3, 0x71, 0x90, 0x13, 0xA8, 0xC1, 0xCF, 0xED, 0x20, 0xF7, 0x1F, 0xD1,
  0x7F, 0xF2, 0x76, 0xB6, 0x01, 0x20, 0xD8, 0x35, 0xA5, 0xD9, 0x3C, 0x43, 0xFD
]);

// Base64URLEncoded version of |kApplicationServerKey|
const kBase64URLEncodedKey = "BFVSaqVujqpHlzYQwWY8HmW_oXvuSMnGu78CGFNyHQx7qeMRtwNSIdNxkBOowc_tIPcf0X_ydrYBINg1pdk8Q_0"