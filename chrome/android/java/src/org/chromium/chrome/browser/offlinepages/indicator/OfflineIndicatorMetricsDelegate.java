// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.offlinepages.indicator;

import androidx.annotation.VisibleForTesting;

import org.chromium.base.metrics.RecordHistogram;
import org.chromium.chrome.browser.preferences.ChromePreferenceKeys;
import org.chromium.chrome.browser.preferences.SharedPreferencesManager;

/**
 * Tracks metrics about how long the Offline Indicator is shown. All state related to the metrics
 * are persisted to prefs, so if Chrome is ever killed while the Offline Indicator is shown, we can
 * continue tracking accurate metrics the next time Chrome is started up.
 */
public class OfflineIndicatorMetricsDelegate {
    /** Clock to use so we can mock the time in tests. */
    public interface Clock {
        long currentTimeMillis();
    }
    private static Clock sClock = System::currentTimeMillis;

    // UMA Histograms.
    public static final String OFFLINE_INDICATOR_SHOWN_DURATION_V2 =
            "OfflineIndicator.ShownDurationV2";
    public static final String OFFLINE_INDICATOR_SHOWN_DURATION_V2_IN_FOREGROUND =
            "OfflineIndicator.ShownDurationV2.InForeground";
    public static final String OFFLINE_INDICATOR_SHOWN_DURATION_V2_IN_BACKGROUND =
            "OfflineIndicator.ShownDurationV2.InBackground";
    public static final String
            OFFLINE_INDICATOR_SHOWN_DURATION_V2_IN_FOREGROUND_WITHOUT_BEING_BACKGROUNDED =
                    "OfflineIndicator.ShownDurationV2.InForegroundWithoutBeingBackgrounded";
    public static final String OFFLINE_INDICATOR_SHOWN_DURATION_V2_UNTIL_FIRST_TIME_BACKGROUNDED =
            "OfflineIndicator.ShownDurationV2.UntilFirstTimeBackgrounded";
    public static final String OFFLINE_INDICATOR_SHOWN_DURATION_V2_NUM_TIMES_BACKGROUNDED =
            "OfflineIndicator.ShownDurationV2.NumTimesBackgrounded";

    /** Whether or not we are tracking a shown duration of the offline indicator. */
    private boolean mIsTrackingShownDuration;

    /** Whether or not the application is in the foreground (true) or background (false) */
    private boolean mIsApplicationForeground;

    /**
     * The wall time in milliseconds of the most recent time the offline indicator began being
     * shown. This value is persisted in prefs if |mIsTrackingShownDuration| is true.
     */
    private long mIndicatorShownWallTimeMs;

    /**
     * While tracking a shown duration, the wall time in miiliseconds of the most recent state
     * change (offline, online, foreground, or background). This value is persisted in prefs when
     * updated.
     */
    private long mLastUpdateWallTimeMs;

    /**
     * While tracking a shown duration, the total time, in milliseconds, that the application was
     * in foreground. This value is persisted in prefs when updated.
     */
    private long mTimeInForegroundMs;

    /**
     * While tracking a shown duration, the total time, in milliseconds, that the application was
     * in background. This value is persisted in prefs when updated.
     */
    private long mTimeInBackgroundMs;

    /**
     * While tracking a shown duration, the time, in milliseconds, that the application was
     * in foreground until the first time it was backgrounded. This value is persisted in prefs when
     * updated.
     */
    private long mFirstTimeInForegroundMs;

    /**
     * While tracking a shown duration, the number of times the application changes from foreground
     * to background. This value is persisted in prefs when updated.
     */
    private int mNumTimesBackgrounded;

    public OfflineIndicatorMetricsDelegate() {
        SharedPreferencesManager sharedPreferencesManager = SharedPreferencesManager.getInstance();

        // Read stored state from Prefs
        if (sharedPreferencesManager.contains(
                    ChromePreferenceKeys.OFFLINE_INDICATOR_V2_WALL_TIME_SHOWN_MS)) {
            mIndicatorShownWallTimeMs = sharedPreferencesManager.readLong(
                    ChromePreferenceKeys.OFFLINE_INDICATOR_V2_WALL_TIME_SHOWN_MS);
            mIsTrackingShownDuration = true;
        }

        if (sharedPreferencesManager.contains(
                    ChromePreferenceKeys.OFFLINE_INDICATOR_V2_LAST_UPDATE_WALL_TIME_MS)) {
            mLastUpdateWallTimeMs = sharedPreferencesManager.readLong(
                    ChromePreferenceKeys.OFFLINE_INDICATOR_V2_LAST_UPDATE_WALL_TIME_MS);
        }

        if (sharedPreferencesManager.contains(
                    ChromePreferenceKeys.OFFLINE_INDICATOR_V2_TIME_IN_FOREGROUND_MS)) {
            mTimeInForegroundMs = sharedPreferencesManager.readLong(
                    ChromePreferenceKeys.OFFLINE_INDICATOR_V2_TIME_IN_FOREGROUND_MS);
        }

        if (sharedPreferencesManager.contains(
                    ChromePreferenceKeys.OFFLINE_INDICATOR_V2_TIME_IN_BACKGROUND_MS)) {
            mTimeInBackgroundMs = sharedPreferencesManager.readLong(
                    ChromePreferenceKeys.OFFLINE_INDICATOR_V2_TIME_IN_BACKGROUND_MS);
        }

        if (sharedPreferencesManager.contains(
                    ChromePreferenceKeys.OFFLINE_INDICATOR_V2_FIRST_TIME_IN_FOREGROUND_MS)) {
            mFirstTimeInForegroundMs = sharedPreferencesManager.readLong(
                    ChromePreferenceKeys.OFFLINE_INDICATOR_V2_FIRST_TIME_IN_FOREGROUND_MS);
        }

        if (sharedPreferencesManager.contains(
                    ChromePreferenceKeys.OFFLINE_INDICATOR_V2_NUM_TIMES_BACKGROUNDED)) {
            mNumTimesBackgrounded = sharedPreferencesManager.readInt(
                    ChromePreferenceKeys.OFFLINE_INDICATOR_V2_NUM_TIMES_BACKGROUNDED);
        }
    }

    /**
     * Returns whether we are currently tracking a shown duration of the offline indicator or not.
     */
    public boolean isTrackingShownDuration() {
        return mIsTrackingShownDuration;
    }

    /**
     * When we are tracking a persisted shown duration and the offline state is initialized to
     * online, then we treat this as if the offline indicator was just hidden.
     * @param isOffline The offline state at initialization.
     */
    public void onOfflineStateInitialized(boolean isOffline) {
        if (mIsTrackingShownDuration && !isOffline) {
            onIndicatorHidden();
        }
    }

    /**
     * When the Offline Indicator is shown, then we begin tracking a shown duration if not already
     * tracking one. This function can be called when already tracking a shown duration, if there
     * was state stored in Prefs when Chrome started up.
     */
    public void onIndicatorShown() {
        if (mIsTrackingShownDuration) return;

        SharedPreferencesManager sharedPreferencesManager = SharedPreferencesManager.getInstance();

        long currentTimeMs = sClock.currentTimeMillis();

        mIsTrackingShownDuration = true;
        mIndicatorShownWallTimeMs = currentTimeMs;
        sharedPreferencesManager.writeLong(
                ChromePreferenceKeys.OFFLINE_INDICATOR_V2_WALL_TIME_SHOWN_MS,
                mIndicatorShownWallTimeMs);

        mLastUpdateWallTimeMs = currentTimeMs;
        sharedPreferencesManager.writeLong(
                ChromePreferenceKeys.OFFLINE_INDICATOR_V2_LAST_UPDATE_WALL_TIME_MS,
                mLastUpdateWallTimeMs);
    }

    /**
     * When the Offline Indicator is hidden, then we record the shown duration. If the indicator is
     * not shown when Chrome starts up and it was shown the last time Chrome was running in the
     * foreground, then we count the indicator as being hidden.
     */
    public void onIndicatorHidden() {
        if (!mIsTrackingShownDuration) return;

        if (mIsApplicationForeground) {
            updateForegroundPeriod();
        } else {
            updateBackgroundPeriod();
        }

        final long shownDurationWallTimeMs = sClock.currentTimeMillis() - mIndicatorShownWallTimeMs;

        // shownDurationWallTimeMs can be negative in cases where the system time is changed, so
        // we want to avoid recording metrics in cases where we know this happened.
        if (shownDurationWallTimeMs >= 0) {
            recordShownDurationHistograms();
        }

        reset();
    }

    /**
     * When the application is foregrounded, we update the state used to track how long the
     * indicator is shown while in the foreground or in the background.
     */
    public void onAppForegrounded() {
        if (mIsTrackingShownDuration) {
            updateBackgroundPeriod();
        }
        mIsApplicationForeground = true;
    }

    /**
     * When the application is backgrounded, we update the state used to track how long the
     * indicator is shown while in the foreground or in the background.
     */
    public void onAppBackgrounded() {
        if (mIsTrackingShownDuration) {
            updateForegroundPeriod();

            // Updates state based on the foreground to background transition.
            SharedPreferencesManager sharedPreferencesManager =
                    SharedPreferencesManager.getInstance();
            if (!sharedPreferencesManager.contains(
                        ChromePreferenceKeys.OFFLINE_INDICATOR_V2_FIRST_TIME_IN_FOREGROUND_MS)) {
                mFirstTimeInForegroundMs = mTimeInForegroundMs;
                sharedPreferencesManager.writeLong(
                        ChromePreferenceKeys.OFFLINE_INDICATOR_V2_FIRST_TIME_IN_FOREGROUND_MS,
                        mFirstTimeInForegroundMs);
            }

            mNumTimesBackgrounded++;
            sharedPreferencesManager.writeInt(
                    ChromePreferenceKeys.OFFLINE_INDICATOR_V2_NUM_TIMES_BACKGROUNDED,
                    mNumTimesBackgrounded);
        }
        mIsApplicationForeground = false;
    }

    /**
     * Updates the tracked metrics given that the app has been in the foreground from
     * |mLastUpdateWallTimeMs| until now. Metrics are persisted to prefs when updated.
     */
    private void updateForegroundPeriod() {
        SharedPreferencesManager sharedPreferencesManager = SharedPreferencesManager.getInstance();

        long currentTimeMs = sClock.currentTimeMillis();
        long timeSinceLastUpdateMs = currentTimeMs - mLastUpdateWallTimeMs;

        mTimeInForegroundMs += timeSinceLastUpdateMs;
        sharedPreferencesManager.writeLong(
                ChromePreferenceKeys.OFFLINE_INDICATOR_V2_TIME_IN_FOREGROUND_MS,
                mTimeInForegroundMs);

        mLastUpdateWallTimeMs = currentTimeMs;
        sharedPreferencesManager.writeLong(
                ChromePreferenceKeys.OFFLINE_INDICATOR_V2_LAST_UPDATE_WALL_TIME_MS,
                mLastUpdateWallTimeMs);
    }

    /**
     * Updates the tracked metrics given that the app has been in the background from
     * |mLastUpdateWallTimeMs| until now. Metrics are persisted to prefs when updated.
     */
    private void updateBackgroundPeriod() {
        SharedPreferencesManager sharedPreferencesManager = SharedPreferencesManager.getInstance();

        long currentTimeMs = sClock.currentTimeMillis();
        long timeSinceLastUpdateMs = currentTimeMs - mLastUpdateWallTimeMs;

        mTimeInBackgroundMs += timeSinceLastUpdateMs;
        sharedPreferencesManager.writeLong(
                ChromePreferenceKeys.OFFLINE_INDICATOR_V2_TIME_IN_BACKGROUND_MS,
                mTimeInBackgroundMs);

        mLastUpdateWallTimeMs = currentTimeMs;
        SharedPreferencesManager.getInstance().writeLong(
                ChromePreferenceKeys.OFFLINE_INDICATOR_V2_LAST_UPDATE_WALL_TIME_MS,
                mLastUpdateWallTimeMs);
    }

    /**
     * Records the set of UMA histograms related to the shown duration of the Offline indicator.
     * Some of the histograms are only recorded in certain cases.
     */
    private void recordShownDurationHistograms() {
        RecordHistogram.recordLongTimesHistogram100(
                OFFLINE_INDICATOR_SHOWN_DURATION_V2, mTimeInForegroundMs + mTimeInBackgroundMs);
        RecordHistogram.recordLongTimesHistogram(
                OFFLINE_INDICATOR_SHOWN_DURATION_V2_IN_FOREGROUND, mTimeInForegroundMs);
        RecordHistogram.recordCount100Histogram(
                OFFLINE_INDICATOR_SHOWN_DURATION_V2_NUM_TIMES_BACKGROUNDED, mNumTimesBackgrounded);

        if (SharedPreferencesManager.getInstance().contains(
                    ChromePreferenceKeys.OFFLINE_INDICATOR_V2_TIME_IN_BACKGROUND_MS)) {
            // These histograms are only recorded if the app was in the background at some point
            // while the offline indicator was shown.
            RecordHistogram.recordLongTimesHistogram(
                    OFFLINE_INDICATOR_SHOWN_DURATION_V2_IN_BACKGROUND, mTimeInBackgroundMs);
            RecordHistogram.recordLongTimesHistogram(
                    OFFLINE_INDICATOR_SHOWN_DURATION_V2_UNTIL_FIRST_TIME_BACKGROUNDED,
                    mFirstTimeInForegroundMs);
        } else {
            assert mNumTimesBackgrounded == 0;

            // This histogram is only recorded if the app was always in the foreground while the
            // offline indicator was shown.
            RecordHistogram.recordLongTimesHistogram(
                    OFFLINE_INDICATOR_SHOWN_DURATION_V2_IN_FOREGROUND_WITHOUT_BEING_BACKGROUNDED,
                    mTimeInForegroundMs);
        }
    }

    /**
     * Resets the state of this instance. Sets the member variables to zero or false, and clears the
     * persisted values from prefs.
     */
    private void reset() {
        mIndicatorShownWallTimeMs = 0;
        mLastUpdateWallTimeMs = 0;
        mTimeInForegroundMs = 0;
        mTimeInBackgroundMs = 0;
        mFirstTimeInForegroundMs = 0;
        mNumTimesBackgrounded = 0;
        mIsTrackingShownDuration = false;

        SharedPreferencesManager sharedPreferencesManager = SharedPreferencesManager.getInstance();
        sharedPreferencesManager.removeKey(
                ChromePreferenceKeys.OFFLINE_INDICATOR_V2_WALL_TIME_SHOWN_MS);
        sharedPreferencesManager.removeKey(
                ChromePreferenceKeys.OFFLINE_INDICATOR_V2_LAST_UPDATE_WALL_TIME_MS);
        sharedPreferencesManager.removeKey(
                ChromePreferenceKeys.OFFLINE_INDICATOR_V2_TIME_IN_FOREGROUND_MS);
        sharedPreferencesManager.removeKey(
                ChromePreferenceKeys.OFFLINE_INDICATOR_V2_TIME_IN_BACKGROUND_MS);
        sharedPreferencesManager.removeKey(
                ChromePreferenceKeys.OFFLINE_INDICATOR_V2_FIRST_TIME_IN_FOREGROUND_MS);
        sharedPreferencesManager.removeKey(
                ChromePreferenceKeys.OFFLINE_INDICATOR_V2_NUM_TIMES_BACKGROUNDED);
    }

    @VisibleForTesting
    static void setClockForTesting(Clock clock) {
        sClock = clock;
    }
}