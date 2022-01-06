// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/app_list/views/app_list_bubble_view.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "ash/app_list/app_list_model_provider.h"
#include "ash/app_list/app_list_util.h"
#include "ash/app_list/model/app_list_folder_item.h"
#include "ash/app_list/views/app_list_a11y_announcer.h"
#include "ash/app_list/views/app_list_bubble_apps_page.h"
#include "ash/app_list/views/app_list_bubble_search_page.h"
#include "ash/app_list/views/app_list_folder_view.h"
#include "ash/app_list/views/apps_grid_view.h"
#include "ash/app_list/views/assistant/app_list_bubble_assistant_page.h"
#include "ash/app_list/views/folder_background_view.h"
#include "ash/app_list/views/productivity_launcher_search_view.h"
#include "ash/app_list/views/scrollable_apps_grid_view.h"
#include "ash/app_list/views/search_box_view.h"
#include "ash/app_list/views/search_result_page_dialog_controller.h"
#include "ash/bubble/bubble_constants.h"
#include "ash/keyboard/ui/keyboard_ui_controller.h"
#include "ash/public/cpp/app_list/app_list_config_provider.h"
#include "ash/public/cpp/metrics_util.h"
#include "ash/public/cpp/shelf_config.h"
#include "ash/public/cpp/shelf_types.h"
#include "ash/public/cpp/view_shadow.h"
#include "ash/search_box/search_box_constants.h"
#include "ash/style/ash_color_provider.h"
#include "ash/style/highlight_border.h"
#include "base/bind.h"
#include "base/check.h"
#include "base/check_op.h"
#include "base/cxx17_backports.h"
#include "base/i18n/rtl.h"
#include "base/metrics/histogram_functions.h"
#include "base/time/time.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/ui_base_types.h"
#include "ui/compositor/animation_throughput_reporter.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_type.h"
#include "ui/events/types/event_type.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/views/animation/animation_builder.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"

using views::BoxLayout;

namespace ash {
namespace {

// Folder view inset from the edge of the bubble.
constexpr int kFolderViewInset = 16;

// Elevation for the bubble's shadow.
constexpr int kShadowElevation = 3;

AppListConfig* GetAppListConfig() {
  return AppListConfigProvider::Get().GetConfigForType(
      AppListConfigType::kDense, /*can_create=*/true);
}

// A simplified horizontal separator that uses a solid color layer for painting.
// This is more efficient than using a views::Separator, which would require
// SetPaintToLayer(ui::LAYER_TEXTURED).
class SeparatorWithLayer : public views::View {
 public:
  SeparatorWithLayer() {
    SetPaintToLayer(ui::LAYER_SOLID_COLOR);
    layer()->SetColor(ColorProvider::Get()->GetContentLayerColor(
        ColorProvider::ContentLayerType::kSeparatorColor));
    layer()->SetFillsBoundsOpaquely(false);
  }
  SeparatorWithLayer(const SeparatorWithLayer&) = delete;
  SeparatorWithLayer& operator=(const SeparatorWithLayer&) = delete;
  ~SeparatorWithLayer() override = default;

  // views::View:
  gfx::Size CalculatePreferredSize() const override {
    // The parent's layout manager will stretch it horizontally.
    return gfx::Size(1, 1);
  }
};

// Returns the layer bounds to use for the start of the show animation and the
// end of the hide animation.
gfx::Rect GetShowHideAnimationBounds(gfx::Rect target_bounds) {
  const int delta_height = target_bounds.height() / 4;  // 25% of height
  const int y_offset = 8;
  return gfx::Rect(
      target_bounds.x(), target_bounds.y() + delta_height + y_offset,
      target_bounds.width(), target_bounds.height() - delta_height);
}

}  // namespace

AppListBubbleView::AppListBubbleView(
    AppListViewDelegate* view_delegate,
    ApplicationDragAndDropHost* drag_and_drop_host)
    : view_delegate_(view_delegate) {
  DCHECK(view_delegate);
  DCHECK(drag_and_drop_host);

  // Set up rounded corners and background blur, similar to TrayBubbleView.
  // Layer background is set in OnThemeChanged().
  SetPaintToLayer();
  layer()->SetRoundedCornerRadius(gfx::RoundedCornersF{kBubbleCornerRadius});
  layer()->SetFillsBoundsOpaquely(false);
  layer()->SetIsFastRoundedCorner(true);
  layer()->SetBackgroundBlur(ColorProvider::kBackgroundBlurSigma);
  layer()->SetBackdropFilterQuality(ColorProvider::kBackgroundBlurQuality);

  views::FillLayout* layout =
      SetLayoutManager(std::make_unique<views::FillLayout>());
  a11y_announcer_ = std::make_unique<AppListA11yAnnouncer>(
      AddChildView(std::make_unique<views::View>()));
  InitContentsView(drag_and_drop_host);

  // Add assistant page as a top-level child so it will fill the bubble and
  // suggestion chips will appear at the bottom of the bubble view.
  assistant_page_ = AddChildView(std::make_unique<AppListBubbleAssistantPage>(
      view_delegate_->GetAssistantViewDelegate()));
  assistant_page_->SetVisible(false);

  InitFolderView(drag_and_drop_host);
  // Folder view is laid out manually based on its contents.
  layout->SetChildViewIgnoredByLayout(folder_view_, true);

  AddAccelerator(ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE));
  AddAccelerator(ui::Accelerator(ui::VKEY_BROWSER_BACK, ui::EF_NONE));
}

AppListBubbleView::~AppListBubbleView() {
  // `a11y_announcer_` depends on a child view, so shut it down before view
  // hierarchy is destroyed.
  a11y_announcer_->Shutdown();

  // AppListFolderView may reference/observe an item on the root apps grid view
  // (associated with the folder), so destroy it before the root apps grid view.
  delete folder_view_;
  folder_view_ = nullptr;
}

void AppListBubbleView::SetDragAndDropHostOfCurrentAppList(
    ApplicationDragAndDropHost* drag_and_drop_host) {
  DCHECK(drag_and_drop_host);
  apps_page_->scrollable_apps_grid_view()->SetDragAndDropHostOfCurrentAppList(
      drag_and_drop_host);
  folder_view_->items_grid_view()->SetDragAndDropHostOfCurrentAppList(
      drag_and_drop_host);
}

void AppListBubbleView::InitContentsView(
    ApplicationDragAndDropHost* drag_and_drop_host) {
  auto* contents = AddChildView(std::make_unique<views::View>());

  auto* layout = contents->SetLayoutManager(
      std::make_unique<BoxLayout>(BoxLayout::Orientation::kVertical));
  layout->set_cross_axis_alignment(BoxLayout::CrossAxisAlignment::kStretch);

  search_box_view_ = contents->AddChildView(std::make_unique<SearchBoxView>(
      /*delegate=*/this, view_delegate_, /*app_list_view=*/nullptr));
  SearchBoxViewBase::InitParams params;
  // Show the assistant button until the user types text.
  params.show_close_button_when_active = false;
  params.create_background = false;
  params.animate_changing_search_icon = false;
  search_box_view_->Init(params);

  // The main view has a solid color layer, so the separator needs its own
  // layer to visibly paint.
  separator_ = contents->AddChildView(std::make_unique<SeparatorWithLayer>());

  // NOTE: Passing drag and drop host from a specific shelf instance assumes
  // that the `apps_page_` will not get reused for showing the app list in
  // another root window.
  apps_page_ = contents->AddChildView(std::make_unique<AppListBubbleAppsPage>(
      view_delegate_, drag_and_drop_host, GetAppListConfig(),
      a11y_announcer_.get(),
      /*folder_controller=*/this));
  // The apps page must fill the bubble so that the apps grid view can flex to
  // include empty space below the visible icons.
  layout->SetFlexForView(apps_page_, 1);

  search_page_dialog_controller_ =
      std::make_unique<SearchResultPageDialogController>(this);
  search_page_ =
      contents->AddChildView(std::make_unique<AppListBubbleSearchPage>(
          view_delegate_, search_page_dialog_controller_.get(),
          search_box_view_));
  search_page_->SetVisible(false);
}

void AppListBubbleView::InitFolderView(
    ApplicationDragAndDropHost* drag_and_drop_host) {
  auto folder_view = std::make_unique<AppListFolderView>(
      this, apps_page_->scrollable_apps_grid_view(),
      /*contents_view=*/nullptr, a11y_announcer_.get(), view_delegate_);
  folder_view->items_grid_view()->SetDragAndDropHostOfCurrentAppList(
      drag_and_drop_host);
  folder_view->UpdateAppListConfig(GetAppListConfig());
  folder_background_view_ =
      AddChildView(std::make_unique<FolderBackgroundView>(folder_view.get()));
  folder_background_view_->SetVisible(false);

  folder_view_ = AddChildView(std::move(folder_view));
  // Folder view will be set visible by its show animation.
  folder_view_->SetVisible(false);
}

void AppListBubbleView::StartShowAnimation() {
  // For performance, don't animate the shadow.
  view_shadow_.reset();

  // Ensure layout is up-to-date before animating views.
  if (needs_layout())
    Layout();
  DCHECK(!needs_layout());

  // Bubble animation specification:
  //
  // Y Position: Down 8px → End position
  // Duration: 150ms
  // Ease: (0.00, 0.00, 0.20, 1.00)
  //
  // Height: 75% → 100%
  // Duration: 150ms
  // Ease: (0.00, 0.00, 0.20, 1.00)
  //
  // Opacity: 0% → 100%
  // Duration: 150ms
  // Ease: Linear

  // Start by making the layer shorter, pushed down, and transparent.
  const gfx::Rect target_bounds = layer()->GetTargetBounds();
  layer()->SetBounds(GetShowHideAnimationBounds(target_bounds));
  layer()->SetOpacity(0.f);

  // Animate the layer to fully opaque at its target bounds.
  views::AnimationBuilder()
      .OnEnded(base::BindOnce(&AppListBubbleView::OnShowAnimationEnded,
                              weak_factory_.GetWeakPtr(), target_bounds))
      .OnAborted(base::BindOnce(&AppListBubbleView::OnShowAnimationEnded,
                                weak_factory_.GetWeakPtr(), target_bounds))
      .Once()
      .SetDuration(base::Milliseconds(150))
      .SetBounds(layer(), target_bounds, gfx::Tween::LINEAR_OUT_SLOW_IN)
      .SetOpacity(layer(), 1.f, gfx::Tween::LINEAR);

  // AppListBubbleAppsPage handles moving the individual views. It also handles
  // smoothness reporting, because the view movement animation has a longer
  // duration.
  if (apps_page_->GetVisible())
    apps_page_->StartShowAnimation();

  // Note: The assistant page handles its own show animation internally.
}

void AppListBubbleView::StartHideAnimation(
    base::OnceClosure on_animation_ended) {
  on_hide_animation_ended_ = std::move(on_animation_ended);

  // For performance, don't animate the shadow.
  view_shadow_.reset();

  // Ensure any in-progress animations have their cleanup callbacks called.
  AbortAllAnimations();

  ui::AnimationThroughputReporter reporter(
      layer()->GetAnimator(),
      metrics_util::ForSmoothness(base::BindRepeating([](int value) {
        base::UmaHistogramPercentage(
            "Apps.ClamshellLauncher.AnimationSmoothness.Close", value);
      })));

  // Animation spec:
  //
  // Y Position: Current → Down 8px
  // Duration: 100ms
  // Ease: (0.4, 0, 1, 1).
  //
  // Height: 100% → 75%
  // Duration: 100ms
  // Ease: (0.4, 0, 1, 1)
  //
  // Opacity: 100% → 0%
  // Duration: 100ms
  // Ease: Linear
  const gfx::Rect target_bounds = layer()->GetTargetBounds();
  const gfx::Rect final_bounds = GetShowHideAnimationBounds(target_bounds);

  if (apps_page_->GetVisible())
    apps_page_->StartHideAnimation();

  views::AnimationBuilder()
      .OnEnded(base::BindOnce(&AppListBubbleView::OnHideAnimationEnded,
                              weak_factory_.GetWeakPtr(), target_bounds))
      .OnAborted(base::BindOnce(&AppListBubbleView::OnHideAnimationEnded,
                                weak_factory_.GetWeakPtr(), target_bounds))
      .Once()
      .SetDuration(base::Milliseconds(100))
      .SetBounds(layer(), final_bounds, gfx::Tween::FAST_OUT_LINEAR_IN)
      .SetOpacity(layer(), 0.f, gfx::Tween::LINEAR);
}

void AppListBubbleView::AbortAllAnimations() {
  apps_page_->AbortAllAnimations();
  layer()->GetAnimator()->AbortAllAnimations();
}

bool AppListBubbleView::Back() {
  if (showing_folder_) {
    folder_view_->CloseFolderPage();
    return true;
  }
  if (search_box_view_->HasSearch()) {
    search_box_view_->ClearSearch();
    return true;
  }

  return false;
}

void AppListBubbleView::ShowPage(AppListBubblePage page) {
  DVLOG(1) << __PRETTY_FUNCTION__ << " page " << static_cast<int>(page);
  // The assistant has its own text input field.
  search_box_view_->SetVisible(page != AppListBubblePage::kAssistant);
  separator_->SetVisible(page != AppListBubblePage::kAssistant);

  apps_page_->SetVisible(page == AppListBubblePage::kApps);
  search_page_->SetVisible(page == AppListBubblePage::kSearch);
  search_page_dialog_controller_->SetEnabled(page ==
                                             AppListBubblePage::kSearch);
  assistant_page_->SetVisible(page == AppListBubblePage::kAssistant);
  switch (page) {
    case AppListBubblePage::kApps:
    case AppListBubblePage::kSearch:
      search_box_view_->SetSearchBoxActive(true, /*event_type=*/ui::ET_UNKNOWN);
      // Explicitly request focus in case the search box was active before.
      search_box_view_->search_box()->RequestFocus();
      break;
    case AppListBubblePage::kAssistant:
      // Explicitly set search box inactive so the next attempt to activate it
      // will succeed.
      search_box_view_->SetSearchBoxActive(false,
                                           /*event_type=*/ui::ET_UNKNOWN);
      assistant_page_->RequestFocus();
      break;
  }
}

bool AppListBubbleView::IsShowingEmbeddedAssistantUI() const {
  return assistant_page_->GetVisible();
}

void AppListBubbleView::ShowEmbeddedAssistantUI() {
  if (IsShowingEmbeddedAssistantUI())
    return;
  ShowPage(AppListBubblePage::kAssistant);
}

int AppListBubbleView::GetHeightToFitAllApps() const {
  return apps_page_->scroll_view()->contents()->bounds().height() +
         search_box_view_->GetPreferredSize().height();
}

const char* AppListBubbleView::GetClassName() const {
  return "AppListBubbleView";
}

bool AppListBubbleView::AcceleratorPressed(const ui::Accelerator& accelerator) {
  switch (accelerator.key_code()) {
    case ui::VKEY_ESCAPE:
    case ui::VKEY_BROWSER_BACK:
      // If the ContentsView does not handle the back action, then this is the
      // top level, so we close the app list.
      if (!Back())
        view_delegate_->DismissAppList();
      break;
    default:
      NOTREACHED();
      return false;
  }

  // Don't let the accelerator propagate any further.
  return true;
}

void AppListBubbleView::OnThemeChanged() {
  views::View::OnThemeChanged();

  SetBackground(views::CreateRoundedRectBackground(
      AshColorProvider::Get()->GetBaseLayerColor(
          AshColorProvider::BaseLayerType::kTransparent80),
      kBubbleCornerRadius));
  SetBorder(std::make_unique<HighlightBorder>(
      kBubbleCornerRadius, HighlightBorder::Type::kHighlightBorder1,
      /*use_light_colors=*/false));
}

void AppListBubbleView::Layout() {
  // The folder view has custom layout code that centers the folder over the
  // associated root apps grid folder item.
  if (showing_folder_) {
    gfx::Rect folder_bounding_box = GetLocalBounds();
    folder_bounding_box.Inset(kFolderViewInset, kFolderViewInset);
    folder_view_->SetBoundingBox(folder_bounding_box);
    folder_view_->UpdatePreferredBounds();
    // NOTE: Folder view bounds are also modified during reparent drag when the
    // view is "visible" but hidden offscreen. See app_list_folder_view.cc.
    folder_view_->SetBoundsRect(folder_view_->preferred_bounds());
  }

  views::View::Layout();
}

void AppListBubbleView::QueryChanged(SearchBoxViewBase* sender) {
  DCHECK_EQ(sender, search_box_view_);
  // TODO(https://crbug.com/1204551): Animated transitions.
  const bool has_search = search_box_view_->HasSearch();
  apps_page_->SetVisible(!has_search);
  search_page_->SetVisible(has_search);
  search_page_dialog_controller_->SetEnabled(has_search);
  assistant_page_->SetVisible(false);

  // Ask the controller to start the search.
  std::u16string query =
      AppListModelProvider::Get()->search_model()->search_box()->text();
  view_delegate_->StartSearch(query);
  SchedulePaint();
}

void AppListBubbleView::AssistantButtonPressed() {
  // Showing the assistant via the delegate triggers the assistant's visibility
  // change notification and ensures its initial visual state is correct.
  view_delegate_->StartAssistant();
}

void AppListBubbleView::CloseButtonPressed() {
  // Activate and focus the search box.
  search_box_view_->SetSearchBoxActive(true, /*event_type=*/ui::ET_UNKNOWN);
  search_box_view_->ClearSearch();
}

void AppListBubbleView::OnSearchBoxKeyEvent(ui::KeyEvent* event) {
  // Nothing to do. Search box starts focused, and FocusManager handles arrow
  // key traversal from there.
}

bool AppListBubbleView::CanSelectSearchResults() {
  return search_page_->GetVisible() &&
         search_page_->search_view()->CanSelectSearchResults();
}

void AppListBubbleView::ShowFolderForItemView(
    AppListItemView* folder_item_view) {
  DVLOG(1) << __FUNCTION__;
  if (folder_view_->IsAnimationRunning())
    return;

  // TODO(jamescook): Record metric for folder open. Either use the existing
  // Apps.AppListFolderOpened or introduce a new metric.

  DCHECK(folder_item_view->is_folder());
  folder_view_->ConfigureForFolderItemView(folder_item_view);
  showing_folder_ = true;
  Layout();
  folder_background_view_->SetVisible(true);
  folder_view_->ScheduleShowHideAnimation(/*show=*/true,
                                          /*hide_for_reparent=*/false);
  if (apps_page_->scrollable_apps_grid_view()->has_selected_view()) {
    // If the user is keyboard navigating, move focus into the folder.
    folder_view_->FocusFirstItem(/*silently=*/false);
  } else {
    // Release focus so that disabling the views below does not shift focus
    // into the folder grid.
    GetFocusManager()->ClearFocus();
  }
  // Disable items behind the folder so they will not be reached in focus
  // traversal.
  DisableFocusForShowingActiveFolder(true);
}

void AppListBubbleView::ShowApps(AppListItemView* folder_item_view,
                                 bool select_folder) {
  DVLOG(1) << __FUNCTION__;
  if (folder_view_->IsAnimationRunning())
    return;

  showing_folder_ = false;
  Layout();
  folder_background_view_->SetVisible(false);
  apps_page_->scrollable_apps_grid_view()->ResetForShowApps();
  folder_view_->ResetItemsGridForClose();
  if (folder_item_view) {
    folder_view_->ScheduleShowHideAnimation(/*show=*/false,
                                            /*hide_for_reparent=*/false);
  } else {
    folder_view_->HideViewImmediately();
  }
  DisableFocusForShowingActiveFolder(false);
  if (folder_item_view && select_folder)
    folder_item_view->RequestFocus();
  else
    search_box_view_->search_box()->RequestFocus();
}

void AppListBubbleView::ReparentFolderItemTransit(
    AppListFolderItem* folder_item) {
  DVLOG(1) << __FUNCTION__;
  if (folder_view_->IsAnimationRunning())
    return;

  showing_folder_ = false;
  Layout();
  folder_background_view_->SetVisible(false);
  folder_view_->ScheduleShowHideAnimation(/*show=*/false,
                                          /*hide_for_reparent=*/true);
  DisableFocusForShowingActiveFolder(false);
}

void AppListBubbleView::ReparentDragEnded() {
  DVLOG(1) << __FUNCTION__;
  // Nothing to do.
}

void AppListBubbleView::DisableFocusForShowingActiveFolder(bool disabled) {
  search_box_view_->SetEnabled(!disabled);
  SetViewIgnoredForAccessibility(search_box_view_, disabled);

  apps_page_->DisableFocusForShowingActiveFolder(disabled);
}

void AppListBubbleView::OnShowAnimationEnded(const gfx::Rect& layer_bounds) {
  // Restore the layer bounds. If the animation completed normally, this isn't
  // visible because the bounds won't change. If the animation was aborted, this
  // is needed to reset state before starting the hide animation.
  layer()->SetBounds(layer_bounds);

  // Add a shadow.
  view_shadow_ = std::make_unique<ViewShadow>(this, kShadowElevation);
  view_shadow_->SetRoundedCornerRadius(kBubbleCornerRadius);
}

void AppListBubbleView::OnHideAnimationEnded(const gfx::Rect& layer_bounds) {
  // Restore the layer bounds. This isn't visible because opacity is 0.
  layer()->SetBounds(layer_bounds);

  // Hide any open folder by showing the apps page.
  ShowApps(/*folder_item_view=*/nullptr, /*select_folder=*/false);

  if (on_hide_animation_ended_)
    std::move(on_hide_animation_ended_).Run();
}

}  // namespace ash