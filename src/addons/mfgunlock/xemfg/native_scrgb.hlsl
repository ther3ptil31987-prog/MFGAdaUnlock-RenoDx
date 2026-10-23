// SPDX-License-Identifier: MIT
// Plan 4.4: XeFG presents HDR only as RGB10A2 in HDR10/BT.2100. A game that
// renders scRGB (FP16, linear BT.709, 1.0 = 80 nits) is encoded to PQ BT.2020
// into the Intel proxy's own backbuffer before Present.
Texture2D<float4> Source : register(t0);

float4 FullscreenVS(uint id : SV_VertexID) : SV_Position {
  const float2 uv = float2((id << 1) & 2, id & 2);
  return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}

// ITU-R BT.2087 linear BT.709 to BT.2020 primaries.
static const float3x3 kBt709ToBt2020 = {
  0.6274040f, 0.3292820f, 0.0433136f,
  0.0690970f, 0.9195400f, 0.0113612f,
  0.0163916f, 0.0880132f, 0.8955950f };

// SMPTE ST 2084 inverse EOTF, absolute luminance in nits.
float3 PqEncode(float3 nits) {
  const float m1 = 0.1593017578125f, m2 = 78.84375f;
  const float c1 = 0.8359375f, c2 = 18.8515625f, c3 = 18.6875f;
  const float3 y = pow(saturate(nits / 10000.0f), m1);
  return pow((c1 + c2 * y) / (1.0f + c3 * y), m2);
}

float4 EncodePS(float4 position : SV_Position) : SV_Target {
  const float4 color = Source.Load(int3(position.xy, 0));
  // Colors outside BT.2020 clip to its gamut, as the desktop compositor does.
  const float3 nits = max(mul(kBt709ToBt2020, color.rgb), 0.0f) * 80.0f;
  return float4(PqEncode(nits), saturate(color.a));
}
