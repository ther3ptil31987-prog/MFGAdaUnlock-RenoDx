"""Scalar contracts and emitted-fragment checks; not a visual quality test."""
import math
from pathlib import Path
import random
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import build_thin_geometry_variants as builder


def sat(x):
    return 0.0 if math.isnan(x) else min(max(x, 0.0), 1.0)


def weight(native, static_error, mutual_error, pair_valid=True):
    native = sat(native)
    agreement = min(sat((static_error - mutual_error - .08) / .15),
                    sat((.15 - mutual_error) / .15)) if pair_valid else 0.0
    evidence = max(agreement, min(sat((native - .2) / .3),
                                  sat((static_error - .25) / .25)))
    evidence = evidence * evidence * (3 - 2 * evidence)
    return native + evidence * (max(native, .85) - native)


def support(motion_support, depth_delta):
    if not math.isfinite(motion_support) or motion_support <= 0:
        return 0.0
    return min(motion_support, sat(3 - abs(depth_delta)))


def geometry_support_v2(neighbor_supports):
    # Smooth only the best existing local-support signal. One perfect neighbor
    # remains sufficient; invalid/NaN support does not add retention.
    strongest = max((sat(value) for value in neighbor_supports), default=0.0)
    return strongest * strongest * (3 - 2 * strongest)


def asymmetric_depth_support(center_depth, neighbor_depth):
    # Larger processed depth is nearer in the normalized provider path.
    return sat(3 - (neighbor_depth - center_depth))


def border_weight(native, boosted, x, y, width, height):
    distance_px = min(min(x, 1 - x) * width,
                      min(y, 1 - y) * height) - .5
    confidence = sat(distance_px / 2)
    confidence = confidence * confidence * (3 - 2 * confidence)
    return native + confidence * (boosted - native)


def main():
    assert support(1, 0) == 1  # one correct neighbor remains sufficient
    assert support(1, 2) == 1
    assert support(1, 2.5) == .5
    assert support(1, 3) == 0
    assert support(1, math.nan) == 0
    assert support(math.nan, 0) == 0
    assert geometry_support_v2([1, 0, 0, 0]) == 1
    assert geometry_support_v2([math.nan, 0, 0, 0]) == 0
    assert geometry_support_v2([0, 0, 0, 0]) == 0
    previous = -1.0
    for step in range(1001):
        candidate = step / 1000
        confidence = geometry_support_v2([candidate, 0, 0, 0])
        assert 0 <= confidence <= 1 and confidence >= previous
        assert .5 <= 1 - .5 * confidence <= 1
        previous = confidence
    assert geometry_support_v2([.25]) < .25
    assert geometry_support_v2([.75]) > .75
    # A foreground center keeps support from a farther neighbor, while a
    # background center is tapered when a nearer occluder crosses it.
    assert asymmetric_depth_support(8, 2) == 1
    assert asymmetric_depth_support(2, 8) == 0
    assert asymmetric_depth_support(2, 4.5) == .5
    rng = random.Random(91)
    for _ in range(20000):
        w, e, m = rng.random(), rng.random(), rng.random()
        refined = weight(w, e, m)
        assert w <= refined <= max(w, .85) + 1e-12
        assert abs(refined - weight(w, e + 1e-7, m)) < 1e-5
        assert abs(refined - weight(w, e, m + 1e-7)) < 1e-5
    assert weight(.2, .250001, .8) == .2
    assert weight(.5, .9, 0) == .85
    assert weight(.1, .08, 0) == .1
    assert weight(.1, .5, 0, False) == .1
    for n, b in ((.1, .85), (.6, .9), (.9, .9)):
        assert border_weight(n, b, .5 / 1920, .5, 1920, 1080) == n
        assert border_weight(n, b, 2.5 / 1920, .5, 1920, 1080) == b
        previous = n
        for pixel in range(1, 25):
            value = border_weight(n, b, (.5 + pixel / 10) / 1920,
                                  .5, 1920, 1080)
            assert n <= value <= b and value >= previous
            previous = value

    blend = '.reg .pred %p<260>;\nld.param.u8 %rs8, [%rd6+220];\n'
    refined = builder.patch_refined_blend(blend)
    header = (ROOT / 'src/addons/mfgunlock/quality_refinement.hpp').read_text()
    fragment = header.split('R"PTX(', 1)[1].split(')PTX"', 1)[0]
    assert fragment in refined
    assert refined.count('MFGUNLOCK_SMOOTH_CONFIDENCE_V1') == 1
    assert refined.count('MFGUNLOCK_FULL_CONFIDENCE_FAST_PATH_V1') == 1
    assert refined.count('@%qv2 bra MFGUNLOCK_SMOOTH_CONFIDENCE_DONE_V1;') == 1
    assert refined.count('MFGUNLOCK_SMOOTH_CONFIDENCE_DONE_V1:') == 1
    border = builder.patch_refined_border_blend(blend)
    border_header = (ROOT / 'src/addons/mfgunlock/quality_border.hpp').read_text()
    border_fragment = border_header.split('R"PTX(', 1)[1].split(')PTX"', 1)[0]
    assert border_fragment in border
    assert border.count('MFGUNLOCK_BORDER_CONFIDENCE_V1') == 1
    assert border.count('MFGUNLOCK_BORDER_INTERIOR_FAST_PATH_V1') == 1
    assert border.count('@%qv2 bra MFGUNLOCK_BORDER_CONFIDENCE_DONE_V1;') == 1
    assert border.count('MFGUNLOCK_BORDER_CONFIDENCE_DONE_V1:') == 1
    assert border.count('sub.f32 %qf6, %qf0, %qf8;') == 1
    assert border.count('sub.f32 %qf6, %qf1, %qf10;') == 1
    # Only verified insertion anchors are accepted. No broad pattern scanning.
    geometry = '.reg .pred %p<656>;\n' + ''.join(
        spec['anchor'] for spec in builder._SILHOUETTE_NEIGHBORS.values())
    refined = builder.patch_refined_geometry(geometry)
    assert refined.count('rcp.approx.ftz.f32 %qgf2') == 2
    assert refined.count('sub.sat.f32 %qgf7') == 8
    assert refined.count('max.f32 %qgf0') == 8
    assert 'div.approx.ftz.f32 %qgf6' not in refined
    assert 'setp.lt.and.f32 %qgp0' not in refined
    v2 = builder.patch_geometry_confidence_v2(geometry)
    assert v2.count('MFGUNLOCK_GEOMETRY_CONFIDENCE_V2') == 2
    assert v2.count('sub.ftz.sat.f32 %qgf6') == 8
    assert v2.count('max.f32 %qgf0') == 8
    assert 'setp.gt.f32 %qgp0' not in v2
    assert '@!%qgp0 mov.f32' not in v2
    assert v2.count('mul.f32 %qgf0, %qgf0, %qgf6;') == 2
    adaptive_geometry = builder.patch_adaptive_geometry_v1(geometry)
    assert adaptive_geometry.count('MFGUNLOCK_ASYMMETRIC_DISOCCLUSION_V1') == 1
    assert adaptive_geometry.count('abs.ftz.f32 %qgf7, %qgf7;') == 0
    assert adaptive_geometry.count('sub.sat.f32 %qgf7') == 8
    adaptive_blend = builder.patch_adaptive_blend(blend)
    adaptive_header = (ROOT / 'src/addons/mfgunlock/adaptive_quality.hpp').read_text()
    adaptive_fragment = adaptive_header.split('R"PTX(', 1)[1].split(')PTX"', 1)[0]
    assert adaptive_fragment in adaptive_blend
    assert adaptive_blend.count('MFGUNLOCK_CANDIDATE_ARBITRATION_V2') == 1
    assert adaptive_blend.count('MFGUNLOCK_AGREEMENT_FAST_PATH_V1') == 1
    assert adaptive_blend.count(
        '@%qv2 bra MFGUNLOCK_CANDIDATE_ARBITRATION_DONE_V2;') == 1
    assert adaptive_blend.count(
        'MFGUNLOCK_CANDIDATE_ARBITRATION_DONE_V2:') == 1
    assert adaptive_blend.count('selp.f32 %qf2, %qf1, %qf0, %qv2;') == 1
    assert adaptive_blend.count('selp.f32 %qf3, %qf10, %qf8, %qv2;') == 1
    inpaint = ('setp.gt.ftz.f32 %p6, %f39, 0f00000000;\n'
               'setp.gt.ftz.f32 %p13, %f43, 0f00000000;\n')
    inpaint = builder.patch_adaptive_inpaint_decision_v1(inpaint)
    assert inpaint.count('setp.gtu.ftz.f32') == 2
    assert inpaint.count('MFGUNLOCK_INPAINT_DECISION_NONFINITE_V1') == 1
    temporal_inpaint = builder._inpaint_temporal_program()
    assert temporal_inpaint.count('ld.global.u8') == 1
    assert temporal_inpaint.count('st.global.u8') == 1
    assert 'tex.' not in temporal_inpaint
    assert 'ld.global.v4.u32' in temporal_inpaint
    assert 'mfgunlock_v34_inpaint_control+32' in temporal_inpaint
    assert 'mov.u32 %qir4, 0;' in temporal_inpaint
    assert 'mov.u32 %qir5, 2;' in temporal_inpaint
    assert 'setp.ge.u32 %qip1, %qir1, 47;' in temporal_inpaint
    assert 'setp.le.u32 %qip2, %qir6, 4;' in temporal_inpaint
    assert 'or.pred %p33, %p33, %qip1;' in temporal_inpaint
    print('quality refinement contracts passed; runtime image quality remains unverified')


if __name__ == '__main__':
    main()
