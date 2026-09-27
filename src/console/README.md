# `src/console/` -- Headless Front-End

The same system, driven from a terminal, with no window and no graphical
desktop. The binary is `industrial-hmi-console`.

It exists to be the proof rather than the product.

## What it proves, and why that is the whole point

`DashboardPresenter` drives this view exactly as it drives the GTK pages.
`ConsoleView` implements `app::ViewObserver` and nothing else, so there is
**zero presenter-side branching per front-end**: no `if (console)` anywhere
above this directory.

The structural half is stronger than the code half. `industrial-hmi-console`
links `objectsModel`, `objectsConfig`, `objectsCore`, `objectsPresenter`,
`objectsConsole` plus the integration object libraries. It does not link
gtkmm, and the target names it nowhere.

So the MVP separation is not a diagram. If a `model::` type or a presenter
had picked up a GTK dependency, this binary would stop linking. That is a
compile-time argument, which is the kind that cannot rot quietly.

**One honest limit, stated because the claim is repeated elsewhere.** Nothing
in CI runs `nm` over the produced binary to assert the symbol count is zero.
The guarantee comes from the link line rather than from a gate, so it holds as
long as nobody adds gtkmm to that target. A real check would be a step that
greps the symbol table, and it does not exist yet.

## Architecture

| File | Responsibility |
| --- | --- |
| `ConsoleView.{h,cpp}` | The `ViewObserver` implementation. Renders state as text, reads stdin on a `std::jthread`, dispatches line-based commands. |
| `InitConsole.{h,cpp}` | The composition root for this front-end. Builds the Model plus Presenter plus View trio, wires them, drives the simulation tick. |

`InitConsole` is the direct counterpart of `QtInitRoot` and of the GTK
application bootstrap. Three composition roots over one presenter is what the
ViewObserver seam buys.

## Threading

One `std::jthread` reads stdin so the loop does not block the model's own
Boost.Asio thread. The presenter is already thread-safe for observer
registration, so the view adds no locking of its own.

## Where to look first

1. `ConsoleView.h`, the observer contract as this front-end sees it
2. `CMakeLists.txt` at the `industrial-hmi-console` target, the link line that
   carries the argument
3. `src/presenter/README.md`, for the seam from the other side
