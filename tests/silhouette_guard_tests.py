#!/usr/bin/env python3
"""Property checks for the silhouette-boundary guard's scalar contract."""

from __future__ import annotations

import math
from pathlib import Path
import random
import sys


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import build_thin_geometry_variants as builder  # noqa: E402


def effective_scale(center_motion, center_depth, neighbors,
                    native_threshold=1.0, aggressive=False):
    supports = []
    threshold = max(float(native_threshold), 1.0)
    depth_limit = 2.0 if aggressive else 3.0
    for motion, depth in neighbors:
        dx = motion[0] - center_motion[0]
        dy = motion[1] - center_motion[1]
        support = 1.0 - (dx * dx + dy * dy) / threshold
        depth_delta = abs(depth - center_depth)
        if not math.isfinite(support) or not math.isfinite(depth_delta):
            support = 0.0
        if support <= 0.0 or not depth_delta < depth_limit:
            support = 0.0
        supports.append(min(support, 1.0))
    if aggressive:
        confidence = min(max(sum(supports) - 1.0, 0.0), 1.0)
        return 1.0 - 0.25 * confidence * confidence
    return 1.0 - 0.5 * max(supports, default=0.0)


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def main() -> None:
    center = (4.0, -2.0)
    incompatible = [((20.0, 20.0), 0.0)] * 4
    check(effective_scale(center, 0.0, incompatible) == 1.0,
          "unsupported boundary must retain the native divisor")

    one_thin_neighbor = [((4.0, -2.0), 0.0)] + incompatible[1:]
    check(effective_scale(center, 0.0, one_thin_neighbor) == 0.5,
          "one coherent same-depth neighbor must preserve full added retention")

    wrong_surface = [((4.0, -2.0), 3.0)] + incompatible[1:]
    check(effective_scale(center, 0.0, wrong_surface) == 1.0,
          "the provider's depth boundary must exclude cross-surface support")

    partial = [((4.5, -2.0), 0.0)] + incompatible[1:]
    check(0.5 < effective_scale(center, 0.0, partial) < 1.0,
          "partial motion support must produce a bounded continuous response")

    unordered = [((math.nan, -2.0), 0.0)] + incompatible[1:]
    check(effective_scale(center, 0.0, unordered) == 1.0,
          "unordered motion must not add retention")

    check(effective_scale(center, 0.0, one_thin_neighbor,
                          aggressive=True) == 1.0,
          "aggressive mode must reject a single-neighbor boundary")
    two_neighbors = [((4.0, -2.0), 0.0), ((4.0, -2.0), 0.0)] + incompatible[2:]
    check(effective_scale(center, 0.0, two_neighbors,
                          aggressive=True) == 0.75,
          "full aggressive support must stop at 0.75 of the native divisor")
    aggressive_depth_edge = [((4.0, -2.0), 2.0),
                             ((4.0, -2.0), 2.0)] + incompatible[2:]
    check(effective_scale(center, 0.0, aggressive_depth_edge,
                          aggressive=True) == 1.0,
          "aggressive mode must use its tighter depth boundary")

    rng = random.Random(0x5A17)
    for _ in range(1000):
        random_neighbors = [
            ((center[0] + rng.uniform(-2.0, 2.0),
              center[1] + rng.uniform(-2.0, 2.0)),
             rng.uniform(-4.0, 4.0))
            for _ in range(4)
        ]
        balanced = effective_scale(center, 0.0, random_neighbors)
        aggressive = effective_scale(center, 0.0, random_neighbors,
                                     aggressive=True)
        check(0.5 <= balanced <= 1.0,
              "balanced scale escaped its bounded range")
        check(0.75 <= aggressive <= 1.0,
              "aggressive scale escaped its bounded range")
        check(aggressive >= balanced,
              "aggressive mode became more permissive than Balanced")

    for direction in builder._SILHOUETTE_NEIGHBORS:
        program = builder._silhouette_support_program(direction)
        check("MFGUNLOCK_SILHOUETTE_BOUNDARY_GUARD_BALANCED_V1" in program,
              "generated PTX marker is missing")
        check(program.count("setp.lt.and.f32") == 4,
              "each cardinal neighbor must have one depth-membership gate")
        check(program.count("max.f32 %qgf0") == 4,
              "the guard must preserve the strongest thin-geometry support")
        check("mul.ftz.f32 %f2, %f2, %qgf11;" in program,
              "the final bounded divisor update is missing")

        aggressive_program = builder._silhouette_support_program(
            direction, aggressive=True)
        check("AGGRESSIVE_V1" in aggressive_program,
              "aggressive PTX marker is missing")
        check(aggressive_program.count("0f40000000") == 4,
              "aggressive depth threshold was not applied to every neighbor")
        check(aggressive_program.count("add.f32 %qgf0") == 4,
              "aggressive support must accumulate all cardinal neighbors")
        check("mul.f32 %qgf0, %qgf0, %qgf0;" in aggressive_program,
              "aggressive nonlinear confidence is missing")
        check("0fBE800000" in aggressive_program,
              "aggressive relaxation cap is missing")

        diagonal_program = builder._diagonal_support_program(direction)
        check("MFGUNLOCK_CONTINUOUS_DIAGONAL_SUPPORT" in diagonal_program,
              "V3.2 diagonal-support marker is missing")
        check("@%qgp1 bra MFGUNLOCK_GEOMETRY_DIAGONALS_DONE" in
              diagonal_program,
              "V3 zero-motion diagonal fast path is missing")
        check("0f3EB33333" in diagonal_program and
              "0f3F19999A" not in diagonal_program,
              "V3.2 continuous ambiguity ramp is missing")
        check("div.approx.f32 %qgf4, %qgf4, %qgf3;" in
              diagonal_program,
              "V3.2 continuous two-axis relevance is missing")
        check(f"mul.f32 %qgf3, {builder._SILHOUETTE_NEIGHBORS[direction]['center'][0]}, "
              f"{builder._SILHOUETTE_NEIGHBORS[direction]['center'][1]};" in
              diagonal_program,
              "V3 motion-quadrant selection is missing")
        check(diagonal_program.count("selp.f32 %qgf3") == 2,
              "V3 must inspect exactly one aligned diagonal pair")
        check(diagonal_program.count("min.f32 %qgf7, %qgf0, %qgf6") == 1,
              "the coherent diagonal pair must update top-two exactly once")
        check("min.f32 %qgf6, %qgf6, %qgf11;" in diagonal_program,
              "V3.2 must require both sides of the diagonal pair")
        check("ld." not in diagonal_program,
              "V3 diagonal support introduced an unexpected load")

        history_program = builder._temporal_history_program(direction)
        check(history_program.count("ld.global") == 4,
              "temporal V3.2 must use four global-load instructions per direction")
        check(history_program.count("ld.global.v4.u32") == 1,
              "temporal V3.2 control vector load is missing")
        check(history_program.count("ld.global.u8") == 1,
              "temporal V3.2 must read one confidence byte per direction")
        check(history_program.count("st.global.u8") == 1,
              "temporal V3.2 must write one confidence byte per direction")
        check("tex." not in history_program,
              "temporal V3.2 introduced an unexpected texture read")
        check("mul.f32 %qgf3, %f1, %qgf3;" in history_program,
              "temporal V3.2 phase must come from provider parameter t")
        check("0f3E4CCCCD" in history_program,
              "temporal recovery cap of 0.20 is missing")
        check("@%qgp1 min.f32 %qgf3, %qgf3, %qgf4;" in
              history_program and
              "@!%qgp1 add.f32 %qgf4, %qgf4, 0f3E4CCCCD;" in
              history_program,
              "the symmetric phase must be drop-only so recovery is capped per source frame")

    print("silhouette guard tests passed")


if __name__ == "__main__":
    main()
