// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SIGNIN_INTERNAL_IDENTITY_MANAGER_PRIMARY_ACCOUNT_POLICY_MANAGER_H_
#define COMPONENTS_SIGNIN_INTERNAL_IDENTITY_MANAGER_PRIMARY_ACCOUNT_POLICY_MANAGER_H_

class PrefService;
class PrimaryAccountManager;

class PrimaryAccountPolicyManager {
 public:
  PrimaryAccountPolicyManager() = default;

  PrimaryAccountPolicyManager(const PrimaryAccountPolicyManager&) = delete;
  PrimaryAccountPolicyManager& operator=(const PrimaryAccountPolicyManager&) =
      delete;

  virtual ~PrimaryAccountPolicyManager() = default;

  // On platforms where PrimaryAccountManager is responsible for dealing with
  // invalid username policy updates, we need to check this during
  // initialization and sign the user out.
  virtual void InitializePolicy(
      PrefService* local_state,
      PrimaryAccountManager* primary_account_manager) = 0;
};

#endif  // COMPONENTS_SIGNIN_INTERNAL_IDENTITY_MANAGER_PRIMARY_ACCOUNT_POLICY_MANAGER_H_
