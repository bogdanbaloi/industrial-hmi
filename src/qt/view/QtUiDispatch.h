#pragma once

#include <QMetaObject>
#include <QObject>
#include <Qt>

#include <utility>

namespace app::view {

/// Run `fn` on `context`'s thread -- the Qt UI thread for widgets. Once the
/// integration backends are running, presenter (and AlertCenter) callbacks can
/// arrive on a backend Asio thread, and touching a widget off the UI thread is
/// undefined behaviour. Every view callback that renders marshals through here,
/// the Qt analog of the GTK `Glib::signal_idle` hop the GTK views use (ADR-0020
/// anticipated this "background producer" path). If `context` is destroyed
/// before the queued call runs, Qt drops the pending event, so passing the
/// widget itself as `context` keeps the hop safe across teardown.
template <typename Fn>
void postToUi(QObject* context, Fn&& fn) {
    // The functor overload trips clang-analyzer-cplusplus.NewDeleteLeaks -- a
    // Qt-internal slot allocation the analyzer mis-traces and reports inside
    // qobjectdefs.h (a system header NOLINT cannot reach), so the check is
    // disabled in .clang-tidy rather than suppressed per-line here.
    QMetaObject::invokeMethod(context, std::forward<Fn>(fn),
                              Qt::QueuedConnection);
}

}  // namespace app::view
