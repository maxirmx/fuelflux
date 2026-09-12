#pragma once

#include "backend.h"
#include <optional>

namespace fuelflux {

// A value copy: it must not change when synchronization flips cache tables.
struct AuthorizationSnapshot {
    UserInfo user;
    std::vector<BackendTankInfo> tanks;
};

struct ProtectedCardSnapshot {
    AuthorizationSnapshot authorization;
    bool pending = false;
};

struct SavedAuthorizationState {
    std::optional<AuthorizationSnapshot> saved;
    bool reportStorageAvailable = false;
    bool pendingReports = true; // Fail closed if storage cannot be read.
};

} // namespace fuelflux
