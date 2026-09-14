// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "backend.h"
#include "backend_utils.h"

#include <gtest/gtest.h>

#include <functional>
#include <future>
#include "message_storage.h"
#include <stdexcept>

using namespace fuelflux;

namespace {

class TestBackendBase : public BackendBase {
public:
    explicit TestBackendBase(std::string controllerUid)
        : BackendBase(std::move(controllerUid), nullptr) {
    }

    std::function<nlohmann::json(const std::string&, const std::string&, const nlohmann::json&, bool)>
        boolTokenHandler;

    std::function<nlohmann::json(const std::string&, const std::string&, const nlohmann::json&, const std::string&)>
        explicitTokenHandler;

    std::string asyncToken;
    void seed(std::shared_ptr<MessageStorage> storage, int role = 1) {
        storage_ = storage; session_.SetToken("old-token");
        authorizedUid_ = "customer"; roleId_ = role; allowance_ = 100;
        fuelTanks_ = {{17, 1, "Tank", 100}};
    }


protected:
    nlohmann::json HttpRequestWrapper(const std::string& endpoint,
                                      const std::string& method,
                                      const nlohmann::json& requestBody,
                                      bool useBearerToken) override {
        if (!boolTokenHandler) {
            throw std::runtime_error("boolTokenHandler not set");
        }
        return boolTokenHandler(endpoint, method, requestBody, useBearerToken);
    }

    nlohmann::json HttpRequestWrapper(const std::string& endpoint,
                                      const std::string& method,
                                      const nlohmann::json& requestBody,
                                      const std::string& bearerToken) override {
        if (!explicitTokenHandler) {
            throw std::runtime_error("explicitTokenHandler not set");
        }
        return explicitTokenHandler(endpoint, method, requestBody, bearerToken);
    }

    void SendAsyncDeauthorizeRequest(const std::string& token) override {
        asyncToken = token;
    }
};

} // namespace

TEST(BackendBaseFetchUserCardsTest, SendsExpectedRequestAndParsesValidCards) {
    TestBackendBase backend("controller-uid-42");

    backend.boolTokenHandler = [](const std::string& endpoint,
                                  const std::string& method,
                                  const nlohmann::json& body,
                                  bool useBearerToken) -> nlohmann::json {
        EXPECT_EQ(endpoint, "/api/pump/cards?first=5&number=3");
        EXPECT_EQ(method, "POST");
        EXPECT_TRUE(useBearerToken);
        EXPECT_TRUE(body.contains("PumpControllerUid"));
        if (!body.contains("PumpControllerUid")) {
            return nlohmann::json::array();
        }
        EXPECT_EQ(body["PumpControllerUid"], "controller-uid-42");

        return nlohmann::json::array({
            {{"Uid", "100"}},
            {{"Uid", "200"}, {"RoleId", 2}, {"Allowance", 19.75}},
            42,
            {{"RoleId", 1}, {"Allowance", 5.5}},
        });
    };

    const auto cards = backend.FetchUserCards(5, 3);

    ASSERT_EQ(cards.size(), 2);
    EXPECT_EQ(cards[0].uid, "100");
    EXPECT_EQ(cards[0].roleId, 0);
    EXPECT_DOUBLE_EQ(cards[0].allowance, 0.0);

    EXPECT_EQ(cards[1].uid, "200");
    EXPECT_EQ(cards[1].roleId, 2);
    EXPECT_DOUBLE_EQ(cards[1].allowance, 19.75);
    EXPECT_TRUE(backend.GetLastError().empty());
}

TEST(BackendBaseFetchUserCardsTest, ErrorResponseReturnsEmptyAndStoresMessage) {
    TestBackendBase backend("controller-uid-42");

    backend.boolTokenHandler = [](const std::string&, const std::string&, const nlohmann::json&, bool) {
        return nlohmann::json{{"CodeError", 7}, {"TextError", "cards api unavailable"}};
    };

    const auto cards = backend.FetchUserCards(0, 100);

    EXPECT_TRUE(cards.empty());
    EXPECT_EQ(backend.GetLastError(), "cards api unavailable");
}

TEST(BackendBaseFetchUserCardsTest, NonArrayResponseReturnsStdBackendError) {
    TestBackendBase backend("controller-uid-42");

    backend.boolTokenHandler = [](const std::string&, const std::string&, const nlohmann::json&, bool) {
        return nlohmann::json{{"unexpected", true}};
    };

    const auto cards = backend.FetchUserCards(0, 10);

    EXPECT_TRUE(cards.empty());
    EXPECT_EQ(backend.GetLastError(), StdBackendError);
}

TEST(BackendBaseFetchUserCardsTest, ExceptionSetsStandardBackendErrorWhenNoPriorError) {
    TestBackendBase backend("controller-uid-42");

    backend.boolTokenHandler = [](const std::string&, const std::string&, const nlohmann::json&, bool) -> nlohmann::json {
        throw std::runtime_error("network throw");
    };

    const auto cards = backend.FetchUserCards(0, 10);

    EXPECT_TRUE(cards.empty());
    EXPECT_EQ(backend.GetLastError(), StdBackendError);
}

TEST(BackendBaseFetchUserCardsTest, BadAllowanceTypeIsHandledAsFailure) {
    TestBackendBase backend("controller-uid-42");

    backend.boolTokenHandler = [](const std::string&, const std::string&, const nlohmann::json&, bool) {
        return nlohmann::json::array({{{"Uid", "100"}, {"Allowance", "not-a-number"}}});
    };

    const auto cards = backend.FetchUserCards(0, 1);

    EXPECT_TRUE(cards.empty());
    EXPECT_EQ(backend.GetLastError(), StdBackendError);
}


TEST(BackendBaseFetchFuelTanksTest, SendsExpectedRequestAndParsesValidTanks) {
    TestBackendBase backend("controller-uid-42");

    backend.boolTokenHandler = [](const std::string& endpoint,
                                  const std::string& method,
                                  const nlohmann::json& body,
                                  bool useBearerToken) -> nlohmann::json {
        EXPECT_EQ(endpoint, "/api/pump/tanks?first=2&number=2");
        EXPECT_EQ(method, "POST");
        EXPECT_TRUE(useBearerToken);
        EXPECT_EQ(body.value("PumpControllerUid", ""), "controller-uid-42");

        return nlohmann::json::array({
            {{"visualNumberTank", 1}, {"idTank", 11}, {"nameTank", "Diesel"}, {"volume", 1000.5}},
            {{"visualNumberTank", 2}},
            7,
            {{"idTank", 99}},
        });
    };

    const auto tanks = backend.FetchFuelTanks(2, 2);

    ASSERT_EQ(tanks.size(), 2);
    EXPECT_EQ(tanks[0].visualNumberTank, 1);
    EXPECT_EQ(tanks[0].idTank, 11);
    EXPECT_EQ(tanks[0].nameTank, "Diesel");
    EXPECT_DOUBLE_EQ(tanks[0].volume, 1000.5);

    EXPECT_EQ(tanks[1].visualNumberTank, 2);
    EXPECT_EQ(tanks[1].idTank, 0);
    EXPECT_TRUE(tanks[1].nameTank.empty());
    EXPECT_DOUBLE_EQ(tanks[1].volume, 0.0);
    EXPECT_TRUE(backend.GetLastError().empty());
}

TEST(BackendBaseFetchFuelTanksTest, ErrorResponseReturnsEmptyAndStoresMessage) {
    TestBackendBase backend("controller-uid-42");

    backend.boolTokenHandler = [](const std::string&, const std::string&, const nlohmann::json&, bool) {
        return nlohmann::json{{"CodeError", 9}, {"TextError", "tanks api unavailable"}};
    };

    const auto tanks = backend.FetchFuelTanks(0, 100);

    EXPECT_TRUE(tanks.empty());
    EXPECT_EQ(backend.GetLastError(), "tanks api unavailable");
}

TEST(BackendBaseFetchFuelTanksTest, BadVolumeTypeIsHandledAsFailure) {
    TestBackendBase backend("controller-uid-42");

    backend.boolTokenHandler = [](const std::string&, const std::string&, const nlohmann::json&, bool) {
        return nlohmann::json::array({{{"visualNumberTank", 1}, {"volume", "bad"}}});
    };

    const auto tanks = backend.FetchFuelTanks(0, 1);

    EXPECT_TRUE(tanks.empty());
    EXPECT_EQ(backend.GetLastError(), StdBackendError);
}

TEST(BackendBaseReportingTest, ControllerFailureHasOnlyReceiptPersistence) {
    for (bool intake : {false, true}) {
        auto storage = std::make_shared<MessageStorage>(":memory:");
        TestBackendBase backend("controller"); backend.seed(storage, intake ? 2 : 1);
        backend.boolTokenHandler = [](const auto&, const auto&, const auto&, bool) {
            return nlohmann::json{{"CodeError", HttpRequestWrapperErrorCode}, {"TextError", "offline"}};
        };
        auto receipt = storage->BeginReceipt("customer", intake ? MessageMethod::Intake : MessageMethod::Refuel, "{}", 5, false);
        ASSERT_TRUE(receipt);
        EXPECT_FALSE(intake ? backend.IntakeUnpersisted(1, 5, IntakeDirection::In) : backend.RefuelUnpersisted(1, 5));
        EXPECT_EQ(storage->BacklogCount(), 0);
        EXPECT_FALSE(backend.WasLastReportPersisted());
        // Simulate restart between backend failure and receipt promotion.
        ASSERT_EQ(storage->PendingReceipts()->size(), 1u);
        EXPECT_TRUE(storage->RetainReceipt(*receipt, false));
        EXPECT_TRUE(storage->RetainReceipt(*receipt, false));
        EXPECT_EQ(storage->BacklogCount(), 1);
    }
}

TEST(BackendBaseReportingTest, SerializedDeauthorizationWaitsAndKeepsTokenOnFailure) {
    auto backend = std::make_shared<TestBackendBase>("controller"); backend->seed(nullptr);
    std::promise<void> entered, release;
    auto gate = release.get_future().share();
    backend->explicitTokenHandler = [&](const auto& endpoint, const auto&, const auto&, const auto& token) {
        EXPECT_EQ(endpoint, "/api/pump/deauthorize"); EXPECT_EQ(token, "old-token");
        entered.set_value(); gate.wait();
        return nlohmann::json{{"CodeError", HttpRequestWrapperErrorCode}};
    };
    auto result = std::async(std::launch::async, [&] { return backend->DeauthorizeAndWait(); });
    EXPECT_EQ(entered.get_future().wait_for(std::chrono::seconds(2)), std::future_status::ready);
    EXPECT_EQ(result.wait_for(std::chrono::milliseconds(0)), std::future_status::timeout);
    release.set_value(); EXPECT_FALSE(result.get()); EXPECT_TRUE(backend->IsAuthorized());
    backend->explicitTokenHandler = [](const auto&, const auto&, const auto&, const auto&) { return nlohmann::json::object(); };
    EXPECT_TRUE(backend->DeauthorizeAndWait()); EXPECT_FALSE(backend->IsAuthorized());
}
