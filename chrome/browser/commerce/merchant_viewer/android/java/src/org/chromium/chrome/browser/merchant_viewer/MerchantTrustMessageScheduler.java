// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.merchant_viewer;

import android.os.Handler;
import android.util.Pair;

import androidx.annotation.VisibleForTesting;

import org.chromium.base.Callback;
import org.chromium.base.ThreadUtils;
import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.chrome.browser.merchant_viewer.MerchantTrustMetrics.MessageClearReason;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.components.messages.DismissReason;
import org.chromium.components.messages.MessageDispatcher;
import org.chromium.components.messages.MessageScopeType;
import org.chromium.ui.modelutil.PropertyModel;

/** Abstracts the logic needed to schedule a message using {@link MessageDispatcher} framework. */
public class MerchantTrustMessageScheduler {
    public static final long MESSAGE_ENQUEUE_NO_DELAY = 0;

    private final MessageDispatcher mMessageDispatcher;
    private final MerchantTrustMetrics mMetrics;
    private final ObservableSupplier<Tab> mTabSupplier;
    private Handler mEnqueueMessageTimer;
    private Pair<MerchantTrustMessageContext, PropertyModel> mScheduledMessage;

    public MerchantTrustMessageScheduler(MessageDispatcher messageDispatcher,
            MerchantTrustMetrics metrics, ObservableSupplier<Tab> tabSupplier) {
        mEnqueueMessageTimer = new Handler(ThreadUtils.getUiThreadLooper());
        mMessageDispatcher = messageDispatcher;
        mMetrics = metrics;
        mTabSupplier = tabSupplier;
    }

    /** Cancels any scheduled messages. */
    void clear(@MessageClearReason int clearReason) {
        mEnqueueMessageTimer.removeCallbacksAndMessages(null);
        if (mScheduledMessage != null && mScheduledMessage.second != null) {
            mMessageDispatcher.dismissMessage(
                    mScheduledMessage.second, DismissReason.SCOPE_DESTROYED);
        }
        clearScheduledMessage(clearReason);
    }

    /** Adds a message to the underlying {@link MessageDispatcher} queue. */
    void schedule(PropertyModel model, MerchantTrustMessageContext messageContext,
            long delayInMillis, Callback<MerchantTrustMessageContext> messageEnqueuedCallback) {
        setScheduledMessage(
                new Pair<MerchantTrustMessageContext, PropertyModel>(messageContext, model));
        mMetrics.recordMetricsForMessagePrepared();
        mEnqueueMessageTimer.postDelayed(() -> {
            if (messageContext.isValid() && mTabSupplier.hasValue()
                    && messageContext.getWebContents().equals(
                            mTabSupplier.get().getWebContents())) {
                mMessageDispatcher.enqueueMessage(
                        model, messageContext.getWebContents(), MessageScopeType.NAVIGATION, false);
                mMetrics.recordMetricsForMessageShown();
                messageEnqueuedCallback.onResult(messageContext);
                setScheduledMessage(null);
            } else {
                messageEnqueuedCallback.onResult(null);
                if (!messageContext.isValid()) {
                    clearScheduledMessage(MessageClearReason.MESSAGE_CONTEXT_NO_LONGER_VALID);
                } else if (mTabSupplier.hasValue()
                        && !messageContext.getWebContents().equals(
                                mTabSupplier.get().getWebContents())) {
                    clearScheduledMessage(MessageClearReason.SWITCH_TO_DIFFERENT_WEBCONTENTS);
                } else {
                    clearScheduledMessage(MessageClearReason.UNKNOWN);
                }
            }
        }, delayInMillis);
    }

    /** Returns the currently scheduled message. */
    MerchantTrustMessageContext getScheduledMessageContext() {
        return mScheduledMessage == null ? null : mScheduledMessage.first;
    }

    private void clearScheduledMessage(@MessageClearReason int clearReason) {
        if (mScheduledMessage != null) {
            mMetrics.recordMetricsForMessageCleared(clearReason);
        }
        setScheduledMessage(null);
    }

    @VisibleForTesting
    void setHandlerForTesting(Handler handler) {
        mEnqueueMessageTimer = handler;
    }

    @VisibleForTesting
    void setScheduledMessage(Pair<MerchantTrustMessageContext, PropertyModel> pair) {
        synchronized (mEnqueueMessageTimer) {
            mScheduledMessage = pair;
        }
    }
}
