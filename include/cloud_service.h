#pragma once

#include "types.h"
#include <functional>
#include <future>

namespace fuelflux {

// Cloud service interface for communication with remote server
class ICloudService {
public:
    virtual ~ICloudService() = default;

    // Connection management
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;
    virtual bool isConnected() const = 0;

    // Authorization operations
    virtual std::future<AuthResponse> authorizeUser(const ControllerId& controllerId, 
                                                   const UserId& userId) = 0;
    
    // Transaction reporting
    virtual std::future<bool> reportRefuelTransaction(const RefuelTransaction& transaction) = 0;
    virtual std::future<bool> reportIntakeTransaction(const IntakeTransaction& transaction) = 0;
    
    // User synchronization (for controller role)
    virtual std::future<std::vector<UserInfo>> getUserList(int first, int count) = 0;
    
    // Tank information
    virtual std::future<std::vector<TankInfo>> getTankInfo(const ControllerId& controllerId) = 0;
};

// Mock cloud service implementation for testing/emulation
class MockCloudService : public ICloudService {
public:
    MockCloudService();
    ~MockCloudService() override = default;

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

    // Configuration for testing
    void setConnectionStatus(bool connected) { isConnected_ = connected; }
    void addTestUser(const UserInfo& user);
    void addTestTank(const TankInfo& tank);
    void setAuthorizationDelay(std::chrono::milliseconds delay) { authDelay_ = delay; }

private:
    bool isConnected_;
    std::vector<UserInfo> testUsers_;
    std::vector<TankInfo> testTanks_;
    std::chrono::milliseconds authDelay_;

    // Helper methods
    UserInfo* findUser(const UserId& userId);
    AuthResponse createAuthResponse(const UserInfo* user) const;
    void initializeTestData();
};

} // namespace fuelflux
