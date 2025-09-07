#include "cloud_service.h"
#include <iostream>
#include <thread>
#include <chrono>

namespace fuelflux {

// MockCloudService implementation
MockCloudService::MockCloudService()
    : isConnected_(false)
    , authDelay_(std::chrono::milliseconds(500))
{
    initializeTestData();
}

bool MockCloudService::initialize() {
    isConnected_ = true;
    std::cout << "[CloudService] Initialized (Mock)" << std::endl;
    return true;
}

void MockCloudService::shutdown() {
    isConnected_ = false;
    std::cout << "[CloudService] Shutdown" << std::endl;
}

bool MockCloudService::isConnected() const {
    return isConnected_;
}

std::future<AuthResponse> MockCloudService::authorizeUser(const ControllerId& controllerId, 
                                                         const UserId& userId) {
    return std::async(std::launch::async, [this, controllerId, userId]() -> AuthResponse {
        std::cout << "[CloudService] Authorizing user: " << userId 
                  << " for controller: " << controllerId << std::endl;
        
        // Simulate network delay
        std::this_thread::sleep_for(authDelay_);
        
        if (!isConnected_) {
            AuthResponse response;
            response.success = false;
            response.errorMessage = "Cloud service not connected";
            return response;
        }
        
        UserInfo* user = findUser(userId);
        return createAuthResponse(user);
    });
}

std::future<bool> MockCloudService::reportRefuelTransaction(const RefuelTransaction& transaction) {
    return std::async(std::launch::async, [this, transaction]() -> bool {
        std::cout << "[CloudService] Reporting refuel transaction: "
                  << "User=" << transaction.userId
                  << ", Tank=" << transaction.tankNumber
                  << ", Volume=" << transaction.volume << "L"
                  << ", Amount=" << transaction.totalAmount << " RUB" << std::endl;
        
        if (!isConnected_) {
            std::cout << "[CloudService] Failed to report transaction - not connected" << std::endl;
            return false;
        }
        
        // Simulate processing delay
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        std::cout << "[CloudService] Transaction reported successfully" << std::endl;
        return true;
    });
}

std::future<bool> MockCloudService::reportIntakeTransaction(const IntakeTransaction& transaction) {
    return std::async(std::launch::async, [this, transaction]() -> bool {
        std::cout << "[CloudService] Reporting intake transaction: "
                  << "Operator=" << transaction.operatorId
                  << ", Tank=" << transaction.tankNumber
                  << ", Volume=" << transaction.volume << "L" << std::endl;
        
        if (!isConnected_) {
            std::cout << "[CloudService] Failed to report transaction - not connected" << std::endl;
            return false;
        }
        
        // Simulate processing delay
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        std::cout << "[CloudService] Intake transaction reported successfully" << std::endl;
        return true;
    });
}

std::future<std::vector<UserInfo>> MockCloudService::getUserList(int first, int count) {
    return std::async(std::launch::async, [this, first, count]() -> std::vector<UserInfo> {
        std::cout << "[CloudService] Getting user list: first=" << first 
                  << ", count=" << count << std::endl;
        
        std::vector<UserInfo> result;
        
        if (!isConnected_) {
            std::cout << "[CloudService] Failed to get user list - not connected" << std::endl;
            return result;
        }
        
        // Simulate processing delay
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        
        int endIndex = std::min(first + count, static_cast<int>(testUsers_.size()));
        for (int i = first; i < endIndex; ++i) {
            result.push_back(testUsers_[i]);
        }
        
        std::cout << "[CloudService] Returned " << result.size() << " users" << std::endl;
        return result;
    });
}

std::future<std::vector<TankInfo>> MockCloudService::getTankInfo(const ControllerId& controllerId) {
    return std::async(std::launch::async, [this, controllerId]() -> std::vector<TankInfo> {
        std::cout << "[CloudService] Getting tank info for controller: " << controllerId << std::endl;
        
        if (!isConnected_) {
            std::cout << "[CloudService] Failed to get tank info - not connected" << std::endl;
            return std::vector<TankInfo>();
        }
        
        // Simulate processing delay
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        std::cout << "[CloudService] Returned " << testTanks_.size() << " tanks" << std::endl;
        return testTanks_;
    });
}

void MockCloudService::addTestUser(const UserInfo& user) {
    testUsers_.push_back(user);
}

void MockCloudService::addTestTank(const TankInfo& tank) {
    testTanks_.push_back(tank);
}

UserInfo* MockCloudService::findUser(const UserId& userId) {
    for (auto& user : testUsers_) {
        if (user.uid == userId) {
            return &user;
        }
    }
    return nullptr;
}

AuthResponse MockCloudService::createAuthResponse(const UserInfo* user) const {
    AuthResponse response;
    
    if (user) {
        response.success = true;
        response.userInfo = *user;
        response.tanks = testTanks_;
        
        std::cout << "[CloudService] Authorization successful for user: " << user->uid
                  << " (Role: " << static_cast<int>(user->role) << ")" << std::endl;
    } else {
        response.success = false;
        response.errorMessage = "Unknown user";
        
        std::cout << "[CloudService] Authorization failed - unknown user" << std::endl;
    }
    
    return response;
}

void MockCloudService::initializeTestData() {
    // Initialize test users as specified in the API documentation
    UserInfo operator1;
    operator1.uid = "1111-1111-1111-1111";
    operator1.role = UserRole::Operator;
    operator1.allowance = 0.0; // Operators don't have fuel allowance
    operator1.price = 0.0;
    testUsers_.push_back(operator1);
    
    UserInfo customer1;
    customer1.uid = "2222-2222-2222-2222";
    customer1.role = UserRole::Customer;
    customer1.allowance = 100.0; // 100 liters allowed
    customer1.price = 64.35; // 64.35 rubles per liter
    testUsers_.push_back(customer1);
    
    UserInfo controller1;
    controller1.uid = "3333-3333-3333-3333";
    controller1.role = UserRole::Controller;
    controller1.allowance = 0.0;
    controller1.price = 0.0;
    testUsers_.push_back(controller1);
    
    // Initialize test tanks
    TankInfo tank1;
    tank1.number = 1;
    tank1.capacity = 5000.0;
    tank1.currentVolume = 3500.0;
    tank1.fuelType = "AI-95";
    testTanks_.push_back(tank1);
    
    TankInfo tank2;
    tank2.number = 2;
    tank2.capacity = 5000.0;
    tank2.currentVolume = 4200.0;
    tank2.fuelType = "AI-92";
    testTanks_.push_back(tank2);
    
    TankInfo tank3;
    tank3.number = 3;
    tank3.capacity = 3000.0;
    tank3.currentVolume = 1800.0;
    tank3.fuelType = "Diesel";
    testTanks_.push_back(tank3);
    
    std::cout << "[CloudService] Initialized with " << testUsers_.size() 
              << " test users and " << testTanks_.size() << " test tanks" << std::endl;
}

} // namespace fuelflux
