// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include <string>

// const std::string CONTROLLER_UID = "232390330480218";
const std::string CONTROLLER_UID = "1"; 
const std::string BACKEND_API_URL = "http://ttft.uxp.ru";

// Safety cap on keyboard input length. In normal operation, inputs are much shorter;
// this limit guards against chained or abnormal conditions that could grow the buffer indefinitely.
constexpr std::size_t INPUT_MAX_LENGTH = 1024;

#ifdef FUELFLUX_UNIX_FOLDER_CONVENTION
const std::string STORAGE_DB_PATH = "/var/fuelflux/db/fuelflux_storage.db";
const std::string CACHE_DB_PATH = "/var/fuelflux/db/fuelflux_cache.db";
const std::string LOG_DIR = "/var/fuelflux/logs";
#else
const std::string STORAGE_DB_PATH = "fuelflux/db/fuelflux_storage.db";
const std::string CACHE_DB_PATH = "fuelflux/db/fuelflux_cache.db";
const std::string LOG_DIR = "fuelflux/logs";
#endif
