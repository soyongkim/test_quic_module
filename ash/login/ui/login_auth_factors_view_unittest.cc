// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/login/ui/login_auth_factors_view.h"

#include "ash/constants/ash_features.h"
#include "ash/login/login_screen_controller.h"
#include "ash/login/ui/arrow_button_view.h"
#include "ash/login/ui/auth_factor_model.h"
#include "ash/login/ui/auth_icon_view.h"
#include "ash/login/ui/login_test_base.h"
#include "ash/login/ui/login_test_utils.h"
#include "ash/shell.h"
#include "ash/strings/grit/ash_strings.h"
#include "base/callback_helpers.h"
#include "base/feature_list.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/scoped_feature_list.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/compositor/scoped_animation_duration_scale_mode.h"
#include "ui/events/event.h"
#include "ui/views/accessibility/ax_event_manager.h"
#include "ui/views/accessibility/ax_event_observer.h"
#include "ui/views/layout/box_layout.h"

namespace ash {

namespace {

constexpr base::TimeDelta kErrorTimeout = base::Seconds(3);

using AuthFactorState = AuthFactorModel::AuthFactorState;

class FakeAuthFactorModel : public AuthFactorModel {
 public:
  explicit FakeAuthFactorModel(AuthFactorType type) : type_(type) {}

  FakeAuthFactorModel(const FakeAuthFactorModel&) = delete;
  FakeAuthFactorModel& operator=(const FakeAuthFactorModel&) = delete;
  ~FakeAuthFactorModel() override = default;

  // AuthFactorModel:
  AuthFactorState GetAuthFactorState() const override { return state_; }

  // AuthFactorModel:
  AuthFactorType GetType() const override { return type_; }

  // AuthFactorModel:
  int GetLabelId() const override {
    return IDS_SMART_LOCK_LABEL_LOOKING_FOR_PHONE;
  }

  // AuthFactorModel:
  bool ShouldAnnounceLabel() const override { return should_announce_label_; }

  // AuthFactorModel:
  int GetAccessibleNameId() const override {
    return IDS_SMART_LOCK_LABEL_LOOKING_FOR_PHONE;
  }

  // AuthFactorModel:
  void DoHandleTapOrClick() override { do_handle_tap_or_click_called_ = true; }

  // AuthFactorModel:
  void DoHandleErrorTimeout() override { do_handle_error_timeout_num_calls_++; }

  // AuthFactorModel:
  void UpdateIcon(AuthIconView* icon) override {
    ASSERT_TRUE(icon);
    icon_ = icon;
  }

  using AuthFactorModel::has_permanent_error_display_timed_out_;
  using AuthFactorModel::RefreshUI;

  AuthFactorType type_;
  AuthFactorState state_ = AuthFactorState::kReady;
  AuthIconView* icon_ = nullptr;
  bool do_handle_tap_or_click_called_ = false;
  bool should_announce_label_ = false;
  int do_handle_error_timeout_num_calls_ = 0;
};

class ScopedAXEventObserver : public views::AXEventObserver {
 public:
  ScopedAXEventObserver(views::View* view, ax::mojom::Event event_type)
      : view_(view), event_type_(event_type) {
    views::AXEventManager::Get()->AddObserver(this);
  }
  ScopedAXEventObserver(const ScopedAXEventObserver&) = delete;
  ScopedAXEventObserver& operator=(const ScopedAXEventObserver&) = delete;
  ~ScopedAXEventObserver() override {
    views::AXEventManager::Get()->RemoveObserver(this);
  }

  bool event_called = false;

 private:
  // views::AXEventObserver:
  void OnViewEvent(views::View* view, ax::mojom::Event event_type) override {
    if (view == view_ && event_type == event_type_) {
      event_called = true;
    }
  }

  views::View* view_;
  ax::mojom::Event event_type_;
};

}  // namespace

class LoginAuthFactorsViewUnittest : public AshTestBase {
 public:
  LoginAuthFactorsViewUnittest(const LoginAuthFactorsViewUnittest&) = delete;
  LoginAuthFactorsViewUnittest& operator=(const LoginAuthFactorsViewUnittest&) =
      delete;

 protected:
  LoginAuthFactorsViewUnittest()
      : AshTestBase(base::test::TaskEnvironment::TimeSource::MOCK_TIME) {
    feature_list_.InitAndEnableFeature(features::kSmartLockUIRevamp);
  }

  ~LoginAuthFactorsViewUnittest() override = default;

  // LoginTestBase:
  void SetUp() override {
    AshTestBase::SetUp();

    // We proxy |view_| inside of |container_| so we can control layout.
    // TODO(crbug.com/1233614): Add layout tests to check positioning/ordering
    // of icons.
    container_ = std::make_unique<views::View>();
    container_->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical));

    view_ = container_->AddChildView(
        std::make_unique<LoginAuthFactorsView>(base::BindRepeating(
            &LoginAuthFactorsViewUnittest::set_click_to_enter_called,
            base::Unretained(this), true)));
  }

  void TearDown() override {
    container_.reset();
    view_ = nullptr;
    AshTestBase::TearDown();
  }

  void AddAuthFactors(std::vector<AuthFactorType> types) {
    for (AuthFactorType type : types) {
      auto auth_factor = std::make_unique<FakeAuthFactorModel>(type);
      auth_factors_.push_back(auth_factor.get());
      view_->AddAuthFactor(std::move(auth_factor));
    }
  }

  size_t GetVisibleIconCount() {
    LoginAuthFactorsView::TestApi test_api(view_);
    size_t count = 0;
    for (auto* icon : test_api.auth_factor_icon_row()->children()) {
      if (icon->GetVisible()) {
        count++;
      }
    }
    return count;
  }

  void set_click_to_enter_called(bool called) {
    click_to_enter_called_ = called;
  }

  base::test::ScopedFeatureList feature_list_;
  std::unique_ptr<views::View> container_;
  LoginAuthFactorsView* view_ = nullptr;  // Owned by container.
  std::vector<FakeAuthFactorModel*> auth_factors_;
  bool click_to_enter_called_ = false;
};

TEST_F(LoginAuthFactorsViewUnittest, NotVisibleIfNoAuthFactors) {
  AddAuthFactors({AuthFactorType::kFingerprint, AuthFactorType::kSmartLock});
  EXPECT_TRUE(view_->GetVisible());

  LoginAuthFactorsView::TestApi test_api(view_);
  auto& auth_factors = test_api.auth_factors();
  auth_factors.clear();
  test_api.UpdateState();

  EXPECT_FALSE(view_->GetVisible());
}

TEST_F(LoginAuthFactorsViewUnittest, NotVisibleIfAuthFactorsUnavailable) {
  AddAuthFactors({AuthFactorType::kFingerprint, AuthFactorType::kSmartLock});
  EXPECT_TRUE(view_->GetVisible());

  for (auto* factor : auth_factors_) {
    factor->state_ = AuthFactorState::kUnavailable;
  }
  LoginAuthFactorsView::TestApi test_api(view_);
  test_api.UpdateState();

  EXPECT_FALSE(view_->GetVisible());
}

TEST_F(LoginAuthFactorsViewUnittest, TapOrClickCalled) {
  AddAuthFactors({AuthFactorType::kFingerprint, AuthFactorType::kSmartLock});
  auto* factor = auth_factors_[0];

  // RefreshUI() calls UpdateIcon(), which captures a pointer to the
  // icon.
  factor->RefreshUI();

  EXPECT_FALSE(factor->do_handle_tap_or_click_called_);
  const gfx::Point point(0, 0);
  factor->icon_->OnMousePressed(ui::MouseEvent(
      ui::ET_MOUSE_PRESSED, point, point, base::TimeTicks::Now(), 0, 0));
  EXPECT_TRUE(factor->do_handle_tap_or_click_called_);
}

TEST_F(LoginAuthFactorsViewUnittest, ShouldAnnounceLabel) {
  AddAuthFactors({AuthFactorType::kFingerprint, AuthFactorType::kSmartLock});
  auto* factor = auth_factors_[0];
  LoginAuthFactorsView::TestApi test_api(view_);
  views::Label* label = test_api.label();
  ScopedAXEventObserver alert_observer(label, ax::mojom::Event::kAlert);
  for (auto* factor : auth_factors_) {
    factor->state_ = AuthFactorState::kAvailable;
  }

  ASSERT_FALSE(factor->ShouldAnnounceLabel());
  ASSERT_FALSE(alert_observer.event_called);

  test_api.UpdateState();
  ASSERT_FALSE(alert_observer.event_called);

  factor->should_announce_label_ = true;
  test_api.UpdateState();
  EXPECT_TRUE(alert_observer.event_called);
}

TEST_F(LoginAuthFactorsViewUnittest, SingleIconInAvailableState) {
  // For test purposes only. No two auth factors should have the same type
  // ordinarily.
  AddAuthFactors({AuthFactorType::kSmartLock, AuthFactorType::kFingerprint,
                  AuthFactorType::kSmartLock});
  LoginAuthFactorsView::TestApi test_api(view_);
  auth_factors_[0]->state_ = AuthFactorState::kAvailable;
  auth_factors_[1]->state_ = AuthFactorState::kAvailable;
  auth_factors_[2]->state_ = AuthFactorState::kUnavailable;
  test_api.UpdateState();

  EXPECT_TRUE(test_api.auth_factor_icon_row()->GetVisible());
  EXPECT_FALSE(test_api.checkmark_icon()->GetVisible());
  EXPECT_FALSE(test_api.arrow_button()->GetVisible());
  EXPECT_FALSE(test_api.arrow_nudge_animation()->GetVisible());

  // The number of icons should match the number of auth factors initialized.
  EXPECT_EQ(auth_factors_.size(),
            test_api.auth_factor_icon_row()->children().size());

  // Only a single icon should be visible in the Available state.
  EXPECT_EQ(1u, GetVisibleIconCount());
}

TEST_F(LoginAuthFactorsViewUnittest, MultipleAuthFactorsInReadyState) {
  // For test purposes only. No two auth factors should have the same type
  // ordinarily.
  AddAuthFactors({AuthFactorType::kSmartLock, AuthFactorType::kFingerprint,
                  AuthFactorType::kSmartLock});
  LoginAuthFactorsView::TestApi test_api(view_);
  auth_factors_[0]->state_ = AuthFactorState::kAvailable;
  auth_factors_[1]->state_ = AuthFactorState::kReady;
  auth_factors_[2]->state_ = AuthFactorState::kReady;
  test_api.UpdateState();

  EXPECT_TRUE(test_api.auth_factor_icon_row()->GetVisible());
  EXPECT_FALSE(test_api.checkmark_icon()->GetVisible());
  EXPECT_FALSE(test_api.arrow_button()->GetVisible());
  EXPECT_FALSE(test_api.arrow_nudge_animation()->GetVisible());

  // The number of icons should match the number of auth factors initialized.
  EXPECT_EQ(auth_factors_.size(),
            test_api.auth_factor_icon_row()->children().size());

  // Only the two ready auth factors should be visible.
  EXPECT_EQ(2u, GetVisibleIconCount());

  // Check that the combined label for Smart Lock and Fingerprint is chosen.
  EXPECT_EQ(
      l10n_util::GetStringUTF16(IDS_AUTH_FACTOR_LABEL_UNLOCK_METHOD_SELECTION),
      test_api.label()->GetText());
}

TEST_F(LoginAuthFactorsViewUnittest, ClickRequired) {
  ui::ScopedAnimationDurationScaleMode non_zero_duration_mode(
      ui::ScopedAnimationDurationScaleMode::NORMAL_DURATION);

  AddAuthFactors({AuthFactorType::kFingerprint, AuthFactorType::kSmartLock});
  LoginAuthFactorsView::TestApi test_api(view_);
  auth_factors_[0]->state_ = AuthFactorState::kReady;
  auth_factors_[1]->state_ = AuthFactorState::kClickRequired;
  test_api.UpdateState();

  // Check that the arrow button and arrow nudge animation is shown and that the
  // label has been updated.
  EXPECT_TRUE(test_api.arrow_button()->GetVisible());
  EXPECT_TRUE(test_api.arrow_nudge_animation()->GetVisible());
  EXPECT_FALSE(test_api.checkmark_icon()->GetVisible());
  EXPECT_FALSE(test_api.auth_factor_icon_row()->GetVisible());
  EXPECT_EQ(l10n_util::GetStringUTF16(IDS_AUTH_FACTOR_LABEL_CLICK_TO_ENTER),
            test_api.label()->GetText());
}

TEST_F(LoginAuthFactorsViewUnittest, ClickingArrowButton) {
  ui::ScopedAnimationDurationScaleMode non_zero_duration_mode(
      ui::ScopedAnimationDurationScaleMode::NORMAL_DURATION);

  AddAuthFactors({AuthFactorType::kFingerprint, AuthFactorType::kSmartLock});
  LoginAuthFactorsView::TestApi test_api(view_);
  auth_factors_[0]->state_ = AuthFactorState::kReady;
  auth_factors_[1]->state_ = AuthFactorState::kClickRequired;
  test_api.UpdateState();

  // Check that the arrow button and arrow nudge animation is shown.
  EXPECT_TRUE(test_api.arrow_button()->GetVisible());
  EXPECT_TRUE(test_api.arrow_nudge_animation()->GetVisible());

  // Simulate clicking arrow nudge animation, which sits on top of arrow button
  // and should relay arrow button click.
  const gfx::Point point(0, 0);
  test_api.arrow_nudge_animation()->OnMousePressed(ui::MouseEvent(
      ui::ET_MOUSE_PRESSED, point, point, base::TimeTicks::Now(), 0, 0));

  // Check that arrow button is still visible and that arrow nudge animation is
  // no longer shown.
  EXPECT_TRUE(test_api.arrow_button()->GetVisible());
  EXPECT_FALSE(test_api.arrow_nudge_animation()->GetVisible());
}

TEST_F(LoginAuthFactorsViewUnittest, Authenticated_LockScreen) {
  GetSessionControllerClient()->SetSessionState(
      session_manager::SessionState::LOCKED);
  Shell::Get()->login_screen_controller()->ShowLockScreen();
  AddAuthFactors({AuthFactorType::kFingerprint, AuthFactorType::kSmartLock});
  LoginAuthFactorsView::TestApi test_api(view_);
  auth_factors_[0]->state_ = AuthFactorState::kAuthenticated;
  auth_factors_[1]->state_ = AuthFactorState::kClickRequired;
  test_api.UpdateState();

  // Check that only the arrow button is shown and that the label has been
  // updated.
  EXPECT_TRUE(test_api.checkmark_icon()->GetVisible());
  EXPECT_FALSE(test_api.arrow_button()->GetVisible());
  EXPECT_FALSE(test_api.arrow_nudge_animation()->GetVisible());
  EXPECT_FALSE(test_api.auth_factor_icon_row()->GetVisible());
  EXPECT_EQ(l10n_util::GetStringUTF16(IDS_AUTH_FACTOR_LABEL_UNLOCKED),
            test_api.label()->GetText());
}

TEST_F(LoginAuthFactorsViewUnittest, Authenticated_LoginScreen) {
  GetSessionControllerClient()->SetSessionState(
      session_manager::SessionState::LOGIN_PRIMARY);
  Shell::Get()->login_screen_controller()->ShowLoginScreen();
  AddAuthFactors({AuthFactorType::kFingerprint, AuthFactorType::kSmartLock});
  LoginAuthFactorsView::TestApi test_api(view_);
  auth_factors_[0]->state_ = AuthFactorState::kAuthenticated;
  auth_factors_[1]->state_ = AuthFactorState::kClickRequired;
  test_api.UpdateState();

  // Check that only the arrow button is shown and that the label has been
  // updated.
  EXPECT_TRUE(test_api.checkmark_icon()->GetVisible());
  EXPECT_FALSE(test_api.arrow_button()->GetVisible());
  EXPECT_FALSE(test_api.arrow_nudge_animation()->GetVisible());
  EXPECT_FALSE(test_api.auth_factor_icon_row()->GetVisible());
  EXPECT_EQ(l10n_util::GetStringUTF16(IDS_AUTH_FACTOR_LABEL_SIGNED_IN),
            test_api.label()->GetText());
}

TEST_F(LoginAuthFactorsViewUnittest, ErrorTemporary) {
  AddAuthFactors({AuthFactorType::kFingerprint, AuthFactorType::kSmartLock});
  LoginAuthFactorsView::TestApi test_api(view_);
  auth_factors_[0]->state_ = AuthFactorState::kErrorTemporary;
  auth_factors_[1]->state_ = AuthFactorState::kReady;
  test_api.UpdateState();

  EXPECT_TRUE(test_api.auth_factor_icon_row()->GetVisible());
  EXPECT_FALSE(test_api.checkmark_icon()->GetVisible());
  EXPECT_FALSE(test_api.arrow_button()->GetVisible());
  EXPECT_FALSE(test_api.arrow_nudge_animation()->GetVisible());

  // Only the error should be visible for the first three seconds after the
  // state update.
  EXPECT_EQ(1u, GetVisibleIconCount());

  ASSERT_EQ(0, auth_factors_[0]->do_handle_error_timeout_num_calls_);
  task_environment()->FastForwardBy(kErrorTimeout);
  EXPECT_EQ(1, auth_factors_[0]->do_handle_error_timeout_num_calls_);
}

TEST_F(LoginAuthFactorsViewUnittest, ErrorPermanent) {
  AddAuthFactors({AuthFactorType::kFingerprint, AuthFactorType::kSmartLock});
  LoginAuthFactorsView::TestApi test_api(view_);
  auth_factors_[0]->state_ = AuthFactorState::kErrorPermanent;
  auth_factors_[1]->state_ = AuthFactorState::kReady;
  test_api.UpdateState();
  auto* factor = auth_factors_[0];

  EXPECT_TRUE(test_api.auth_factor_icon_row()->GetVisible());
  EXPECT_FALSE(test_api.checkmark_icon()->GetVisible());
  EXPECT_FALSE(test_api.arrow_button()->GetVisible());
  EXPECT_FALSE(test_api.arrow_nudge_animation()->GetVisible());

  // Only the error should be visible for the first three seconds after the
  // state update.
  EXPECT_EQ(1u, GetVisibleIconCount());

  // Fast-forward four seconds. Ensure that the OnErrorTimeout() callback gets
  // called, and |has_permanent_error_display_timed_out_| correctly reflects
  // whether the most recent timeout has expired.
  ASSERT_EQ(0, factor->do_handle_error_timeout_num_calls_);
  EXPECT_FALSE(factor->has_permanent_error_display_timed_out_);
  task_environment()->FastForwardBy(base::Seconds(4));
  EXPECT_EQ(1, factor->do_handle_error_timeout_num_calls_);
  EXPECT_TRUE(factor->has_permanent_error_display_timed_out_);

  // After timeout, permanent errors are shown alongside ready auth factors.
  test_api.UpdateState();
  EXPECT_EQ(2u, GetVisibleIconCount());

  // Simulate a click event.
  EXPECT_FALSE(factor->do_handle_tap_or_click_called_);
  factor->RefreshUI();
  const gfx::Point point(0, 0);
  factor->icon_->OnMousePressed(ui::MouseEvent(
      ui::ET_MOUSE_PRESSED, point, point, base::TimeTicks::Now(), 0, 0));
  EXPECT_TRUE(factor->do_handle_tap_or_click_called_);

  // Clicking causes only the error to be visible.
  test_api.UpdateState();
  EXPECT_EQ(1u, GetVisibleIconCount());

  // Fast-forward four seconds. Ensure that the OnErrorTimeout() callback gets
  // called, and |has_permanent_error_display_timed_out_| correctly reflects
  // whether the most recent timeout has expired.
  ASSERT_EQ(1, factor->do_handle_error_timeout_num_calls_);
  EXPECT_FALSE(factor->has_permanent_error_display_timed_out_);
  task_environment()->FastForwardBy(base::Seconds(4));
  EXPECT_EQ(2, factor->do_handle_error_timeout_num_calls_);
  EXPECT_TRUE(factor->has_permanent_error_display_timed_out_);

  // After timeout, permanent errors are shown alongside ready auth factors.
  test_api.UpdateState();
  EXPECT_EQ(2u, GetVisibleIconCount());
}

TEST_F(LoginAuthFactorsViewUnittest, CanUsePin) {
  AddAuthFactors({AuthFactorType::kSmartLock, AuthFactorType::kFingerprint});

  for (bool can_use_pin : {true, false}) {
    view_->SetCanUsePin(can_use_pin);
    EXPECT_EQ(can_use_pin, AuthFactorModel::can_use_pin());
    EXPECT_EQ(can_use_pin, auth_factors_[0]->can_use_pin());
    EXPECT_EQ(can_use_pin, auth_factors_[1]->can_use_pin());
  }
}

}  // namespace ash