struct PSInput {
  float4 position : SV_POSITION;
  float2 uv : TEXCOORD0;
};
Texture2D src : register(t0);
SamplerState tex_sampler : register(s0);
float4 main(PSInput input) : SV_Target0 {
  float4 color = src.Sample(tex_sampler, input.uv);
  return color;
}
