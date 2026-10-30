// SPDX-License-Identifier: MIT
// 1.7 (plan E.2): a UI texture for games without HUD-less color. A pixel that
// stayed the same across `threshold` real frames is treated as HUD: XeFG
// composes it from the newest real frame over every generated frame instead
// of interpolating it with the scene. Everything else is transparent.
Texture2D<float4> Current : register(t0);
Texture2D<float4> Previous : register(t1);
Texture2D<uint> PersistIn : register(t2);
Texture2D<float4> Reference : register(t3); // the game's HUD-less color
RWTexture2D<uint> PersistOut : register(u0);
RWTexture2D<float4> Ui : register(u1);
cbuffer Params : register(b0) {
  uint2 size;
  uint threshold;  // consecutive unchanged real frames before a pixel is HUD
  float tolerance; // stillness: largest per-channel change still unchanged;
                   // coverage: smallest difference from HUD-less that is HUD
  uint reset;      // 1: no previous frame; history starts empty
  uint coverage;   // 1: Reference is valid; count frames the pixel is HUD
};
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (any(id.xy >= size)) return;
  const float4 c = Current[id.xy];
  bool counted;
  if (coverage != 0) {
    // HUD wherever the back buffer differs from the HUD-less image, also for
    // semi-transparent HUD over a moving scene.
    const float3 h = abs(c.rgb - Reference[id.xy].rgb);
    counted = max(h.r, max(h.g, h.b)) > tolerance;
  } else {
    const float3 d = abs(c.rgb - Previous[id.xy].rgb);
    counted = reset == 0 && max(d.r, max(d.g, d.b)) <= tolerance;
  }
  const uint before = reset != 0 ? 0u : PersistIn[id.xy];
  const uint persist = counted ? min(before + 1, 255u) : 0u;
  PersistOut[id.xy] = persist;
  // One pixel of dilation, from the previous frame's persistence, covers the
  // anti-aliased edges of HUD glyphs, which blend with the moving scene.
  bool hud = persist >= threshold;
  [unroll] for (int y = -1; y <= 1; ++y) {
    [unroll] for (int x = -1; x <= 1; ++x) {
      const int2 q = int2(id.xy) + int2(x, y);
      if (reset == 0 && all(q >= 0) && all(q < int2(size)))
        hud = hud || PersistIn[q] >= threshold;
    }
  }
  // Colour is zero where alpha is zero, so straight and premultiplied
  // composition read the same texture identically.
  Ui[id.xy] = hud ? float4(c.rgb, 1.0) : float4(0.0, 0.0, 0.0, 0.0);
}
