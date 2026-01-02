#include "backend.h"
#include "logger.h"
#include <httplib.h>
#include <stdexcept>
#include <sstream>

namespace fuelflux {

Backend::Backend(const std::string& baseAPI, const std::string& controllerUid)
    : baseAPI_(baseAPI)
    , controllerUid_(controllerUid)
    , isAuthorized_(false)
    , roleId_(0)
    , allowance_(0.0)
    , price_(0.0)
{
    LOG_INFO("Backend initialized with base API: {} and controller UID: {}", baseAPI_, controllerUid_);
}

Backend::~Backend() {
    if (isAuthorized_) {
        LOG_WARN("Backend destroyed while still authorized");
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
        size_t portPos = urlToParse.find(':');
        size_t pathPos = urlToParse.find('/');
        
        if (portPos != std::string::npos && (pathPos == std::string::npos || portPos < pathPos)) {
            host = urlToParse.substr(0, portPos);
            std::string portStr;
            if (pathPos != std::string::npos) {
                portStr = urlToParse.substr(portPos + 1, pathPos - portPos - 1);
            } else {
                portStr = urlToParse.substr(portPos + 1);
            }
            port = std::stoi(portStr);
        } else if (pathPos != std::string::npos) {
            host = urlToParse.substr(0, pathPos);
        } else {
            host = urlToParse;
        }
        
        LOG_DEBUG("Connecting to {}:{} for endpoint: {}", host, port, endpoint);
        
        // Create HTTP client
        httplib::Client client(host, port);
        client.set_connection_timeout(5, 0); // 5 seconds
        client.set_read_timeout(10, 0);      // 10 seconds
        client.set_write_timeout(10, 0);     // 10 seconds
        
        // Prepare headers
        httplib::Headers headers = {
            {"Content-Type", "application/json"}
        };
        
        if (useBearerToken && !token_.empty()) {
            headers.insert({"Authorization", "Bearer " + token_});
        }
        
        // Prepare request body
        std::string bodyStr = requestBody.dump();
        
        LOG_DEBUG("Request: {} {} with body: {}", method, endpoint, bodyStr);
        
        // Make request
        httplib::Result res;
        if (method == "POST") {
            res = client.Post(endpoint.c_str(), headers, bodyStr, "application/json");
        } else if (method == "GET") {
            res = client.Get(endpoint.c_str(), headers);
        } else {
            throw std::runtime_error("Unsupported HTTP method: " + method);
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
            LOG_ERROR("{}", errorMsg);
            throw std::runtime_error(errorMsg);
        }
        
        LOG_DEBUG("Response status: {} body: {}", res->status, res->body);
        
        // Check HTTP status code
        if (res->status >= 200 && res->status < 300) {
            // Success response
            nlohmann::json responseJson;
            
            // Handle empty or "null" response
            if (res->body.empty() || res->body == "null") {
                responseJson = nlohmann::json(nullptr);
            } else {
                try {
                    responseJson = nlohmann::json::parse(res->body);
                } catch (const std::exception& e) {
                    LOG_ERROR("Failed to parse response JSON: {}", e.what());
                    throw std::runtime_error("Failed to parse response JSON: " + std::string(e.what()));
                }
            }
            
            // Check for application-level errors (200 OK but with error in JSON)
            if (responseJson.is_object() && responseJson.contains("CodeError")) {
                int errorCode = responseJson["CodeError"].get<int>();
                if (errorCode != 0) {
                    std::string errorText = responseJson.value("TextError", "Unknown error");
                    LOG_ERROR("Backend returned error: Code={}, Text={}", errorCode, errorText);
                    
                    // Create error JSON
                    nlohmann::json errorJson;
                    errorJson["CodeError"] = errorCode;
                    errorJson["TextError"] = errorText;
                    throw std::runtime_error("Backend error: " + errorText);
                }
            }
            
            return responseJson;
        } else {
            // HTTP error response
            std::ostringstream oss;
            oss << "System error, code " << res->status;
            std::string errorText = oss.str();
            
            LOG_ERROR("HTTP error: {}", errorText);
            
            // Create error JSON
            nlohmann::json errorJson;
            errorJson["CodeError"] = -1;
            errorJson["TextError"] = errorText;
            
            throw std::runtime_error(errorText);
        }
    } catch (const std::exception& e) {
        LOG_ERROR("HttpRequestWrapper exception: {}", e.what());
        lastError_ = e.what();
        throw;
    }
}

bool Backend::Authorize(const std::string& uid) {
    try {
        // Check if already authorized
        if (isAuthorized_) {
            lastError_ = "Already authorized. Call Deauthorize first.";
            LOG_ERROR("{}", lastError_);
            return false;
        }
        
        LOG_INFO("Authorizing card UID: {}", uid);
        
        // Prepare request body
        nlohmann::json requestBody;
        requestBody["CardUid"] = uid;
        requestBody["PumpControllerUid"] = controllerUid_;
        
        // Make request
        nlohmann::json response = HttpRequestWrapper("/api/pump/authorize", "POST", requestBody, false);
        
        // Parse response - validate all fields before updating state
        if (!response.is_object()) {
            lastError_ = "Invalid response format";
            LOG_ERROR("{}", lastError_);
            return false;
        }
        
        // Extract token (required)
        if (!response.contains("Token") || !response["Token"].is_string()) {
            lastError_ = "Missing or invalid Token in response";
            LOG_ERROR("{}", lastError_);
            return false;
        }
        std::string token = response["Token"].get<std::string>();
        
        // Extract RoleId (required)
        if (!response.contains("RoleId") || !response["RoleId"].is_number_integer()) {
            lastError_ = "Missing or invalid RoleId in response";
            LOG_ERROR("{}", lastError_);
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
        
        LOG_INFO("Authorization successful: RoleId={}, Allowance={}, Price={}, Tanks={}", 
                 roleId_, allowance_, price_, fuelTanks_.size());
        
        return true;
    } catch (const std::exception& e) {
        lastError_ = e.what();
        LOG_ERROR("Authorization failed: {}", lastError_);
        return false;
    }
}

bool Backend::Deauthorize() {
    try {
        // Check if not authorized
        if (!isAuthorized_) {
            lastError_ = "Not authorized. Call Authorize first.";
            LOG_ERROR("{}", lastError_);
            return false;
        }
        
        LOG_INFO("Deauthorizing");
        
        // Prepare empty request body
        nlohmann::json requestBody = nlohmann::json::object();
        
        // Make request with bearer token
        nlohmann::json response = HttpRequestWrapper("/api/pump/deauthorize", "POST", requestBody, true);
        
        // Clear instance variables only after successful deauthorization
        token_.clear();
        roleId_ = 0;
        allowance_ = 0.0;
        price_ = 0.0;
        fuelTanks_.clear();
        isAuthorized_ = false;
        lastError_.clear();
        
        LOG_INFO("Deauthorization successful");
        
        return true;
    } catch (const std::exception& e) {
        // On failure, clear state anyway since we can't recover from this
        // The backend may or may not have deauthorized us, so it's safer to clear our state
        token_.clear();
        roleId_ = 0;
        allowance_ = 0.0;
        price_ = 0.0;
        fuelTanks_.clear();
        isAuthorized_ = false;
        
        lastError_ = e.what();
        LOG_ERROR("Deauthorization failed: {} (state cleared for safety)", lastError_);
        return false;
    }
}

} // namespace fuelflux
