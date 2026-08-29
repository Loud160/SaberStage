#pragma once

#include "saberstage/avatar/PoseTypes.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace saberstage::avatar::calibration {

inline constexpr std::uint32_t kPlayerProfileVersion = 1;
inline constexpr std::uint32_t kCalibrationAlgorithmVersion = 2;

enum class CalibrationMode : std::uint8_t { Basic, Advanced };

// Automatic preserves the streamlined, hands-off sequence. StepByStep adds
// explicit user gates before each countdown and between accepted captures so
// a new user can read every instruction at their own pace.
enum class CalibrationProgression : std::uint8_t { Automatic, StepByStep };

enum class CalibrationStep : std::uint8_t {
    Neutral,
    ArmsDown,
    ArmsT,
    ArmsForward,
    ArmsOutward45,
    ArmsY,
    ArmsOverhead,
    HandsChest,
    SameSideShoulders,
    CrossBody,
    LookLeft,
    LookRight,
    LookUp,
    LookDown,
    LeanLeft,
    LeanRight,
    LeanForward,
    LeanBackward,
    Squat,
    ForwardDuck,
    StepLeft,
    StepRight,
    StepForward,
    StepBackward,
    TurnLeft45,
    TurnRight45,
    Count,
};

inline constexpr std::size_t kCalibrationStepCount =
    static_cast<std::size_t>(CalibrationStep::Count);

enum class CalibrationPhase : std::uint8_t {
    Idle,
    Introduction,
    AwaitingStepStart,
    Preparing,
    Capturing,
    AwaitingContinue,
    AwaitingRetry,
    Review,
    Complete,
    Failed,
};

enum class CalibrationCue : std::uint8_t {
    None,
    CountdownTick,
    MeasurementStarted,
    MeasurementCompleted,
};

struct CalibrationFrame {
    float timeSeconds = 0.0F;
    TrackedPose head{};
    TrackedPose controller[2]{};
    TrackedPose grip[2]{};
    bool gripObserved[2]{};
};

struct StaticCaptureSummary {
    CalibrationStep step = CalibrationStep::Neutral;
    Pose head{};
    Pose controller[2]{};
    Pose grip[2]{};
    float effectiveReach[2]{};
    float durationSeconds = 0.0F;
    float stableSampleFraction = 0.0F;
    float confidence = 0.0F;
    bool usedSaberGrip[2]{};
    bool valid = false;
};

struct MotionFeatures {
    Vec3 peakHeadDisplacementNormalized{};
    Vec3 finalHeadDisplacementNormalized{};
    Vec3 peakControllerMidpointDisplacementNormalized{};
    Vec3 finalControllerMidpointDisplacementNormalized{};
    float peakHeadSpeedNormalized = 0.0F;
    float peakControllerMidpointSpeedNormalized = 0.0F;
    float peakRollRadians = 0.0F;
    float peakPitchRadians = 0.0F;
    float finalYawRadians = 0.0F;
    float peakYawRadians = 0.0F;
    float returnFraction = 0.0F;
    float durationSeconds = 0.0F;
};

struct MotionCapture {
    CalibrationStep step = CalibrationStep::LeanLeft;
    std::vector<CalibrationFrame> frames;
    MotionFeatures features{};
    float confidence = 0.0F;
    bool valid = false;
};

struct GripFit {
    // Controller-to-visible-grip is directly observable when Beat Saber exposes
    // both transforms. gripToCanonicalHand is avatar-independent and is later
    // combined with each VRM's rest hand axes.
    Pose controllerToGrip[2]{};
    Pose gripToCanonicalHand[2]{};
    std::uint32_t controllerToGripObservationCount[2]{};
    bool controllerToGripObserved[2]{};
    bool fitUsesSaberGrip[2]{};
    float meanPositionResidual[2]{};
    float maximumPositionResidual[2]{};
    float meanRotationResidualDegrees[2]{};
    float maximumRotationResidualDegrees[2]{};
    float confidence[2]{};
};

struct ReachModel {
    float effectiveReachNormalized[2]{};
    float maximumComfortableExtensionNormalized[2]{};
    float neutralReachNormalized[2]{};
    float forwardReachNormalized[2]{};
    float overheadReachNormalized[2]{};
    float crossBodyReachNormalized[2]{};
    float confidence[2]{};
};

struct DirectionalMotionSignature {
    float peakDisplacementNormalized = 0.0F;
    float finalDisplacementNormalized = 0.0F;
    float controllerMidpointNormalized = 0.0F;
    float peakSpeedNormalized = 0.0F;
    float headTiltRadians = 0.0F;
    float returnFraction = 0.0F;
    float durationSeconds = 0.0F;
    float confidence = 0.0F;
};

struct LeanEnvelope {
    float leftNormalized = 0.08F;
    float rightNormalized = 0.08F;
    float forwardNormalized = 0.10F;
    float backwardNormalized = 0.07F;
    DirectionalMotionSignature directions[4]{};
    float confidence = 0.0F;
};

struct CrouchModel {
    float squatDropNormalized = 0.22F;
    float squatForwardNormalized = 0.02F;
    float duckDropNormalized = 0.16F;
    float duckForwardNormalized = 0.12F;
    float squatConfidence = 0.0F;
    float duckConfidence = 0.0F;
};

struct TurnModel {
    float softNeckConeDegrees = 28.0F;
    float turnDwellSeconds = 0.16F;
    float settleHoldSeconds = 0.14F;
    float bodyYawDegreesPerSecond = 105.0F;
    float leftConfidence = 0.0F;
    float rightConfidence = 0.0F;
};

struct PlayerCalibrationProfile {
    std::uint32_t profileVersion = kPlayerProfileVersion;
    std::uint32_t algorithmVersion = kCalibrationAlgorithmVersion;
    std::string calibratedAtUtc;
    std::string deviceConfiguration = "Quest-HMD-two-controllers";
    CalibrationMode mode = CalibrationMode::Basic;
    std::array<StaticCaptureSummary, kCalibrationStepCount> staticCaptures{};
    std::array<MotionCapture, kCalibrationStepCount> motionCaptures{};
    GripFit grip{};
    ReachModel reach{};
    LeanEnvelope lean{};
    DirectionalMotionSignature steps[4]{};
    CrouchModel crouch{};
    TurnModel turn{};
    float overallConfidence = 0.0F;
    bool complete = false;
    bool valid = false;
};

// Fixed-size, trivially-copyable gameplay view of the persistent profile.
// Calibration trajectories never cross into the per-frame solver.
struct RuntimePlayerProfile {
    Pose controllerToGrip[2]{};
    Quaternion gripToCanonicalHand[2]{};
    bool controllerToGripObserved[2]{};
    bool gripFitUsesSaber[2]{};
    float effectiveReachNormalized[2]{};
    float leanBoundaryNormalized[4]{0.08F, 0.08F, 0.10F, 0.07F};
    DirectionalMotionSignature leanSignature[4]{};
    DirectionalMotionSignature stepSignature[4]{};
    CrouchModel crouch{};
    TurnModel turn{};
    float gripResidualDegrees[2]{};
    float overallConfidence = 0.0F;
    bool valid = false;
};

struct ProfileLoadResult {
    bool loaded = false;
    bool incompatible = false;
    bool repairedFallback = false;
    std::string message;
};

[[nodiscard]] std::string_view CalibrationStepName(CalibrationStep step) noexcept;
[[nodiscard]] std::string_view CalibrationInstruction(CalibrationStep step) noexcept;
[[nodiscard]] std::string_view CalibrationCaptureInstruction(CalibrationStep step) noexcept;
[[nodiscard]] bool IsStaticCalibrationStep(CalibrationStep step) noexcept;
[[nodiscard]] RuntimePlayerProfile BuildRuntimeProfile(const PlayerCalibrationProfile& profile) noexcept;
[[nodiscard]] ProfileLoadResult LoadPlayerCalibrationProfile(
    const std::filesystem::path& path,
    PlayerCalibrationProfile& profile) noexcept;
bool SavePlayerCalibrationProfile(
    const std::filesystem::path& path,
    const PlayerCalibrationProfile& profile,
    std::string* error = nullptr) noexcept;
bool FitPlayerCalibrationProfile(PlayerCalibrationProfile& profile, std::string* error = nullptr) noexcept;

} // namespace saberstage::avatar::calibration

static_assert(std::is_trivially_copyable_v<saberstage::avatar::calibration::RuntimePlayerProfile>);
