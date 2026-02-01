// Copyright (C) 2025 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include <memory>

namespace fuelflux {

class IBackend;
class MessageStorage;

std::unique_ptr<IBackend> CreateBackend(std::shared_ptr<MessageStorage> storage = nullptr);
std::shared_ptr<IBackend> CreateBackendShared(std::shared_ptr<MessageStorage> storage = nullptr);

} // namespace fuelflux
