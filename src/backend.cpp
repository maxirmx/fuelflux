// Copyright(C) 2025 Maxim[maxirmx] Samsonov(www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "backend.h"
#include "logger.h"
#include <httplib.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <stdexcept>
#include <sstream>

namespace fuelflux {

// Поскольку контроллер обладает крайне ограниченными возможностями по выводу текстовых сообщений,
// и наличие квалифициированного персонала рядом не предполагается, мы ограничимся двумя универсальными
// сообщениями об ошибке.
const std::string StdControllerError = "Ошибка контроллера";
const std::string StdBackendError = "Ошибка портала";
constexpr int HttpRequestWrapperErrorCode = -1;
const std::string HttpRequestWrapperErrorText = "Ошибка связи с сервером";

bool IsErrorResponse(const nlohmann::json& response, std::string* errorText) {
    if (!response.is_object()) {
        return false;
    }
    const auto codeIt = response.find("CodeError");
    if (codeIt == response.end() || !codeIt->is_number_integer()) {
        return false;
    }
    const int code = codeIt->get<int>();
    if (code == 0) {
        return false;
    }
    if (errorText) {
        *errorText = response.value("TextError", "Неизвестная ошибка");
    }
    return true;
}

nlohmann::json BuildWrapperErrorResponse() {
    return nlohmann::json{
        {"CodeError", HttpRequestWrapperErrorCode},
        {"TextError", HttpRequestWrapperErrorText}
    };
}

bool IsNetworkErrorResponse(const nlohmann::json& response) {
    if (!response.is_object()) {
        return false;
    }
    const auto codeIt = response.find("CodeError");
    if (codeIt == response.end() || !codeIt->is_number_integer()) {
        return false;
    }
    return codeIt->get<int>() == HttpRequestWrapperErrorCode;
}

Backend::Backend(const std::string& baseAPI,
                 const std::string& controllerUid,
                 const std::string& backlogDbPath,
                 std::chrono::milliseconds backlogInterval) : 
    baseAPI_(baseAPI),
    controllerUid_(controllerUid),
    isAuthorized_(false),
    roleId_(0),
    allowance_(0.0),
    price_(0.0),
    backlogStore_(backlogDbPath),
    backlogInterval_(backlogInterval)
{
// If the user configured an HTTPS backend but cpp-httplib was built without OpenSSL,
// reject early with a clear error message.
#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
    if (!baseAPI_.empty()) {
        const std::string https_prefix = "https://";
        if (baseAPI_.compare(0, https_prefix.size(), https_prefix) == 0) {
            std::string err = "HTTPS backend requested but OpenSSL support is not available in this build";
            LOG_ERROR("{}", err);
            throw std::runtime_error(err);
        }
    }
#endif

    LOG_BCK_INFO("Backend initialized with base API: {} and controller UID: {}", baseAPI_, controllerUid_);

    if (backlogStore_.Initialize()) {
        const auto pending = backlogStore_.Count();
        LOG_BCK_INFO("Backlog database initialized at {} with {} pending items", backlogStore_.Path(), pending);
        StartBacklogWorker();
    } else {
        LOG_BCK_ERROR("Backlog database initialization failed; backlog processing disabled");
    }
}

Backend::~Backend() {
    StopBacklogWorker();
    if (isAuthorized_) {
        LOG_BCK_WARN("Backend destroyed while still authorized");
    }
}

nlohmann::json Backend::HttpRequestWrapper(const std::string& endpoint, 
                                            const std::string& method,
                                            const nlohmann::json& requestBody,
                                            bool useBearerToken) {
    try {
        // Parse the base URL to extract host and port
        std::string host;
        int port = 80;
        std::string scheme = "http";
        
        // Simple URL parsing
        std::string urlToParse = baseAPI_;
        
        // Extract scheme
        size_t schemePos = urlToParse.find("://");
        if (schemePos != std::string::npos) {
            scheme = urlToParse.substr(0, schemePos);
            urlToParse = urlToParse.substr(schemePos + 3);
        }
        
        // Set default port based on scheme
        if (scheme == "https") {
            port = 443;
        }
        
        // Extract host and port
        // Handle IPv6 addresses in brackets [::1] or [2001:db8::1]
        size_t portPos = std::string::npos;
        size_t pathPos = urlToParse.find('/');
        bool isIPv6 = false;
        
        if (!urlToParse.empty() && urlToParse[0] == '[') {
            // IPv6 address in brackets
            isIPv6 = true;
            size_t bracketEnd = urlToParse.find(']');
            if (bracketEnd != std::string::npos) {
                host = urlToParse.substr(1, bracketEnd - 1); // Extract without brackets
                // Check for port after the closing bracket
                if (bracketEnd + 1 < urlToParse.size() && urlToParse[bracketEnd + 1] == ':') {
                    portPos = bracketEnd + 1;
                }
            } else {
                // Malformed IPv6 address - missing closing bracket
                LOG_BCK_WARN("Malformed IPv6 address in URL: {}", urlToParse);
                host = urlToParse.substr(1); // Try to use what we have
            }
        } else {
            // IPv4 or hostname - look for port after last colon before path
            portPos = urlToParse.find(':');
        }
        
        if (!isIPv6 && portPos != std::string::npos && (pathPos == std::string::npos || portPos < pathPos)) {
            host = urlToParse.substr(0, portPos);
            std::string portStr;
            if (pathPos != std::string::npos) {
                portStr = urlToParse.substr(portPos + 1, pathPos - portPos - 1);
            } 
            else {
                portStr = urlToParse.substr(portPos + 1);
            }
            port = std::stoi(portStr);
        } 
        else if (isIPv6 && portPos != std::string::npos) {
            // IPv6 with port: already extracted host, now extract port
            std::string portStr;
            if (pathPos != std::string::npos) {
                portStr = urlToParse.substr(portPos + 1, pathPos - portPos - 1);
            } else {
                portStr = urlToParse.substr(portPos + 1);
            }
            port = std::stoi(portStr);
        }
        else if (!isIPv6 && pathPos != std::string::npos) {
            host = urlToParse.substr(0, pathPos);
        } 
        else if (!isIPv6 && host.empty()) {
            host = urlToParse;
        }
        
        // Validate that we have a host
        if (host.empty()) {
            LOG_BCK_ERROR("Failed to parse host from URL: {}", baseAPI_);
            return BuildWrapperErrorResponse();
        }
        
        LOG_BCK_DEBUG("Connecting to {}:{} for endpoint: {} (scheme={})", host, port, endpoint, scheme);
        
        // Prepare headers
        std::string hostHeader;
        // IPv6 addresses must be enclosed in brackets in the Host header
        if (isIPv6) {
            hostHeader = "[" + host + "]";
        } else {
            hostHeader = host;
        }
        // Include port in Host header only when it is non-default for the scheme
        if (!((scheme == "http" && port == 80) || (scheme == "https" && port == 443))) {
            hostHeader += ":" + std::to_string(port);
        }

        httplib::Headers headers = {
            {"User-Agent", "fuelflux/0.1.0"},
            {"Accept", "*/*"},
            {"Host", hostHeader}
        };
        
        if (useBearerToken && !token_.empty()) {
            headers.insert({"Authorization", "Bearer " + token_});
        }
        
        // Prepare request body
        std::string bodyStr = requestBody.dump();
        
        LOG_BCK_DEBUG("Request: {} {} with body: {}", method, endpoint, bodyStr);
        
        // helper lambda to perform request on either Client or SSLClient
        auto doRequest = [&](auto &client) -> httplib::Result {
            client.set_connection_timeout(5, 0); // 5 seconds
            client.set_read_timeout(10, 0);      // 10 seconds
            client.set_write_timeout(10, 0);     // 10 seconds
            client.set_keep_alive(true);

            if (method == "POST") {
                return client.Post(endpoint.c_str(), headers, bodyStr, "application/json");
            } 
            else if (method == "GET") {
                return client.Get(endpoint.c_str(), headers);
            } 
            LOG_BCK_ERROR("Unsupported HTTP method: {}", method);
            return httplib::Result();
        };

        httplib::Result res;
        if (scheme == "https") {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            httplib::SSLClient sslClient(host.c_str(), port);
            // By default SSL verification is enabled. To customize CA path/cert use sslClient.set_ca_cert_file(...)
            res = doRequest(sslClient);
#else
            std::string err = "HTTPS requested but cpp-httplib built without OpenSSL support";
            LOG_BCK_ERROR("{}", err);
            return BuildWrapperErrorResponse();
#endif
        } else {
            httplib::Client client(host.c_str(), port);
            res = doRequest(client);
        }
        
        // Check if request succeeded
        if (!res) {
            std::string errorMsg = "HTTP request failed: ";
            switch (res.error()) {
                case httplib::Error::Connection:
                    errorMsg += "Connection error";
                    break;
                case httplib::Error::BindIPAddress:
                    errorMsg += "Bind IP address error";
                    break;
                case httplib::Error::Read:
                    errorMsg += "Read error";
                    break;
                case httplib::Error::Write:
                    errorMsg += "Write error";
                    break;
                case httplib::Error::ExceedRedirectCount:
                    errorMsg += "Exceed redirect count";
                    break;
                case httplib::Error::Canceled:
                    errorMsg += "Canceled";
                    break;
                case httplib::Error::SSLConnection:
                    errorMsg += "SSL connection error";
                    break;
                case httplib::Error::SSLLoadingCerts:
                    errorMsg += "SSL loading certs error";
                    break;
                case httplib::Error::SSLServerVerification:
                    errorMsg += "SSL server verification error";
                    break;
                case httplib::Error::UnsupportedMultipartBoundaryChars:
                    errorMsg += "Unsupported multipart boundary chars";
                    break;
                case httplib::Error::Compression:
                    errorMsg += "Compression error";
                    break;
                default:
                    errorMsg += "Unknown error";
                    break;
            }
            LOG_BCK_ERROR("{}", errorMsg);
            return BuildWrapperErrorResponse();
        }
        
        LOG_BCK_DEBUG("Response status: {} body: {}", res->status, res->body);
        
        // Check HTTP status code
        if (res->status >= 200 && res->status < 300) {
            // Success response
            nlohmann::json responseJson;
            
            // Handle empty or "null" response
            if (res->body.empty() || res->body == "null") {
                responseJson = nlohmann::json(nullptr);
            } 
            else {
                try {
                    responseJson = nlohmann::json::parse(res->body);
                } 
                catch (const std::exception& e) {
                    LOG_BCK_ERROR("Failed to parse response JSON: {}", e.what());
                    return BuildWrapperErrorResponse();
                }
            }
            
            return responseJson;
        } 
        else {
            // HTTP error response
            std::ostringstream oss;
            oss << "System error, code " << res->status;
            LOG_BCK_ERROR("{}", oss.str());
            return BuildWrapperErrorResponse();
        }
    } 
    catch (const std::exception& e) {
        LOG_BCK_ERROR("HTTP request exception: {}", e.what());
        return BuildWrapperErrorResponse();
    }
}

bool Backend::Authorize(const std::string& uid) {
    std::lock_guard<std::mutex> lock(backendMutex_);
    try {
        // Check if already authorized
        if (isAuthorized_) {
            LOG_BCK_ERROR("{}", "Already authorized. Call Deauthorize first.");
            lastError_ = StdControllerError;
            return false;
        }
        
        LOG_BCK_INFO("Authorizing card UID: {}", uid);
        
        // Prepare request body
        nlohmann::json requestBody;
        requestBody["CardUid"] = uid;
        requestBody["PumpControllerUid"] = controllerUid_;
        
        // Make request
        nlohmann::json response = HttpRequestWrapper("/api/pump/authorize", "POST", requestBody, false);
        std::string responseError;
        if (IsErrorResponse(response, &responseError)) {
            LOG_BCK_ERROR("Authorization failed: {}", responseError);
            lastError_ = responseError;
            return false;
        }
        
        // Parse response - validate all fields before updating state
        if (!response.is_object()) {
            lastError_ = "Invalid response format";
            LOG_BCK_ERROR("{}", lastError_);
            return false;
        }
        
        // Extract token (required)
        if (!response.contains("Token") || !response["Token"].is_string()) {
            LOG_BCK_ERROR("{}", "Missing or invalid Token in response");
            lastError_ = StdBackendError;
            return false;
        }
        std::string token = response["Token"].get<std::string>();
        
        // Extract RoleId (required)
        if (!response.contains("RoleId") || !response["RoleId"].is_number_integer()) {
            LOG_BCK_ERROR("{}", "Missing or invalid RoleId in response");
            lastError_ = StdBackendError;
            return false;
        }
        int roleId = response["RoleId"].get<int>();
        
        // Extract Allowance (optional, can be null)
        double allowance = 0.0;
        if (response.contains("Allowance") && !response["Allowance"].is_null()) {
            allowance = response["Allowance"].get<double>();
        }
        
        // Extract Price (optional, can be null)
        double price = 0.0;
        if (response.contains("Price") && !response["Price"].is_null()) {
            price = response["Price"].get<double>();
        }
        
        // Extract fuelTanks (optional, can be null or array)
        std::vector<BackendTankInfo> fuelTanks;
        if (response.contains("fuelTanks") && response["fuelTanks"].is_array()) {
            for (const auto& tank : response["fuelTanks"]) {
                BackendTankInfo tankInfo;
                tankInfo.idTank = tank.value("idTank", 0);
                tankInfo.nameTank = tank.value("nameTank", "");
                fuelTanks.push_back(tankInfo);
            }
        }
        
        // All validations passed - update state atomically
        token_ = token;
        roleId_ = roleId;
        allowance_ = allowance;
        price_ = price;
        fuelTanks_ = fuelTanks;
        isAuthorized_ = true;
        lastError_.clear();
        currentUid_ = uid;
        
        LOG_BCK_INFO("Authorization successful: RoleId={}, Allowance={}, Price={}, Tanks={}",
                 roleId_, allowance_, price_, fuelTanks_.size());
        
        return true;
    } 
    catch (const std::exception& e) {
        LOG_BCK_ERROR("Authorization failed: {}", e.what());
        lastError_ = StdControllerError;
        return false;
    }
}

bool Backend::Deauthorize() {
    std::lock_guard<std::mutex> lock(backendMutex_);
    try {
        // Check if not authorized
        if (!isAuthorized_) {
            LOG_BCK_ERROR("{}", "Not authorized. Call Authorize first.");
            lastError_ = StdControllerError;
            return false;
        }
        
        LOG_BCK_INFO("Deauthorizing");
        
        // Prepare empty request body
        nlohmann::json requestBody = nlohmann::json::object();
        
        // Make request with bearer token
        nlohmann::json response = HttpRequestWrapper("/api/pump/deauthorize", "POST", requestBody, true);
        std::string responseError;
        if (IsErrorResponse(response, &responseError)) {
            // On failure, clear state anyway since we can't recover from this
            // The backend may or may not have deauthorized us, so it's safer to clear our state
            token_.clear();
            roleId_ = 0;
            allowance_ = 0.0;
            price_ = 0.0;
            fuelTanks_.clear();
            isAuthorized_ = false;
            currentUid_.clear();

            LOG_BCK_ERROR("Deauthorization failed: {} (state cleared for safety)", responseError);
            lastError_ = responseError;
            return false;
        }
        
        // Clear instance variables only after successful deauthorization
        token_.clear();
        roleId_ = 0;
        allowance_ = 0.0;
        price_ = 0.0;
        fuelTanks_.clear();
        isAuthorized_ = false;
        lastError_.clear();
        currentUid_.clear();
        
        LOG_BCK_INFO("Deauthorization successful");
        
        return true;
    } 
    catch (const std::exception& e) {
        // On failure, clear state anyway since we can't recover from this
        // The backend may or may not have deauthorized us, so it's safer to clear our state
        token_.clear();
        roleId_ = 0;
        allowance_ = 0.0;
        price_ = 0.0;
        fuelTanks_.clear();
        isAuthorized_ = false;
        currentUid_.clear();
        
        LOG_BCK_ERROR("Deauthorization failed: {} (state cleared for safety)", e.what());
        lastError_ = StdControllerError;
        return false;
    }
}


bool Backend::Refuel(TankNumber tankNumber, Volume volume) {
    return SendRefuelReport(tankNumber, volume, true);
}

bool Backend::Intake(TankNumber tankNumber, Volume volume, IntakeDirection direction) {
    return SendIntakeReport(tankNumber, volume, direction, true);
}

bool Backend::SendRefuelReport(TankNumber tankNumber, Volume volume, bool allowBacklog, std::int64_t timestampMs) {
    std::lock_guard<std::mutex> lock(backendMutex_);
    try {
        if (!isAuthorized_) {
            LOG_BCK_ERROR("Invalid refueling report: backend is not authorized");
            lastError_ = StdControllerError;
            return false;
        }

        if (roleId_ != static_cast<int>(UserRole::Customer)) {
            LOG_BCK_ERROR("Invalid refueling report: role {} is not allowed (expected Customer)", roleId_);
            lastError_ = StdControllerError;
            return false;
        }

        const auto tankIt = std::find_if(
            fuelTanks_.begin(),
            fuelTanks_.end(),
            [tankNumber](const BackendTankInfo& tank) { return tank.idTank == tankNumber; });
        if (tankIt == fuelTanks_.end()) {
            LOG_BCK_ERROR("Invalid refueling report: tank {} not found in authorized tanks", tankNumber);
            lastError_ = StdControllerError;
            return false;
        }

        if (volume < 0.0) {
            LOG_BCK_ERROR("Invalid refueling report: volume {} must be non-negative", volume);
            lastError_ = StdControllerError;
            return false;
        }

        if (volume > allowance_) {
            LOG_BCK_ERROR("Invalid refueling report: volume {} exceeds allowance {}", volume, allowance_);
            lastError_ = StdControllerError;
            return false;
        }

        std::int64_t actualTimestampMs;
        if (timestampMs < 0) {
            const auto now = std::chrono::system_clock::now();
            actualTimestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                         now.time_since_epoch())
                                         .count();
        } else {
            actualTimestampMs = timestampMs;
        }

        nlohmann::json requestBody;
        requestBody["TankNumber"] = tankNumber;
        requestBody["FuelVolume"] = volume;
        requestBody["TimeAt"] = actualTimestampMs;

        LOG_BCK_INFO("Refueling report: tank={}, volume={}, timestamp_ms={}", tankNumber, volume, actualTimestampMs);

        nlohmann::json response = HttpRequestWrapper("/api/pump/refuel", "POST", requestBody, true);
        std::string responseError;
        if (IsErrorResponse(response, &responseError)) {
            LOG_BCK_ERROR("Failed to send refueling report: {}", responseError);
            lastError_ = responseError;
            if (allowBacklog && IsNetworkErrorResponse(response)) {
                EnqueueBacklog(BacklogMethod::Refuel, requestBody);
            }
            return false;
        }

        // Decrease remaining allowance by the refueled volume after successful API call.
        allowance_ -= volume;
        if (allowance_ < 0.0) {
            allowance_ = 0.0;
        }
        lastError_.clear();
        LOG_BCK_INFO("Refueling report accepted");
        return true;
    } catch (const std::exception& e) {
        LOG_BCK_ERROR("Failed to send refueling report: {}", e.what());
        if (lastError_.empty()) {
            lastError_ = StdBackendError;
        }
        return false;
    }
}

bool Backend::SendIntakeReport(TankNumber tankNumber, Volume volume, IntakeDirection direction, bool allowBacklog, std::int64_t timestampMs) {
    std::lock_guard<std::mutex> lock(backendMutex_);
    try {
        if (!isAuthorized_) {
            LOG_BCK_ERROR("Invalid intake report: backend is not authorized");
            lastError_ = StdControllerError;
            return false;
        }

        if (roleId_ != static_cast<int>(UserRole::Operator)) {
            LOG_BCK_ERROR("Invalid intake report: role {} is not allowed (expected Operator)", roleId_);
            lastError_ = StdControllerError;
            return false;
        }

        const auto tankIt = std::find_if(
            fuelTanks_.begin(),
            fuelTanks_.end(),
            [tankNumber](const BackendTankInfo& tank) { return tank.idTank == tankNumber; });
        if (tankIt == fuelTanks_.end()) {
            LOG_BCK_ERROR("Invalid intake report: tank {} not found in authorized tanks", tankNumber);
            lastError_ = StdControllerError;
            return false;
        }

        if (volume < 0.0) {
            LOG_BCK_ERROR("Invalid intake report: volume {} must be non-negative", volume);
            lastError_ = StdControllerError;
            return false;
        }

        if (direction != IntakeDirection::In && direction != IntakeDirection::Out) {
            LOG_BCK_ERROR("Invalid intake report: direction {} is not supported", static_cast<int>(direction));
            lastError_ = StdControllerError;
            return false;
        }

        std::int64_t actualTimestampMs;
        if (timestampMs < 0) {
            const auto now = std::chrono::system_clock::now();
            actualTimestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                         now.time_since_epoch())
                                         .count();
        } else {
            actualTimestampMs = timestampMs;
        }

        nlohmann::json requestBody;
        requestBody["TankNumber"] = tankNumber;
        requestBody["IntakeVolume"] = volume;
        requestBody["Direction"] = static_cast<int>(direction);
        requestBody["TimeAt"] = actualTimestampMs;

        LOG_BCK_INFO("Fuel intake report: tank={}, volume={}, direction={}, timestamp_ms={}",
                 tankNumber,
                 volume,
                 static_cast<int>(direction),
                 actualTimestampMs);

        nlohmann::json response = HttpRequestWrapper("/api/pump/fuel-intake", "POST", requestBody, true);
        std::string responseError;
        if (IsErrorResponse(response, &responseError)) {
            LOG_BCK_ERROR("Failed to send fuel intake report: {}", responseError);
            lastError_ = responseError;
            if (allowBacklog && IsNetworkErrorResponse(response)) {
                EnqueueBacklog(BacklogMethod::Intake, requestBody);
            }
            return false;
        }

        lastError_.clear();
        LOG_BCK_INFO("Fuel intake report accepted");
        return true;
    } catch (const std::exception& e) {
        LOG_BCK_ERROR("Failed to send fuel intake report: {}", e.what());
        if (lastError_.empty()) {
            lastError_ = StdBackendError;
        }
        return false;
    }
}

bool Backend::EnqueueBacklog(BacklogMethod method, const nlohmann::json& payload) {
    if (currentUid_.empty()) {
        LOG_BCK_WARN("Backlog enqueue skipped: no current UID available");
        return false;
    }
    if (!backlogStore_.Enqueue(currentUid_, method, payload.dump())) {
        LOG_BCK_ERROR("Failed to enqueue backlog item for uid {}", currentUid_);
        return false;
    }
    LOG_BCK_INFO("Backlog item queued for uid {}", currentUid_);
    return true;
}

bool Backend::ProcessBacklogOnce() {
    {
        std::lock_guard<std::mutex> lock(backendMutex_);
        if (isAuthorized_) {
            LOG_BCK_DEBUG("Backlog processing skipped: backend is authorized");
            return false;
        }
    }
    auto items = backlogStore_.FetchAll();
    if (items.empty()) {
        return false;
    }
    for (const auto& item : items) {
        nlohmann::json payload;
        try {
            payload = nlohmann::json::parse(item.data);
        } catch (const std::exception& e) {
            LOG_BCK_ERROR("Failed to parse backlog payload {}, removing item: {}", item.id, e.what());
            backlogStore_.Remove(item.id);
            continue;
        }

        if (!Authorize(item.uid)) {
            LOG_BCK_WARN("Backlog processing failed to authorize uid {}", item.uid);
            return false;
        }

        bool actionOk = false;
        if (item.method == BacklogMethod::Refuel) {
            TankNumber tankNumber = payload.value("TankNumber", -1);
            Volume volume = payload.value("FuelVolume", -1.0);
            std::int64_t timeAt = payload.value("TimeAt", -1);
            actionOk = SendRefuelReport(tankNumber, volume, false, timeAt);
        } else if (item.method == BacklogMethod::Intake) {
            TankNumber tankNumber = payload.value("TankNumber", -1);
            Volume volume = payload.value("IntakeVolume", -1.0);
            IntakeDirection direction = static_cast<IntakeDirection>(payload.value("Direction", 0));
            std::int64_t timeAt = payload.value("TimeAt", -1);
            actionOk = SendIntakeReport(tankNumber, volume, direction, false, timeAt);
        }

        bool deauthOk = Deauthorize();
        if (actionOk && deauthOk) {
            backlogStore_.Remove(item.id);
            LOG_BCK_INFO("Backlog item {} processed successfully", item.id);
        } else {
            LOG_BCK_WARN("Backlog item {} processing failed, will retry later", item.id);
            return false;
        }
    }
    return true;
}

void Backend::BacklogWorkerLoop() {
    std::unique_lock<std::mutex> lock(backlogCvMutex_);
    while (backlogRunning_) {
        lock.unlock();
        ProcessBacklogOnce();
        lock.lock();
        backlogCv_.wait_for(lock, backlogInterval_, [this]() { return !backlogRunning_; });
    }
}

void Backend::StartBacklogWorker() {
    std::lock_guard<std::mutex> lock(backlogCvMutex_);
    if (backlogRunning_) {
        return;
    }
    backlogRunning_ = true;
    backlogThread_ = std::thread([this]() { BacklogWorkerLoop(); });
}

void Backend::StopBacklogWorker() {
    std::lock_guard<std::mutex> lock(backlogCvMutex_);
    if (!backlogRunning_) {
        return;
    }
    backlogRunning_ = false;
    backlogCv_.notify_all();
    if (backlogThread_.joinable()) {
        backlogThread_.join();
    }
}

} // namespace fuelflux
