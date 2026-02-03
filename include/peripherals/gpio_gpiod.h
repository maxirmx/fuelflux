#pragma once
#include <string>

class GpioLine {
public:
    GpioLine(int line_offset, bool output, bool initial_value,
             std::string chip_path = "/dev/gpiochip0",
             std::string consumer = "nhd12864");
    ~GpioLine();

    GpioLine(const GpioLine&) = delete;
    GpioLine& operator=(const GpioLine&) = delete;

    void set(bool value);
    bool get() const;

private:
    struct Impl;
    Impl* impl_;
};

class SoftPwm {
public:
    SoftPwm(GpioLine& line, int frequency_hz);
    ~SoftPwm();

    void start(int duty_percent);
    void set_duty(int duty_percent);
    void stop();

private:
    GpioLine& line_;
    int freq_;
    int duty_{0};
    bool running_{false};
    void* thread_{nullptr};
    void clamp();
};
