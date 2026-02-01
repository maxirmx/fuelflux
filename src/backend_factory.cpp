// Copyright (C) 2025 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "backend_factory.h"

#include <utility>

#include "backend.h"
#include "config.h"
#include "logger.h"
#include "sim800c_backend.h"

namespace fuelflux {

std::unique_ptr<IBackend> CreateBackend(std::shared_ptr<MessageStorage> storage) {
    switch (BACKEND_TYPE) {
        case BackendType::Http:
            return std::make_unique<Backend>(BACKEND_API_URL, CONTROLLER_UID, std::move(storage));
        case BackendType::Sim800c:
            return std::make_unique<Sim800cBackend>(SIM800C_DEVICE_PATH,
                                                    SIM800C_BAUD_RATE,
                                                    SIM800C_APN,
                                                    SIM800C_CONNECT_TIMEOUT_MS,
                                                    SIM800C_RESPONSE_TIMEOUT_MS,
                                                    std::move(storage));
        default:
            LOG_BCK_WARN("Unknown backend type requested; defaulting to HTTP backend");
            return std::make_unique<Backend>(BACKEND_API_URL, CONTROLLER_UID, std::move(storage));
    }
}

std::shared_ptr<IBackend> CreateBackendShared(std::shared_ptr<MessageStorage> storage) {
    return std::shared_ptr<IBackend>(CreateBackend(std::move(storage)));
}

} // namespace fuelflux
