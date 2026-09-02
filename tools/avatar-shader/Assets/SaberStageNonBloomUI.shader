// Multiview-safe UI accent shader for SaberStage's world-space panels.
//
// Beat Saber uses the rendered alpha channel as a bloom weight. Ordinary UI
// materials can therefore turn a bright blue border into a large glowing haze.
// This pass uses source alpha for normal RGB blending while explicitly writing
// zero alpha to the framebuffer, keeping the border bright without contributing
// to the game's bloom mask.
Shader "SaberStage/NonBloomUI"
{
    Properties
    {
        _MainTex ("Texture", 2D) = "white" {}
        _Color ("Tint", Color) = (1,1,1,1)
    }
    SubShader
    {
        Tags { "RenderType"="Transparent" "Queue"="Transparent+20" "IgnoreProjector"="True" }
        Pass
        {
            Cull Off
            ZWrite Off
            ZTest [unity_GUIZTestMode]
            // RGB: conventional alpha blend. Alpha: replace destination alpha
            // with zero so this visual never enters Beat Saber's bloom mask.
            Blend SrcAlpha OneMinusSrcAlpha, Zero Zero

            CGPROGRAM
            #pragma target 3.0
            #pragma vertex vert
            #pragma fragment frag
            #pragma multi_compile _ STEREO_MULTIVIEW_ON STEREO_INSTANCING_ON
            #pragma multi_compile_instancing
            #include "UnityCG.cginc"

            sampler2D _MainTex;
            float4 _MainTex_ST;
            fixed4 _Color;

            struct appdata
            {
                float4 vertex : POSITION;
                float2 uv : TEXCOORD0;
                fixed4 color : COLOR;
                UNITY_VERTEX_INPUT_INSTANCE_ID
            };

            struct v2f
            {
                float4 position : SV_POSITION;
                float2 uv : TEXCOORD0;
                fixed4 color : COLOR;
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
                output.color = input.color;
                return output;
            }

            fixed4 frag(v2f input) : SV_Target
            {
                UNITY_SETUP_INSTANCE_ID(input);
                UNITY_SETUP_STEREO_EYE_INDEX_POST_VERTEX(input);
                const fixed4 sampled = tex2D(_MainTex, input.uv);
                return fixed4(sampled.rgb * _Color.rgb * input.color.rgb,
                              sampled.a * _Color.a * input.color.a);
            }
            ENDCG
        }
    }
}
