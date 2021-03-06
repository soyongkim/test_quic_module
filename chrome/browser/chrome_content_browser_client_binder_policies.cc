// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chrome_content_browser_client_binder_policies.h"

#include "chrome/browser/ui/webui/bluetooth_internals/bluetooth_internals.mojom.h"
#include "components/autofill/content/common/mojom/autofill_driver.mojom.h"
#include "components/page_load_metrics/common/page_load_metrics.mojom.h"
#include "components/subresource_filter/core/mojom/subresource_filter.mojom.h"
#include "extensions/buildflags/buildflags.h"
#include "third_party/blink/public/common/features.h"

#if BUILDFLAG(ENABLE_EXTENSIONS)
#include "extensions/common/mojom/frame.mojom.h"
#endif

namespace {

// Registers policies for interfaces registered in
// `RegisterBrowserInterfaceBindersForFrame()`.
void RegisterPoliciesForNonAssociatedInterfaces(
    content::MojoBinderPolicyMap& policy_map) {
  // Prerendering does not happen for WebUI pages, so set kUnexpected as the
  // policy for interfaces registered by WebUI.
  policy_map.SetNonAssociatedPolicy<::mojom::BluetoothInternalsHandler>(
      content::MojoBinderNonAssociatedPolicy::kUnexpected);
}

// Registers policies for channel-associated interfaces registered in
// `BindAssociatedReceiverFromFrame()`.
void RegisterPoliciesForChannelAssociatedInterfaces(
    content::MojoBinderPolicyMap& policy_map) {
  policy_map.SetAssociatedPolicy<page_load_metrics::mojom::PageLoadMetrics>(
      content::MojoBinderAssociatedPolicy::kGrant);
  policy_map
      .SetAssociatedPolicy<subresource_filter::mojom::SubresourceFilterHost>(
          content::MojoBinderAssociatedPolicy::kGrant);
#if BUILDFLAG(ENABLE_EXTENSIONS)
  // TODO(https://crbug.com/1278141): Revisit this decision after Prerender2
  // support desktops.
  // TODO(https://crbug.com/1259007): Introduce kExperimentalGrant and
  // replace kGrant with kExperimentalGrant.
  if (blink::features::IsPrerender2Enabled()) {
    policy_map.SetAssociatedPolicy<extensions::mojom::LocalFrameHost>(
        content::MojoBinderAssociatedPolicy::kGrant);
  }
#endif
}

}  // namespace

void RegisterChromeMojoBinderPoliciesForSameOriginPrerendering(
    content::MojoBinderPolicyMap& policy_map) {
  RegisterPoliciesForNonAssociatedInterfaces(policy_map);
  RegisterPoliciesForChannelAssociatedInterfaces(policy_map);
}
