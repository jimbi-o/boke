struct PSOutput {
  float4 color0 : SV_Target0;
  float4 color1 : SV_Target1;
  float4 color2 : SV_Target2;
  float4 color3 : SV_Target3;
};
PSOutput main() {
  PSOutput output;
  output.color0 = float4(0.0f, 1.0f, 1.0f, 1.0f);
  output.color1 = float4(1.0f, 0.0f, 1.0f, 1.0f);
  output.color2 = float4(1.0f, 1.0f, 0.0f, 1.0f);
  output.color3 = float4(1.0f, 1.0f, 1.0f, 1.0f);
  return output;
}
