using System;
using UnityEngine;

namespace SaberStage.PcPoseAnalyzer
{
    [Serializable]
    internal sealed class SessionHeader
    {
        public string recordType = "session";
        public int schemaVersion = 1;
        public string createdUtc;
        public string analyzerVersion;
        public string gameVersion;
        public string customAvatarVersion;
        public string methodology = "FinalIK black-box observation: observable tracking targets in, resulting humanoid transforms out.";
        public string coordinateConvention = "Torso-local Unity meters: +X avatar right, +Y avatar up, +Z avatar forward.";
        public float continuousSampleRateHz;
        public float calibrationSampleRateHz;
        public bool controllerButtonsEnabled;
        public bool vrControllerOverlayEnabled;
    }

    [Serializable]
    internal sealed class MarkerRecord
    {
        public string recordType = "marker";
        public string createdUtc;
        public int frame;
        public float unscaledTime;
        public string marker;
        public string captureId;
        public string detail;
    }

    [Serializable]
    internal sealed class PoseSample
    {
        public string recordType = "pose-sample";
        public string createdUtc;
        public int frame;
        public float unscaledTime;
        public string captureId;
        public string captureMode;
        public string avatarName;
        public TorsoFrameData torso;
        public TargetSetData targets;
        public BoneSetData bones;
        public ArmMetricsData leftArm;
        public ArmMetricsData rightArm;
        public SpineMetricsData spine;
        public FingerSetData fingers;
    }

    [Serializable]
    internal sealed class CalibrationRecord
    {
        public string recordType = "calibration-observation";
        public string createdUtc;
        public int frame;
        public float unscaledTime;
        public string reason;
        public string avatarName;
        public float authoredEyeHeightMeters;
        public float authoredArmSpanMeters;
        public float runtimeScale;
        public float absoluteScale;
        public float reportedScaledEyeHeightMeters;
        public float observedHeadTargetAboveAvatarRootMeters;
        public float observedOutputHeadAboveAvatarRootMeters;
        public float observedControllerSpanMeters;
        public float observedOutputWristSpanMeters;
        public float heightScaleCandidate;
        public float armSpanScaleCandidate;
        public SerializableVector3 avatarRootWorld;
        public SerializableVector3 headTargetWorld;
        public SerializableVector3 leftTargetWorld;
        public SerializableVector3 rightTargetWorld;
    }

    [Serializable]
    internal sealed class TorsoFrameData
    {
        public SerializableVector3 originWorld;
        public SerializableVector3 rightWorld;
        public SerializableVector3 upWorld;
        public SerializableVector3 forwardWorld;
        public float shoulderWidthMeters;
        public float torsoHeightMeters;
    }

    [Serializable]
    internal sealed class TargetSetData
    {
        public PoseData head;
        public PoseData leftHand;
        public PoseData rightHand;
    }

    [Serializable]
    internal sealed class BoneSetData
    {
        public PoseData pelvis;
        public PoseData chest;
        public PoseData head;
        public PoseData leftShoulder;
        public PoseData leftElbow;
        public PoseData leftWrist;
        public PoseData rightShoulder;
        public PoseData rightElbow;
        public PoseData rightWrist;
    }

    [Serializable]
    internal sealed class PoseData
    {
        public bool available;
        public SerializableVector3 position;
        public SerializableVector3 right;
        public SerializableVector3 up;
        public SerializableVector3 forward;
    }

    [Serializable]
    internal sealed class ArmMetricsData
    {
        public bool available;
        public float upperArmMeters;
        public float forearmMeters;
        public float reachNormalized;
        public float elbowFlexionDegrees;
        public float elbowFlareDegrees;
        public SerializableVector3 elbowPoleTorso;
        public SerializableVector3 handFromShoulderTorso;
        public SerializableVector3 targetFromShoulderTorso;
        public float wristTargetErrorMeters;
        public SerializableVector3 wristPalmForwardTorso;
        public SerializableVector3 targetForwardTorso;
        public SerializableVector3 targetUpTorso;
    }

    [Serializable]
    internal sealed class SpineMetricsData
    {
        public bool available;
        public float chestLeanForwardDegrees;
        public float chestLeanRightDegrees;
        public SerializableVector3 headFromPelvisTorso;
        public SerializableVector3 chestFromPelvisTorso;
    }

    [Serializable]
    internal sealed class FingerSetData
    {
        public FingerInputSetData inputCurls;
        public HandFingerOutputData leftOutput;
        public HandFingerOutputData rightOutput;
    }

    [Serializable]
    internal sealed class FingerInputSetData
    {
        public FingerCurlData left;
        public FingerCurlData right;
    }

    [Serializable]
    internal sealed class FingerCurlData
    {
        public bool available;
        public float thumb;
        public float index;
        public float middle;
        public float ring;
        public float little;
        public float appliedFallbackCurl;
    }

    [Serializable]
    internal sealed class HandFingerOutputData
    {
        public bool available;
        public FingerChainData thumb;
        public FingerChainData index;
        public FingerChainData middle;
        public FingerChainData ring;
        public FingerChainData little;
    }

    [Serializable]
    internal sealed class FingerChainData
    {
        public bool available;
        public HandRelativeBoneData proximal;
        public HandRelativeBoneData intermediate;
        public HandRelativeBoneData distal;
        public float chainBendDegrees;
    }

    [Serializable]
    internal sealed class HandRelativeBoneData
    {
        public bool available;
        public SerializableVector3 positionFromHand;
        public SerializableVector3 rightInHand;
        public SerializableVector3 upInHand;
        public SerializableVector3 forwardInHand;
        public SerializableQuaternion rotationFromHand;
    }

    [Serializable]
    internal struct SerializableVector3
    {
        public float x;
        public float y;
        public float z;

        public SerializableVector3(Vector3 value)
        {
            x = value.x;
            y = value.y;
            z = value.z;
        }
    }

    [Serializable]
    internal struct SerializableQuaternion
    {
        public float x;
        public float y;
        public float z;
        public float w;

        public SerializableQuaternion(Quaternion value)
        {
            x = value.x;
            y = value.y;
            z = value.z;
            w = value.w;
        }
    }
}
