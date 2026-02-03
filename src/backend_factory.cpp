// Copyright (C) 2025 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "backend_factory.h"

#include <utility>

#include "backend.h"
#include "config.h"
#include "logger.h"

#ifdef TARGET_SIM800C
#include "sim800c_backend.h"
#endif

namespace fuelflux {

std::unique_ptr<IBackend> CreateBackend(std::shared_ptr<MessageStorage> storage) {
#ifdef TARGET_SIM800C
    return std::make_unique<Sim800cBackend>(BACKEND_API_URL,
                                            CONTROLLER_UID,
                                            SIM800C_DEVICE_PATH,
                                            SIM800C_BAUD_RATE,
                                            SIM800C_APN,
                                            SIM800C_APN_USER,
                                            SIM800C_APN_PASSWORD,
                                            SIM800C_CONNECT_TIMEOUT_MS,
                                            SIM800C_RESPONSE_TIMEOUT_MS,
                                            std::move(storage));
#else
    return std::make_unique<Backend>(BACKEND_API_URL, CONTROLLER_UID, std::move(storage));
#endif
}

std::shared_ptr<IBackend> CreateBackendShared(std::shared_ptr<MessageStorage> storage) {
    return std::shared_ptr<IBackend>(CreateBackend(std::move(storage)));
}

} // namespace fuelflux
