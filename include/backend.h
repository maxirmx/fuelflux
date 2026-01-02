#pragma once

#include "cloud_service.h"
#include <string>
#include <memory>
#include <nlohmann/json.hpp>
#include <httplib.h>

namespace fuelflux {

// Backend service implementation using REST API
class Backend : public ICloudService {
public:
    // Constructor
    // Parameters:
    //   baseAPI - base URL of backend REST API (e.g., "http://api.example.com")
    //   controllerUid - UID of controller
    Backend(const std::string& baseAPI, const std::string& controllerUid);
    ~Backend() override = default;

    // ICloudService implementation
    bool initialize() override;
    void shutdown() override;
    bool isConnected() const override;

    std::future<AuthResponse> authorizeUser(const ControllerId& controllerId, 
                                          const UserId& userId) override;
    
    std::future<bool> reportRefuelTransaction(const RefuelTransaction& transaction) override;
    std::future<bool> reportIntakeTransaction(const IntakeTransaction& transaction) override;
    
    std::future<std::vector<UserInfo>> getUserList(int first, int count) override;
    std::future<std::vector<TankInfo>> getTankInfo(const ControllerId& controllerId) override;

private:
    // Private method to handle common parsing of responses from the backend
    // Backend reports call failures in two ways:
    // 1. With 200 OK response but JSON body like:
    //    { "CodeError": 1, "TextError": "Не найдена карта или контроллер" }
    // 2. With error HTTP response
    // This method converts both to standardized error format:
    //    { "CodeError": -1, "TextError": "Системная ошибка, код <status>" }
    nlohmann::json HttpRequestWrapper(
        const std::string& method,
        const std::string& path,
        const nlohmann::json& body = nlohmann::json::object()
    );

    // Helper method to parse host and port from base API URL
    bool parseBaseAPI(const std::string& baseAPI);

    std::string baseAPI_;
    std::string controllerUid_;
    std::string host_;
    int port_;
    std::string scheme_;
    bool isConnected_;
    std::unique_ptr<httplib::Client> httpClient_;
};

} // namespace fuelflux
