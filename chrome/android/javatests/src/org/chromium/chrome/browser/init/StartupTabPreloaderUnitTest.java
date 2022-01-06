// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.init;

import android.content.ComponentName;
import android.content.Intent;
import android.net.Uri;

import androidx.test.filters.SmallTest;

import org.junit.Assert;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.Callback;
import org.chromium.base.ThreadUtils;
import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.base.supplier.Supplier;
import org.chromium.chrome.browser.IntentHandler;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.metrics.ActivityTabStartupMetricsTracker;
import org.chromium.chrome.browser.tabmodel.ChromeTabCreator;
import org.chromium.chrome.browser.tabmodel.TabCreator;
import org.chromium.chrome.browser.tabmodel.TabCreatorManager;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;
import org.chromium.chrome.browser.tabmodel.document.TabDelegate;
import org.chromium.chrome.test.ChromeJUnit4ClassRunner;
import org.chromium.chrome.test.util.browser.Features;
import org.chromium.chrome.test.util.browser.Features.DisableFeatures;
import org.chromium.chrome.test.util.browser.Features.EnableFeatures;
import org.chromium.content_public.browser.LoadUrlParams;
import org.chromium.content_public.common.Referrer;
import org.chromium.network.mojom.ReferrerPolicy;

/**
 * Unit tests for {@link StartupTabPreloader}.
 */
@RunWith(ChromeJUnit4ClassRunner.class)
public class StartupTabPreloaderUnitTest {
    private static final String SITE_A = "https://a.com";
    private static final String SITE_B = "https://b.com";
    private static final String SITE_C = "https://c.com";
    private static final String INVALID_SCHEME = "javascript:alert()";
    private static final Intent VIEW_INTENT =
            new Intent(Intent.ACTION_VIEW).setData(Uri.parse(SITE_A));
    private static final Intent CHROME_MAIN_COMPONENT_INTENT =
            new Intent()
                    .setComponent(new ComponentName("com.google.android.apps.chrome",
                            "com.google.android.apps.chrome.Main"))
                    .setData(Uri.parse(SITE_A));
    private static final Intent INCOGNITO_VIEW_INTENT =
            new Intent(Intent.ACTION_VIEW)
                    .setData(Uri.parse(SITE_A))
                    .putExtra(IntentHandler.EXTRA_OPEN_NEW_INCOGNITO_TAB, true);
    private static final Intent VIEW_INTENT_WITH_INVALID_SCHEME =
            new Intent(Intent.ACTION_VIEW).setData(Uri.parse(INVALID_SCHEME));
    private static final Intent MAIN_INTENT_WITH_URL =
            new Intent(Intent.ACTION_MAIN).setData(Uri.parse(SITE_B));
    private static final Intent MAIN_INTENT_WITHOUT_URL = new Intent(Intent.ACTION_MAIN);
    private static final TabCreatorManager sChromeTabCreator = new ChromeTabCreatorManager();
    private static final TabCreatorManager sUninitializedChromeTabCreatorManager =
            new UninitializedChromeTabCreatorManager();
    private static final TabCreatorManager sNonChromeTabCreator = new NonChromeTabCreatorManager();

    @Rule
    public Features.JUnitProcessor processor = new Features.JUnitProcessor();

    @Test
    @SmallTest
    public void testDoLoadUrlParamsMatchForWarmupManagerNavigation() {
        LoadUrlParams siteAWithSiteBReferrer_1 = new LoadUrlParams(SITE_A);
        siteAWithSiteBReferrer_1.setReferrer(new Referrer(SITE_B, ReferrerPolicy.DEFAULT));
        LoadUrlParams siteAWithSiteBReferrer_2 = new LoadUrlParams(SITE_A);
        siteAWithSiteBReferrer_2.setReferrer(new Referrer(SITE_B, ReferrerPolicy.DEFAULT));

        LoadUrlParams siteAWithSiteBReferrer = new LoadUrlParams(SITE_A);
        siteAWithSiteBReferrer.setReferrer(new Referrer(SITE_B, ReferrerPolicy.DEFAULT));

        LoadUrlParams siteAWithSiteCReferrer = new LoadUrlParams(SITE_A);
        siteAWithSiteCReferrer.setReferrer(new Referrer(SITE_C, ReferrerPolicy.DEFAULT));

        LoadUrlParams siteAWithNoReferrer = new LoadUrlParams(SITE_A);
        LoadUrlParams siteBWithNoReferrer_1 = new LoadUrlParams(SITE_B);
        LoadUrlParams siteBWithNoReferrer_2 = new LoadUrlParams(SITE_B);

        Assert.assertTrue(StartupTabPreloader.doLoadUrlParamsMatchForWarmupManagerNavigation(
                siteAWithSiteBReferrer_1, siteAWithSiteBReferrer_2));
        Assert.assertTrue(StartupTabPreloader.doLoadUrlParamsMatchForWarmupManagerNavigation(
                siteBWithNoReferrer_2, siteBWithNoReferrer_2));

        Assert.assertFalse(StartupTabPreloader.doLoadUrlParamsMatchForWarmupManagerNavigation(
                siteAWithSiteBReferrer_1, siteAWithSiteCReferrer));
        Assert.assertFalse(StartupTabPreloader.doLoadUrlParamsMatchForWarmupManagerNavigation(
                siteAWithSiteBReferrer_1, siteAWithNoReferrer));
        Assert.assertFalse(StartupTabPreloader.doLoadUrlParamsMatchForWarmupManagerNavigation(
                siteAWithSiteBReferrer_1, siteBWithNoReferrer_1));
        Assert.assertFalse(StartupTabPreloader.doLoadUrlParamsMatchForWarmupManagerNavigation(
                siteAWithNoReferrer, siteBWithNoReferrer_1));
    }

    @Test
    @SmallTest
    @DisableFeatures(ChromeFeatureList.ELIDE_TAB_PRELOAD_AT_STARTUP)
    public void testShouldLoadTab_AllowViewIntents() {
        StartupTabPreloader preloader = createStartupTabPreloader(VIEW_INTENT, sChromeTabCreator);
        Assert.assertTrue(preloader.shouldLoadTab());
        Assert.assertEquals(StartupTabPreloader.LoadDecisionReason.ALL_SATISFIED,
                preloader.getLoadDecisionReason());
    }

    @Test
    @SmallTest
    @DisableFeatures(ChromeFeatureList.ELIDE_TAB_PRELOAD_AT_STARTUP)
    public void testShouldLoadTab_AllowChromeMainComponentIntentWithUrl() {
        StartupTabPreloader preloader =
                createStartupTabPreloader(CHROME_MAIN_COMPONENT_INTENT, sChromeTabCreator);
        Assert.assertTrue(preloader.shouldLoadTab());
        Assert.assertEquals(StartupTabPreloader.LoadDecisionReason.ALL_SATISFIED,
                preloader.getLoadDecisionReason());
    }

    @Test
    @SmallTest
    @DisableFeatures(ChromeFeatureList.ELIDE_TAB_PRELOAD_AT_STARTUP)
    public void testShouldLoadTab_AllowMainIntentsWithUrl() {
        StartupTabPreloader preloader =
                createStartupTabPreloader(MAIN_INTENT_WITH_URL, sChromeTabCreator);
        Assert.assertTrue(preloader.shouldLoadTab());
        Assert.assertEquals(StartupTabPreloader.LoadDecisionReason.ALL_SATISFIED,
                preloader.getLoadDecisionReason());
    }

    @Test
    @SmallTest
    @DisableFeatures(ChromeFeatureList.ELIDE_TAB_PRELOAD_AT_STARTUP)
    public void testShouldLoadTab_BlockedMainIntentsWithoutUrl() {
        StartupTabPreloader preloader =
                createStartupTabPreloader(MAIN_INTENT_WITHOUT_URL, sChromeTabCreator);
        Assert.assertFalse(preloader.shouldLoadTab());
        Assert.assertEquals(
                StartupTabPreloader.LoadDecisionReason.NO_URL, preloader.getLoadDecisionReason());
    }

    @Test
    @SmallTest
    @EnableFeatures(ChromeFeatureList.ELIDE_TAB_PRELOAD_AT_STARTUP)
    public void testShouldLoadTab_BlockedWhenSpecifiedByFeature() {
        StartupTabPreloader preloader = createStartupTabPreloader(VIEW_INTENT, sChromeTabCreator);
        Assert.assertFalse(preloader.shouldLoadTab());
        Assert.assertEquals(StartupTabPreloader.LoadDecisionReason.DISABLED_BY_FEATURE,
                preloader.getLoadDecisionReason());
    }

    @Test
    @SmallTest
    @DisableFeatures(ChromeFeatureList.ELIDE_TAB_PRELOAD_AT_STARTUP)
    public void testShouldLoadTab_BlockedInvalidSchemeIntent() {
        StartupTabPreloader preloader =
                createStartupTabPreloader(VIEW_INTENT_WITH_INVALID_SCHEME, sChromeTabCreator);
        Assert.assertFalse(preloader.shouldLoadTab());
        Assert.assertEquals(StartupTabPreloader.LoadDecisionReason.INTENT_IGNORED,
                preloader.getLoadDecisionReason());
    }

    @Test
    @SmallTest
    @DisableFeatures(ChromeFeatureList.ELIDE_TAB_PRELOAD_AT_STARTUP)
    public void testShouldLoadTab_BlockedNonChromeTabCreators() {
        StartupTabPreloader preloader =
                createStartupTabPreloader(VIEW_INTENT, sNonChromeTabCreator);
        Assert.assertFalse(preloader.shouldLoadTab());
        Assert.assertEquals(StartupTabPreloader.LoadDecisionReason.WRONG_TAB_CREATOR,
                preloader.getLoadDecisionReason());
    }

    @Test
    @SmallTest
    @DisableFeatures(ChromeFeatureList.ELIDE_TAB_PRELOAD_AT_STARTUP)
    public void testShouldLoadTab_BlockedIncognitoIntents() {
        StartupTabPreloader preloader =
                createStartupTabPreloader(INCOGNITO_VIEW_INTENT, sChromeTabCreator);
        Assert.assertFalse(preloader.shouldLoadTab());
        Assert.assertEquals(StartupTabPreloader.LoadDecisionReason.INCOGNITO,
                preloader.getLoadDecisionReason());
    }

    @Test
    @SmallTest
    @DisableFeatures(ChromeFeatureList.ELIDE_TAB_PRELOAD_AT_STARTUP)
    public void testShouldLoadTab_UninitializedTabCreatorManager() {
        StartupTabPreloader preloader =
                createStartupTabPreloader(VIEW_INTENT, sUninitializedChromeTabCreatorManager);
        Assert.assertFalse(preloader.shouldLoadTab());
        Assert.assertEquals(StartupTabPreloader.LoadDecisionReason.NO_TAB_CREATOR,
                preloader.getLoadDecisionReason());
    }

    private StartupTabPreloader createStartupTabPreloader(
            Intent intent, TabCreatorManager tabCreatorManager) {
        // StartupTabPreloader calls into code that asserts that it is on the UI thread, which
        // doesn't exist in this unittesting context.
        ThreadUtils.setThreadAssertsDisabledForTesting(true);

        return new StartupTabPreloader(
                new Supplier<Intent>() {
                    @Override
                    public Intent get() {
                        return intent;
                    }
                },
                new ActivityLifecycleDispatcherImpl(null), null, tabCreatorManager,
                new IntentHandler(null, null),
                new ActivityTabStartupMetricsTracker(new ObservableSupplier<TabModelSelector>() {
                    @Override
                    public TabModelSelector addObserver(Callback<TabModelSelector> obs) {
                        return null;
                    }

                    @Override
                    public void removeObserver(Callback<TabModelSelector> obs) {}

                    @Override
                    public TabModelSelector get() {
                        return null;
                    }
                }));
    }

    private static class ChromeTabCreatorManager implements TabCreatorManager {
        @Override
        public TabCreator getTabCreator(boolean incognito) {
            Assert.assertFalse(incognito);
            return new ChromeTabCreator(null, null, null, null, false, null, null, null, null);
        }
    }

    private static class UninitializedChromeTabCreatorManager implements TabCreatorManager {
        @Override
        public TabCreator getTabCreator(boolean incognito) {
            throw new IllegalStateException("uninitialized for test");
        }
    }

    private static class NonChromeTabCreatorManager implements TabCreatorManager {
        @Override
        public TabCreator getTabCreator(boolean incognito) {
            Assert.assertFalse(incognito);

            // The important thing is this isn't ChromeTabCreator.
            return new TabDelegate(false);
        }
    }
}