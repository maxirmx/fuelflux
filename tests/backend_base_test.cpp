// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "backend.h"
#include "backend_utils.h"

#include <gtest/gtest.h>

#include <functional>
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
