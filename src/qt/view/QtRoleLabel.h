#pragma once

#include "src/auth/Role.h"

#include <QObject>
#include <QString>

namespace app::view {

/// Single source of the human, translatable label for an auth::Role. The
/// Role enum's `roleName()` is the stable wire / audit code (OPERATOR / ...);
/// this is the operator-facing label. Shared so the Users page table and the
/// add/edit dialog's role combo never drift apart.
[[nodiscard]] inline QString roleLabel(auth::Role role) {
    switch (role) {
        case auth::Role::Operator:    return QObject::tr("Operator");
        case auth::Role::Maintenance: return QObject::tr("Maintenance");
        case auth::Role::Admin:       return QObject::tr("Admin");
    }
    return {};
}

}  // namespace app::view
