// Copyright (C) 2025 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#pragma once

#include <string>

const std::string CONTROLLER_UID = "232390330480218";  
const std::string BACKEND_API_URL = "http://ttft.uxp.ru";
const std::string STORAGE_DB_PATH = "fuelflux_storage.db";

const std::string SIM800C_DEVICE_PATH = "/dev/ttyS5";
const int SIM800C_BAUD_RATE = 9600;
const std::string SIM800C_APN = "internet";
const std::string SIM800C_APN_USER = "";
const std::string SIM800C_APN_PASSWORD = "";
const int SIM800C_CONNECT_TIMEOUT_MS = 10000;
const int SIM800C_RESPONSE_TIMEOUT_MS = 15000;
