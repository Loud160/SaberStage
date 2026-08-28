#pragma once

#include "saberstage/avatar/AvatarSolver.hpp"
#include "saberstage/avatar/vrm/VrmUnityRuntime.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace UnityEngine {
class Animator;
}

namespace saberstage::camera {
class CameraManager;
}

namespace saberstage::avatar {

// Owns the Unity/IL2CPP boundary for one humanoid avatar. A VRM loader supplies
// a humanoid Animator once its asset is ready; no loader-specific type crosses
// into the native solver.
class AvatarManager final {
public:
    explicit AvatarManager(camera::CameraManager& camera);
    ~AvatarManager();

    AvatarManager(const AvatarManager&) = delete;
    AvatarManager& operator=(const AvatarManager&) = delete;

    bool Start();
    void Stop() noexcept;
    bool BindHumanoidAnimator(
        UnityEngine::Animator* animator,
        Pose leftControllerToWrist = {},
        Pose rightControllerToWrist = {},
        Vec3 modelForward = {0.0F, 0.0F, 1.0F}) noexcept;
    void UnbindHumanoidAnimator() noexcept;
    bool RecalibrateNeutral() noexcept;

    bool LoadVrmAvatar(
        const std::filesystem::path& path,
        std::uint32_t maximumTextureDimension = 1024,
        std::string* error = nullptr,
        bool bindSolver = true) noexcept;
    bool BindLoadedVrmAvatar(std::string* error = nullptr) noexcept;
    void UnloadVrmAvatar() noexcept;
    void SetAvatarVisible(bool visible) noexcept;
    void SetControllerToWristOffsets(Pose left, Pose right) noexcept;
    bool SetExpression(std::string_view presetName, float weight, std::string* error = nullptr) noexcept;

    void SampleTracking() noexcept;
    void SolveAndWrite() noexcept;
    void EnsureSolvedForSpectatorRender() noexcept;
    void LogDiagnostics() const noexcept;

    [[nodiscard]] bool IsBound() const noexcept;
    [[nodiscard]] bool HasLoadedVrmAvatar() const noexcept;
    [[nodiscard]] const vrm::VrmAsset* LoadedVrmAsset() const noexcept;
    [[nodiscard]] const vrm::RuntimeStatistics* LoadedVrmStatistics() const noexcept;
    [[nodiscard]] const AvatarCalibration& Calibration() const noexcept;
    [[nodiscard]] const PlayerCalibration& Player() const noexcept;
    [[nodiscard]] const SolverDiagnostics& Diagnostics() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace saberstage::avatar
