// Copyright(C) 2025 Maxim[maxirmx] Samsonov(www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "backend.h"
#include "logger.h"
#include <httplib.h>
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <sstream>

namespace fuelflux {

// Поскольку контроллер обладает крайне ограниченными возможностями по выводу текстовых сообщений,
// и наличие квалифициированного персонала рядом не предполагается, мы ограничимся двумя универсальными
// сообщениями об ошибке.
const std::string StdControllerError = "Ошибка контроллера";
const std::string StdBackendError = "Ошибка портала";
const std::string UnknownBackendError = "Неизвестная ошибка";

Backend::Backend(const std::string& baseAPI, const std::string& controllerUid) : 
    baseAPI_(baseAPI),
    controllerUid_(controllerUid),
    isAuthorized_(false),
    roleId_(0),
    allowance_(0.0),
    price_(0.0),
    lastErrorCode_(0)
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
}

Backend::~Backend() {
    if (isAuthorized_) {
        LOG_BCK_WARN("Backend destroyed while still authorized");
    }
}

bool Backend::HttpRequestWrapper(const std::string& endpoint, 
                                 const std::string& method,
                                 const nlohmann::json& requestBody,
                                 nlohmann::json& responseOut,
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
        size_t portPos = urlToParse.find(':');
        size_t pathPos = urlToParse.find('/');
        
        if (portPos != std::string::npos && (pathPos == std::string::npos || portPos < pathPos)) {
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
        else if (pathPos != std::string::npos) {
            host = urlToParse.substr(0, pathPos);
        } 
        else {
            host = urlToParse;
        }
        
        LOG_BCK_DEBUG("Connecting to {}:{} for endpoint: {} (scheme={})", host, port, endpoint, scheme);
        
        // Prepare headers
        httplib::Headers headers = {
            {"Content-Type", "application/json"}
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

            if (method == "POST") {
                return client.Post(endpoint.c_str(), headers, bodyStr, "application/json");
            } 
            else if (method == "GET") {
                return client.Get(endpoint.c_str(), headers);
            } 
            else {
                throw std::runtime_error("Unsupported HTTP method: " + method);
            }
        };

        httplib::Result res;
        if (scheme == "https") {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            httplib::SSLClient sslClient(host.c_str(), port);
            // By default SSL verification is enabled. To customize CA path/cert use sslClient.set_ca_cert_file(...)
            res = doRequest(sslClient);
#else
            std::string err = "HTTPS requested but cpp-httplib built without OpenSSL support";
            throw std::runtime_error(err);
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
            throw std::runtime_error(errorMsg);
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
                    throw std::runtime_error("Failed to parse response JSON: " + std::string(e.what()));
                }
            }
            
            // Check for application-level errors (200 OK but with error in JSON)
            if (responseJson.is_object() && responseJson.contains("CodeError")) {
                int errorCode = responseJson["CodeError"].get<int>();
                if (errorCode != 0) {
                    // Backend reported an error - handle gracefully
                    lastErrorCode_ = errorCode;
                    lastError_ = responseJson.value("TextError", UnknownBackendError);
                    return false;
                }
            }
            
            // Success - return the parsed response
            responseOut = responseJson;
            return true;
        } 
        else {
            // HTTP error response
            std::ostringstream oss;
            oss << "System error, code " << res->status;
            std::string errorText = oss.str();
            
            throw std::runtime_error(errorText);
        }
    } 
    catch (const std::exception& e) {
        // Internal/unexpected errors - set error state and re-throw
        if (lastError_.empty()) {
            lastError_ = e.what();
        }
        if (lastErrorCode_ == 0) {
            lastErrorCode_ = -1;
        }
        throw;
    }
}

bool Backend::Authorize(const std::string& uid) {
    try {
        // Check if already authorized
        if (isAuthorized_) {
            LOG_BCK_ERROR("{}", "Already authorized. Call Deauthorize first.");
            lastError_ = StdControllerError;
            lastErrorCode_ = -1;
            return false;
        }
        
        LOG_BCK_INFO("Authorizing card UID: {}", uid);
        
        // Prepare request body
        nlohmann::json requestBody;
        requestBody["CardUid"] = uid;
        requestBody["PumpControllerUid"] = controllerUid_;
        
        // Make request
        nlohmann::json response;
        if (!HttpRequestWrapper("/api/pump/authorize", "POST", requestBody, response, false)) {
            // Backend reported an error - already logged in lastError_ and lastErrorCode_
            LOG_BCK_ERROR("Authorization failed with backend error code {}: {}", lastErrorCode_, lastError_);
            return false;
        }
        
        // Parse response - validate all fields before updating state
        if (!response.is_object()) {
            lastError_ = "Invalid response format";
            lastErrorCode_ = -1;
            LOG_BCK_ERROR("{}", lastError_);
            return false;
        }
        
        // Extract token (required)
        if (!response.contains("Token") || !response["Token"].is_string()) {
            LOG_BCK_ERROR("{}", "Missing or invalid Token in response");
            lastError_ = StdBackendError;
            lastErrorCode_ = -1;
            return false;
        }
        std::string token = response["Token"].get<std::string>();
        
        // Extract RoleId (required)
        if (!response.contains("RoleId") || !response["RoleId"].is_number_integer()) {
            LOG_BCK_ERROR("{}", "Missing or invalid RoleId in response");
            lastError_ = StdBackendError;
            lastErrorCode_ = -1;
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
        lastErrorCode_ = 0;
        
        LOG_BCK_INFO("Authorization successful: RoleId={}, Allowance={}, Price={}, Tanks={}",
                 roleId_, allowance_, price_, fuelTanks_.size());
        
        return true;
    } 
    catch (const std::exception& e) {
        LOG_BCK_ERROR("Authorization failed: {}", e.what());
        if (lastError_.empty()) {
            lastError_ = StdControllerError;
        }
        if (lastErrorCode_ == 0) {
            lastErrorCode_ = -1;
        }
        return false;
    }
}

bool Backend::Deauthorize() {
    try {
        // Check if not authorized
        if (!isAuthorized_) {
            LOG_BCK_ERROR("{}", "Not authorized. Call Authorize first.");
            lastError_ = StdControllerError;
            lastErrorCode_ = -1;
            return false;
        }
        
        LOG_BCK_INFO("Deauthorizing");
        
        // Prepare empty request body
        nlohmann::json requestBody = nlohmann::json::object();
        
        // Make request with bearer token
        nlohmann::json response;
        if (!HttpRequestWrapper("/api/pump/deauthorize", "POST", requestBody, response, true)) {
            // Backend reported an error - already logged in lastError_ and lastErrorCode_
            LOG_BCK_ERROR("Deauthorization failed with backend error code {}: {}", lastErrorCode_, lastError_);
            // On failure, clear state anyway since we can't recover from this
            // The backend may or may not have deauthorized us, so it's safer to clear our state
            token_.clear();
            roleId_ = 0;
            allowance_ = 0.0;
            price_ = 0.0;
            fuelTanks_.clear();
            isAuthorized_ = false;
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
        lastErrorCode_ = 0;
        
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
        
        LOG_BCK_ERROR("Deauthorization failed: {} (state cleared for safety)", e.what());
        if (lastError_.empty()) {
            lastError_ = StdControllerError;
        }
        if (lastErrorCode_ == 0) {
            lastErrorCode_ = -1;
        }
        return false;
    }
}


bool Backend::Refuel(TankNumber tankNumber, Volume volume) {
    try {
        if (!isAuthorized_) {
            LOG_BCK_ERROR("Invalid refueling report: backend is not authorized");
            lastError_ = StdControllerError;
            lastErrorCode_ = -1;
            return false;
        }

        if (roleId_ != static_cast<int>(UserRole::Customer)) {
            LOG_BCK_ERROR("Invalid refueling report: role {} is not allowed (expected Customer)", roleId_);
            lastError_ = StdControllerError;
            lastErrorCode_ = -1;
            return false;
        }

        const auto tankIt = std::find_if(
            fuelTanks_.begin(),
            fuelTanks_.end(),
            [tankNumber](const BackendTankInfo& tank) { return tank.idTank == tankNumber; });
        if (tankIt == fuelTanks_.end()) {
            LOG_BCK_ERROR("Invalid refueling report: tank {} not found in authorized tanks", tankNumber);
            lastError_ = StdControllerError;
            lastErrorCode_ = -1;
            return false;
        }

        if (volume < 0.0) {
            LOG_BCK_ERROR("Invalid refueling report: volume {} must be non-negative", volume);
            lastError_ = StdControllerError;
            lastErrorCode_ = -1;
            return false;
        }

        if (volume > allowance_) {
            LOG_BCK_ERROR("Invalid refueling report: volume {} exceeds allowance {}", volume, allowance_);
            lastError_ = StdControllerError;
            lastErrorCode_ = -1;
            return false;
        }

        const auto now = std::chrono::system_clock::now();
        const auto timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     now.time_since_epoch())
                                     .count();

        nlohmann::json requestBody;
        requestBody["TankNumber"] = tankNumber;
        requestBody["FuelVolume"] = volume;
        requestBody["TimeAt"] = timestampMs;

        LOG_BCK_INFO("Refueling report: tank={}, volume={}, timestamp_ms={}", tankNumber, volume, timestampMs);

        nlohmann::json response;
        if (!HttpRequestWrapper("/api/pump/refuel", "POST", requestBody, response, true)) {
            // Backend reported an error - already logged in lastError_ and lastErrorCode_
            LOG_BCK_ERROR("Refuel failed with backend error code {}: {}", lastErrorCode_, lastError_);
            return false;
        }

        // Decrease remaining allowance by the refueled volume after successful API call.
        allowance_ -= volume;
        if (allowance_ < 0.0) {
            allowance_ = 0.0;
        }
        lastError_.clear();
        lastErrorCode_ = 0;
        LOG_BCK_INFO("Refueling report accepted");
        return true;
    } catch (const std::exception& e) {
        LOG_BCK_ERROR("Failed to send refueling report: {}", e.what());
        if (lastError_.empty()) {
            lastError_ = StdBackendError;
        }
        if (lastErrorCode_ == 0) {
            lastErrorCode_ = -1;
        }
        return false;
    }
}

bool Backend::Intake(TankNumber tankNumber, Volume volume, IntakeDirection direction) {
    try {
        if (!isAuthorized_) {
            LOG_BCK_ERROR("Invalid intake report: backend is not authorized");
            lastError_ = StdControllerError;
            lastErrorCode_ = -1;
            return false;
        }

        if (roleId_ != static_cast<int>(UserRole::Operator)) {
            LOG_BCK_ERROR("Invalid intake report: role {} is not allowed (expected Operator)", roleId_);
            lastError_ = StdControllerError;
            lastErrorCode_ = -1;
            return false;
        }

        const auto tankIt = std::find_if(
            fuelTanks_.begin(),
            fuelTanks_.end(),
            [tankNumber](const BackendTankInfo& tank) { return tank.idTank == tankNumber; });
        if (tankIt == fuelTanks_.end()) {
            LOG_BCK_ERROR("Invalid intake report: tank {} not found in authorized tanks", tankNumber);
            lastError_ = StdControllerError;
            lastErrorCode_ = -1;
            return false;
        }

        if (volume < 0.0) {
            LOG_BCK_ERROR("Invalid intake report: volume {} must be non-negative", volume);
            lastError_ = StdControllerError;
            lastErrorCode_ = -1;
            return false;
        }

        if (direction != IntakeDirection::In && direction != IntakeDirection::Out) {
            LOG_BCK_ERROR("Invalid intake report: direction {} is not supported", static_cast<int>(direction));
            lastError_ = StdControllerError;
            lastErrorCode_ = -1;
            return false;
        }

        const auto now = std::chrono::system_clock::now();
        const auto timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     now.time_since_epoch())
                                     .count();

        nlohmann::json requestBody;
        requestBody["TankNumber"] = tankNumber;
        requestBody["IntakeVolume"] = volume;
        requestBody["Direction"] = static_cast<int>(direction);
        requestBody["TimeAt"] = timestampMs;

        LOG_BCK_INFO("Fuel intake report: tank={}, volume={}, direction={}, timestamp_ms={}",
                 tankNumber,
                 volume,
                 static_cast<int>(direction),
                 timestampMs);

        nlohmann::json response;
        if (!HttpRequestWrapper("/api/pump/fuel-intake", "POST", requestBody, response, true)) {
            // Backend reported an error - already logged in lastError_ and lastErrorCode_
            LOG_BCK_ERROR("Intake failed with backend error code {}: {}", lastErrorCode_, lastError_);
            return false;
        }

        lastError_.clear();
        lastErrorCode_ = 0;
        LOG_BCK_INFO("Fuel intake report accepted");
        return true;
    } catch (const std::exception& e) {
        LOG_BCK_ERROR("Failed to send fuel intake report: {}", e.what());
        if (lastError_.empty()) {
            lastError_ = StdBackendError;
        }
        if (lastErrorCode_ == 0) {
            lastErrorCode_ = -1;
        }
        return false;
    }
}

} // namespace fuelflux
