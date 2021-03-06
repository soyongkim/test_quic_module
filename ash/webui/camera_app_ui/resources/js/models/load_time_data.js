// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// eslint-disable-next-line no-unused-vars
import {Flag} from '../flag.js';
// eslint-disable-next-line no-unused-vars
import {I18nString} from '../i18n_string.js';

/**
 * Returns the device board name.
 * @return {string}
 */
export function getBoard() {
  return window.loadTimeData.getString('board_name');
}

/**
 * Returns the device type.
 * @return {string}
 */
export function getDeviceType() {
  return window.loadTimeData.getString('device_type');
}

/**
 * Returns the I18N message generated by given |name| and |substitutions|.
 * @param {!I18nString} name
 * @param {...(string|number)} substitutions
 * @return {string}
 */
export function getI18nMessage(name, ...substitutions) {
  return window.loadTimeData.getStringF(name, ...substitutions);
}

/**
 * @return {string} Returns 'ltr'/'rtl' for left-to-right/right-to-left system
 *     UI language.
 */
export function getTextDirection() {
  return window.loadTimeData.getString('textdirection');
}

/**
 * Returns the boolean value of the chrome flag.
 * @param {Flag} flag
 * @return {boolean}
 */
export function getChromeFlag(flag) {
  return window.loadTimeData.getBoolean(flag);
}
