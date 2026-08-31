using System;
using UnityEngine;

namespace SaberStage.PcPoseAnalyzer
{
    internal static class PoseSampler
    {
        public static PoseSample TryCreateSample(AvatarBinding avatar, string captureId, string captureMode)
        {
            if (!avatar.TryGetTargets(out Transform headTarget, out Transform leftTarget, out Transform rightTarget))
            {
                return null;
            }

            Animator animator = avatar.Animator;
            Transform pelvis = animator.GetBoneTransform(HumanBodyBones.Hips);
            Transform chest = animator.GetBoneTransform(HumanBodyBones.UpperChest) ?? animator.GetBoneTransform(HumanBodyBones.Chest) ?? animator.GetBoneTransform(HumanBodyBones.Spine);
            Transform head = animator.GetBoneTransform(HumanBodyBones.Head);
            Transform leftShoulder = animator.GetBoneTransform(HumanBodyBones.LeftUpperArm);
            Transform leftElbow = animator.GetBoneTransform(HumanBodyBones.LeftLowerArm);
            Transform leftWrist = animator.GetBoneTransform(HumanBodyBones.LeftHand);
            Transform rightShoulder = animator.GetBoneTransform(HumanBodyBones.RightUpperArm);
            Transform rightElbow = animator.GetBoneTransform(HumanBodyBones.RightLowerArm);
            Transform rightWrist = animator.GetBoneTransform(HumanBodyBones.RightHand);

            if (pelvis == null || chest == null || head == null || leftShoulder == null || leftElbow == null || leftWrist == null || rightShoulder == null || rightElbow == null || rightWrist == null)
            {
                return null;
            }

            TorsoFrame frame = TorsoFrame.Create(animator.transform, pelvis, chest, leftShoulder, rightShoulder);

            return new PoseSample
            {
                createdUtc = DateTime.UtcNow.ToString("O"),
                frame = Time.frameCount,
                unscaledTime = Time.unscaledTime,
                captureId = captureId,
                captureMode = captureMode,
                avatarName = avatar.AvatarName,
                torso = frame.ToData(),
                targets = new TargetSetData
                {
                    head = MakePose(headTarget, frame),
                    leftHand = MakePose(leftTarget, frame),
                    rightHand = MakePose(rightTarget, frame),
                },
                bones = new BoneSetData
                {
                    pelvis = MakePose(pelvis, frame),
                    chest = MakePose(chest, frame),
                    head = MakePose(head, frame),
                    leftShoulder = MakePose(leftShoulder, frame),
                    leftElbow = MakePose(leftElbow, frame),
                    leftWrist = MakePose(leftWrist, frame),
                    rightShoulder = MakePose(rightShoulder, frame),
                    rightElbow = MakePose(rightElbow, frame),
                    rightWrist = MakePose(rightWrist, frame),
                },
                leftArm = MakeArmMetrics(leftShoulder, leftElbow, leftWrist, leftTarget, frame, isLeft: true),
                rightArm = MakeArmMetrics(rightShoulder, rightElbow, rightWrist, rightTarget, frame, isLeft: false),
                spine = MakeSpineMetrics(pelvis, chest, head, frame),
                fingers = new FingerSetData
                {
                    inputCurls = avatar.ReadFingerInputs(),
                    leftOutput = MakeHandFingerOutput(animator, isLeft: true),
                    rightOutput = MakeHandFingerOutput(animator, isLeft: false),
                },
            };
        }

        private static HandFingerOutputData MakeHandFingerOutput(Animator animator, bool isLeft)
        {
            Transform hand = animator.GetBoneTransform(isLeft ? HumanBodyBones.LeftHand : HumanBodyBones.RightHand);
            if (hand == null)
            {
                return new HandFingerOutputData { available = false };
            }

            return new HandFingerOutputData
            {
                available = true,
                thumb = MakeFingerChain(animator, hand,
                    isLeft ? HumanBodyBones.LeftThumbProximal : HumanBodyBones.RightThumbProximal,
                    isLeft ? HumanBodyBones.LeftThumbIntermediate : HumanBodyBones.RightThumbIntermediate,
                    isLeft ? HumanBodyBones.LeftThumbDistal : HumanBodyBones.RightThumbDistal),
                index = MakeFingerChain(animator, hand,
                    isLeft ? HumanBodyBones.LeftIndexProximal : HumanBodyBones.RightIndexProximal,
                    isLeft ? HumanBodyBones.LeftIndexIntermediate : HumanBodyBones.RightIndexIntermediate,
                    isLeft ? HumanBodyBones.LeftIndexDistal : HumanBodyBones.RightIndexDistal),
                middle = MakeFingerChain(animator, hand,
                    isLeft ? HumanBodyBones.LeftMiddleProximal : HumanBodyBones.RightMiddleProximal,
                    isLeft ? HumanBodyBones.LeftMiddleIntermediate : HumanBodyBones.RightMiddleIntermediate,
                    isLeft ? HumanBodyBones.LeftMiddleDistal : HumanBodyBones.RightMiddleDistal),
                ring = MakeFingerChain(animator, hand,
                    isLeft ? HumanBodyBones.LeftRingProximal : HumanBodyBones.RightRingProximal,
                    isLeft ? HumanBodyBones.LeftRingIntermediate : HumanBodyBones.RightRingIntermediate,
                    isLeft ? HumanBodyBones.LeftRingDistal : HumanBodyBones.RightRingDistal),
                little = MakeFingerChain(animator, hand,
                    isLeft ? HumanBodyBones.LeftLittleProximal : HumanBodyBones.RightLittleProximal,
                    isLeft ? HumanBodyBones.LeftLittleIntermediate : HumanBodyBones.RightLittleIntermediate,
                    isLeft ? HumanBodyBones.LeftLittleDistal : HumanBodyBones.RightLittleDistal),
            };
        }

        private static FingerChainData MakeFingerChain(Animator animator, Transform hand, HumanBodyBones proximalId, HumanBodyBones intermediateId, HumanBodyBones distalId)
        {
            Transform proximal = animator.GetBoneTransform(proximalId);
            Transform intermediate = animator.GetBoneTransform(intermediateId);
            Transform distal = animator.GetBoneTransform(distalId);

            if (proximal == null || intermediate == null || distal == null)
            {
                return new FingerChainData { available = false };
            }

            Vector3 firstSegment = intermediate.position - proximal.position;
            Vector3 secondSegment = distal.position - intermediate.position;

            return new FingerChainData
            {
                available = true,
                proximal = MakeHandRelativeBone(hand, proximal),
                intermediate = MakeHandRelativeBone(hand, intermediate),
                distal = MakeHandRelativeBone(hand, distal),
                chainBendDegrees = Vector3.Angle(firstSegment, secondSegment),
            };
        }

        private static HandRelativeBoneData MakeHandRelativeBone(Transform hand, Transform bone)
        {
            Quaternion rotationFromHand = Quaternion.Inverse(hand.rotation) * bone.rotation;
            return new HandRelativeBoneData
            {
                available = true,
                positionFromHand = new SerializableVector3(hand.InverseTransformPoint(bone.position)),
                rightInHand = new SerializableVector3(hand.InverseTransformDirection(bone.right)),
                upInHand = new SerializableVector3(hand.InverseTransformDirection(bone.up)),
                forwardInHand = new SerializableVector3(hand.InverseTransformDirection(bone.forward)),
                rotationFromHand = new SerializableQuaternion(rotationFromHand),
            };
        }

        private static PoseData MakePose(Transform transform, TorsoFrame frame)
        {
            if (transform == null)
            {
                return new PoseData { available = false };
            }

            return new PoseData
            {
                available = true,
                position = new SerializableVector3(frame.PositionToLocal(transform.position)),
                right = new SerializableVector3(frame.DirectionToLocal(transform.right)),
                up = new SerializableVector3(frame.DirectionToLocal(transform.up)),
                forward = new SerializableVector3(frame.DirectionToLocal(transform.forward)),
            };
        }

        private static ArmMetricsData MakeArmMetrics(Transform shoulder, Transform elbow, Transform wrist, Transform target, TorsoFrame frame, bool isLeft)
        {
            Vector3 shoulderToElbow = elbow.position - shoulder.position;
            Vector3 elbowToWrist = wrist.position - elbow.position;
            Vector3 shoulderToWrist = wrist.position - shoulder.position;
            Vector3 shoulderToTarget = target.position - shoulder.position;

            float upperArm = shoulderToElbow.magnitude;
            float forearm = elbowToWrist.magnitude;
            float fullReach = Mathf.Max(upperArm + forearm, 0.0001f);
            float flexion = Vector3.Angle(shoulder.position - elbow.position, wrist.position - elbow.position);

            Vector3 armAxis = shoulderToWrist.sqrMagnitude > 0.000001f ? shoulderToWrist.normalized : Vector3.forward;
            Vector3 poleWorld = Vector3.ProjectOnPlane(elbow.position - shoulder.position, armAxis).normalized;
            Vector3 poleTorso = frame.DirectionToLocal(poleWorld);

            float outward = isLeft ? -poleTorso.x : poleTorso.x;
            float downward = -poleTorso.y;
            float flare = Mathf.Atan2(outward, downward) * Mathf.Rad2Deg;

            return new ArmMetricsData
            {
                available = true,
                upperArmMeters = upperArm,
                forearmMeters = forearm,
                reachNormalized = shoulderToWrist.magnitude / fullReach,
                elbowFlexionDegrees = flexion,
                elbowFlareDegrees = flare,
                elbowPoleTorso = new SerializableVector3(poleTorso),
                handFromShoulderTorso = new SerializableVector3(frame.DirectionToLocal(shoulderToWrist) / fullReach),
                targetFromShoulderTorso = new SerializableVector3(frame.DirectionToLocal(shoulderToTarget) / fullReach),
                wristTargetErrorMeters = Vector3.Distance(wrist.position, target.position),
                wristPalmForwardTorso = new SerializableVector3(frame.DirectionToLocal(wrist.forward)),
                targetForwardTorso = new SerializableVector3(frame.DirectionToLocal(target.forward)),
                targetUpTorso = new SerializableVector3(frame.DirectionToLocal(target.up)),
            };
        }

        private static SpineMetricsData MakeSpineMetrics(Transform pelvis, Transform chest, Transform head, TorsoFrame frame)
        {
            Vector3 chestUp = frame.DirectionToLocal(chest.up);
            float forwardLean = Mathf.Atan2(chestUp.z, Mathf.Max(0.0001f, chestUp.y)) * Mathf.Rad2Deg;
            float rightLean = Mathf.Atan2(chestUp.x, Mathf.Max(0.0001f, chestUp.y)) * Mathf.Rad2Deg;

            return new SpineMetricsData
            {
                available = true,
                chestLeanForwardDegrees = forwardLean,
                chestLeanRightDegrees = rightLean,
                headFromPelvisTorso = new SerializableVector3(frame.DirectionToLocal(head.position - pelvis.position)),
                chestFromPelvisTorso = new SerializableVector3(frame.DirectionToLocal(chest.position - pelvis.position)),
            };
        }

        private readonly struct TorsoFrame
        {
            private TorsoFrame(Vector3 origin, Vector3 right, Vector3 up, Vector3 forward, float shoulderWidth, float torsoHeight)
            {
                Origin = origin;
                Right = right;
                Up = up;
                Forward = forward;
                ShoulderWidth = shoulderWidth;
                TorsoHeight = torsoHeight;
            }

            public Vector3 Origin { get; }
            public Vector3 Right { get; }
            public Vector3 Up { get; }
            public Vector3 Forward { get; }
            public float ShoulderWidth { get; }
            public float TorsoHeight { get; }

            public static TorsoFrame Create(Transform avatarRoot, Transform pelvis, Transform chest, Transform leftShoulder, Transform rightShoulder)
            {
                Vector3 up = (chest.position - pelvis.position).normalized;
                if (up.sqrMagnitude < 0.5f)
                {
                    up = avatarRoot.up;
                }

                Vector3 forward = Vector3.ProjectOnPlane(avatarRoot.forward, up).normalized;
                if (forward.sqrMagnitude < 0.5f)
                {
                    forward = Vector3.Cross((rightShoulder.position - leftShoulder.position).normalized, up).normalized;
                }

                Vector3 right = Vector3.Cross(up, forward).normalized;
                Vector3 shoulderDirection = (rightShoulder.position - leftShoulder.position).normalized;
                if (Vector3.Dot(right, shoulderDirection) < 0.0f)
                {
                    right = -right;
                    forward = -forward;
                }

                return new TorsoFrame(
                    chest.position,
                    right,
                    up,
                    forward,
                    Vector3.Distance(leftShoulder.position, rightShoulder.position),
                    Vector3.Distance(pelvis.position, chest.position));
            }

            public Vector3 PositionToLocal(Vector3 worldPosition) => DirectionToLocal(worldPosition - Origin);

            public Vector3 DirectionToLocal(Vector3 worldDirection)
            {
                return new Vector3(
                    Vector3.Dot(worldDirection, Right),
                    Vector3.Dot(worldDirection, Up),
                    Vector3.Dot(worldDirection, Forward));
            }

            public TorsoFrameData ToData()
            {
                return new TorsoFrameData
                {
                    originWorld = new SerializableVector3(Origin),
                    rightWorld = new SerializableVector3(Right),
                    upWorld = new SerializableVector3(Up),
                    forwardWorld = new SerializableVector3(Forward),
                    shoulderWidthMeters = ShoulderWidth,
                    torsoHeightMeters = TorsoHeight,
                };
            }
        }
    }
}
