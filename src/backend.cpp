// Copyright(C) 2025 Maxim[maxirmx] Samsonov(www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "backend.h"
#include "backend_utils.h"
#include "logger.h"
#include <httplib.h>
#include <mutex>
#include <sstream>
#include <stdexcept>
#ifdef TARGET_SIM800C
#include <ifaddrs.h>
#include <cstring>
#include <cerrno>
#endif

namespace fuelflux {

#ifdef TARGET_SIM800C
namespace {
constexpr const char kPppInterface[] = "ppp0";

bool IsPppInterfaceAvailable() {
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) != 0) {
        LOG_BCK_ERROR("Failed to get network interfaces: {} (errno: {})", std::strerror(errno), errno);
        return false;
    }

    bool found = false;
    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_name && std::strcmp(ifa->ifa_name, kPppInterface) == 0) {
            found = true;
            break;
        }
    }
    freeifaddrs(ifaddr);
    return found;
}

bool ShouldBindToPppInterface(const std::string& host) {
    if (host == "localhost" || host == "127.0.0.1" || host == "::1") {
        return false;
    }
    return IsPppInterfaceAvailable();
}
} // namespace
#endif

Backend::Backend(const std::string& baseAPI, const std::string& controllerUid, std::shared_ptr<MessageStorage> storage)
    : BackendBase(controllerUid, std::move(storage))
    , baseAPI_(baseAPI)
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

Backend::~Backend() = default;

nlohmann::json Backend::HttpRequestWrapper(const std::string& endpoint,
                                           const std::string& method,
                                           const nlohmann::json& requestBody,
                                           bool useBearerToken) {
    std::lock_guard<std::recursive_mutex> lock(requestMutex_);

    networkError_ = false;

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
        // Handle IPv6 addresses in brackets [::1] or [2001:db8::1]
        size_t portPos = std::string::npos;
        size_t pathPos = urlToParse.find('/');
        bool isIPv6 = false;

        if (!urlToParse.empty() && urlToParse[0] == '[') {
            // IPv6 address in brackets
            isIPv6 = true;
            size_t bracketEnd = urlToParse.find(']');
            if (bracketEnd != std::string::npos) {
                host = urlToParse.substr(1, bracketEnd - 1); // Extract without brackets
                // Check for port after the closing bracket
                if (bracketEnd + 1 < urlToParse.size() && urlToParse[bracketEnd + 1] == ':') {
                    portPos = bracketEnd + 1;
                }
            } else {
                // Malformed IPv6 address - missing closing bracket
                LOG_BCK_WARN("Malformed IPv6 address in URL: {}", urlToParse);
                host = urlToParse.substr(1); // Try to use what we have
            }
        } else {
            // IPv4 or hostname - look for port after last colon before path
            portPos = urlToParse.find(':');
        }

        if (!isIPv6 && portPos != std::string::npos && (pathPos == std::string::npos || portPos < pathPos)) {
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
        else if (isIPv6 && portPos != std::string::npos) {
            // IPv6 with port: already extracted host, now extract port
            std::string portStr;
            if (pathPos != std::string::npos) {
                portStr = urlToParse.substr(portPos + 1, pathPos - portPos - 1);
            } else {
                portStr = urlToParse.substr(portPos + 1);
            }
            port = std::stoi(portStr);
        }
        else if (!isIPv6 && pathPos != std::string::npos) {
            host = urlToParse.substr(0, pathPos);
        }
        else if (!isIPv6 && host.empty()) {
            host = urlToParse;
        }

        // Validate that we have a host
        if (host.empty()) {
            LOG_BCK_ERROR("Failed to parse host from URL: {}", baseAPI_);
            networkError_ = true;
            return BuildWrapperErrorResponse();
        }

        LOG_BCK_DEBUG("Connecting to {}:{} for endpoint: {} (scheme={})", host, port, endpoint, scheme);

        // Prepare headers
        std::string hostHeader;
        // IPv6 addresses must be enclosed in brackets in the Host header
        if (isIPv6) {
            hostHeader = "[" + host + "]";
        } else {
            hostHeader = host;
        }
        // Include port in Host header only when it is non-default for the scheme
        if (!((scheme == "http" && port == 80) || (scheme == "https" && port == 443))) {
            hostHeader += ":" + std::to_string(port);
        }

        httplib::Headers headers = {
            {"User-Agent", "fuelflux/0.1.0"},
            {"Accept", "*/*"},
            {"Host", hostHeader}
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
            client.set_keep_alive(true);
#ifdef TARGET_SIM800C
            if (ShouldBindToPppInterface(host)) {
                client.set_interface(kPppInterface);
            }
#endif

            if (method == "POST") {
                return client.Post(endpoint.c_str(), headers, bodyStr, "application/json");
            }
            else if (method == "GET") {
                return client.Get(endpoint.c_str(), headers);
            }
            LOG_BCK_ERROR("Unsupported HTTP method: {}", method);
            return httplib::Result();
        };

        httplib::Result res;
        if (scheme == "https") {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            httplib::SSLClient sslClient(host.c_str(), port);
            // By default SSL verification is enabled. To customize CA path/cert use sslClient.set_ca_cert_file(...)
            res = doRequest(sslClient);
#else
            std::string err = "HTTPS requested but cpp-httplib built without OpenSSL support";
            LOG_BCK_ERROR("{}", err);
            networkError_ = true;
            return BuildWrapperErrorResponse();
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
            LOG_BCK_ERROR("{}", errorMsg);
            networkError_ = true;
            return BuildWrapperErrorResponse();
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
                    LOG_BCK_ERROR("Failed to parse response JSON: {}", e.what());
                    networkError_ = true;
                    return BuildWrapperErrorResponse();
                }
            }

            return responseJson;
        }
        else {
            // HTTP error response
            std::ostringstream oss;
            oss << "System error, code " << res->status;
            LOG_BCK_ERROR("{}", oss.str());
            networkError_ = true;
            return BuildWrapperErrorResponse();
        }
    }
    catch (const std::exception& e) {
        LOG_BCK_ERROR("HTTP request exception: {}", e.what());
        networkError_ = true;
        return BuildWrapperErrorResponse();
    }
}

} // namespace fuelflux
