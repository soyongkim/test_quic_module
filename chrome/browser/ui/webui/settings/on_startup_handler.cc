// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/settings/on_startup_handler.h"

#include <string>

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/check_op.h"
#include "chrome/browser/extensions/settings_api_helpers.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/settings/settings_utils.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/web_ui.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/management_policy.h"
#include "extensions/common/extension.h"

namespace settings {

// static
const char OnStartupHandler::kOnStartupNtpExtensionEventName[] =
    "update-ntp-extension";

OnStartupHandler::OnStartupHandler(Profile* profile) : profile_(profile) {
  DCHECK(profile);
}
OnStartupHandler::~OnStartupHandler() {}

void OnStartupHandler::OnJavascriptAllowed() {
  extension_registry_observation_.Observe(
      extensions::ExtensionRegistry::Get(profile_));
}

void OnStartupHandler::OnJavascriptDisallowed() {
  extension_registry_observation_.Reset();
}

void OnStartupHandler::RegisterMessages() {
  web_ui()->RegisterDeprecatedMessageCallback(
      "getNtpExtension",
      base::BindRepeating(&OnStartupHandler::HandleGetNtpExtension,
                          base::Unretained(this)));
  web_ui()->RegisterDeprecatedMessageCallback(
      "validateStartupPage",
      base::BindRepeating(&OnStartupHandler::HandleValidateStartupPage,
                          base::Unretained(this)));
}

void OnStartupHandler::OnExtensionUnloaded(
    content::BrowserContext* browser_context,
    const extensions::Extension* extension,
    extensions::UnloadedExtensionReason reason) {
  FireWebUIListener(kOnStartupNtpExtensionEventName, *GetNtpExtension());
}

void OnStartupHandler::OnExtensionReady(
    content::BrowserContext* browser_context,
    const extensions::Extension* extension) {
  FireWebUIListener(kOnStartupNtpExtensionEventName, *GetNtpExtension());
}

std::unique_ptr<base::Value> OnStartupHandler::GetNtpExtension() {
  const extensions::Extension* ntp_extension =
      extensions::GetExtensionOverridingNewTabPage(profile_);
  if (!ntp_extension) {
    std::unique_ptr<base::Value> none(new base::Value);
    return none;
  }

  std::unique_ptr<base::DictionaryValue> dict(new base::DictionaryValue);
  dict->SetString("id", ntp_extension->id());
  dict->SetString("name", ntp_extension->name());
  dict->SetBoolean("canBeDisabled",
                   !extensions::ExtensionSystem::Get(profile_)
                        ->management_policy()
                        ->MustRemainEnabled(ntp_extension, nullptr));
  return dict;
}

void OnStartupHandler::HandleGetNtpExtension(const base::ListValue* args) {
  const base::Value& callback_id = args->GetList()[0];
  AllowJavascript();

  ResolveJavascriptCallback(callback_id, *GetNtpExtension());
}

void OnStartupHandler::HandleValidateStartupPage(const base::ListValue* args) {
  CHECK_EQ(args->GetList().size(), 2U);
  const base::Value& callback_id = args->GetList()[0];
  const std::string& url_string = args->GetList()[1].GetString();
  AllowJavascript();

  bool valid = settings_utils::FixupAndValidateStartupPage(url_string, nullptr);
  ResolveJavascriptCallback(callback_id, base::Value(valid));
}

}  // namespace settings