#include "backend.h"
#include "logger.h"
#include <regex>
#include <sstream>

namespace fuelflux {

Backend::Backend(const std::string& baseAPI, const std::string& controllerUid)
    : baseAPI_(baseAPI)
    , controllerUid_(controllerUid)
    , port_(80)
    , scheme_("http")
    , isConnected_(false)
{
    if (!parseBaseAPI(baseAPI)) {
        LOG_CLOUD_ERROR("Failed to parse base API URL: {}", baseAPI);
    }
}

bool Backend::parseBaseAPI(const std::string& baseAPI) {
    // Parse URL in format: http://host:port or https://host:port or http://host
    std::regex urlPattern(R"(^(https?)://([^:/]+)(?::(\d+))?(.*)$)");
    std::smatch matches;
    
    if (std::regex_match(baseAPI, matches, urlPattern)) {
        scheme_ = matches[1].str();
        host_ = matches[2].str();
        
        if (matches[3].matched) {
            port_ = std::stoi(matches[3].str());
        } else {
            port_ = (scheme_ == "https") ? 443 : 80;
        }
        
        LOG_CLOUD_DEBUG("Parsed API URL - Scheme: {}, Host: {}, Port: {}", 
                       scheme_, host_, port_);
        return true;
    }
    
    return false;
}

bool Backend::initialize() {
    try {
        // Create HTTP client
        if (scheme_ == "https") {
            httpClient_ = std::make_unique<httplib::Client>(host_, port_);
            httpClient_->enable_server_certificate_verification(false);
        } else {
            httpClient_ = std::make_unique<httplib::Client>(host_, port_);
        }
        
        // Set timeout
        httpClient_->set_connection_timeout(5, 0); // 5 seconds
        httpClient_->set_read_timeout(10, 0);      // 10 seconds
        httpClient_->set_write_timeout(10, 0);     // 10 seconds
        
        isConnected_ = true;
        LOG_CLOUD_INFO("Backend initialized: {}://{}:{}", scheme_, host_, port_);
        return true;
    } catch (const std::exception& e) {
        LOG_CLOUD_ERROR("Failed to initialize backend: {}", e.what());
        isConnected_ = false;
        return false;
    }
}

void Backend::shutdown() {
    httpClient_.reset();
    isConnected_ = false;
    LOG_CLOUD_INFO("Backend shutdown");
}

bool Backend::isConnected() const {
    return isConnected_;
}

nlohmann::json Backend::HttpRequestWrapper(
    const std::string& method,
    const std::string& path,
    const nlohmann::json& body
) {
    if (!httpClient_) {
        nlohmann::json errorResponse;
        errorResponse["CodeError"] = -1;
        errorResponse["TextError"] = "HTTP client not initialized";
        return errorResponse;
    }

    try {
        httplib::Result res;
        std::string bodyStr = body.dump();
        
        LOG_CLOUD_DEBUG("HTTP {} {} - Body: {}", method, path, bodyStr);
        
        httplib::Headers headers = {
            {"Content-Type", "application/json"},
            {"Accept", "application/json"}
        };
        
        if (method == "GET") {
            res = httpClient_->Get(path.c_str(), headers);
        } else if (method == "POST") {
            res = httpClient_->Post(path.c_str(), headers, bodyStr, "application/json");
        } else if (method == "PUT") {
            res = httpClient_->Put(path.c_str(), headers, bodyStr, "application/json");
        } else if (method == "DELETE") {
            res = httpClient_->Delete(path.c_str(), headers);
        } else {
            nlohmann::json errorResponse;
            errorResponse["CodeError"] = -1;
            errorResponse["TextError"] = "Unsupported HTTP method: " + method;
            return errorResponse;
        }
        
        // Check if request failed at HTTP level
        if (!res) {
            nlohmann::json errorResponse;
            errorResponse["CodeError"] = -1;
            auto err = res.error();
            errorResponse["TextError"] = "Системная ошибка, код " + std::to_string(static_cast<int>(err));
            LOG_CLOUD_ERROR("HTTP request failed: {}", httplib::to_string(err));
            return errorResponse;
        }
        
        // Check HTTP status code
        if (res->status != 200) {
            nlohmann::json errorResponse;
            errorResponse["CodeError"] = -1;
            errorResponse["TextError"] = "Системная ошибка, код " + std::to_string(res->status);
            LOG_CLOUD_ERROR("HTTP request returned status {}: {}", res->status, res->body);
            return errorResponse;
        }
        
        // Parse response body
        nlohmann::json responseJson;
        try {
            responseJson = nlohmann::json::parse(res->body);
        } catch (const nlohmann::json::parse_error& e) {
            nlohmann::json errorResponse;
            errorResponse["CodeError"] = -1;
            errorResponse["TextError"] = "Failed to parse JSON response";
            LOG_CLOUD_ERROR("JSON parse error: {}", e.what());
            return errorResponse;
        }
        
        // Check if response contains error fields
        if (responseJson.contains("CodeError")) {
            int codeError = responseJson["CodeError"].get<int>();
            if (codeError != 0) {
                LOG_CLOUD_WARN("Backend returned error: CodeError={}, TextError={}", 
                             codeError, 
                             responseJson.value("TextError", "Unknown error"));
            }
        }
        
        return responseJson;
        
    } catch (const std::exception& e) {
        nlohmann::json errorResponse;
        errorResponse["CodeError"] = -1;
        errorResponse["TextError"] = "Exception: " + std::string(e.what());
        LOG_CLOUD_ERROR("Exception in HttpRequestWrapper: {}", e.what());
        return errorResponse;
    }
}

std::future<AuthResponse> Backend::authorizeUser(
    const ControllerId& controllerId, 
    const UserId& userId
) {
    return std::async(std::launch::async, [this, controllerId, userId]() -> AuthResponse {
        LOG_CLOUD_INFO("Authorizing user: {} for controller: {}", userId, controllerId);
        
        AuthResponse response;
        
        if (!isConnected_) {
            response.success = false;
            response.errorMessage = "Backend service not connected";
            return response;
        }
        
        // Build request body
        nlohmann::json requestBody;
        requestBody["controllerId"] = controllerId;
        requestBody["userId"] = userId;
        
        // Make API call
        std::string path = "/api/auth/authorize";
        nlohmann::json apiResponse = HttpRequestWrapper("POST", path, requestBody);
        
        // Check for errors
        if (apiResponse.contains("CodeError") && apiResponse["CodeError"].get<int>() != 0) {
            response.success = false;
            response.errorMessage = apiResponse.value("TextError", "Authorization failed");
            LOG_CLOUD_ERROR("Authorization failed: {}", response.errorMessage);
            return response;
        }
        
        // Parse successful response
        response.success = apiResponse.value("success", false);
        
        if (response.success && apiResponse.contains("userInfo")) {
            auto& userJson = apiResponse["userInfo"];
            response.userInfo.uid = userJson.value("uid", "");
            
            // Parse role
            std::string roleStr = userJson.value("role", "unknown");
            if (roleStr == "operator") {
                response.userInfo.role = UserRole::Operator;
            } else if (roleStr == "customer") {
                response.userInfo.role = UserRole::Customer;
            } else if (roleStr == "controller") {
                response.userInfo.role = UserRole::Controller;
            } else {
                response.userInfo.role = UserRole::Unknown;
            }
            
            response.userInfo.allowance = userJson.value("allowance", 0.0);
            response.userInfo.price = userJson.value("price", 0.0);
        }
        
        if (response.success && apiResponse.contains("tanks")) {
            for (auto& tankJson : apiResponse["tanks"]) {
                TankInfo tank;
                tank.number = tankJson.value("number", 0);
                tank.capacity = tankJson.value("capacity", 0.0);
                tank.currentVolume = tankJson.value("currentVolume", 0.0);
                tank.fuelType = tankJson.value("fuelType", "");
                response.tanks.push_back(tank);
            }
        }
        
        LOG_CLOUD_INFO("Authorization result: {}", response.success ? "success" : "failed");
        return response;
    });
}

std::future<bool> Backend::reportRefuelTransaction(const RefuelTransaction& transaction) {
    return std::async(std::launch::async, [this, transaction]() -> bool {
        LOG_CLOUD_INFO("Reporting refuel transaction: User={}, Tank={}, Volume={}L, Amount={} RUB",
                      transaction.userId, transaction.tankNumber, transaction.volume, transaction.totalAmount);
        
        if (!isConnected_) {
            LOG_CLOUD_ERROR("Failed to report transaction - not connected");
            return false;
        }
        
        // Build request body
        nlohmann::json requestBody;
        requestBody["controllerId"] = controllerUid_;
        requestBody["userId"] = transaction.userId;
        requestBody["tankNumber"] = transaction.tankNumber;
        requestBody["volume"] = transaction.volume;
        requestBody["totalAmount"] = transaction.totalAmount;
        
        // Convert timestamp to string (ISO 8601 format)
        auto timeT = std::chrono::system_clock::to_time_t(transaction.timestamp);
        std::stringstream ss;
        ss << std::put_time(std::gmtime(&timeT), "%Y-%m-%dT%H:%M:%SZ");
        requestBody["timestamp"] = ss.str();
        
        // Make API call
        std::string path = "/api/transactions/refuel";
        nlohmann::json apiResponse = HttpRequestWrapper("POST", path, requestBody);
        
        // Check for errors
        if (apiResponse.contains("CodeError") && apiResponse["CodeError"].get<int>() != 0) {
            LOG_CLOUD_ERROR("Failed to report transaction: {}", 
                          apiResponse.value("TextError", "Unknown error"));
            return false;
        }
        
        bool success = apiResponse.value("success", false);
        LOG_CLOUD_INFO("Transaction reported: {}", success ? "success" : "failed");
        return success;
    });
}

std::future<bool> Backend::reportIntakeTransaction(const IntakeTransaction& transaction) {
    return std::async(std::launch::async, [this, transaction]() -> bool {
        LOG_CLOUD_INFO("Reporting intake transaction: Operator={}, Tank={}, Volume={}L",
                      transaction.operatorId, transaction.tankNumber, transaction.volume);
        
        if (!isConnected_) {
            LOG_CLOUD_ERROR("Failed to report transaction - not connected");
            return false;
        }
        
        // Build request body
        nlohmann::json requestBody;
        requestBody["controllerId"] = controllerUid_;
        requestBody["operatorId"] = transaction.operatorId;
        requestBody["tankNumber"] = transaction.tankNumber;
        requestBody["volume"] = transaction.volume;
        
        // Convert timestamp to string (ISO 8601 format)
        auto timeT = std::chrono::system_clock::to_time_t(transaction.timestamp);
        std::stringstream ss;
        ss << std::put_time(std::gmtime(&timeT), "%Y-%m-%dT%H:%M:%SZ");
        requestBody["timestamp"] = ss.str();
        
        // Make API call
        std::string path = "/api/transactions/intake";
        nlohmann::json apiResponse = HttpRequestWrapper("POST", path, requestBody);
        
        // Check for errors
        if (apiResponse.contains("CodeError") && apiResponse["CodeError"].get<int>() != 0) {
            LOG_CLOUD_ERROR("Failed to report intake: {}", 
                          apiResponse.value("TextError", "Unknown error"));
            return false;
        }
        
        bool success = apiResponse.value("success", false);
        LOG_CLOUD_INFO("Intake transaction reported: {}", success ? "success" : "failed");
        return success;
    });
}

std::future<std::vector<UserInfo>> Backend::getUserList(int first, int count) {
    return std::async(std::launch::async, [this, first, count]() -> std::vector<UserInfo> {
        LOG_CLOUD_DEBUG("Getting user list: first={}, count={}", first, count);
        
        std::vector<UserInfo> result;
        
        if (!isConnected_) {
            LOG_CLOUD_ERROR("Failed to get user list - not connected");
            return result;
        }
        
        // Build request body
        nlohmann::json requestBody;
        requestBody["controllerId"] = controllerUid_;
        requestBody["first"] = first;
        requestBody["count"] = count;
        
        // Make API call
        std::string path = "/api/users/list";
        nlohmann::json apiResponse = HttpRequestWrapper("POST", path, requestBody);
        
        // Check for errors
        if (apiResponse.contains("CodeError") && apiResponse["CodeError"].get<int>() != 0) {
            LOG_CLOUD_ERROR("Failed to get user list: {}", 
                          apiResponse.value("TextError", "Unknown error"));
            return result;
        }
        
        // Parse users from response
        if (apiResponse.contains("users") && apiResponse["users"].is_array()) {
            for (auto& userJson : apiResponse["users"]) {
                UserInfo user;
                user.uid = userJson.value("uid", "");
                
                // Parse role
                std::string roleStr = userJson.value("role", "unknown");
                if (roleStr == "operator") {
                    user.role = UserRole::Operator;
                } else if (roleStr == "customer") {
                    user.role = UserRole::Customer;
                } else if (roleStr == "controller") {
                    user.role = UserRole::Controller;
                } else {
                    user.role = UserRole::Unknown;
                }
                
                user.allowance = userJson.value("allowance", 0.0);
                user.price = userJson.value("price", 0.0);
                
                result.push_back(user);
            }
        }
        
        LOG_CLOUD_DEBUG("Returned {} users", result.size());
        return result;
    });
}

std::future<std::vector<TankInfo>> Backend::getTankInfo(const ControllerId& controllerId) {
    return std::async(std::launch::async, [this, controllerId]() -> std::vector<TankInfo> {
        LOG_CLOUD_DEBUG("Getting tank info for controller: {}", controllerId);
        
        std::vector<TankInfo> result;
        
        if (!isConnected_) {
            LOG_CLOUD_ERROR("Failed to get tank info - not connected");
            return result;
        }
        
        // Build request body
        nlohmann::json requestBody;
        requestBody["controllerId"] = controllerId;
        
        // Make API call
        std::string path = "/api/tanks/info";
        nlohmann::json apiResponse = HttpRequestWrapper("POST", path, requestBody);
        
        // Check for errors
        if (apiResponse.contains("CodeError") && apiResponse["CodeError"].get<int>() != 0) {
            LOG_CLOUD_ERROR("Failed to get tank info: {}", 
                          apiResponse.value("TextError", "Unknown error"));
            return result;
        }
        
        // Parse tanks from response
        if (apiResponse.contains("tanks") && apiResponse["tanks"].is_array()) {
            for (auto& tankJson : apiResponse["tanks"]) {
                TankInfo tank;
                tank.number = tankJson.value("number", 0);
                tank.capacity = tankJson.value("capacity", 0.0);
                tank.currentVolume = tankJson.value("currentVolume", 0.0);
                tank.fuelType = tankJson.value("fuelType", "");
                result.push_back(tank);
            }
        }
        
        LOG_CLOUD_DEBUG("Returned {} tanks", result.size());
        return result;
    });
}

} // namespace fuelflux
