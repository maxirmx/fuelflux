// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "peripherals/gps_receiver.h"

#include "hardware/hardware_config.h"
#include "logger.h"
#include "timing_config.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#if defined(TARGET_REAL_GPS) && !defined(_WIN32)
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace fuelflux::peripherals {
namespace {

constexpr std::size_t kMaximumNmeaLineLength = 1024;

struct ParsedNmeaSentence {
    bool checksumValid = false;
    std::optional<GpsPosition> position;
};

int hexadecimalValue(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    return -1;
}

std::vector<std::string_view> splitFields(std::string_view body) {
    std::vector<std::string_view> fields;
    std::size_t start = 0;
    while (true) {
        const auto separator = body.find(',', start);
        if (separator == std::string_view::npos) {
            fields.emplace_back(body.substr(start));
            break;
        }
        fields.emplace_back(body.substr(start, separator - start));
        start = separator + 1;
    }
    return fields;
}

bool hasSentenceSuffix(std::string_view sentenceType, std::string_view suffix) {
    return sentenceType.size() >= suffix.size() &&
           sentenceType.substr(sentenceType.size() - suffix.size()) == suffix;
}

bool containsOnlyDigits(std::string_view value) {
    return !value.empty() &&
           std::all_of(value.begin(), value.end(), [](char character) {
               return character >= '0' && character <= '9';
           });
}

std::optional<int> parseNonNegativeInteger(std::string_view value) {
    if (!containsOnlyDigits(value)) {
        return std::nullopt;
    }

    try {
        std::size_t parsed = 0;
        const int result = std::stoi(std::string(value), &parsed);
        if (parsed != value.size() || result < 0) {
            return std::nullopt;
        }
        return result;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<double> parseCoordinate(std::string_view coordinate,
                                      std::string_view hemisphere,
                                      std::size_t degreeDigits,
                                      int maximumDegrees,
                                      char positiveHemisphere,
                                      char negativeHemisphere) {
    if (hemisphere.size() != 1 ||
        (hemisphere.front() != positiveHemisphere &&
         hemisphere.front() != negativeHemisphere)) {
        return std::nullopt;
    }

    const auto decimalPoint = coordinate.find('.');
    if (decimalPoint != std::string_view::npos &&
        coordinate.find('.', decimalPoint + 1) != std::string_view::npos) {
        return std::nullopt;
    }

    const std::size_t integralDigits = decimalPoint == std::string_view::npos
        ? coordinate.size()
        : decimalPoint;
    if (integralDigits != degreeDigits + 2) {
        return std::nullopt;
    }

    for (char character : coordinate) {
        if (character != '.' && (character < '0' || character > '9')) {
            return std::nullopt;
        }
    }

    try {
        const int degrees = std::stoi(std::string(coordinate.substr(0, degreeDigits)));
        std::size_t parsedMinutes = 0;
        const std::string minutesText(coordinate.substr(degreeDigits));
        const double minutes = std::stod(minutesText, &parsedMinutes);
        if (parsedMinutes != minutesText.size() || !std::isfinite(minutes) ||
            minutes < 0.0 || minutes >= 60.0 || degrees > maximumDegrees ||
            (degrees == maximumDegrees && minutes != 0.0)) {
            return std::nullopt;
        }

        double result = static_cast<double>(degrees) + minutes / 60.0;
        if (hemisphere.front() == negativeHemisphere) {
            result = -result;
        }
        return result;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

ParsedNmeaSentence parseNmeaSentence(const std::string& rawLine) {
    std::string_view line(rawLine);
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
        line.remove_suffix(1);
    }

    if (line.size() < 5 || line.front() != '$') {
        return {};
    }

    const auto checksumSeparator = line.find('*');
    if (checksumSeparator == std::string_view::npos ||
        checksumSeparator + 3 != line.size()) {
        return {};
    }

    const int checksumHigh = hexadecimalValue(line[checksumSeparator + 1]);
    const int checksumLow = hexadecimalValue(line[checksumSeparator + 2]);
    if (checksumHigh < 0 || checksumLow < 0) {
        return {};
    }

    unsigned char calculatedChecksum = 0;
    for (std::size_t index = 1; index < checksumSeparator; ++index) {
        calculatedChecksum ^= static_cast<unsigned char>(line[index]);
    }
    const auto expectedChecksum = static_cast<unsigned char>(
        (checksumHigh << 4) | checksumLow);
    if (calculatedChecksum != expectedChecksum) {
        return {};
    }

    const auto fields = splitFields(line.substr(1, checksumSeparator - 1));
    ParsedNmeaSentence parsed;
    if (fields.empty() || fields[0].size() < 3 ||
        !std::all_of(fields[0].begin(), fields[0].end(), [](char character) {
            return (character >= 'A' && character <= 'Z') ||
                   (character >= '0' && character <= '9');
        })) {
        return parsed;
    }
    parsed.checksumValid = true;

    std::string_view latitudeText;
    std::string_view latitudeHemisphere;
    std::string_view longitudeText;
    std::string_view longitudeHemisphere;

    if (hasSentenceSuffix(fields[0], "RMC")) {
        if (fields.size() < 7 || fields[2] != "A") {
            return parsed;
        }
        latitudeText = fields[3];
        latitudeHemisphere = fields[4];
        longitudeText = fields[5];
        longitudeHemisphere = fields[6];
    } else if (hasSentenceSuffix(fields[0], "GGA")) {
        if (fields.size() < 7) {
            return parsed;
        }
        const auto fixQuality = parseNonNegativeInteger(fields[6]);
        if (!fixQuality || *fixQuality == 0) {
            return parsed;
        }
        latitudeText = fields[2];
        latitudeHemisphere = fields[3];
        longitudeText = fields[4];
        longitudeHemisphere = fields[5];
    } else {
        return parsed;
    }

    const auto latitude = parseCoordinate(
        latitudeText, latitudeHemisphere, 2, 90, 'N', 'S');
    const auto longitude = parseCoordinate(
        longitudeText, longitudeHemisphere, 3, 180, 'E', 'W');
    if (!latitude || !longitude) {
        return parsed;
    }

    parsed.position = GpsPosition{
        *latitude,
        *longitude,
        std::chrono::system_clock::now()
    };
    return parsed;
}

#if defined(TARGET_REAL_GPS) && !defined(_WIN32)
class FileDescriptor {
public:
    explicit FileDescriptor(int value) : value_(value) {}
    ~FileDescriptor() {
        if (value_ >= 0) {
            ::close(value_);
        }
    }

    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;

    int get() const { return value_; }

private:
    int value_;
};

speed_t serialSpeedForBaudRate(int baudRate) {
    if (baudRate == 115200) {
        return B115200;
    }
    throw std::invalid_argument("Unsupported GPS baud rate: " +
                                std::to_string(baudRate));
}

void configureSerialPort(int fileDescriptor, int baudRate) {
    termios settings{};
    if (::tcgetattr(fileDescriptor, &settings) != 0) {
        throw std::runtime_error("tcgetattr failed: " +
                                 std::string(std::strerror(errno)));
    }

    ::cfmakeraw(&settings);
    settings.c_cflag &= ~(CSIZE | PARENB | CSTOPB);
#ifdef CRTSCTS
    settings.c_cflag &= ~CRTSCTS;
#endif
    settings.c_cflag |= CS8 | CLOCAL | CREAD;
    settings.c_iflag &= ~(IXON | IXOFF | IXANY);
    settings.c_cc[VMIN] = 0;
    settings.c_cc[VTIME] = 0;

    const speed_t speed = serialSpeedForBaudRate(baudRate);
    if (::cfsetispeed(&settings, speed) != 0 ||
        ::cfsetospeed(&settings, speed) != 0) {
        throw std::runtime_error("Failed to set GPS baud rate: " +
                                 std::string(std::strerror(errno)));
    }
    if (::tcsetattr(fileDescriptor, TCSANOW, &settings) != 0) {
        throw std::runtime_error("tcsetattr failed: " +
                                 std::string(std::strerror(errno)));
    }
}
#endif

} // namespace

HardwareGpsReceiver::HardwareGpsReceiver()
    : HardwareGpsReceiver(
        hardware::config::gps::SERIAL_DEVICE,
        hardware::config::gps::BAUD_RATE,
        {},
        std::chrono::duration_cast<std::chrono::milliseconds>(
            timing::kGpsRetryInterval),
        std::chrono::duration_cast<std::chrono::milliseconds>(
            timing::kGpsSilenceTimeout),
        std::chrono::duration_cast<std::chrono::milliseconds>(
            timing::kGpsPositionLogInterval),
        timing::kGpsSerialPollInterval,
        {}) {
}

HardwareGpsReceiver::HardwareGpsReceiver(
    ChunkReader chunkReader,
    std::chrono::milliseconds retryInterval,
    std::chrono::milliseconds silenceTimeout,
    std::chrono::milliseconds positionLogInterval,
    PositionLogger positionLogger)
    : HardwareGpsReceiver(
        "injected GPS source",
        hardware::config::gps::BAUD_RATE,
        std::move(chunkReader),
        retryInterval,
        silenceTimeout,
        positionLogInterval,
        std::chrono::milliseconds{1},
        std::move(positionLogger)) {
}

HardwareGpsReceiver::HardwareGpsReceiver(
    std::string serialDevice,
    int baudRate,
    ChunkReader chunkReader,
    std::chrono::milliseconds retryInterval,
    std::chrono::milliseconds silenceTimeout,
    std::chrono::milliseconds positionLogInterval,
    std::chrono::milliseconds serialPollInterval,
    PositionLogger positionLogger)
    : serialDevice_(std::move(serialDevice))
    , baudRate_(baudRate)
    , chunkReader_(std::move(chunkReader))
    , retryInterval_(retryInterval)
    , silenceTimeout_(silenceTimeout)
    , positionLogInterval_(positionLogInterval)
    , serialPollInterval_(serialPollInterval)
    , positionLogger_(std::move(positionLogger)) {
    if (serialDevice_.empty()) {
        throw std::invalid_argument("GPS serial device is required");
    }
    if (retryInterval_ <= std::chrono::milliseconds::zero() ||
        silenceTimeout_ <= std::chrono::milliseconds::zero() ||
        positionLogInterval_ <= std::chrono::milliseconds::zero() ||
        serialPollInterval_ <= std::chrono::milliseconds::zero()) {
        throw std::invalid_argument("GPS timing intervals must be positive");
    }
    if (!positionLogger_) {
        positionLogger_ = [](const GpsPosition& position) {
            LOG_PERIPH_INFO("GPS position: latitude={:.6f}, longitude={:.6f}",
                            position.latitudeDegrees,
                            position.longitudeDegrees);
        };
    }
}

HardwareGpsReceiver::~HardwareGpsReceiver() {
    shutdown();
}

bool HardwareGpsReceiver::initialize() {
    if (initialized_.load(std::memory_order_acquire)) {
        return true;
    }

    LOG_PERIPH_INFO("Initializing GPS receiver (device={}, baud={})",
                    serialDevice_, baudRate_);
    stopRequested_.store(false, std::memory_order_release);
    connected_.store(false, std::memory_order_release);
    hasLoggedPosition_ = false;
    lastPositionLogAt_ = {};

    try {
        workerThread_ = std::thread(&HardwareGpsReceiver::workerLoop, this);
        initialized_.store(true, std::memory_order_release);
        return true;
    } catch (const std::exception& ex) {
        LOG_PERIPH_ERROR("Failed to start GPS receiver worker: {}", ex.what());
        return false;
    }
}

void HardwareGpsReceiver::shutdown() {
    const bool wasInitialized = initialized_.exchange(false, std::memory_order_acq_rel);
    if (!wasInitialized && !workerThread_.joinable()) {
        return;
    }

    stopRequested_.store(true, std::memory_order_release);
    waitCv_.notify_all();
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
    connected_.store(false, std::memory_order_release);
    LOG_PERIPH_INFO("GPS receiver shut down");
}

bool HardwareGpsReceiver::isConnected() const {
    return connected_.load(std::memory_order_acquire);
}

std::optional<GpsPosition> HardwareGpsReceiver::getLastPosition() const {
    std::lock_guard<std::mutex> lock(positionMutex_);
    return lastPosition_;
}

void HardwareGpsReceiver::workerLoop() {
    bool outageActive = false;
    while (!stopRequested_.load(std::memory_order_acquire)) {
        try {
            if (chunkReader_) {
                readInjectedSession(outageActive);
            } else {
                readSerialSession(outageActive);
            }
        } catch (const std::exception& ex) {
            connected_.store(false, std::memory_order_release);
            if (!outageActive) {
                LOG_PERIPH_ERROR("GPS receiver unavailable: {}", ex.what());
                outageActive = true;
            }
        } catch (...) {
            connected_.store(false, std::memory_order_release);
            if (!outageActive) {
                LOG_PERIPH_ERROR("GPS receiver unavailable: unknown error");
                outageActive = true;
            }
        }

        if (stopRequested_.load(std::memory_order_acquire)) {
            break;
        }
        if (waitForStop(retryInterval_)) {
            break;
        }
    }
    connected_.store(false, std::memory_order_release);
}

void HardwareGpsReceiver::readSerialSession(bool& outageActive) {
#if defined(TARGET_REAL_GPS) && !defined(_WIN32)
    const int rawFileDescriptor = ::open(
        serialDevice_.c_str(), O_RDONLY | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (rawFileDescriptor < 0) {
        throw std::runtime_error("Cannot open " + serialDevice_ + ": " +
                                 std::string(std::strerror(errno)));
    }
    FileDescriptor fileDescriptor(rawFileDescriptor);
    configureSerialPort(fileDescriptor.get(), baudRate_);

    std::string lineBuffer;
    bool discardingLine = false;
    auto lastValidTrafficAt = std::chrono::steady_clock::now();
    char buffer[512];

    while (!stopRequested_.load(std::memory_order_acquire)) {
        pollfd descriptor{};
        descriptor.fd = fileDescriptor.get();
        descriptor.events = POLLIN;
        const int pollResult = ::poll(
            &descriptor, 1, static_cast<int>(serialPollInterval_.count()));
        if (pollResult < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::runtime_error("GPS poll failed: " +
                                     std::string(std::strerror(errno)));
        }
        if (pollResult > 0) {
            if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
                throw std::runtime_error("GPS serial device disconnected");
            }
            if ((descriptor.revents & POLLIN) != 0) {
                const ssize_t bytesRead = ::read(
                    fileDescriptor.get(), buffer, sizeof(buffer));
                if (bytesRead > 0) {
                    if (consumeChunk(std::string(buffer, static_cast<std::size_t>(bytesRead)),
                                     lineBuffer, discardingLine, outageActive)) {
                        lastValidTrafficAt = std::chrono::steady_clock::now();
                    }
                } else if (bytesRead == 0) {
                    throw std::runtime_error("GPS serial device reached end of stream");
                } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                    throw std::runtime_error("GPS read failed: " +
                                             std::string(std::strerror(errno)));
                }
            }
        }

        if (std::chrono::steady_clock::now() - lastValidTrafficAt >= silenceTimeout_) {
            throw std::runtime_error("GPS data stream is silent");
        }
    }
#else
    (void)outageActive;
    throw std::runtime_error("Real GPS support is disabled in this build");
#endif
}

void HardwareGpsReceiver::readInjectedSession(bool& outageActive) {
    std::string lineBuffer;
    bool discardingLine = false;
    auto lastValidTrafficAt = std::chrono::steady_clock::now();

    while (!stopRequested_.load(std::memory_order_acquire)) {
        const auto chunk = chunkReader_();
        if (chunk && !chunk->empty()) {
            if (consumeChunk(*chunk, lineBuffer, discardingLine, outageActive)) {
                lastValidTrafficAt = std::chrono::steady_clock::now();
            }
        } else if (waitForStop(serialPollInterval_)) {
            return;
        }

        if (std::chrono::steady_clock::now() - lastValidTrafficAt >= silenceTimeout_) {
            throw std::runtime_error("GPS data stream is silent");
        }
    }
}

bool HardwareGpsReceiver::consumeChunk(const std::string& chunk,
                                       std::string& lineBuffer,
                                       bool& discardingLine,
                                       bool& outageActive) {
    bool receivedValidTraffic = false;
    for (char character : chunk) {
        if (character == '\n') {
            if (!discardingLine && !lineBuffer.empty()) {
                receivedValidTraffic = processLine(lineBuffer, outageActive) ||
                                       receivedValidTraffic;
            }
            lineBuffer.clear();
            discardingLine = false;
            continue;
        }
        if (character == '\r' || discardingLine) {
            continue;
        }
        if (lineBuffer.size() >= kMaximumNmeaLineLength) {
            lineBuffer.clear();
            discardingLine = true;
            continue;
        }
        lineBuffer.push_back(character);
    }
    return receivedValidTraffic;
}

bool HardwareGpsReceiver::processLine(const std::string& line, bool& outageActive) {
    const auto parsed = parseNmeaSentence(line);
    if (!parsed.checksumValid) {
        return false;
    }

    const bool wasConnected = connected_.exchange(true, std::memory_order_acq_rel);
    if (outageActive) {
        LOG_PERIPH_INFO("GPS receiver recovered ({})", serialDevice_);
        outageActive = false;
    } else if (!wasConnected) {
        LOG_PERIPH_DEBUG("GPS receiver is receiving valid NMEA traffic ({})",
                         serialDevice_);
    }

    if (parsed.position) {
        storePosition(*parsed.position);
    }
    return true;
}

void HardwareGpsReceiver::storePosition(const GpsPosition& position) {
    {
        std::lock_guard<std::mutex> lock(positionMutex_);
        lastPosition_ = position;
    }

    const auto now = std::chrono::steady_clock::now();
    if (!hasLoggedPosition_ || now - lastPositionLogAt_ >= positionLogInterval_) {
        hasLoggedPosition_ = true;
        lastPositionLogAt_ = now;
        try {
            positionLogger_(position);
        } catch (const std::exception& ex) {
            LOG_PERIPH_ERROR("Failed to log GPS position: {}", ex.what());
        } catch (...) {
            LOG_PERIPH_ERROR("Failed to log GPS position: unknown error");
        }
    }
}

bool HardwareGpsReceiver::waitForStop(std::chrono::milliseconds duration) {
    std::unique_lock<std::mutex> lock(waitMutex_);
    return waitCv_.wait_for(lock, duration, [this]() {
        return stopRequested_.load(std::memory_order_acquire);
    });
}

} // namespace fuelflux::peripherals
