// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_CORE_BROWSER_UI_SUGGESTION_H_
#define COMPONENTS_AUTOFILL_CORE_BROWSER_UI_SUGGESTION_H_

#include <string>

#include "base/strings/string_piece.h"
#include "base/types/strong_alias.h"
#include "build/build_config.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "ui/gfx/image/image.h"
#include "url/gurl.h"
namespace autofill {

struct Suggestion {
  using IsLoading = base::StrongAlias<class IsLoadingTag, bool>;

  enum MatchMode {
    PREFIX_MATCH,    // for prefix matched suggestions;
    SUBSTRING_MATCH  // for substring matched suggestions;
  };

  Suggestion();
  Suggestion(const Suggestion& other);
  Suggestion(Suggestion&& other);

  explicit Suggestion(std::u16string value);
  // Constructor for unit tests. It will convert the strings from UTF-8 to
  // UTF-16.
  Suggestion(base::StringPiece value,
             base::StringPiece label,
             std::string icon,
             int frontend_id);

  Suggestion& operator=(const Suggestion& other);
  Suggestion& operator=(Suggestion&& other);

  ~Suggestion();

  // GUID generated by the backend layer. This identifies the exact autofill
  // profile that generated this suggestion.
  std::string backend_id;

  // ID for the frontend to use in identifying the particular result. Positive
  // values are sent over IPC to identify the item selected. Negative values
  // (see popup_item_ids.h) have special built-in meanings.
  int frontend_id = 0;

  // The text that will be filled in to the focused field and is displayed as
  // the main text in the suggestion. Its style depends on |is_value_secondary|.
  std::u16string value;

  // The text displayed on the second line in a suggestion.
  std::u16string label;
  // A label to be shown beneath |label| that will display information about any
  // credit card offers or rewards.
  std::u16string offer_label;
  // Used only for passwords to show the password value.
  // Also used to display an extra line of information if two line
  // display is enabled.
  std::u16string additional_label;
  // Contains an image to display for the suggestion.
  gfx::Image custom_icon;

#if defined(OS_ANDROID)
  // The url for the custom icon. This is used by android to fetch the image as
  // android does not support gfx::Image directly.
  GURL custom_icon_url;
#endif  // OS_ANDROID

  // TODO(crbug.com/1019660): Identify icons with enum instead of strings.
  // If |custom_icon| is empty, the name of the fallback built-in icon.
  std::string icon;
  // For passwords, this icon string shows whether the suggestion originates
  // from local or account store. It is also used on the settings entry for
  // the credit card Autofill popup to indicate if all credit cards are server
  // cards.
  std::string store_indicator_icon;
  MatchMode match = PREFIX_MATCH;
  // Whether |value| should be displayed as secondary text.
  bool is_value_secondary = false;
  // Whether suggestion was interacted with and is now in a loading state.
  IsLoading is_loading = IsLoading(false);
  // The In-Product-Help feature that should be shown for the suggestion.
  std::string feature_for_iph;

  // If specified, this text will be played back as voice over for a11y.
  absl::optional<std::u16string> voice_over;
};

}  // namespace autofill

#endif  // COMPONENTS_AUTOFILL_CORE_BROWSER_UI_SUGGESTION_H_