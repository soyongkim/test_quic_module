// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_RENDERER_NET_NET_ERROR_HELPER_CORE_H_
#define CHROME_RENDERER_NET_NET_ERROR_HELPER_CORE_H_

#include <memory>
#include <string>

#include "base/callback.h"
#include "base/memory/weak_ptr.h"
#include "build/build_config.h"
#include "components/error_page/common/error.h"
#include "components/error_page/common/localized_error.h"
#include "components/error_page/common/net_error_info.h"
#include "net/base/net_errors.h"
#include "url/gurl.h"

#if defined(OS_ANDROID)
#include "chrome/renderer/net/available_offline_content_helper.h"
#include "chrome/renderer/net/page_auto_fetcher_helper_android.h"
#endif

namespace content {
class RenderFrame;
}

// Class that contains the logic for how the NetErrorHelper.  This allows for
// testing the logic without a RenderView or WebFrame, which are difficult to
// mock, and for testing races which are impossible to reliably reproduce
// with real RenderViews or WebFrames.
class NetErrorHelperCore {
 public:
  enum FrameType {
    MAIN_FRAME,
    SUB_FRAME,
  };

  enum PageType {
    NON_ERROR_PAGE,
    ERROR_PAGE,
  };

  enum Button {
    NO_BUTTON,
    RELOAD_BUTTON,
    MORE_BUTTON,
    EASTER_EGG,
    DIAGNOSE_ERROR,
    DOWNLOAD_BUTTON,  // "Download page later" experiment.
  };

  // The Delegate handles all interaction with the RenderView, WebFrame, and
  // the network, as well as the generation of error pages.
  class Delegate {
   public:
    // Generates an error page's HTML for the given error.
    virtual error_page::LocalizedError::PageState GenerateLocalizedErrorPage(
        const error_page::Error& error,
        bool is_failed_post,
        bool can_show_network_diagnostics_dialog,
        std::string* html) const = 0;

    // Create extra Javascript bindings in the error page. Will only be invoked
    // after an error page has finished loading.
    virtual void EnablePageHelperFunctions() = 0;

    // Updates the currently displayed error page with a new error code.  The
    // currently displayed error page must have finished loading, and must have
    // been generated by a call to GenerateLocalizedErrorPage.
    virtual error_page::LocalizedError::PageState UpdateErrorPage(
        const error_page::Error& error,
        bool is_failed_post,
        bool can_show_network_diagnostics_dialog) = 0;

    // Tell the currently displayed error page about the user's current easter
    // egg game high score (from the user's synced preferences).  The currently
    // displayed error page must have finished loading, and must have been
    // generated by a call to GenerateLocalizedErrorPage.
    virtual void InitializeErrorPageEasterEggHighScore(int high_score) = 0;

    // Request the current easter egg high score stored in the user's synced
    // preferences from the browser.  The delegate should call
    // OnEasterEggHighScoreReceived() with the response.
    virtual void RequestEasterEggHighScore() = 0;

    // Starts a reload of the observed frame.
    virtual void ReloadFrame() = 0;

    // Run the platform diagnostics too for the specified URL.
    virtual void DiagnoseError(const GURL& page_url) = 0;

    // Schedule to download the page at a later time.
    virtual void DownloadPageLater() = 0;

    // Inform that download button is being shown in the error page.
    virtual void SetIsShowingDownloadButton(bool show) = 0;

    // Signals that offline content is available.
    virtual void OfflineContentAvailable(
        bool list_visible_by_prefs,
        const std::string& offline_content_json) = 0;

    // Returns the render frame associated with NetErrorHelper.
    virtual content::RenderFrame* GetRenderFrame() = 0;

#if defined(OS_ANDROID)
    // Called after an attempt to automatically schedule a background fetch for
    // a page with a network error.
    virtual void SetAutoFetchState(
        chrome::mojom::OfflinePageAutoFetcherScheduleResult result) = 0;
#endif

   protected:
    virtual ~Delegate() {}
  };

  explicit NetErrorHelperCore(Delegate* delegate);
  ~NetErrorHelperCore();

  // Sets values in |pending_error_page_info_|. If |error_html| is not null, it
  // initializes |error_html| with the HTML of an error page in response to
  // |error|.  Updates internals state with the assumption the page will be
  // loaded immediately.
  void PrepareErrorPage(FrameType frame_type,
                        const error_page::Error& error,
                        bool is_failed_post,
                        std::string* error_html);

  // These methods handle tracking the actual state of the page.
  void OnCommitLoad(FrameType frame_type, const GURL& url);
  void OnFinishLoad(FrameType frame_type);

  void CancelPendingAutoReload();

  // Notifies |this| that network error information from the browser process
  // has been received.
  void OnNetErrorInfo(error_page::DnsProbeStatus status);

  // Notifies |this| if it can use a local error diagnostics service through its
  // delegate.
  void OnSetCanShowNetworkDiagnosticsDialog(
      bool can_show_network_diagnostics_dialog);

  // Notifies |this| about the current high score that's saved in the user's
  // synced preferences.
  void OnEasterEggHighScoreReceived(int high_score);

#if defined(OS_ANDROID)
  void SetPageAutoFetcherHelperForTesting(
      std::unique_ptr<PageAutoFetcherHelper> page_auto_fetcher_helper);
#endif

  // Execute the effect of pressing the specified button.
  // Note that the visual effects of the 'MORE' button are taken
  // care of in JavaScript.
  void ExecuteButtonPress(Button button);

  // Opens a suggested offline item.
  void LaunchOfflineItem(const std::string& id, const std::string& name_space);

  // Shows all available offline content.
  void LaunchDownloadsPage();

  void CancelSavePage();
  void SavePageForLater();

  // Signals the user changed the visibility of the offline content list in the
  // dino page.
  void ListVisibilityChanged(bool is_visible);

 private:
  struct ErrorPageInfo;

  // Sets values in |pending_error_page_info| for a main frame error page. If
  // |error_html| is not null, it also fetches the string containing the error
  // page HTML, and sets error_html to it. Depending on
  // |pending_error_page_info|, may show a DNS probe error page.  May modify
  // |pending_error_page_info|.
  void PrepareErrorPageForMainFrame(ErrorPageInfo* pending_error_page_info,
                                    std::string* error_html);

  // Updates the currently displayed error page with a new error based on the
  // most recently received DNS probe result.  The page must have finished
  // loading before this is called.
  void UpdateErrorPage();

  // Called after the error page is loaded and is showing the final error code.
  // This is either called on page load, or after a DNS probe finishes.
  void ErrorPageLoadedWithFinalErrorCode();

  error_page::Error GetUpdatedError(const ErrorPageInfo& error_info) const;

  void Reload();

  Delegate* const delegate_;

  // The last DnsProbeStatus received from the browser.
  error_page::DnsProbeStatus last_probe_status_;

  // Information for the provisional / "pre-provisional" error page.  NULL when
  // there's no page pending, or the pending page is not an error page.
  std::unique_ptr<ErrorPageInfo> pending_error_page_info_;

  // Information for the committed error page.  NULL when the committed page is
  // not an error page.
  std::unique_ptr<ErrorPageInfo> committed_error_page_info_;

  bool can_show_network_diagnostics_dialog_;

  // This value is set only when a navigation has been initiated from
  // the error page.  It is used to detect when such navigations result
  // in errors.
  Button navigation_from_button_;

#if defined(OS_ANDROID)
  AvailableOfflineContentHelper available_content_helper_;
  std::unique_ptr<PageAutoFetcherHelper> page_auto_fetcher_helper_;
#endif
};

#endif  // CHROME_RENDERER_NET_NET_ERROR_HELPER_CORE_H_