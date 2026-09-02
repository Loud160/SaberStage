#pragma once

#include <exception>
#include <cstdint>
#include <mutex>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>

namespace saberstage {

// Central recovery boundary for unexpected native failures. Worker threads may
// enqueue messages, but only TickMainThread is allowed to touch Unity UI.
class ErrorManager final {
public:
    static ErrorManager& Instance() noexcept;

    void ReportInternal(
        std::string_view context,
        std::string_view detail,
        std::source_location source = std::source_location::current()) noexcept;
    void ReportUserVisible(
        std::string title,
        std::string detail) noexcept;
    void TickMainThread() noexcept;

    template <typename Function>
    bool Guard(
        std::string_view context,
        Function&& function,
        std::string_view userTitle = {},
        std::string_view userDetail = {},
        std::source_location source = std::source_location::current()) noexcept {
        try {
            std::forward<Function>(function)();
            return true;
        } catch (const std::exception& exception) {
            ReportInternal(context, exception.what(), source);
        } catch (...) {
            ReportInternal(context, "unknown native exception", source);
        }
        if (!userTitle.empty()) {
            ReportUserVisible(
                std::string(userTitle),
                userDetail.empty()
                    ? "SaberStage could not complete this operation. Details were written to the SaberStage log."
                    : std::string(userDetail));
        }
        return false;
    }

private:
    ErrorManager() = default;

    void TickMainThreadImpl();
    void Acknowledge(std::uint64_t generation) noexcept;
    void Release(std::uint64_t generation, bool requeue) noexcept;
    void RecordDialogFailure(std::string_view detail) noexcept;

    std::mutex mutex_;
    std::optional<std::pair<std::string, std::string>> pendingDialog_;
    std::optional<std::pair<std::string, std::string>> activeDialog_;
    std::uint64_t dialogGeneration_ = 0;
    bool dialogVisible_ = false;
    bool dialogAcknowledged_ = false;
    bool dialogFailureLogged_ = false;
};

} // namespace saberstage
