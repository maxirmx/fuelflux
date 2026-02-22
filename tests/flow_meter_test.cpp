// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include "peripherals/flow_meter.h"

using namespace fuelflux;
using namespace fuelflux::peripherals;

// Test fixture for HardwareFlowMeter simulation features
class FlowMeterSimulationTest : public ::testing::Test {
protected:
    std::unique_ptr<HardwareFlowMeter> flowMeter;
    bool gpioAvailable = false;
    
    void SetUp() override {
        flowMeter = std::make_unique<HardwareFlowMeter>();
        // Initialize - may fail if GPIO hardware is not available
        gpioAvailable = flowMeter->initialize();
    }
    
    void TearDown() override {
        if (flowMeter) {
            flowMeter->shutdown();
        }
    }
};

#ifdef TARGET_REAL_FLOW_METER
// Tests that only run when TARGET_REAL_FLOW_METER is defined

TEST_F(FlowMeterSimulationTest, SimulationDisabledByDefault) {
    EXPECT_FALSE(flowMeter->isSimulationEnabled());
}

TEST_F(FlowMeterSimulationTest, CanEnableSimulation) {
    EXPECT_TRUE(flowMeter->setSimulationEnabled(true));
    EXPECT_TRUE(flowMeter->isSimulationEnabled());
}

TEST_F(FlowMeterSimulationTest, CanDisableSimulation) {
    EXPECT_TRUE(flowMeter->setSimulationEnabled(true));
    EXPECT_TRUE(flowMeter->isSimulationEnabled());
    
    EXPECT_TRUE(flowMeter->setSimulationEnabled(false));
    EXPECT_FALSE(flowMeter->isSimulationEnabled());
}

TEST_F(FlowMeterSimulationTest, CannotToggleSimulationWhileMeasuring) {
    if (!gpioAvailable) {
        GTEST_SKIP() << "GPIO hardware not available for this test";
    }
    
    // Enable simulation before starting measurement
    EXPECT_TRUE(flowMeter->setSimulationEnabled(true));
    
    // Start measurement
    flowMeter->startMeasurement();
    
    // Try to disable simulation while measuring - should fail
    EXPECT_FALSE(flowMeter->setSimulationEnabled(false));
    EXPECT_TRUE(flowMeter->isSimulationEnabled());
    
    // Stop measurement
    flowMeter->stopMeasurement();
    
    // Now we should be able to toggle
    EXPECT_TRUE(flowMeter->setSimulationEnabled(false));
    EXPECT_FALSE(flowMeter->isSimulationEnabled());
}

TEST_F(FlowMeterSimulationTest, SimulationGeneratesPulses) {
    if (!gpioAvailable) {
        GTEST_SKIP() << "GPIO hardware not available for this test";
    }
    
    // Enable simulation
    EXPECT_TRUE(flowMeter->setSimulationEnabled(true));
    
    // Start measurement
    flowMeter->startMeasurement();
    
    // Wait for some simulated pulses to be generated
    // Default simulation rate is 1.0 L/s, so after ~200ms we should have some volume
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    
    Volume volume = flowMeter->getCurrentVolume();
    
    // Stop measurement
    flowMeter->stopMeasurement();
    
    // Volume should be greater than 0 due to simulation
    EXPECT_GT(volume, 0.0);
    // Should be roughly 0.25 liters (1.0 L/s * 0.25s), allow for timing variance
    EXPECT_LT(volume, 0.5);
}

TEST_F(FlowMeterSimulationTest, SimulationRespectsMeasurementStartStop) {
    if (!gpioAvailable) {
        GTEST_SKIP() << "GPIO hardware not available for this test";
    }
    
    // Enable simulation
    EXPECT_TRUE(flowMeter->setSimulationEnabled(true));
    
    // Don't start measurement yet - volume should stay at 0
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(flowMeter->getCurrentVolume(), 0.0);
    
    // Now start measurement
    flowMeter->startMeasurement();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    Volume volumeWhileMeasuring = flowMeter->getCurrentVolume();
    EXPECT_GT(volumeWhileMeasuring, 0.0);
    
    // Stop measurement
    flowMeter->stopMeasurement();
    Volume volumeAfterStop = flowMeter->getCurrentVolume();
    
    // Wait a bit more - volume should not increase after stopping
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_EQ(flowMeter->getCurrentVolume(), volumeAfterStop);
}

TEST_F(FlowMeterSimulationTest, SimulationInvokesCallback) {
    if (!gpioAvailable) {
        GTEST_SKIP() << "GPIO hardware not available for this test";
    }
    
    std::atomic<int> callbackCount{0};
    Volume lastCallbackVolume = 0.0;
    
    flowMeter->setFlowCallback([&callbackCount, &lastCallbackVolume](Volume vol) {
        callbackCount++;
        lastCallbackVolume = vol;
    });
    
    // Enable simulation and start measurement
    EXPECT_TRUE(flowMeter->setSimulationEnabled(true));
    flowMeter->startMeasurement();
    
    // Wait for callbacks (simulation thread calls callback every 100ms when pulses are added)
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    
    flowMeter->stopMeasurement();
    
    // Should have received multiple callbacks during measurement
    EXPECT_GT(callbackCount.load(), 0);
    // Last callback volume should be non-zero
    EXPECT_GT(lastCallbackVolume, 0.0);
}

TEST_F(FlowMeterSimulationTest, ResetCounterClearsVolume) {
    if (!gpioAvailable) {
        GTEST_SKIP() << "GPIO hardware not available for this test";
    }
    
    // Enable simulation and measure some volume
    EXPECT_TRUE(flowMeter->setSimulationEnabled(true));
    flowMeter->startMeasurement();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    flowMeter->stopMeasurement();
    
    Volume volumeBeforeReset = flowMeter->getTotalVolume();
    EXPECT_GT(volumeBeforeReset, 0.0);
    
    // Reset counter
    flowMeter->resetCounter();
    
    // Both current and total should be zero
    EXPECT_EQ(flowMeter->getCurrentVolume(), 0.0);
    EXPECT_EQ(flowMeter->getTotalVolume(), 0.0);
}

TEST_F(FlowMeterSimulationTest, TotalVolumeAccumulates) {
    if (!gpioAvailable) {
        GTEST_SKIP() << "GPIO hardware not available for this test";
    }
    
    EXPECT_TRUE(flowMeter->setSimulationEnabled(true));
    
    // First measurement
    flowMeter->startMeasurement();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    flowMeter->stopMeasurement();
    Volume firstMeasurement = flowMeter->getCurrentVolume();
    Volume totalAfterFirst = flowMeter->getTotalVolume();
    
    EXPECT_GT(firstMeasurement, 0.0);
    EXPECT_EQ(totalAfterFirst, firstMeasurement);
    
    // Second measurement
    flowMeter->startMeasurement();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    flowMeter->stopMeasurement();
    Volume secondMeasurement = flowMeter->getCurrentVolume();
    Volume totalAfterSecond = flowMeter->getTotalVolume();
    
    EXPECT_GT(secondMeasurement, 0.0);
    // Total should be sum of both measurements
    EXPECT_DOUBLE_EQ(totalAfterSecond, firstMeasurement + secondMeasurement);
}

TEST_F(FlowMeterSimulationTest, SimulationThreadShutsDownCleanly) {
    if (!gpioAvailable) {
        GTEST_SKIP() << "GPIO hardware not available for this test";
    }
    
    // Enable simulation and start measurement
    EXPECT_TRUE(flowMeter->setSimulationEnabled(true));
    flowMeter->startMeasurement();
    
    // Let it run briefly
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Stop measurement should cleanly shut down the simulation thread
    flowMeter->stopMeasurement();
    
    // Should be able to start and stop again
    flowMeter->startMeasurement();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    flowMeter->stopMeasurement();
    
    // No crashes or hangs indicates clean shutdown
    EXPECT_TRUE(true);
}

TEST_F(FlowMeterSimulationTest, MultipleStartStopCycles) {
    if (!gpioAvailable) {
        GTEST_SKIP() << "GPIO hardware not available for this test";
    }
    
    EXPECT_TRUE(flowMeter->setSimulationEnabled(true));
    
    for (int i = 0; i < 5; ++i) {
        flowMeter->startMeasurement();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        flowMeter->stopMeasurement();
        
        // Each cycle should produce some volume
        EXPECT_GT(flowMeter->getCurrentVolume(), 0.0);
    }
    
    // Total volume should be sum of all cycles
    EXPECT_GT(flowMeter->getTotalVolume(), 0.0);
}

TEST_F(FlowMeterSimulationTest, SimulationStateThreadSafe) {
    if (!gpioAvailable) {
        GTEST_SKIP() << "GPIO hardware not available for this test";
    }
    
    // This test verifies atomic operations are properly used
    EXPECT_TRUE(flowMeter->setSimulationEnabled(true));
    
    // Start measurement in simulation mode
    flowMeter->startMeasurement();
    
    // Try to query simulation state from this thread while simulation thread is running
    for (int i = 0; i < 100; ++i) {
        bool enabled = flowMeter->isSimulationEnabled();
        EXPECT_TRUE(enabled);
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    
    flowMeter->stopMeasurement();
    
    // Should still be in consistent state
    EXPECT_TRUE(flowMeter->isSimulationEnabled());
}

TEST_F(FlowMeterSimulationTest, CallbackInvokedOnStopMeasurement) {
    if (!gpioAvailable) {
        GTEST_SKIP() << "GPIO hardware not available for this test";
    }
    
    std::atomic<int> callbackCount{0};
    Volume finalVolume = 0.0;
    
    flowMeter->setFlowCallback([&callbackCount, &finalVolume](Volume vol) {
        callbackCount++;
        finalVolume = vol;
    });
    
    // Enable simulation and measure
    EXPECT_TRUE(flowMeter->setSimulationEnabled(true));
    flowMeter->startMeasurement();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    int countBeforeStop = callbackCount.load();
    flowMeter->stopMeasurement();
    
    // Callback should have been invoked at least once more on stopMeasurement
    EXPECT_GT(callbackCount.load(), countBeforeStop);
    EXPECT_GT(finalVolume, 0.0);
}

#else
// Tests for non-TARGET_REAL_FLOW_METER builds (stub mode)

TEST_F(FlowMeterSimulationTest, SimulationNotSupportedInStubMode) {
    // setSimulationEnabled should return false in stub mode
    EXPECT_FALSE(flowMeter->setSimulationEnabled(true));
    EXPECT_FALSE(flowMeter->setSimulationEnabled(false));
    
    // isSimulationEnabled should always return false
    EXPECT_FALSE(flowMeter->isSimulationEnabled());
}

TEST_F(FlowMeterSimulationTest, StubModeBasicFunctionality) {
    // Verify basic flow meter operations work in stub mode
    flowMeter->startMeasurement();
    EXPECT_EQ(flowMeter->getCurrentVolume(), 0.0);
    
    flowMeter->stopMeasurement();
    EXPECT_EQ(flowMeter->getCurrentVolume(), 0.0);
    EXPECT_EQ(flowMeter->getTotalVolume(), 0.0);
}

#endif

// Tests that run in both modes

TEST_F(FlowMeterSimulationTest, InitializeSucceeds) {
    // When GPIO hardware is available, initialization should succeed
    // When not available (e.g., in CI), it will fail
    if (gpioAvailable) {
        EXPECT_TRUE(flowMeter->isConnected());
    } else {
        EXPECT_FALSE(flowMeter->isConnected());
    }
}

TEST_F(FlowMeterSimulationTest, ShutdownWorks) {
    flowMeter->shutdown();
    EXPECT_FALSE(flowMeter->isConnected());
}

TEST_F(FlowMeterSimulationTest, CanStartStopMeasurement) {
    flowMeter->startMeasurement();
    // Basic operation should work without crashing
    flowMeter->stopMeasurement();
    EXPECT_TRUE(true);
}
