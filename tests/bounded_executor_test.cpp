// Copyright (C) 2025, 2026 Maxim [maxirmx] Samsonov (www.sw.consulting)
// All rights reserved.
// This file is a part of fuelflux application

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include "bounded_executor.h"

using namespace fuelflux;

TEST(BoundedExecutorTest, ExecutesTasksInBoundedThreads) {
    BoundedExecutor executor(2, 10);
    std::atomic<int> counter{0};
    
    // Submit 5 tasks
    for (int i = 0; i < 5; ++i) {
        bool submitted = executor.Submit([&counter]() {
            counter++;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        });
        EXPECT_TRUE(submitted);
    }
    
    // Wait for tasks to complete deterministically
    executor.Shutdown();
    EXPECT_EQ(counter.load(), 5);
}

TEST(BoundedExecutorTest, RejectsTasksWhenQueueFull) {
    BoundedExecutor executor(1, 3);  // 1 worker, max 3 tasks in queue
    std::atomic<int> tasksStarted{0};
    std::atomic<int> tasksCompleted{0};
    std::atomic<bool> blockTasks{true};
    
    // Submit 3 tasks to fill the queue
    for (int i = 0; i < 3; ++i) {
        bool submitted = executor.Submit([&tasksStarted, &tasksCompleted, &blockTasks]() {
            tasksStarted++;
            while (blockTasks.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            tasksCompleted++;
        });
        EXPECT_TRUE(submitted) << "Task " << i << " should be accepted";
    }
    
    // Wait for first task to start executing (removed from queue)
    while (tasksStarted.load() == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // At this point: 1 task executing, 2 tasks in queue
    // Queue still has 1 slot free, so we should be able to add 1 more
    bool submitted1 = executor.Submit([&tasksCompleted, &blockTasks]() {
        while (blockTasks.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        tasksCompleted++;
    });
    EXPECT_TRUE(submitted1) << "Should accept task when queue has space";
    
    // Now queue is full (3 tasks), try to add one more - should be rejected
    bool submitted2 = executor.Submit([&tasksCompleted]() {
        tasksCompleted++;
    });
    EXPECT_FALSE(submitted2) << "Should reject task when queue is full";
    
    // Unblock tasks and wait for them to complete
    blockTasks.store(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT_EQ(tasksCompleted.load(), 4);
}

TEST(BoundedExecutorTest, ShutdownWaitsForTasks) {
    BoundedExecutor executor(2, 10);
    std::atomic<int> counter{0};
    
    // Submit tasks
    for (int i = 0; i < 5; ++i) {
        executor.Submit([&counter]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            counter++;
        });
    }
    
    // Shutdown should wait for all tasks to complete
    executor.Shutdown();
    EXPECT_EQ(counter.load(), 5);
}

TEST(BoundedExecutorTest, RejectsTasksAfterShutdown) {
    BoundedExecutor executor(2, 10);
    executor.Shutdown();
    
    std::atomic<int> counter{0};
    bool submitted = executor.Submit([&counter]() {
        counter++;
    });
    
    EXPECT_FALSE(submitted);
    EXPECT_EQ(counter.load(), 0);
}

TEST(BoundedExecutorTest, HandlesExceptionsInTasks) {
    BoundedExecutor executor(1, 5);
    std::atomic<int> counter{0};
    
    // Submit task that throws
    executor.Submit([]() {
        throw std::runtime_error("Test exception");
    });
    
    // Submit normal task - should still execute
    executor.Submit([&counter]() {
        counter++;
    });
    
    // Deterministically wait for all tasks (including the normal one) to complete
    executor.Shutdown();
    EXPECT_EQ(counter.load(), 1);
}
