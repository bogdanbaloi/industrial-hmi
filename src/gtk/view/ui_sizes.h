#pragma once

namespace app::view::sizes {

// Spacing / margin scale - keep the visual rhythm consistent across dialogs
// and pages. Larger values layer outward (outer frame uses Large, inner
// content uses Medium, tight row spacing uses Small).
inline constexpr int kSpacingTiny   = 4;
inline constexpr int kSpacingSmall  = 8;
inline constexpr int kSpacingMedium = 12;
inline constexpr int kSpacingLarge  = 20;
inline constexpr int kSpacingXL     = 24;

// Quality gauge (Cairo-drawn circular gauge).
inline constexpr int kGaugeSize = 100;  // square widget, width = height

// Trend chart (Cairo-drawn line chart in each quality card).
inline constexpr int    kTrendChartDefaultWidth  = 300;
inline constexpr int    kTrendChartDefaultHeight = 80;
inline constexpr int    kTrendChartInlineHeight  = 60;
inline constexpr size_t kTrendChartCapacity      = 60;  // data points buffered

// Y-axis range for pass-rate trend (matches simulation clamp in SimulatedModel)
inline constexpr float kTrendChartMinY = 85.0f;
inline constexpr float kTrendChartMaxY = 100.0f;

// Control panel buttons.
inline constexpr int kControlButtonWidth  = 100;
inline constexpr int kControlButtonHeight = 40;

// Dialog sizing (About / Add Product / Edit Product / Delete Confirm).
inline constexpr int kAboutDialogWidth    = 420;
inline constexpr int kAboutDialogHeight   = 480;
inline constexpr int kFormDialogWidth     = 400;
inline constexpr int kFormDialogHeight    = 350;
inline constexpr int kConfirmDialogWidth  = 400;
inline constexpr int kConfirmDialogHeight = 150;

// ProductsPage ColumnView fixed column widths.
// Sized for the longest header translation we ship (Spanish
// "Codigo de producto", "Existencias bajas", Italian "Codice prodotto"
// and Portuguese "Codigo do produto"). Columns stay resizable at
// runtime; these are just the initial widths.
inline constexpr int kProductsColumnCodeWidth    = 170;
inline constexpr int kProductsColumnStatusWidth  = 160;
inline constexpr int kProductsColumnStockWidth   = 120;
inline constexpr int kProductsColumnQualityWidth = 110;

// Spin-button ranges for product form.
inline constexpr int    kStockSpinMin  = 0;
inline constexpr int    kStockSpinMax  = 10000;
inline constexpr int    kStockSpinStep = 10;
inline constexpr double kQualitySpinMin  = 0.0;
inline constexpr double kQualitySpinMax  = 100.0;
inline constexpr double kQualitySpinStep = 0.1;

// Format buffer guidance (we now use std::vformat which returns std::string,
// so these are advisory for legacy read-only sites if any remain).
inline constexpr int kSmallFormatBuffer = 16;

}  // namespace app::view::sizes
