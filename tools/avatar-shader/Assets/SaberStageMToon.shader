Shader "SaberStage/MToon"
{
    Properties
    {
        _MainTex ("Main Texture", 2D) = "white" {}
        _MainTexCoord ("Main UV Set", Float) = 0
        _MainTexRotation ("Main UV Rotation", Float) = 0
        _Color ("Color", Color) = (1,1,1,1)
        _ShadeTexture ("Shade Texture", 2D) = "white" {}
        _ShadeTextureCoord ("Shade UV Set", Float) = 0
        _ShadeTextureRotation ("Shade UV Rotation", Float) = 0
        _ShadeColor ("Shade Color", Color) = (0.97,0.81,0.86,1)
        _ShadingGradeTexture ("Shading Grade", 2D) = "white" {}
        _ShadingGradeTextureCoord ("Grade UV Set", Float) = 0
        _ShadingGradeTextureRotation ("Grade UV Rotation", Float) = 0
        _ShadingGradeRate ("Shading Grade Rate", Range(0,1)) = 1
        _ShadeShift ("Shade Shift", Range(-1,1)) = 0
        _ShadeToony ("Shade Toony", Range(0,1)) = 0.9
        _BumpMap ("Normal Map", 2D) = "bump" {}
        _BumpMapCoord ("Normal UV Set", Float) = 0
        _BumpMapRotation ("Normal UV Rotation", Float) = 0
        _BumpScale ("Normal Scale", Range(0,2)) = 1
        _EmissionMap ("Emission", 2D) = "white" {}
        _EmissionMapCoord ("Emission UV Set", Float) = 0
        _EmissionMapRotation ("Emission UV Rotation", Float) = 0
        _EmissionColor ("Emission Color", Color) = (0,0,0,0)
        _RimTexture ("Rim Texture", 2D) = "white" {}
        _RimTextureCoord ("Rim UV Set", Float) = 0
        _RimTextureRotation ("Rim UV Rotation", Float) = 0
        _RimColor ("Rim Color", Color) = (0,0,0,1)
        _RimLightingMix ("Rim Lighting Mix", Range(0,1)) = 0
        _RimFresnelPower ("Rim Fresnel", Range(0,100)) = 1
        _RimLift ("Rim Lift", Range(0,1)) = 0
        _SphereAdd ("MatCap", 2D) = "black" {}
        _LightColorAttenuation ("Light Color Attenuation", Range(0,1)) = 0
        _IndirectLightIntensity ("Indirect Light Intensity", Range(0,1)) = 0.1
        _MaterialDebugStage ("Material Debug Stage", Float) = 0
        _AvatarLightingMode ("Avatar Lighting Mode", Float) = 1
        _Cutoff ("Alpha Cutoff", Range(0,1)) = 0.5
        [Enum(UnityEngine.Rendering.CullMode)] _Cull ("Cull", Float) = 2
        [Enum(UnityEngine.Rendering.BlendMode)] _SrcBlend ("Source Blend", Float) = 1
        [Enum(UnityEngine.Rendering.BlendMode)] _DstBlend ("Destination Blend", Float) = 0
        [Toggle] _ZWrite ("Depth Write", Float) = 1
        [Toggle] _AlphaToMask ("Alpha To Coverage", Float) = 0
        _CutoutSmoothing ("Cutout Smoothing", Range(0,3)) = 1
    }

    SubShader
    {
        Tags { "RenderType"="Opaque" "Queue"="Geometry" }
        Pass
        {
            Tags { "LightMode"="ForwardBase" }
            Cull [_Cull]
            Blend [_SrcBlend] [_DstBlend]
            ZWrite [_ZWrite]
            AlphaToMask [_AlphaToMask]

            CGPROGRAM
            #pragma target 3.0
            #pragma vertex vert
            #pragma fragment frag
            #pragma multi_compile_fwdbase
            #pragma multi_compile _ STEREO_MULTIVIEW_ON STEREO_INSTANCING_ON
            #pragma multi_compile_instancing
            #pragma multi_compile_local _ SABERSTAGE_NORMAL_MAP
            #pragma multi_compile_local _ SABERSTAGE_RIM_LIGHT
            #pragma multi_compile_local _ SABERSTAGE_MATCAP
            #pragma multi_compile_local _ SABERSTAGE_EMISSION
            #pragma multi_compile_local _ SABERSTAGE_ALPHA_TEST
            #pragma multi_compile_local _ SABERSTAGE_UNLIT
            #include "UnityCG.cginc"
            #include "Lighting.cginc"

            sampler2D _MainTex, _ShadeTexture, _ShadingGradeTexture;
            sampler2D _BumpMap, _EmissionMap, _RimTexture, _SphereAdd;
            float4 _MainTex_ST, _ShadeTexture_ST, _ShadingGradeTexture_ST;
            float4 _BumpMap_ST, _EmissionMap_ST, _RimTexture_ST;
            float4 _Color, _ShadeColor, _EmissionColor, _RimColor;
            float _ShadeShift, _ShadeToony, _ShadingGradeRate, _BumpScale, _RimLightingMix;
            float _RimFresnelPower, _RimLift, _Cutoff;
            float _AlphaToMask, _CutoutSmoothing;
            float _LightColorAttenuation, _IndirectLightIntensity;
            float _MaterialDebugStage, _AvatarLightingMode;
            float _MainTexCoord, _ShadeTextureCoord, _ShadingGradeTextureCoord;
            float _BumpMapCoord, _EmissionMapCoord, _RimTextureCoord;
            float _MainTexRotation, _ShadeTextureRotation, _ShadingGradeTextureRotation;
            float _BumpMapRotation, _EmissionMapRotation, _RimTextureRotation;

            struct appdata
            {
                float4 vertex : POSITION;
                float3 normal : NORMAL;
                float4 tangent : TANGENT;
                float2 uv0 : TEXCOORD0;
                float2 uv1 : TEXCOORD1;
                UNITY_VERTEX_INPUT_INSTANCE_ID
            };

            struct v2f
            {
                float4 position : SV_POSITION;
                float2 uv0 : TEXCOORD0;
                float2 uv1 : TEXCOORD1;
                float3 worldPosition : TEXCOORD2;
                float3 worldNormal : TEXCOORD3;
                float3 worldTangent : TEXCOORD4;
                float3 worldBitangent : TEXCOORD5;
                UNITY_VERTEX_INPUT_INSTANCE_ID
                UNITY_VERTEX_OUTPUT_STEREO
            };

            v2f vert(appdata input)
            {
                v2f output;
                UNITY_SETUP_INSTANCE_ID(input);
                UNITY_TRANSFER_INSTANCE_ID(input, output);
                UNITY_INITIALIZE_VERTEX_OUTPUT_STEREO(output);
                output.position = UnityObjectToClipPos(input.vertex);
                output.uv0 = input.uv0;
                output.uv1 = input.uv1;
                output.worldPosition = mul(unity_ObjectToWorld, input.vertex).xyz;
                output.worldNormal = UnityObjectToWorldNormal(input.normal);
                output.worldTangent = UnityObjectToWorldDir(input.tangent.xyz);
                output.worldBitangent = cross(output.worldNormal, output.worldTangent) * input.tangent.w;
                return output;
            }

            float2 transformedUv(float2 uv0, float2 uv1, float uvSet, float4 st, float rotation)
            {
                float2 uv = lerp(uv0, uv1, step(0.5, uvSet));
                uv *= st.xy;
                float sineValue = sin(rotation);
                float cosineValue = cos(rotation);
                uv = float2(cosineValue * uv.x - sineValue * uv.y,
                            sineValue * uv.x + cosineValue * uv.y);
                return uv + st.zw;
            }

            fixed4 frag(v2f input) : SV_Target
            {
                UNITY_SETUP_INSTANCE_ID(input);
                UNITY_SETUP_STEREO_EYE_INDEX_POST_VERTEX(input);
                float2 mainUv = transformedUv(input.uv0, input.uv1, _MainTexCoord, _MainTex_ST, _MainTexRotation);
                fixed4 rawMainSample = tex2D(_MainTex, mainUv);
                fixed4 mainSample = rawMainSample * _Color;
                #if defined(SABERSTAGE_ALPHA_TEST)
                    if (_CutoutSmoothing > 0.5)
                    {
                        // Convert the authored cutout into a bounded coverage
                        // ramp. With AlphaToMask this feeds MSAA sample
                        // coverage. On a single-sample target a stable
                        // screen-space dither provides avatar-only smoothing
                        // without changing Beat Saber's complete render path.
                        float smoothingWidth = max(
                            fwidth(mainSample.a) * (0.65 * _CutoutSmoothing), 0.0001);
                        float coverage = saturate(
                            (mainSample.a - _Cutoff) / smoothingWidth + 0.5);
                        if (_AlphaToMask < 0.5)
                        {
                            float2 pixel = floor(input.position.xy);
                            float threshold = frac(52.9829189 * frac(
                                dot(pixel, float2(0.06711056, 0.00583715))));
                            clip(coverage - threshold);
                        }
                        else
                        {
                            clip(coverage - 0.001);
                        }
                        mainSample.a = coverage;
                    }
                    else
                    {
                        clip(mainSample.a - _Cutoff);
                    }
                #endif

                float3 normal = normalize(input.worldNormal);
                #if defined(SABERSTAGE_NORMAL_MAP)
                    float2 bumpUv = transformedUv(input.uv0, input.uv1, _BumpMapCoord, _BumpMap_ST, _BumpMapRotation);
                    float3 tangentNormal = UnpackScaleNormal(tex2D(_BumpMap, bumpUv), _BumpScale);
                    normal = normalize(input.worldTangent * tangentNormal.x +
                        input.worldBitangent * tangentNormal.y + normal * tangentNormal.z);
                #endif

                // Stage 1 deliberately bypasses _Color. If it differs from
                // the source image, the texture assignment/color-space path
                // is wrong rather than the toon model.
                if (_MaterialDebugStage > 0.5 && _MaterialDebugStage < 1.5)
                    return rawMainSample;
                #if defined(SABERSTAGE_UNLIT)
                    return mainSample;
                #endif
                float3 lightDirection = normalize(_WorldSpaceLightPos0.xyz);
                float ndotl = dot(normal, lightDirection);
                float2 gradeUv = transformedUv(input.uv0, input.uv1, _ShadingGradeTextureCoord, _ShadingGradeTexture_ST, _ShadingGradeTextureRotation);
                float grade = 1.0 - _ShadingGradeRate *
                    (1.0 - tex2D(_ShadingGradeTexture, gradeUv).r);
                float lightIntensity = ndotl * 0.5 + 0.5;
                lightIntensity = lightIntensity * grade * 2.0 - 1.0;
                float minimumThreshold = _ShadeShift;
                float maximumThreshold = lerp(1.0, _ShadeShift, saturate(_ShadeToony));
                float litWeight = saturate((lightIntensity - minimumThreshold) /
                    max(0.00001, maximumThreshold - minimumThreshold));
                float3 lit = mainSample.rgb;
                float2 shadeUv = transformedUv(input.uv0, input.uv1, _ShadeTextureCoord, _ShadeTexture_ST, _ShadeTextureRotation);
                float3 shadeTexture = tex2D(_ShadeTexture, shadeUv).rgb;
                // Stage 3 proves the toon threshold before introducing a
                // separate shade image. Configured mode and stage 4 onward use
                // the authored MToon shade texture/fallback binding.
                if (_MaterialDebugStage > 2.5 && _MaterialDebugStage < 3.5)
                    shadeTexture = rawMainSample.rgb;
                float3 shade = shadeTexture * _ShadeColor.rgb;
                float3 direct = lerp(shade, lit, litWeight);
                float3 viewDirection = normalize(_WorldSpaceCameraPos.xyz - input.worldPosition);
                float3 directLight = max(_LightColor0.rgb, 0.0);
                float directMaximum = max(0.00001, max(directLight.x, max(directLight.y, directLight.z)));
                directLight = lerp(directLight, directMaximum.xxx, saturate(_LightColorAttenuation));
                float3 toonedGi = 0.5 * (
                    ShadeSH9(float4(0, 1, 0, 1)) + ShadeSH9(float4(0, -1, 0, 1)));
                float3 directionalGi = ShadeSH9(float4(normal, 1.0));
                float3 indirectLight = max(lerp(toonedGi, directionalGi,
                    saturate(_IndirectLightIntensity)), 0.0);
                float indirectMaximum = max(0.00001, max(indirectLight.x, max(indirectLight.y, indirectLight.z)));
                indirectLight = lerp(indirectLight, indirectMaximum.xxx, saturate(_LightColorAttenuation));
                float3 environmentLight = directLight + indirectLight;
                float3 appliedLight = environmentLight;
                if (_AvatarLightingMode > 0.5 && _AvatarLightingMode < 1.5)
                {
                    // Balanced retains the map hue and direction while making
                    // an avatar readable in dark Beat Saber environments.
                    appliedLight = max(environmentLight, 0.42);
                }
                else if (_AvatarLightingMode >= 1.5)
                {
                    // Studio is shader-local: it lights only the avatar and
                    // therefore cannot brighten the gameplay environment.
                    float3 studioKeyDirection = normalize(viewDirection + float3(0.15, 0.45, 0.0));
                    float key = saturate(dot(normal, studioKeyDirection) * 0.5 + 0.5);
                    float studio = lerp(0.55, 0.95, key);
                    appliedLight = max(environmentLight * 0.20, studio.xxx);
                }
                float3 color = direct * appliedLight;

                #if defined(SABERSTAGE_RIM_LIGHT)
                    float fresnel = pow(saturate(1.0 - dot(normal, viewDirection) + _RimLift),
                        max(0.01, _RimFresnelPower));
                    float2 rimUv = transformedUv(input.uv0, input.uv1, _RimTextureCoord, _RimTexture_ST, _RimTextureRotation);
                    float3 rim = tex2D(_RimTexture, rimUv).rgb * _RimColor.rgb * fresnel;
                    color += rim * lerp(1.0, appliedLight, _RimLightingMix);
                #endif
                #if defined(SABERSTAGE_MATCAP)
                    float3 cameraUp = normalize(UNITY_MATRIX_V[1].xyz);
                    float3 viewUp = normalize(cameraUp - viewDirection * dot(viewDirection, cameraUp));
                    float3 viewRight = normalize(cross(viewDirection, viewUp));
                    float2 matcapUv = float2(dot(viewRight, normal), dot(viewUp, normal)) * 0.5 + 0.5;
                    color += tex2D(_SphereAdd, matcapUv).rgb;
                #endif
                #if defined(SABERSTAGE_EMISSION)
                    float2 emissionUv = transformedUv(input.uv0, input.uv1, _EmissionMapCoord, _EmissionMap_ST, _EmissionMapRotation);
                    color += min(tex2D(_EmissionMap, emissionUv).rgb * _EmissionColor.rgb, 4.0);
                #endif
                return fixed4(color, mainSample.a);
            }
            ENDCG
        }
    }
    Fallback "Unlit/Texture"
}
