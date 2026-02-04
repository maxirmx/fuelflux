// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include "peripherals/gpio_gpiod.h"
#include <gpiod.h>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <sstream>

struct GpioLine::Impl {
    gpiod_chip* chip{nullptr};
    gpiod_line* line{nullptr};
    bool is_output{false};
};

static std::runtime_error gpiod_err(const std::string& what) {
    std::ostringstream oss;
    oss << what << " (errno=" << errno << ": " << std::strerror(errno) << ")";
    return std::runtime_error(oss.str());
}

GpioLine::GpioLine(int line_offset, bool output, bool initial_value,
                   std::string chip_path, std::string consumer) {
    impl_ = new Impl();
    errno = 0;
    impl_->chip = gpiod_chip_open(chip_path.c_str());
    if (!impl_->chip) throw gpiod_err("Failed to open gpio chip " + chip_path);

    errno = 0;
    impl_->line = gpiod_chip_get_line(impl_->chip, line_offset);
    if (!impl_->line) throw gpiod_err("Failed to get gpio line offset " + std::to_string(line_offset));

    impl_->is_output = output;
    if (output) {
        errno = 0;
        if (gpiod_line_request_output(impl_->line, consumer.c_str(), initial_value ? 1 : 0) != 0) {
            throw gpiod_err("Failed to request output line " + std::to_string(line_offset));
        }
    } else {
        errno = 0;
        if (gpiod_line_request_input(impl_->line, consumer.c_str()) != 0) {
            throw gpiod_err("Failed to request input line " + std::to_string(line_offset));
        }
    }
}

GpioLine::~GpioLine() {
    if (!impl_) return;
    if (impl_->line) gpiod_line_release(impl_->line);
    if (impl_->chip) gpiod_chip_close(impl_->chip);
    delete impl_;
    impl_ = nullptr;
}

void GpioLine::set(bool value) {
    if (!impl_->is_output) throw std::runtime_error("GPIO line is not output");
    errno = 0;
    if (gpiod_line_set_value(impl_->line, value ? 1 : 0) != 0) throw gpiod_err("Failed to set gpio value");
}

bool GpioLine::get() const {
    errno = 0;
    int v = gpiod_line_get_value(impl_->line);
    if (v < 0) throw gpiod_err("Failed to read gpio value");
    return v != 0;
}

// Soft PWM helper
struct SoftPwmThread {
    std::thread t;
    std::atomic<bool> stop{false};
    std::atomic<int> duty{0};
    GpioLine* line{nullptr};
    int freq{1000};
};

static void pwm_loop(SoftPwmThread* th) {
    using namespace std::chrono;
    const double period = 1.0 / (th->freq > 0 ? th->freq : 500);
    while (!th->stop.load()) {
        int d = th->duty.load();
        double on = period * (double)d / 100.0;
        double off = period - on;
        if (on > 0) { th->line->set(true); std::this_thread::sleep_for(duration<double>(on)); }
        if (off > 0) { th->line->set(false); std::this_thread::sleep_for(duration<double>(off)); }
    }
    th->line->set(false);
}

SoftPwm::SoftPwm(GpioLine& line, int frequency_hz) : line_(line), freq_(frequency_hz) {}
SoftPwm::~SoftPwm() { stop(); }

void SoftPwm::clamp() {
    if (duty_ < 0) duty_ = 0;
    if (duty_ > 100) duty_ = 100;
}

void SoftPwm::start(int duty_percent) {
    if (running_) return;
    duty_ = duty_percent; clamp();
    auto* th = new SoftPwmThread();
    th->line = &line_;
    th->freq = freq_;
    th->duty.store(duty_);
    thread_ = th;
    running_ = true;
    th->t = std::thread(pwm_loop, th);
}

void SoftPwm::set_duty(int duty_percent) {
    duty_ = duty_percent; clamp();
    if (thread_) static_cast<SoftPwmThread*>(thread_)->duty.store(duty_);
}

void SoftPwm::stop() {
    if (!running_ || !thread_) return;
    auto* th = static_cast<SoftPwmThread*>(thread_);
    th->stop.store(true);
    if (th->t.joinable()) th->t.join();
    delete th;
    thread_ = nullptr;
    running_ = false;
}
