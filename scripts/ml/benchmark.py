"""Benchmark inference latency for FP32 and INT8 ONNX variants.

What this script does:

    1. Loads each ONNX model into ONNX Runtime (CPU execution provider).
    2. Runs WARMUP_ITERATIONS forward passes to let JIT compilation,
       cache loading, and lazy initialization stabilize.
    3. Runs MEASUREMENT_ITERATIONS more forward passes, recording the
       wall-clock duration of each.
    4. Computes percentiles (p50, p95, p99) and prints them as a table.
    5. Writes the same numbers to benchmark-results.json so they can be
       embedded in the README or pasted onto a CV.

Why we measure percentiles, not averages:

    Latency distributions are usually right-skewed -- most inferences
    are tightly clustered, but rare outliers (page faults, GC, kernel
    interrupts) stretch the long tail. Averages absorb those outliers
    and report numbers that don't reflect either the typical case or
    the worst case.

    p50 (median) is the typical-case latency: half of inferences
    finish faster than this. Useful for capacity planning.

    p99 is what real-time systems care about: 99% of inferences
    finish within this budget; the remaining 1% are at risk of
    breaching real-time guarantees. For a quality inspection station
    seeing 10 products / second, p99 directly tells you whether the
    production line will hit its throughput target.

Why warmup matters:

    The first inference includes one-time costs that shouldn't be
    amortized into steady-state numbers:
        - ONNX Runtime building its execution graph
        - Memory allocator carving out pages
        - CPU caches getting populated with model weights
        - Branch predictor learning the access pattern

    Reporting cold-start latency mixed with steady state is a common
    amateur mistake. We discard the warmup iterations explicitly.

Usage:

    python benchmark.py \\
        --fp32-onnx ../../assets/models/mobilenetv2_fp32.onnx \\
        --int8-onnx ../../assets/models/mobilenetv2_int8.onnx \\
        --output benchmark-results.json

Talking points for an interview:

    Q: "How do you benchmark inference correctly?"
    A: Three rules. First, warm up -- the first 5-10 inferences are
       slower for cache and JIT reasons; discard them. Second, run
       enough iterations that percentile estimates stabilize; for
       p99 you need at least 100 samples and ideally 1000. Third,
       report percentiles, not averages -- averages hide tail behaviour
       that dominates user-perceived performance.

    Q: "Why CPU and not GPU?"
    A: Industrial PCs typically don't have discrete GPUs; the
       deployment target is the CPU. Reporting GPU numbers would
       be misleading because the production hardware can't run
       them. Also, GPU latency is dominated by host-device data
       transfer for small batches (1 image), which often makes
       CPU faster than GPU for single-image inference even before
       considering hardware availability.

    Q: "What hardware did you measure on?"
    A: Always document this -- "p99 = 18ms" without a CPU model is
       meaningless. The benchmark script writes hostname, CPU model,
       and physical core count alongside the latency numbers, so
       results stay reproducible and explainable.
"""
from __future__ import annotations

import argparse
import json
import logging
import platform
import sys
import time
from pathlib import Path

import numpy as np
import onnxruntime as ort

# Sample input shape: single ImageNet-style RGB image.
INPUT_BATCH: int = 1
INPUT_CHANNELS: int = 3
INPUT_SIZE: int = 224

# Warmup matters because the first inferences include one-time costs
# (graph construction, cache population). 20 is plenty for ONNX Runtime
# to stabilize.
WARMUP_ITERATIONS: int = 20

# 200 measurement iterations gives stable estimates for p50 and p95;
# for p99 it's at the lower bound of credibility but still useful as
# a directional signal. Bump higher (1000+) for production benchmarks.
MEASUREMENT_ITERATIONS: int = 200

# Percentiles reported in the output table.
PERCENTILES: tuple[int, ...] = (50, 90, 95, 99)

# Conversion: numpy time deltas come out in seconds.
SECONDS_TO_MS: float = 1000.0


def configure_logging(verbose: bool) -> None:
    level = logging.DEBUG if verbose else logging.INFO
    logging.basicConfig(
        level=level,
        format="%(asctime)s [%(levelname)5s] %(message)s",
        datefmt="%H:%M:%S",
        stream=sys.stderr,
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Benchmark FP32 / INT8 ONNX inference latency on CPU.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "--fp32-onnx",
        type=Path,
        required=True,
        help="Path to the FP32 .onnx file.",
    )
    parser.add_argument(
        "--int8-onnx",
        type=Path,
        default=None,
        help="Path to the INT8 .onnx; if absent, only FP32 is benchmarked.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("benchmark-results.json"),
        help="Where to write the structured results.",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Enable DEBUG-level logging.",
    )
    return parser.parse_args()


def make_input() -> np.ndarray:
    """Deterministic synthetic input.

    We don't care about prediction quality during benchmarking -- only
    timing. A fixed-seed random tensor keeps results reproducible across
    runs, which matters when comparing CPU generations or build settings.
    """
    rng = np.random.default_rng(seed=42)
    return rng.standard_normal(
        (INPUT_BATCH, INPUT_CHANNELS, INPUT_SIZE, INPUT_SIZE),
        dtype=np.float32,
    )


def benchmark_model(
    model_path: Path,
    input_array: np.ndarray,
) -> dict[str, float]:
    """Time MEASUREMENT_ITERATIONS inferences and return the stats.

    Returns a dict shaped for direct JSON serialisation:
        {
            "model": "mobilenetv2_fp32.onnx",
            "size_mb": 8.74,
            "p50_ms": 11.8,
            "p90_ms": 13.2,
            "p95_ms": 14.5,
            "p99_ms": 28.4,
            "min_ms": 11.1,
            "max_ms": 142.0,
            "iterations": 200,
        }
    """
    log = logging.getLogger(__name__)

    log.info("Loading %s ...", model_path)
    session = ort.InferenceSession(
        str(model_path), providers=["CPUExecutionProvider"])
    input_name = session.get_inputs()[0].name

    log.info("Warmup: %d iterations", WARMUP_ITERATIONS)
    for _ in range(WARMUP_ITERATIONS):
        session.run(None, {input_name: input_array})

    log.info("Measuring: %d iterations", MEASUREMENT_ITERATIONS)
    timings_s = np.empty(MEASUREMENT_ITERATIONS, dtype=np.float64)
    for i in range(MEASUREMENT_ITERATIONS):
        start = time.perf_counter()
        session.run(None, {input_name: input_array})
        timings_s[i] = time.perf_counter() - start

    timings_ms = timings_s * SECONDS_TO_MS
    size_mb = model_path.stat().st_size / (1024.0 * 1024.0)

    stats: dict[str, float] = {
        "model": model_path.name,
        "size_mb": round(size_mb, 2),
        "iterations": MEASUREMENT_ITERATIONS,
        "min_ms": round(float(np.min(timings_ms)), 3),
        "max_ms": round(float(np.max(timings_ms)), 3),
        "mean_ms": round(float(np.mean(timings_ms)), 3),
    }
    for p in PERCENTILES:
        stats[f"p{p}_ms"] = round(float(np.percentile(timings_ms, p)), 3)
    return stats


def collect_environment() -> dict[str, str | int]:
    """Hardware + runtime metadata so results stay reproducible.

    A latency number without "what CPU" attached is just a number;
    embedding the environment makes the benchmark explainable months
    later when someone re-runs it.
    """
    return {
        "hostname": platform.node(),
        "platform": platform.platform(),
        "python_version": platform.python_version(),
        "onnxruntime_version": ort.__version__,
        "cpu_processor": platform.processor() or "unknown",
        "cpu_machine": platform.machine(),
    }


def print_table(rows: list[dict[str, float]]) -> None:
    """Pretty-print the results table to stderr.

    Format:
        Model                       Size      p50      p95      p99    Iter
        mobilenetv2_fp32.onnx       8.74     11.8     14.5     28.4     200
        mobilenetv2_int8.onnx       2.45      6.3      8.1     17.9     200
    """
    header = f"{'Model':<28} {'Size MB':>8} {'p50 ms':>8} {'p95 ms':>8} {'p99 ms':>8} {'Iter':>6}"
    print(header, file=sys.stderr)
    print("-" * len(header), file=sys.stderr)
    for r in rows:
        print(
            f"{r['model']:<28} "
            f"{r['size_mb']:>8.2f} "
            f"{r['p50_ms']:>8.2f} "
            f"{r['p95_ms']:>8.2f} "
            f"{r['p99_ms']:>8.2f} "
            f"{r['iterations']:>6}",
            file=sys.stderr,
        )


def main() -> int:
    args = parse_args()
    configure_logging(args.verbose)
    log = logging.getLogger(__name__)

    if not args.fp32_onnx.is_file():
        log.error("FP32 ONNX not found: %s", args.fp32_onnx)
        return 1

    input_array = make_input()
    rows: list[dict[str, float]] = []
    rows.append(benchmark_model(args.fp32_onnx, input_array))

    if args.int8_onnx is not None:
        if not args.int8_onnx.is_file():
            log.error("INT8 ONNX not found: %s", args.int8_onnx)
            return 1
        rows.append(benchmark_model(args.int8_onnx, input_array))

    print_table(rows)

    payload: dict[str, object] = {
        "environment": collect_environment(),
        "warmup_iterations": WARMUP_ITERATIONS,
        "measurement_iterations": MEASUREMENT_ITERATIONS,
        "results": rows,
    }
    args.output.write_text(json.dumps(payload, indent=2))
    log.info("Wrote results to %s", args.output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
