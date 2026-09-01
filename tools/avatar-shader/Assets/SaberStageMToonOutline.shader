Shader "SaberStage/MToonOutline"
{
    Properties
    {
        _OutlineColor ("Outline Color", Color) = (0,0,0,1)
        _OutlineWidth ("Outline Width", Range(0,0.02)) = 0.002
        _OutlineWidthTexture ("Outline Width Mask", 2D) = "white" {}
        _OutlineColorMode ("Outline Color Mode (0 fixed, 1 mixed)", Float) = 0
        _OutlineLightingMix ("Outline Lighting Mix", Range(0,1)) = 1
        _MainTex ("Main Texture", 2D) = "white" {}
        _MainTexCoord ("Main UV Set", Float) = 0
        _MainTexRotation ("Main UV Rotation", Float) = 0
        _Cutoff ("Alpha Cutoff", Range(0,1)) = 0.5
        [Toggle] _AlphaToMask ("Alpha To Coverage", Float) = 0
        _CutoutSmoothing ("Cutout Smoothing", Range(0,3)) = 1
    }
    SubShader
    {
        Tags { "RenderType"="Opaque" "Queue"="Geometry-1" }
        Pass
        {
            Cull Front
            ZWrite On
            AlphaToMask [_AlphaToMask]
            CGPROGRAM
            #pragma target 3.0
            #pragma vertex vert
            #pragma fragment frag
            #pragma multi_compile _ STEREO_MULTIVIEW_ON STEREO_INSTANCING_ON
            #pragma multi_compile_instancing
            #pragma multi_compile_local _ SABERSTAGE_ALPHA_TEST
            #include "UnityCG.cginc"
            sampler2D _MainTex, _OutlineWidthTexture;
            float4 _MainTex_ST, _OutlineColor;
            float _OutlineWidth, _OutlineColorMode, _OutlineLightingMix;
            float _Cutoff, _MainTexCoord, _MainTexRotation;
            float _AlphaToMask, _CutoutSmoothing;
            struct appdata { float4 vertex:POSITION; float3 normal:NORMAL; float2 uv0:TEXCOORD0; float2 uv1:TEXCOORD1; UNITY_VERTEX_INPUT_INSTANCE_ID };
            struct v2f { float4 position:SV_POSITION; float2 uv0:TEXCOORD0; float2 uv1:TEXCOORD1; UNITY_VERTEX_INPUT_INSTANCE_ID UNITY_VERTEX_OUTPUT_STEREO };
            float2 transformedUv(float2 uv0, float2 uv1) {
                float2 uv = lerp(uv0, uv1, step(0.5, _MainTexCoord));
                uv *= _MainTex_ST.xy;
                float s = sin(_MainTexRotation), c = cos(_MainTexRotation);
                return float2(c * uv.x - s * uv.y, s * uv.x + c * uv.y) + _MainTex_ST.zw;
            }
            v2f vert(appdata input) {
                v2f output;
                UNITY_SETUP_INSTANCE_ID(input);
                UNITY_TRANSFER_INSTANCE_ID(input, output);
                UNITY_INITIALIZE_VERTEX_OUTPUT_STEREO(output);
                // MToon thins or suppresses the outline per-region through the
                // width mask's red channel (nostrils, inner ears, palms).
                // Sampled in the vertex stage, so it needs an explicit LOD.
                float2 uv = transformedUv(input.uv0, input.uv1);
                float widthMask = tex2Dlod(_OutlineWidthTexture, float4(uv, 0, 0)).r;
                float3 displaced = input.vertex.xyz + input.normal * (_OutlineWidth * widthMask);
                output.position = UnityObjectToClipPos(float4(displaced, 1));
                output.uv0 = input.uv0;
                output.uv1 = input.uv1;
                return output;
            }
            fixed4 frag(v2f input):SV_Target {
                UNITY_SETUP_INSTANCE_ID(input);
                UNITY_SETUP_STEREO_EYE_INDEX_POST_VERTEX(input);
                fixed4 mainSample = tex2D(_MainTex, transformedUv(input.uv0, input.uv1));
                float cutoutCoverage = 1.0;
                #if defined(SABERSTAGE_ALPHA_TEST)
                    float alpha = mainSample.a;
                    if (_CutoutSmoothing > 0.5)
                    {
                        float smoothingWidth = max(
                            fwidth(alpha) * (0.65 * _CutoutSmoothing), 0.0001);
                        alpha = saturate((alpha - _Cutoff) / smoothingWidth + 0.5);
                        if (_AlphaToMask < 0.5)
                        {
                            float2 pixel = floor(input.position.xy);
                            float threshold = frac(52.9829189 * frac(
                                dot(pixel, float2(0.06711056, 0.00583715))));
                            clip(alpha - threshold);
                        }
                        else
                        {
                            clip(alpha - 0.001);
                        }
                        cutoutCoverage = alpha;
                    }
                    else
                    {
                        clip(alpha - _Cutoff);
                    }
                #endif
                // Fixed mode paints the authored color; mixed mode (hair and
                // similar) tints the outline with the surface texture, faded
                // by the authored lighting mix.
                float3 mixedTint = lerp(float3(1, 1, 1), mainSample.rgb, saturate(_OutlineLightingMix));
                float3 tint = lerp(float3(1, 1, 1), mixedTint, step(0.5, _OutlineColorMode));
                return fixed4(_OutlineColor.rgb * tint, _OutlineColor.a * cutoutCoverage);
            }
            ENDCG
        }
    }
}
