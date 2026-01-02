#pragma once

#include <string>
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>
#include "types.h"

namespace fuelflux {

// Tank information structure for backend
struct BackendTankInfo {
    int idTank;
    std::string nameTank;
};

// Backend class for real REST API communication
class Backend {
public:
    // Constructor
    // Parameters:
    //   baseAPI - base URL of backend REST API
    //   controllerUid - UID of controller
    Backend(const std::string& baseAPI, const std::string& controllerUid);
    
    ~Backend();

    // Authorize method
    // Parameter: uid - card UID
    // Returns: true on success, false on failure
    bool Authorize(const std::string& uid);

    // Deauthorize method
    // Returns: true on success, false on failure
    bool Deauthorize();

    // Refuel method
    // Parameters:
    //   tankNumber - fuel tank number
    //   volume - fuel volume to refuel
    // Returns: true on success, false on failure
    bool Refuel(TankNumber tankNumber, Volume volume);

    // Getters for authorized state
    bool IsAuthorized() const { return isAuthorized_; }
    const std::string& GetToken() const { return token_; }
    int GetRoleId() const { return roleId_; }
    double GetAllowance() const { return allowance_; }
    double GetPrice() const { return price_; }
    const std::vector<BackendTankInfo>& GetFuelTanks() const { return fuelTanks_; }
    const std::string& GetLastError() const { return lastError_; }

private:
    // Private method for common parsing of responses from the backend
    // Returns: parsed JSON or throws exception
    nlohmann::json HttpRequestWrapper(const std::string& endpoint, 
                                       const std::string& method,
                                       const nlohmann::json& requestBody,
                                       bool useBearerToken = false);

    // Base URL of backend REST API
    std::string baseAPI_;
    
    // Controller UID
    std::string controllerUid_;
    
    // Authorization state
    bool isAuthorized_;
    
    // Instance variables set by Authorize
    std::string token_;
    int roleId_;
    double allowance_;
    double price_;
    std::vector<BackendTankInfo> fuelTanks_;
    
    // Last error message
    std::string lastError_;
};

} // namespace fuelflux
