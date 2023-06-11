#define VERTEX_NUM 3
#define FACE_NUM 1
struct Camera {
  matrix view_matrix;
  matrix projection_matrix;
};
struct VertexOut {
  float4 pos : SV_Position;
};

ConstantBuffer<Camera> camera : register(b0);

[NumThreads(1, 1, 1)]
[OutputTopology("triangle")]
void main(out vertices VertexOut vertices[VERTEX_NUM], out indices uint3 faces[FACE_NUM]) {
  float4 pos[VERTEX_NUM];
  pos[0] = float4(-1.0f, 0.0f, 0.0f, 1.0f);
  pos[1] = float4( 0.0f, 1.0f, 0.0f, 1.0f);
  pos[2] = float4( 1.0f, 0.0f, 0.0f, 1.0f);
  [unroll]
  for (uint i = 0; i < VERTEX_NUM; i++) {
    float4 pos_vs = mul(camera.view_matrix, pos[i]);
    pos[i] = mul(camera.projection_matrix, pos_vs);
  }
  SetMeshOutputCounts(VERTEX_NUM, FACE_NUM);
  vertices[0].pos = pos[0];
  vertices[1].pos = pos[1];
  vertices[2].pos = pos[2];
  faces[0] = uint3(0, 1, 2);
}
