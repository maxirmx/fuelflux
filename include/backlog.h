// Copyright (C) 2025 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct sqlite3;

namespace fuelflux {

enum class BacklogMethod {
    Refuel,
    Intake
};

struct BacklogItem {
    std::int64_t id = 0;
    std::string uid;
    BacklogMethod method = BacklogMethod::Refuel;
    std::string data;
};

class BacklogStore {
public:
    explicit BacklogStore(std::string dbPath);
    ~BacklogStore();

    bool Initialize();
    bool Enqueue(const std::string& uid, BacklogMethod method, const std::string& data);
    std::vector<BacklogItem> FetchAll();
    bool Remove(std::int64_t id);
    std::size_t Count() const;
    const std::string& Path() const { return dbPath_; }

private:
    bool Execute(const std::string& statement) const;

    std::string dbPath_;
    mutable std::mutex mutex_;
    ::sqlite3* db_ = nullptr;
};

} // namespace fuelflux
