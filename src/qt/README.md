# `src/qt/` -- Qt 6 Widgets Front-End

A second, complete desktop front-end over the **same** presenters as the GTK
one. Built only when `-DBUILD_QT_FRONTEND=ON`, which is OFF by default. The
binary is `industrial-hmi-qt`.

It exists to prove the view layer is replaceable, not to be a second product.

## Why a whole second front-end

ADR-0020 makes the argument. Toolkit independence is easy to claim and hard to
prove: a codebase that has only ever had one view layer cannot know which of
its habits are architecture and which are GTK.

So the seam was tested by using it. Twenty-four view classes plus fourteen
`.ui` files later, `DashboardPresenter` is unchanged, and the presenters carry
no branch per front-end. What did NOT have to change is the result.

The console binary makes the same point structurally, by linking no gtkmm at
all. This one makes it by weight: a full second UI, with pages, dialogs,
theming and translation, sitting on an untouched presenter layer.

## What it covers

Dashboard, alerts, history, trends, products, goods receipt, multi-station,
audit log, users, settings, plus the login, change-password, reset-password and
user-form dialogs. That is the GTK feature set rather than a demo subset,
which is what makes the comparison honest.

## The parts worth reading

| File | Why it is interesting |
| --- | --- |
| `QtInitRoot.{h,cpp}` | The composition root, the direct counterpart of `InitConsole`. Builds the same Model plus Presenter plus View collaborators, wires them, drives the simulation tick, shows the shell. A third composition root over one presenter is the claim in code. |
| `QtUiDispatch` | The thread hop. The model runs its own Boost.Asio thread, Qt owns its GUI thread, so observer callbacks are marshalled rather than called across. |
| `QtGettextTranslator` | The same gettext catalogue the GTK build uses, adapted to Qt's translator interface, so eleven languages are shared rather than duplicated. |
| `QtPaletteManager`, `QtTheme` | Theming kept out of the widgets, the same separation the GTK side uses for its CSS. |

## Threading

Qt owns the GUI thread. The model owns a Boost.Asio thread. Nothing crosses
directly: `QtUiDispatch` marshals every observer callback onto the GUI thread,
which is the Qt equivalent of what `Glib::signal_idle` does on the GTK side.

The presenters know about neither. That is the point of them.

## What lives here versus what does not

- No business rules. They are in Model or Presenter, and a rule appearing in a
  widget here is a layering violation the review is meant to catch.
- No `model::` includes. The view talks to presenters through view models.
- No protocol knowledge. Backends are in `src/integration/`.

## Where to look first

1. `docs/adr/0020-qt-frontend-mvp-toolkit-independence.md`, why this exists
2. `QtInitRoot.h`, the composition root comment comparing itself to the console
3. `src/presenter/README.md`, the seam from the side that does not change
