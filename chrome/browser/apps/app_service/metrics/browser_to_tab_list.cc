// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/apps/app_service/metrics/browser_to_tab_list.h"

#include "ui/aura/window.h"

namespace apps {

BrowserToTabList::BrowserToTabList() = default;

BrowserToTabList::~BrowserToTabList() = default;

BrowserToTabList::BrowserToTab::BrowserToTab(
    const aura::Window* browser_window,
    const base::UnguessableToken& tab_id)
    : browser_window(browser_window), tab_id(tab_id) {}

bool BrowserToTabList::HasActivatedTab(const aura::Window* browser_window) {
  for (const auto& it : active_browsers_to_tabs_) {
    if (it.browser_window == browser_window) {
      return true;
    }
  }
  return false;
}

const aura::Window* BrowserToTabList::GetBrowserWindow(
    const base::UnguessableToken& tab_id) const {
  for (const auto& it : active_browsers_to_tabs_) {
    if (it.tab_id == tab_id) {
      return it.browser_window;
    }
  }
  return nullptr;
}

void BrowserToTabList::AddActivatedTab(const aura::Window* browser_window,
                                       const base::UnguessableToken& tab_id) {
  bool found = false;
  for (const auto& it : active_browsers_to_tabs_) {
    if (it.browser_window == browser_window && it.tab_id == tab_id) {
      found = true;
      break;
    }
  }

  if (!found) {
    active_browsers_to_tabs_.push_back(BrowserToTab(browser_window, tab_id));
  }
}

void BrowserToTabList::RemoveActivatedTab(
    const base::UnguessableToken& tab_id) {
  active_browsers_to_tabs_.remove_if(
      [&](const BrowserToTab& item) { return item.tab_id == tab_id; });
}

}  // namespace apps
