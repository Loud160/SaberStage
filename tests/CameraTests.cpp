#include "saberstage/camera/CameraProfile.hpp"
#include "saberstage/camera/FrameDemand.hpp"
#include "saberstage/camera/Math.hpp"
#include "saberstage/camera/MotionPipeline.hpp"
#include "saberstage/camera/MovementScript.hpp"
#include "saberstage/preview/PreviewRenderPolicy.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool Near(float left, float right, float epsilon = 0.002F) {
    return std::abs(left - right) <= epsilon;
}

float VectorLength(saberstage::camera::Vec3 value) {
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

float DirectionDot(saberstage::camera::Vec3 left, saberstage::camera::Vec3 right) {
    const auto leftLength = VectorLength(left);
    const auto rightLength = VectorLength(right);
    if (leftLength <= 0.00001F || rightLength <= 0.00001F) return 0.0F;
    return (left.x * right.x + left.y * right.y + left.z * right.z) /
        (leftLength * rightLength);
}

} // namespace

int main() {
    using namespace saberstage::camera;

    const auto yaw90 = FromEulerDegrees({0.0F, 90.0F, 0.0F});
    const auto forward = Rotate(yaw90, {0.0F, 0.0F, 1.0F});
    Check(Near(forward.x, 1.0F) && Near(forward.z, 0.0F), "Unity-style yaw rotates local forward correctly");
    const auto composed = Compose({{10.0F, 0.0F, 0.0F}, yaw90}, {{0.0F, 0.0F, 2.0F}, {}});
    Check(Near(composed.position.x, 12.0F), "player-relative position composes through current forward");
    const auto recoveredLocal = RelativeTo({{10.0F, 0.0F, 0.0F}, yaw90}, composed);
    Check(Near(recoveredLocal.position.x, 0.0F) && Near(recoveredLocal.position.z, 2.0F),
          "world camera placement converts back into player-relative coordinates");
    Check(Near(YawDegrees(yaw90), 90.0F), "yaw extraction supports recenter math");

    auto invalid = DefaultCameraProfile();
    invalid.profileId = "second-camera-not-yet-supported";
    invalid.position.x = 2000.0F;
    invalid.rotationDegrees.y = 725.0F;
    invalid.farClipMeters = 0.01F;
    invalid.requestedWidth = 1279;
    const auto repaired = ValidateAndRepair(invalid);
    Check(repaired.changed && repaired.repairedFields >= 4, "profile validation repairs unsafe fields individually");
    Check(invalid.profileId == kPrimaryCameraId, "Prompt 3 retains one stable camera identity");

    auto legacyUiExclusion = DefaultCameraProfile();
    legacyUiExclusion.excludedLayersMask = 1 << 5;
    const auto legacyUiRepair = ValidateAndRepair(legacyUiExclusion);
    Check(legacyUiRepair.changed && legacyUiExclusion.excludedLayersMask == 0,
          "legacy whole-UI exclusion is repaired so menus remain visible");
    auto restrictedHeadsetMask = DefaultCameraProfile();
    restrictedHeadsetMask.excludedLayersMask = 1 << 9;
    const auto spectatorMask = ResolveSpectatorCullingMask(restrictedHeadsetMask, 1 << 4);
    Check((spectatorMask & kUiLayerMask) != 0,
          "spectator mask explicitly includes UI even when the headset mask does not");
    Check((spectatorMask & kAvatarLayerMask) != 0 && (spectatorMask & (1 << 12)) != 0,
          "spectator mask includes third-person and saber layers needed for a useful shot");
    Check((spectatorMask & kFirstPersonLayerMask) == 0,
          "third-person spectator mask excludes headset-only hover and avatar meshes");
    Check((spectatorMask & (1 << 9)) == 0,
          "non-UI per-profile exclusions remain effective");
    auto attemptedAvatarExclusion = DefaultCameraProfile();
    attemptedAvatarExclusion.excludedLayersMask = kAvatarLayerMask;
    Check((ResolveSpectatorCullingMask(attemptedAvatarExclusion, 0) & kAvatarLayerMask) != 0,
          "SaberStage avatars remain mandatory in Primary camera and preview output");
    Check(Near(invalid.rotationDegrees.y, 5.0F), "profile rotation is normalized without losing intent");
    Check((invalid.requestedWidth & 1) == 0, "profile output dimensions are made even");

    constexpr std::string_view validScript = R"json({
      "syncToSong": true,
      "loop": false,
      "frames": [
        {
          "position": {"x": 0, "y": 2, "z": -3},
          "rotation": {"x": 0, "y": 0, "z": 0},
          "FOV": 70,
          "holdTime": 1
        },
        {
          "position": {"x": 10, "y": 2, "z": -3},
          "rotation": {"x": 0, "y": 90, "z": 0},
          "FOV": 90,
          "duration": 2,
          "holdTime": 1,
          "transition": "Eased"
        }
      ]
    })json";
    const auto loaded = ParseMovementScript(validScript);
    Check(static_cast<bool>(loaded), "independently-authored Camera2-format fixture parses");
    if (loaded) {
        Check(loaded.script->syncToSong && !loaded.script->loop, "script clock and loop flags parse");
        Check(Near(loaded.script->durationSeconds, 4.0F), "script duration includes transition and hold time");
        const Pose base{{-5.0F, 1.0F, 1.0F}, {}};
        const auto beginning = EvaluateMovementScript(*loaded.script, 0.0F, base, 60.0F);
        Check(Near(beginning.pose.position.x, 0.0F), "zero-duration first keyframe activates immediately");
        const auto halfway = EvaluateMovementScript(*loaded.script, 2.0F, base, 60.0F);
        Check(Near(halfway.pose.position.x, 5.0F) && Near(halfway.fovDegrees, 80.0F),
              "eased midpoint deterministically interpolates pose and FOV");
        const auto seekAgain = EvaluateMovementScript(*loaded.script, 2.0F, base, 60.0F);
        Check(Near(seekAgain.pose.position.x, halfway.pose.position.x),
              "direct time evaluation makes restart and practice seek deterministic");
        const auto completed = EvaluateMovementScript(*loaded.script, 100.0F, base, 60.0F);
        Check(completed.complete && Near(completed.pose.position.x, 10.0F),
              "non-looping script holds its final valid frame");
        auto looping = *loaded.script;
        looping.loop = true;
        const auto looped = EvaluateMovementScript(looping, 5.5F, base, 60.0F);
        Check(!looped.complete && looped.pose.position.x > 0.0F && looped.pose.position.x < 10.0F,
              "looping script wraps deterministically without per-frame accumulation");
    }

    const auto inspectionOrbit = LoadMovementScript(
        std::filesystem::path(SABERSTAGE_SOURCE_DIR) / "examples" / "MovementScripts",
        "AvatarBodyInspectionOrbit.json");
    Check(static_cast<bool>(inspectionOrbit), "avatar body-inspection orbit script parses through the runtime loader");
    if (inspectionOrbit) {
        Check(inspectionOrbit.script->syncToSong && inspectionOrbit.script->loop,
              "inspection orbit is a looping song-time script");
        Check(inspectionOrbit.script->frames.size() == 241 && Near(inspectionOrbit.script->durationSeconds, 240.0F),
              "inspection orbit has a four-minute rise/fall cycle at one-second resolution");
        const auto start = EvaluateMovementScript(*inspectionOrbit.script, 0.0F, {}, 70.0F);
        const auto oneCircle = EvaluateMovementScript(*inspectionOrbit.script, 60.0F, {}, 70.0F);
        const auto beforeReverse = EvaluateMovementScript(*inspectionOrbit.script, 119.0F, {}, 70.0F);
        const auto apex = EvaluateMovementScript(*inspectionOrbit.script, 120.0F, {}, 70.0F);
        const auto afterReverse = EvaluateMovementScript(*inspectionOrbit.script, 121.0F, {}, 70.0F);
        const auto descending = EvaluateMovementScript(*inspectionOrbit.script, 180.0F, {}, 70.0F);
        Check(Near(start.pose.position.x, oneCircle.pose.position.x) &&
              Near(start.pose.position.z, oneCircle.pose.position.z) &&
              oneCircle.pose.position.y > start.pose.position.y,
              "inspection camera completes one orbit every 60 seconds while rising");
        Check(Near(apex.pose.position.y, 2.05F) &&
              Near(descending.pose.position.y, oneCircle.pose.position.y),
              "inspection camera reaches above-head height then reverses vertically");
        Check(Near(beforeReverse.pose.position.x, afterReverse.pose.position.x) &&
              Near(beforeReverse.pose.position.z, afterReverse.pose.position.z) &&
              afterReverse.pose.position.y < apex.pose.position.y,
              "inspection camera retraces the orbit in reverse while descending");
        for (const auto sample : {start, oneCircle, apex, descending}) {
            const auto forward = Rotate(sample.pose.rotation, {0.0F, 0.0F, 1.0F});
            const auto towardAvatar = Vec3{
                -sample.pose.position.x,
                1.10F - sample.pose.position.y,
                -sample.pose.position.z};
            Check(DirectionDot(forward, towardAvatar) > 0.999F,
                  "inspection camera remains aimed at the avatar focus point");
        }
        Check(Rotate(start.pose.rotation, {0.0F, 0.0F, 1.0F}).y > 0.0F &&
              Rotate(apex.pose.rotation, {0.0F, 0.0F, 1.0F}).y < 0.0F,
              "inspection camera looks upward at knee height and downward above the head");
    }

    const auto unknown = ParseMovementScript(R"({"frames":[{"position":{"x":0,"y":0,"z":0},"rotation":{"x":0,"y":0,"z":0},"command":"exec"}]})");
    Check(!unknown && unknown.error.find("unsupported") != std::string::npos,
          "unsupported script properties are rejected before activation");
    const auto invalidTransition = ParseMovementScript(R"({"frames":[{"transition":"Teleport"}]})");
    Check(!invalidTransition, "unsupported interpolation is rejected");
    const auto unsafePosition = ParseMovementScript(R"({"frames":[{"position":{"x":1001,"y":0,"z":0}}]})");
    Check(!unsafePosition, "script position safety limit is enforced");
    ScriptLimits tiny;
    tiny.maxBytes = 8;
    Check(!ParseMovementScript(validScript, tiny), "script byte limit is enforced before parsing");
    Check(!LoadMovementScript(std::filesystem::temp_directory_path(), "../outside.json"),
          "movement script loader rejects traversal before filesystem access");

    auto profile = DefaultCameraProfile();
    profile.positionSmoothingSeconds = 0.5F;
    profile.rotationSmoothingSeconds = 0.5F;
    MotionPipeline smoothing;
    const MotionInput initial{{}, {{0.0F, 0.0F, 0.0F}, {}}, std::nullopt, 0.0F, 1.0F / 60.0F};
    smoothing.Evaluate(profile, initial);
    MotionInput moved = initial;
    moved.baseLocal.position.x = 10.0F;
    const auto smoothed = smoothing.Evaluate(profile, moved);
    Check(smoothed.smoothedLocalPose.position.x > 0.0F && smoothed.smoothedLocalPose.position.x < 10.0F,
          "general position smoothing is time based");

    profile.positionSmoothingSeconds = 0.0F;
    profile.rotationSmoothingSeconds = 0.0F;
    profile.anchoredFloatEnabled = true;
    profile.anchoredFloatMaxOffsetMeters = 0.65F;
    MotionPipeline floating;
    MotionInput floatInput{{}, BasePose(profile), std::nullopt, 90.0F, 1.0F / 60.0F};
    MotionOutput floatOutput;
    for (int index = 0; index < 300; ++index) floatOutput = floating.Evaluate(profile, floatInput);
    Check(floatOutput.anchoredFloatOffsetMeters > 0.60F && floatOutput.anchoredFloatOffsetMeters <= 0.651F,
          "anchored float converges smoothly and remains bounded");
    Check(Near(profile.position.x, 0.0F), "anchored float never mutates saved base placement");
    floating.Reset();
    floatInput.headYawRelativeDegrees = 0.0F;
    floatOutput = floating.Evaluate(profile, floatInput);
    Check(Near(floatOutput.anchoredFloatOffsetMeters, 0.0F), "recenter reset clears anchored-float velocity and offset");

    if (loaded) {
        profile.anchoredFloatEnabled = true;
        MotionPipeline precedence;
        auto scripted = EvaluateMovementScript(*loaded.script, 2.0F, BasePose(profile), profile.fovDegrees);
        const auto output = precedence.Evaluate(profile, {{}, BasePose(profile), scripted, 90.0F, 1.0F});
        Check(output.smoothedLocalPose.position.x > 4.9F,
              "movement script replaces base before smoothing and anchored float");
        Check(output.worldPose.position.x > output.smoothedLocalPose.position.x,
              "anchored float composes after script and smoothing");
    }

    FrameDemandRegistry demands;
    const auto dockedPreviewDemand = saberstage::preview::DockedPreviewRenderDemand();
    Check(dockedPreviewDemand.width == 1920 && dockedPreviewDemand.height == 1080 &&
              dockedPreviewDemand.framesPerSecond == 15,
          "the docked menu preview renders at 1080p with a bounded cadence");
    const auto floatingPreviewDemand = saberstage::preview::FloatingPreviewRenderDemand();
    Check(floatingPreviewDemand.width == 512 && floatingPreviewDemand.height == 288 &&
              floatingPreviewDemand.framesPerSecond == 15,
          "the movable preview retains its lower-cost standalone profile");
    Check(demands.Set("preview", {"primary", 640, 360, 30}), "preview demand is accepted");
    Check(demands.Set("capture", {"primary", 1920, 1080, 60}), "capture demand is accepted");
    const auto combined = demands.Combined("primary");
    Check(combined.active && combined.width == 1920 && combined.height == 1080 && combined.framesPerSecond == 60,
          "multiple future consumers negotiate one camera target and cadence");
    demands.Clear();
    Check(!demands.Combined("primary").active, "no consumer means no spectator render demand");

    FrameScheduler scheduler;
    Check(!scheduler.Advance(1.0F / 60.0F, 30), "30 FPS demand does not render every 60 Hz update");
    Check(scheduler.Advance(1.0F / 60.0F, 30), "30 FPS demand renders on its own cadence");
    scheduler.Reset();
    Check(!scheduler.Advance(1.0F / 90.0F, 60) && scheduler.Advance(1.0F / 90.0F, 60),
          "60 FPS demand is scheduled independently from a 90 Hz HMD update");

    if (failures == 0) std::cout << "All camera tests passed\n";
    return failures == 0 ? 0 : 1;
}
