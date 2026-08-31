// Camera-preview surface shader for SaberStage's world-space monitors.
//
// Why this exists (two lessons imported from the author's Big Screen mod):
// 1. A shader whose STEREO_MULTIVIEW_ON variants are missing "binds without
//    error and rasterizes nothing in either eye" on the Quest's single-pass
//    multiview renderer. Stock shaders fetched with Shader.Find (such as
//    Unlit/Texture) only carry the variants Beat Saber happened to package,
//    which made the popout preview invisible in the headset while the mono
//    spectator camera could still see it. This bundle is built with the
//    Oculus Multiview XR configuration, so the variants are guaranteed.
// 2. The spectator RenderTexture's alpha channel is Beat Saber's bloom
//    weight, not image opacity. The fragment therefore forces alpha to 1 so
//    neither UI alpha blending nor the bloom post-process misreads it.
Shader "SaberStage/VideoPreview"
{
    Properties
    {
        _MainTex ("Texture", 2D) = "black" {}
        _Color ("Tint", Color) = (1,1,1,1)
    }
    SubShader
    {
        Tags { "RenderType"="Opaque" "Queue"="Geometry" }
        Pass
        {
            Cull Off
            ZWrite On
            CGPROGRAM
            #pragma target 3.0
            #pragma vertex vert
            #pragma fragment frag
            #pragma multi_compile _ STEREO_MULTIVIEW_ON STEREO_INSTANCING_ON
            #pragma multi_compile_instancing
            #include "UnityCG.cginc"
            sampler2D _MainTex;
            float4 _MainTex_ST;
            float4 _Color;
            struct appdata
            {
                float4 vertex : POSITION;
                float2 uv : TEXCOORD0;
                UNITY_VERTEX_INPUT_INSTANCE_ID
            };
            struct v2f
            {
                float4 position : SV_POSITION;
                float2 uv : TEXCOORD0;
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
                output.uv = TRANSFORM_TEX(input.uv, _MainTex);
                return output;
            }
            fixed4 frag(v2f input) : SV_Target
            {
                UNITY_SETUP_INSTANCE_ID(input);
                UNITY_SETUP_STEREO_EYE_INDEX_POST_VERTEX(input);
                return fixed4(tex2D(_MainTex, input.uv).rgb * _Color.rgb, 1.0);
            }
            ENDCG
        }
    }
}
