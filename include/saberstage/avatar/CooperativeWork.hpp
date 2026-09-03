// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Owns a native, move-only cooperative job with explicit main-thread steps.
// - Cancellation destroys suspended state; it never pumps Unity or runs a worker.
#pragma once
#include <coroutine>
#include <exception>
#include <utility>

namespace saberstage::avatar {
class CooperativeWork {
public:
    struct promise_type;
    using Handle = std::coroutine_handle<promise_type>;
    struct promise_type {
        std::exception_ptr error;
        CooperativeWork get_return_object() { return CooperativeWork(Handle::from_promise(*this)); }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        std::suspend_always yield_value(std::nullptr_t) noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept { error = std::current_exception(); }
    };
    CooperativeWork() = default;
    explicit CooperativeWork(Handle handle) : handle_(handle) {}
    CooperativeWork(const CooperativeWork&) = delete;
    CooperativeWork& operator=(const CooperativeWork&) = delete;
    CooperativeWork(CooperativeWork&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
    CooperativeWork& operator=(CooperativeWork&& other) noexcept {
        if (this != &other) { Reset(); handle_ = std::exchange(other.handle_, {}); }
        return *this;
    }
    ~CooperativeWork() { Reset(); }
    void Reset() noexcept { if (handle_) handle_.destroy(); handle_ = {}; }
    bool Resume() {
        if (!handle_ || handle_.done()) return false;
        handle_.resume();
        if (handle_.promise().error) std::rethrow_exception(handle_.promise().error);
        return !handle_.done();
    }
private:
    Handle handle_{};
};
} // namespace saberstage::avatar
