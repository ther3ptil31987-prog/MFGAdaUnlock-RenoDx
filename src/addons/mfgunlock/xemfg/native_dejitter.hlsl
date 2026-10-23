// SPDX-License-Identifier: MIT
// Plan 4.2: removes the per-frame jitter difference from game motion vectors
// so XeFG can run its unjittered reconstruction program.
Texture2D<float2> Source : register(t0);
RWTexture2D<float2> Target : register(u0);
cbuffer Params : register(b0) {
  uint2 base;   // source subrectangle origin
  uint2 size;   // subrectangle size, equal to the target size
  float2 delta; // jitter difference in buffer units, see DejitterDelta
};
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (any(id.xy >= size)) return;
  Target[id.xy] = Source[id.xy + base] - delta;
}
