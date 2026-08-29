Shader "SaberStage/MToonOutline"
{
    Properties
    {
        _OutlineColor ("Outline Color", Color) = (0,0,0,1)
        _OutlineWidth ("Outline Width", Range(0,0.02)) = 0.002
        _MainTex ("Main Texture", 2D) = "white" {}
        _MainTexCoord ("Main UV Set", Float) = 0
        _MainTexRotation ("Main UV Rotation", Float) = 0
        _Cutoff ("Alpha Cutoff", Range(0,1)) = 0.5
        [Toggle] _AlphaToMask ("Alpha To Coverage", Float) = 0
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
            sampler2D _MainTex;
            float4 _MainTex_ST, _OutlineColor;
            float _OutlineWidth, _Cutoff, _MainTexCoord, _MainTexRotation;
            struct appdata { float4 vertex:POSITION; float3 normal:NORMAL; float2 uv0:TEXCOORD0; float2 uv1:TEXCOORD1; UNITY_VERTEX_INPUT_INSTANCE_ID };
            struct v2f { float4 position:SV_POSITION; float2 uv0:TEXCOORD0; float2 uv1:TEXCOORD1; UNITY_VERTEX_INPUT_INSTANCE_ID UNITY_VERTEX_OUTPUT_STEREO };
            v2f vert(appdata input) {
                v2f output;
                UNITY_SETUP_INSTANCE_ID(input);
                UNITY_TRANSFER_INSTANCE_ID(input, output);
                UNITY_INITIALIZE_VERTEX_OUTPUT_STEREO(output);
                output.position = UnityObjectToClipPos(float4(input.vertex.xyz + input.normal * _OutlineWidth, 1));
                output.uv0 = input.uv0;
                output.uv1 = input.uv1;
                return output;
            }
            float2 transformedUv(float2 uv0, float2 uv1) {
                float2 uv = lerp(uv0, uv1, step(0.5, _MainTexCoord));
                uv *= _MainTex_ST.xy;
                float s = sin(_MainTexRotation), c = cos(_MainTexRotation);
                return float2(c * uv.x - s * uv.y, s * uv.x + c * uv.y) + _MainTex_ST.zw;
            }
            fixed4 frag(v2f input):SV_Target {
                UNITY_SETUP_INSTANCE_ID(input);
                UNITY_SETUP_STEREO_EYE_INDEX_POST_VERTEX(input);
                #if defined(SABERSTAGE_ALPHA_TEST)
                    float alpha = tex2D(_MainTex, transformedUv(input.uv0, input.uv1)).a;
                    float alphaWidth = max(fwidth(alpha), 0.0001);
                    alpha = saturate((alpha - _Cutoff) / alphaWidth + 0.5);
                    clip(alpha - 0.001);
                #endif
                return _OutlineColor;
            }
            ENDCG
        }
    }
}
