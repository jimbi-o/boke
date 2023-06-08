#define VERTEX_NUM 3
#define FACE_NUM 1
struct VertexOut {
  float4 position : SV_POSITION;
  float2 uv : TEXCOORD0;
};
[NumThreads(1, 1, 1)]
[OutputTopology("triangle")]
void main(out vertices VertexOut vertices[VERTEX_NUM], out indices uint3 faces[FACE_NUM]) {
  SetMeshOutputCounts(VERTEX_NUM, FACE_NUM);
  vertices[0].position = float4(-1.0f,  1.0f, 0.0f, 1.0f);
  vertices[0].uv = float2(0.0f, 0.0f);
  vertices[1].position = float4( 3.0f,  1.0f, 0.0f, 1.0f);
  vertices[1].uv = float2(2.0f, 0.0f);
  vertices[2].position = float4(-1.0f, -3.0f, 0.0f, 1.0f);
  vertices[2].uv = float2(0.0f, 2.0f);
  faces[0] = uint3(0, 1, 2);
}
