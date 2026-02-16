// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include <gtest/gtest.h>

#include "url_utils.h"

using namespace fuelflux;

TEST(UrlUtilsTest, ExtractsHostFromHttpUrl) {
    EXPECT_EQ(ExtractHostFromUrl("http://ttft.uxp.ru"), "ttft.uxp.ru");
}

TEST(UrlUtilsTest, ExtractsHostWithPathAndPort) {
    EXPECT_EQ(ExtractHostFromUrl("https://example.com:8443/api/v1"), "example.com");
}

TEST(UrlUtilsTest, ExtractsHostWithoutScheme) {
    EXPECT_EQ(ExtractHostFromUrl("example.com/path"), "example.com");
}

TEST(UrlUtilsTest, ExtractsIpv6Host) {
    EXPECT_EQ(ExtractHostFromUrl("http://[2001:db8::1]:8080/status"), "2001:db8::1");
}

TEST(UrlUtilsTest, ExtractsHostWithUserInfo) {
    EXPECT_EQ(ExtractHostFromUrl("http://user:pass@example.org:8080/path"), "example.org");
}

TEST(UrlUtilsTest, ReturnsHostForPlainHostname) {
    EXPECT_EQ(ExtractHostFromUrl("localhost"), "localhost");
}
