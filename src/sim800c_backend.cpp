// Copyright (C) 2025 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "sim800c_backend.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sstream>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#include <utility>

#include "backend_utils.h"
#include "logger.h"

namespace fuelflux {

namespace {

speed_t MapBaudRate(int baudRate) {
    switch (baudRate) {
        case 1200:
            return B1200;
        case 2400:
            return B2400;
        case 4800:
            return B4800;
        case 9600:
            return B9600;
        case 19200:
            return B19200;
        case 38400:
            return B38400;
        case 57600:
            return B57600;
        case 115200:
            return B115200;
        default:
            return 0;
    }
}

bool ContainsAny(const std::string& data, const std::vector<std::string>& tokens) {
    for (const auto& token : tokens) {
        if (data.find(token) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

Sim800cBackend::Sim800cBackend(std::string apiUrl,
                               std::string controllerUid,
                               std::string devicePath,
                               int baudRate,
                               std::string apn,
                               std::string apnUser,
                               std::string apnPassword,
                               int connectTimeoutMs,
                               int responseTimeoutMs,
                               std::shared_ptr<MessageStorage> storage)
    : BackendBase(std::move(controllerUid), std::move(storage))
    , apiUrl_(std::move(apiUrl))
    , devicePath_(std::move(devicePath))
    , baudRate_(baudRate)
    , apn_(std::move(apn))
    , apnUser_(std::move(apnUser))
    , apnPassword_(std::move(apnPassword))
    , connectTimeoutMs_(connectTimeoutMs)
    , responseTimeoutMs_(responseTimeoutMs)
{
    LOG_BCK_INFO(
        "SIM800C backend configured: api_url={}, controller_uid={}, device={}, baud={}, apn={}, connect_timeout_ms={}, response_timeout_ms={}",
        apiUrl_,
        controllerUid_,
        devicePath_,
        baudRate_,
        apn_,
        connectTimeoutMs_,
        responseTimeoutMs_);
}

Sim800cBackend::~Sim800cBackend() {
    if (serialFd_ >= 0) {
        ::close(serialFd_);
        serialFd_ = -1;
    }
}

bool Sim800cBackend::InitializeModem() {
    if (modemReady_) {
        return true;
    }

    if (serialFd_ < 0) {
        serialFd_ = ::open(devicePath_.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
        if (serialFd_ < 0) {
            LOG_BCK_ERROR("Failed to open SIM800C device {}: {}", devicePath_, std::strerror(errno));
            lastError_ = HttpRequestWrapperErrorText;
            networkError_ = true;
            return false;
        }

        termios tty{};
        if (tcgetattr(serialFd_, &tty) != 0) {
            LOG_BCK_ERROR("Failed to read serial attributes: {}", std::strerror(errno));
            lastError_ = HttpRequestWrapperErrorText;
            networkError_ = true;
            return false;
        }

        speed_t speed = MapBaudRate(baudRate_);
        if (speed == 0) {
            LOG_BCK_ERROR("Unsupported baud rate: {}", baudRate_);
            lastError_ = StdControllerError;
            return false;
        }

        cfsetospeed(&tty, speed);
        cfsetispeed(&tty, speed);

        tty.c_cflag = static_cast<tcflag_t>(tty.c_cflag & ~CSIZE);
        tty.c_cflag |= (CS8 | CLOCAL | CREAD);
        tty.c_cflag &= static_cast<tcflag_t>(~(PARENB | PARODD | CSTOPB | CRTSCTS));
        tty.c_iflag &= static_cast<tcflag_t>(~(IXON | IXOFF | IXANY));
        tty.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO | ECHOE | ISIG));
        tty.c_oflag &= static_cast<tcflag_t>(~OPOST);
        tty.c_cc[VMIN] = 0;
        tty.c_cc[VTIME] = 0;

        if (tcsetattr(serialFd_, TCSANOW, &tty) != 0) {
            LOG_BCK_ERROR("Failed to configure serial port: {}", std::strerror(errno));
            lastError_ = HttpRequestWrapperErrorText;
            networkError_ = true;
            return false;
        }

        tcflush(serialFd_, TCIOFLUSH);
    }

    std::string response;
    if (!SendCommand("AT", &response, responseTimeoutMs_)) {
        LOG_BCK_ERROR("SIM800C AT check failed: {}", response);
        lastError_ = HttpRequestWrapperErrorText;
        networkError_ = true;
        return false;
    }

    if (!SendCommand("ATE0", &response, responseTimeoutMs_)) {
        LOG_BCK_ERROR("SIM800C ATE0 failed: {}", response);
        lastError_ = HttpRequestWrapperErrorText;
        networkError_ = true;
        return false;
    }

    modemReady_ = true;
    return true;
}

bool Sim800cBackend::EnsureBearer() {
    if (!InitializeModem()) {
        return false;
    }

    if (bearerReady_ && IsConnected()) {
        return true;
    }

    std::string response;
    if (!SendCommand("AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\"", &response, responseTimeoutMs_)) {
        LOG_BCK_ERROR("Failed to set bearer type: {}", response);
        lastError_ = HttpRequestWrapperErrorText;
        networkError_ = true;
        return false;
    }

    const std::string apnCommand = "AT+SAPBR=3,1,\"APN\",\"" + apn_ + "\"";
    if (!SendCommand(apnCommand, &response, responseTimeoutMs_)) {
        LOG_BCK_ERROR("Failed to set APN: {}", response);
        lastError_ = HttpRequestWrapperErrorText;
        networkError_ = true;
        return false;
    }

    if (!apnUser_.empty()) {
        const std::string userCommand = "AT+SAPBR=3,1,\"USER\",\"" + apnUser_ + "\"";
        if (!SendCommand(userCommand, &response, responseTimeoutMs_)) {
            LOG_BCK_ERROR("Failed to set APN user: {}", response);
            lastError_ = HttpRequestWrapperErrorText;
            networkError_ = true;
            return false;
        }
    }

    if (!apnPassword_.empty()) {
        const std::string passCommand = "AT+SAPBR=3,1,\"PWD\",\"" + apnPassword_ + "\"";
        if (!SendCommand(passCommand, &response, responseTimeoutMs_)) {
            LOG_BCK_ERROR("Failed to set APN password: {}", response);
            lastError_ = HttpRequestWrapperErrorText;
            networkError_ = true;
            return false;
        }
    }

    if (!SendCommand("AT+SAPBR=1,1", &response, connectTimeoutMs_)) {
        LOG_BCK_ERROR("Failed to open bearer: {}", response);
        lastError_ = HttpRequestWrapperErrorText;
        networkError_ = true;
        return false;
    }

    bearerReady_ = true;
    return IsConnected();
}

bool Sim800cBackend::IsConnected() {
    std::string cregResponse;
    bool registered = false;
    if (SendCommand("AT+CREG?", &cregResponse, responseTimeoutMs_)) {
        if (cregResponse.find(",1") != std::string::npos || cregResponse.find(",5") != std::string::npos) {
            registered = true;
        }
    }

    std::string cgattResponse;
    bool attached = false;
    if (SendCommand("AT+CGATT?", &cgattResponse, responseTimeoutMs_)) {
        if (cgattResponse.find("+CGATT: 1") != std::string::npos) {
            attached = true;
        }
    }

    std::string sapbrResponse;
    bool bearerUp = false;
    if (SendCommand("AT+SAPBR=2,1", &sapbrResponse, responseTimeoutMs_)) {
        if (sapbrResponse.find("+SAPBR: 1,1") != std::string::npos) {
            bearerUp = true;
        }
    }

    if (!registered || !attached || !bearerUp) {
        LOG_BCK_WARN(
            "SIM800C connection status: registered={}, attached={}, bearer={} "
            "CREG_response='{}' CGATT_response='{}' SAPBR_response='{}'",
            registered,
            attached,
            bearerUp,
            cregResponse,
            cgattResponse,
            sapbrResponse);
    }

    return registered && attached && bearerUp;
}

bool Sim800cBackend::SendCommand(const std::string& command,
                                 std::string* response,
                                 int timeoutMs,
                                 const std::vector<std::string>& terminators) {
    if (serialFd_ < 0) {
        return false;
    }

    if (!command.empty()) {
        std::string payload = command;
        payload += "\r";
        ssize_t totalWritten = 0;
        while (totalWritten < static_cast<ssize_t>(payload.size())) {
            ssize_t written = ::write(serialFd_, payload.data() + totalWritten, payload.size() - totalWritten);
            if (written < 0) {
                return false;
            }
            totalWritten += written;
        }
    }

    std::string buffer;
    const auto start = std::chrono::steady_clock::now();
    while (true) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - start)
                                 .count();
        if (elapsed >= timeoutMs) {
            break;
        }

        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(serialFd_, &readSet);
        struct timeval tv;
        const auto remaining = timeoutMs - elapsed;
        tv.tv_sec = remaining / 1000;
        tv.tv_usec = (remaining % 1000) * 1000;
        int ready = ::select(serialFd_ + 1, &readSet, nullptr, nullptr, &tv);
        if (ready > 0 && FD_ISSET(serialFd_, &readSet)) {
            char tmp[256];
            ssize_t readBytes = ::read(serialFd_, tmp, sizeof(tmp));
            if (readBytes > 0) {
                buffer.append(tmp, static_cast<size_t>(readBytes));
                if (ContainsAny(buffer, terminators)) {
                    break;
                }
            }
        }
    }

    if (response) {
        *response = buffer;
    }

    if (buffer.find("ERROR") != std::string::npos) {
        return false;
    }

    if (!terminators.empty() && !ContainsAny(buffer, terminators)) {
        return false;
    }

    return true;
}

bool Sim800cBackend::SendRaw(const std::string& data) {
    if (serialFd_ < 0) {
        return false;
    }
    ssize_t totalWritten = 0;
    while (totalWritten < static_cast<ssize_t>(data.size())) {
        ssize_t written = ::write(serialFd_, data.data() + totalWritten, data.size() - totalWritten);
        if (written < 0) {
            return false;
        }
        totalWritten += written;
    }
    return true;
}

bool Sim800cBackend::SendHttpData(const std::string& payload) {
    std::ostringstream oss;
    oss << "AT+HTTPDATA=" << payload.size() << "," << responseTimeoutMs_;
    std::string response;
    if (!SendCommand(oss.str(), &response, responseTimeoutMs_, {"DOWNLOAD", "ERROR"})) {
        LOG_BCK_ERROR("HTTPDATA command failed: {}", response);
        return false;
    }

    if (!SendRaw(payload)) {
        return false;
    }

    if (!SendCommand("", &response, responseTimeoutMs_)) {
        LOG_BCK_ERROR("HTTPDATA payload send failed: {}", response);
        return false;
    }

    return true;
}

nlohmann::json Sim800cBackend::HttpRequestWrapper(const std::string& endpoint,
                                                  const std::string& method,
                                                  const nlohmann::json& requestBody,
                                                  bool useBearerToken) {
    networkError_ = false;
    if (!EnsureBearer()) {
        networkError_ = true;
        return BuildWrapperErrorResponse();
    }

    const std::string url = BuildUrl(endpoint);
    std::string response;

    SendCommand("AT+HTTPTERM", &response, responseTimeoutMs_);

    if (!SendCommand("AT+HTTPINIT", &response, responseTimeoutMs_)) {
        LOG_BCK_ERROR("HTTPINIT failed: {}", response);
        networkError_ = true;
        return BuildWrapperErrorResponse();
    }

    if (!SendCommand("AT+HTTPPARA=\"CID\",1", &response, responseTimeoutMs_)) {
        LOG_BCK_ERROR("HTTPPARA CID failed: {}", response);
        networkError_ = true;
        return BuildWrapperErrorResponse();
    }

    const std::string urlCommand = "AT+HTTPPARA=\"URL\",\"" + url + "\"";
    if (!SendCommand(urlCommand, &response, responseTimeoutMs_)) {
        LOG_BCK_ERROR("HTTPPARA URL failed: {}", response);
        networkError_ = true;
        return BuildWrapperErrorResponse();
    }

    if (useBearerToken && !token_.empty()) {
        const std::string auth = "AT+HTTPPARA=\"USERDATA\",\"Authorization: Bearer " + token_ + "\"";
        if (!SendCommand(auth, &response, responseTimeoutMs_)) {
            LOG_BCK_ERROR("HTTPPARA USERDATA failed: {}", response);
            networkError_ = true;
            return BuildWrapperErrorResponse();
        }
    }

    if (method == "POST") {
        if (!SendCommand("AT+HTTPPARA=\"CONTENT\",\"application/json\"", &response, responseTimeoutMs_)) {
            LOG_BCK_ERROR("HTTPPARA CONTENT failed: {}", response);
            networkError_ = true;
            return BuildWrapperErrorResponse();
        }

        const std::string payload = requestBody.dump();
        if (!SendHttpData(payload)) {
            networkError_ = true;
            return BuildWrapperErrorResponse();
        }
    }

    int actionCode = 0;
    if (method == "GET") {
        actionCode = 0;
    } else if (method == "POST") {
        actionCode = 1;
    } else {
        LOG_BCK_ERROR("Unsupported HTTP method: {}", method);
        networkError_ = true;
        return BuildWrapperErrorResponse();
    }

    std::ostringstream actionCmd;
    actionCmd << "AT+HTTPACTION=" << actionCode;
    if (!SendCommand(actionCmd.str(), &response, responseTimeoutMs_)) {
        LOG_BCK_ERROR("HTTPACTION failed: {}", response);
        networkError_ = true;
        return BuildWrapperErrorResponse();
    }

    int status = 0;
    int length = 0;
    auto actionPos = response.find("+HTTPACTION:");
    if (actionPos != std::string::npos) {
        std::string actionLine = response.substr(actionPos);
        const auto lineEnd = actionLine.find('\n');
        if (lineEnd != std::string::npos) {
            actionLine = actionLine.substr(0, lineEnd);
        }
        std::replace(actionLine.begin(), actionLine.end(), '\r', ' ');
        std::replace(actionLine.begin(), actionLine.end(), '\n', ' ');
        char tag[16];
        int methodOut = 0;
        if (std::sscanf(actionLine.c_str(), "%15[^:]: %d,%d,%d", tag, &methodOut, &status, &length) != 4) {
            status = 0;
        }
    }

    if (status < 200 || status >= 300) {
        LOG_BCK_ERROR("HTTPACTION status error: {}", status);
        networkError_ = true;
        return BuildWrapperErrorResponse();
    }

    std::string payload;
    if (length > 0) {
        if (!SendCommand("AT+HTTPREAD", &response, responseTimeoutMs_)) {
            LOG_BCK_ERROR("HTTPREAD failed: {}", response);
            networkError_ = true;
            return BuildWrapperErrorResponse();
        }

        auto dataPos = response.find("+HTTPREAD:");
        if (dataPos != std::string::npos) {
            auto start = response.find('\n', dataPos);
            if (start != std::string::npos) {
                ++start;
                auto end = response.rfind("OK");
                if (end != std::string::npos && end > start) {
                    payload = response.substr(start, end - start);
                }
            }
        }
    }

    SendCommand("AT+HTTPTERM", &response, responseTimeoutMs_);

    if (payload.empty() || payload == "null") {
        return nlohmann::json(nullptr);
    }

    try {
        return nlohmann::json::parse(payload);
    } catch (const std::exception& e) {
        LOG_BCK_ERROR("Failed to parse SIM800C response JSON: {}", e.what());
        networkError_ = true;
        return BuildWrapperErrorResponse();
    }
}

std::string Sim800cBackend::BuildUrl(const std::string& endpoint) const {
    if (apiUrl_.empty()) {
        return endpoint;
    }
    if (endpoint.empty()) {
        return apiUrl_;
    }

    if (apiUrl_.back() == '/' && endpoint.front() == '/') {
        return apiUrl_.substr(0, apiUrl_.size() - 1) + endpoint;
    }
    if (apiUrl_.back() != '/' && endpoint.front() != '/') {
        return apiUrl_ + '/' + endpoint;
    }
    return apiUrl_ + endpoint;
}

} // namespace fuelflux
