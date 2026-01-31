// Copyright (C) 2025 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "types.h"

struct sqlite3;

namespace fuelflux {

struct BacklogItem {
    int id = 0;
    std::string uid;
    BacklogMethod method = BacklogMethod::Refuel;
    std::string data;
};

class BacklogStorage {
public:
    explicit BacklogStorage(const std::string& path);
    ~BacklogStorage();

    bool IsOpen() const;
    bool AddItem(const std::string& uid, BacklogMethod method, const std::string& data);
    std::vector<BacklogItem> LoadAll();
    bool RemoveItem(int id);

private:
    bool Execute(const std::string& sql);
    void Close();

    sqlite3* db_;
    std::mutex mutex_;
};

} // namespace fuelflux
