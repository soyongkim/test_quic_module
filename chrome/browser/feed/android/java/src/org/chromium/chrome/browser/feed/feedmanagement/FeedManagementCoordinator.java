// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.feed.feedmanagement;

import android.app.Activity;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.ListView;

import androidx.appcompat.app.AppCompatActivity;
import androidx.appcompat.widget.Toolbar;

import org.chromium.chrome.browser.feed.R;
import org.chromium.chrome.browser.feed.feedmanagement.FeedManagementMediator.AutoplayManagementLauncher;
import org.chromium.chrome.browser.feed.feedmanagement.FeedManagementMediator.FollowManagementLauncher;
import org.chromium.ui.modelutil.LayoutViewBuilder;
import org.chromium.ui.modelutil.MVCListAdapter.ModelList;
import org.chromium.ui.modelutil.ModelListAdapter;
/**
 * Sets up the model, adapter, and mediator for FeedManagement surface.  It is based on the doc at
 * https://chromium.googlesource.com/chromium/src/+/HEAD/docs/ui/android/mvc_simple_list_tutorial.md
 */
public class FeedManagementCoordinator {
    private FeedManagementMediator mMediator;
    private AppCompatActivity mActivity;
    private final View mView;

    public FeedManagementCoordinator(Activity activity,
            FollowManagementLauncher followManagementLauncher,
            AutoplayManagementLauncher autoplayManagementLauncher) {
        mActivity = (AppCompatActivity) activity;
        ModelList listItems = new ModelList();

        // Once this is attached to the ListView, there is no need to hold a reference to it.
        ModelListAdapter adapter = new ModelListAdapter(listItems);
        adapter.registerType(FeedManagementItemProperties.DEFAULT_ITEM_TYPE,
                new LayoutViewBuilder<FeedManagementItemView>(R.layout.feed_management_list_item),
                FeedManagementItemViewBinder::bind);

        // Inflate the XML.
        mView = LayoutInflater.from(mActivity).inflate(R.layout.feed_management_activity, null);
        ListView listView = (ListView) mView.findViewById(R.id.feed_management_menu);
        listView.setAdapter(adapter);

        // Set up the toolbar and back button.
        Toolbar toolbar = (Toolbar) mView.findViewById(R.id.action_bar);
        mActivity.setSupportActionBar(toolbar);
        mActivity.getSupportActionBar().setDisplayHomeAsUpEnabled(true);
        toolbar.setNavigationOnClickListener(this::handleBackArrowClick);

        mMediator = new FeedManagementMediator(
                mActivity, listItems, followManagementLauncher, autoplayManagementLauncher);
    }

    public View getView() {
        return mView;
    }

    private void handleBackArrowClick(View view) {
        // Navigate back.
        mActivity.finish();
    }
}
