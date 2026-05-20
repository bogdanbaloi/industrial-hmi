# `src/gtk/view/` -- GTK4 View Layer

The entire desktop UI for the Industrial HMI. GTK4 / gtkmm4 widgets,
custom-drawn industrial-style controls, multi-language + multi-palette
theming, and the routing layer that mounts pages into the application
shell. Implements the `ViewObserver` interface from `src/presenter/` so
swapping the view for the headless console binary is a one-line
substitution in the composition root.

---

## Why this module exists separately

The MVP discipline pays off when the View stays passive: receives
`ViewModel` DTOs from presenters, renders, forwards user gestures
back to presenters as method calls. Everything in this folder
follows that rule. There is no business logic here:

- No quality threshold decisions (the presenter computes them).
- No RBAC checks (the presenter enforces them; the page-level
  `applyRole` only toggles widget sensitivity as a UX hint).
- No model state outside the rendered ViewModel snapshot.
- No SQL, no auth, no integration backends.

Result: the view is the most replaceable layer in the stack. Build
a Qt or web frontend tomorrow, reuse every presenter / model /
auth / integration line of code unchanged.

What lives here is everything **GTK4-specific**: widget tree, CSS
theming, modal lifecycle, idle marshalling onto the GTK main
thread, gettext-driven language switching, custom Cairo drawing
for industrial-style gauges.

---

## Directory layout

```
src/gtk/view/
├── MainWindow.{h,cpp}            Window shell + sidebar + notebook
├── MainWindowKeyDispatch.{h,cpp} Global keyboard shortcuts (F1-F11, Esc)
├── DialogManager.{h,cpp}         Info/error/confirm modal helpers
├── AboutDialog.{h,cpp}           "About" modal (version, license, links)
├── LoginDialog.{h,cpp}           Auth gate (runs BEFORE MainWindow exists)
├── ThemeManager.h                Palette + layout + CSS orchestration
├── colors.h, css_classes.h       Shared design tokens
├── ui_sizes.h                    Pixel constants (margins, padding, etc.)
├── pages/                        One per Notebook tab (Dashboard, Products, ...)
├── widgets/                      Reusable building blocks (gauges, charts, badges)
└── dialogs/                      User-management modals (Add/Edit, Reset Pw, Profile)
```

---

## Architecture (the bits that aren't in the presenter README)

### MVP contract reinforced

```
                  ┌──────────────────┐
                  │   Presenter      │  (src/presenter/)
                  │   ViewObserver   │
                  └────────┬─────────┘
                           │ ViewModel callback
                           │ (on the presenter thread!)
                           ▼
                  ┌──────────────────┐
                  │  Glib::signal_   │  marshal to GTK main thread
                  │  idle().connect  │  BEFORE touching any widget
                  └────────┬─────────┘
                           │
                           ▼
                  ┌──────────────────┐
                  │   Page / Widget  │  set_text / queue_draw / ...
                  │   (THIS LAYER)   │
                  └──────────────────┘
```

Every page that implements `ViewObserver` follows the same pattern
in its callback bodies: capture the ViewModel by value into a
lambda, schedule a `Glib::signal_idle()` that updates widgets on
the GTK main thread. The presenter never knows or cares which
thread it's invoked from.

### Page registry

```cpp
// MainWindow.cpp -- buildPages():
dashboardPage_ = Gtk::make_managed<DashboardPage>(*dialogManager_);
dashboardPage_->initialize(dashboardPresenter_);
registerPage(dashboardPage_);

productsPage_ = Gtk::make_managed<ProductsPage>(*dialogManager_);
productsPage_->initialize(productsPresenter_);
registerPage(productsPage_);

// Role-gated registration -- only if the signed-in user has the role:
if (canViewAuditLog(currentUser.role)) {
    auditLogPage_ = Gtk::make_managed<AuditLogPage>(...);
    registerPage(auditLogPage_);
}
```

Every page inherits from `Page` (`src/gtk/view/pages/Page.h`), a
thin base that owns the `DialogManager` reference + a virtual
`pageTitle()`. `registerPage(p)` appends to the notebook with
`p->pageTitle()` as the label. Adding a new page is one new file
+ one registration line.

### Theme system

Three independent axes:

| Axis | Class | Surface |
|---|---|---|
| **Palette** (colour scheme) | `ThemeManager` | 8 presets, runtime switch via Settings |
| **Layout** (window structure) | `ThemeManager::reloadLayout` | Default vs Blueprint top-bar; rebuilds widget tree without restart |
| **Language** | `core::initI18n` + `gettext` | 11 languages, runtime switch |

Each switch tears down + rebuilds the affected widgets in place.
The page registry is stable; what changes is the visual rendering.

### Custom Cairo widgets (industrial look)

The off-the-shelf GTK widgets look like a generic desktop app.
Industrial HMIs have a distinct visual language -- circular
quality gauges, multi-series trend charts, status LEDs, "active"
chips with a glowing border. These don't exist as GTK widgets;
they're Cairo `DrawingArea` subclasses:

- `QualityGauge` -- arc + numeric centre, colour from threshold
- `TrendChart` -- multi-series line + grid + Y-axis labels
- `SystemStatusBadge` -- coloured dot + state label
- `AvatarWidget` -- rounded-square clip, either decoded pixbuf or
  hashed-colour tile with initials
- `BackendHealthBar` -- horizontal strip of per-protocol dots

Each is ~100 lines of `set_draw_func(...)` + Cairo calls. The
`QualityGauge::queue_draw()` is the only request the presenter
needs to issue when the value changes.

### Modal dialogs with inner main loops

GTK4 removed the synchronous `dialog.run()` helper from GTK3, so
every modal needs its own inner `Glib::MainLoop`. Pattern repeated
across `LoginDialog`, `UserEditDialog`, `ResetPasswordDialog`,
`ProfileDialog`:

```cpp
Result runModal() {
    innerLoop_ = Glib::MainLoop::create();
    present();
    // ... buttons connect signal handlers that call innerLoop_->quit()
    innerLoop_->run();          // blocks until quit
    return result_;
}
```

`set_modal(true)` + `set_transient_for(parent)` darken the
background and prevent input on the host window. The inner loop
keeps GTK painting + ticking other timers while the dialog is
focused.

### Sign-out lifecycle (the hardest bit)

The sidebar `UserBadge` exposes a `Sign out` action. The handler:

1. Hides the current MainWindow.
2. Calls `gtkApp->hold()` to keep the application alive across
   the zero-windows transition.
3. Runs `LoginDialog::runModal()` inside the same Gtk::Application.
4. On success: spawns a **fresh MainWindow** from an idle
   callback, so role-gated pages (UsersPage, AuditLogPage)
   re-evaluate against the new user. Closes the old window.
   `gtkApp->release()` after the new window is attached.
5. On cancel: `gtkApp->quit()` -- the operator is done.

The rebuild-rather-than-rehydrate decision is the only one that
keeps tabs in sync with role; documented inline in `MainWindow::
handleSignOut()`.

---

## Pages -- per-tab overview

| Page | Presenter | Notable concern |
|---|---|---|
| `DashboardPage` | `DashboardPresenter` | 3 equipment cards + 3 quality gauges + work-unit progress + control panel. Cached `currentRole_` to gate Calibration / Reset on every `ControlPanelViewModel` update. |
| `ProductsPage` | `ProductsPresenter` | CRUD table with search, async writes via `Glib::signal_idle`. |
| `SettingsPage` | (none -- pure view over Config + Theme + Logger) | Palette + layout + language switchers + log tail; reloads in place. |
| `HistoryPage` | reads from `HistoryReader` directly | Multi-series TrendChart with range presets (1h / 24h / 7d). |
| `AuditLogPage` | reads from `AuditLogger` directly | 5-axis filter dropdowns + CSV export for compliance walks (21 CFR Part 11). |
| `UsersPage` | `UsersPresenter` | Admin-only grid + Add/Edit/Reset/Delete + Toast feedback. |
| `QualityInspectionPage` | `QualityInspectionPresenter` | ONNX top-K result rendered as a list; image preview. Optional, gated on `BUILD_ML_CLASSIFIER`. |

Every page extends `Page` and implements `ViewObserver`.
Construction takes the `DialogManager`; `initialize(presenter)`
wires the observer link. Most pages have an `applyRole(Role)`
method called by `MainWindow` after auth resolves so widgets that
the current role can't touch get `set_sensitive(false)`.

---

## Widgets -- reusable building blocks

| Widget | Purpose |
|---|---|
| `QualityGauge` | Circular arc gauge with numeric centre + colour from threshold. Custom Cairo `DrawingArea`. |
| `TrendChart` | Multi-series line chart with grid + Y-axis labels. Custom Cairo. |
| `AlertsPanel` | Coalesced alert list; presenter (`AlertCenter`) handles dedup + debounce. |
| `SystemStatusBadge` | Coloured dot + state label. Idle / Running / Stopped / Calibrating / Error. |
| `LiveClock` | Wall-clock label updated on a 1-second timer. |
| `BackendHealthBar` | Per-backend health dots (TCP / MQTT / Modbus / OPC-UA). |
| `UserBadge` | Sidebar: avatar + username + role + Profile + Sign out buttons. Subscribes to `Session::signalChanged` so an admin editing their own row sees the change immediately. |
| `AvatarWidget` | Square tile -- decoded pixbuf if uploaded, hashed-colour palette + initials otherwise. |
| `Toast` | Slide-in banner for action feedback. Auto-dismiss for success, stays for errors. Configurable duration + position. |

---

## Dialogs -- modal flows

| Dialog | Trigger | Notable |
|---|---|---|
| `LoginDialog` | Application startup (when auth enabled) + sign-out | `#ifdef _WIN32` centring via SetWindowPos -- GTK4 dropped positioning APIs and Win32 doesn't centre small modals by default. |
| `UserEditDialog` | Admin clicks Add User / Edit | One dialog drives both Add + Edit via a mode flag. Admin uses ResetPasswordDialog for password changes. |
| `ResetPasswordDialog` | Admin clicks Reset Password | Distinct audit verb (`RESET_PASSWORD`) from self-service `CHANGE_PASSWORD`. |
| `ProfileDialog` | UserBadge `Profile` button | Self-service: avatar upload + change-own-password. |
| `AboutDialog` | Menu / F1 | Version + license + links. |

All four (Login / UserEdit / ResetPassword / Profile) follow the
inner-main-loop pattern. `DialogManager` exposes simpler helpers
(`showInfo`, `showError`, `showConfirm`) for common cases that
don't need a custom modal.

---

## CSS theming

`assets/styles/adwaita-theme.css` is the active stylesheet. CSS
class tokens live in `css_classes.h` so a page never types raw
strings:

```cpp
add_css_class(css::kHeading);
add_css_class(css::kDimLabel);
add_css_class(css::kSuccess);    // toast tone, gauge "ok" colour
```

The theme uses `@define-color` for the palette tokens; switching
palettes in `ThemeManager` rewrites the colour values and reapplies
the stylesheet -- no widget rebuild needed.

Toast banners (`Toast.cpp`) and `UserBadge` are the freshest
examples of CSS-driven theming.

---

## Threading model

- **GTK4 main loop owns the UI thread.** Every widget mutation
  must happen there.
- **Presenter callbacks fire on the model's signal thread**
  (typically a worker, not the UI thread). Every `on*()` override
  in a Page captures the ViewModel by value and uses
  `Glib::signal_idle()` to hop to the UI thread before touching
  widgets.
- **Async writes** (ProductsPresenter add / update / delete)
  marshal back via `Glib::signal_idle` -- the only place this
  layer touches Glib from outside the UI thread.
- **Modal dialogs** run inner `Glib::MainLoop`s; GTK keeps
  painting + other timers run during the loop. The host window's
  input is blocked by `set_modal(true)` + `set_transient_for`.
- **Timer-driven widgets** (LiveClock 1 s, AuditLogPage auto-
  refresh 5 s, MainWindow auto-refresh 2 s) use
  `Glib::signal_timeout` -- ticks on the UI thread, never
  contends.

---

## Internationalisation (i18n)

All user-visible strings flow through `_()` (the gettext macro).
11 catalogs live under `po/`; the build pipeline compiles them
into `.mo` files mounted at `share/locale/`.

Language switching is runtime:

1. SettingsPage emits language change.
2. `ThemeManager::applyLanguage` calls `core::initI18n` with the
   new locale.
3. `MainWindow::reloadLayout` tears down + rebuilds pages so
   every `_()` re-resolves against the new catalog.

No restart needed.

---

## Testing

`tests/HistoryPageTest.cpp`, `tests/AuditLogPageTest.cpp`,
`tests/UsersPageTest.cpp`, `tests/DashboardPageTest.cpp`,
`tests/ProductsPageTest.cpp`, `tests/SettingsPageTest.cpp` --
GUI smoke tests via `xvfb-run` + a custom main (`ViewTestMain.cpp`)
that calls `gtk_init()` before the gtest harness runs. Verify the
page constructs against an in-memory model + injected presenter,
the initial paint produces the expected widget tree, and a
`ViewModel` callback updates the relevant widget without throwing.

`tests/DialogManagerTest.cpp` -- info / error / confirm helpers.

`tests/MainWindowKeyDispatchTest.cpp` -- F-key + Escape routing.

The custom modal dialogs (LoginDialog, UserEditDialog, etc.) are
**not** unit-tested -- their inner `Glib::MainLoop` is hard to
drive synchronously from a test. They're exercised end-to-end
via Docker compose scenarios.

Run isolated:

```bash
cd build/debug
xvfb-run ctest -R 'Page|Dialog|MainWindow' --output-on-failure
```

---

## Out of scope (intentional)

- **Adw widgets (libadwaita)** -- would add ~1 MB dependency for
  a Toast widget we wrote in 150 lines. Same story for
  notification overlays.
- **Hand-rolled animations** -- GTK4 ships transition support
  via `Gtk::Revealer` and CSS `transition`. The sidebar slide-in
  uses these; no `Glib::signal_timeout` interpolation tickers.
- **Drag-and-drop product reordering** -- ProductsPage uses
  `Gtk::ColumnView` with sort headers; manual reorder would
  need a different DnD model.
- **Per-page CSS overrides** -- one stylesheet for the whole
  app. Page-specific themes would fragment maintenance.
- **GTK3 fallback** -- the codebase relies on GTK4-only
  primitives (`Gtk::WindowControls`, `Gtk::FileDialog`).
