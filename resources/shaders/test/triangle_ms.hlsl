#define VERTEX_NUM 3
#define FACE_NUM 1
struct VertexOut {
  float4 pos : SV_Position;
};
[NumThreads(1, 1, 1)]
[OutputTopology("triangle")]
void main(out vertices VertexOut vertices[VERTEX_NUM], out indices uint3 faces[FACE_NUM]) {
  SetMeshOutputCounts(VERTEX_NUM, FACE_NUM);
  vertices[0].pos = float4(1.0f, 0.0f, 0.0f, 1.0f);
  vertices[1].pos = float4(0.0f, 1.0f, 0.0f, 1.0f);
  vertices[2].pos = float4(-1.0f, 0.0f, 0.0f, 1.0f);
  faces[0] = uint3(0, 1, 2);
}
