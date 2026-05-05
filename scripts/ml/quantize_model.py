"""Convert an FP32 ONNX model to dynamic INT8 quantization.

What this script does:

    1. Reads the FP32 .onnx produced by export_model.py.
    2. Calls onnxruntime.quantization.quantize_dynamic() which rewrites
       the graph: every Linear / MatMul layer's weight tensor goes
       FP32 (4 bytes) -> INT8 (1 byte). Activations stay FP32 but are
       quantized on-the-fly per inference.
    3. Writes the result as a separate .onnx file alongside the FP32
       original so we can A/B benchmark them.

What "dynamic quantization" actually does (in plain language):

    Weights live in the model file -- they're known at quantization
    time, so we can pick the optimal INT8 scale/zero-point factors
    for each tensor up-front and store them.

    Activations only exist during inference -- different inputs produce
    different activation distributions. Dynamic quantization handles
    them per-batch: at the start of each Linear layer, the runtime
    computes a quick min/max on the activation tensor, picks a
    quantization scale, casts to INT8, runs the integer matmul against
    the pre-quantized weights, then dequantizes the output back to
    FP32 for the next layer.

    The downsides:
        - Per-inference scale computation has a small overhead
          (mostly hidden by the much faster INT8 matmul).
        - Convolutional layers don't benefit -- ONNX Runtime's dynamic
          quantization currently only rewrites Linear / MatMul ops.
          For full Conv coverage you'd need static (calibration-based)
          quantization, which is Phase 4+ work.

    Why we accept those downsides:
        - Zero retraining required. One function call.
        - No calibration dataset needed.
        - Accuracy drop on MobileNetV2 dynamic quant is typically <1%
          on ImageNet top-1, well within the noise of model-to-model
          variation.
        - The point of Phase 0 is to demonstrate the pipeline -- the
          static-quantization story slots in cleanly later because
          the C++ inference side doesn't change at all (ONNX Runtime
          handles both quant types transparently).

Usage:

    python quantize_model.py \\
        --input  ../../assets/models/mobilenetv2_fp32.onnx \\
        --output ../../assets/models/mobilenetv2_int8.onnx
"""
from __future__ import annotations

import argparse
import logging
import sys
from pathlib import Path

from onnxruntime.quantization import QuantType, quantize_dynamic

# Quantization "weight type" -- the dtype used to store the rewritten
# weights. QInt8 is signed INT8, range [-128, 127]; QUInt8 is unsigned,
# range [0, 255]. ONNX Runtime documentation recommends QUInt8 for
# CPUs with VNNI instructions (Intel Cascade Lake and later, AMD Zen2+).
# We default to QUInt8 -- if the deployment target is older hardware,
# swap to QInt8.
WEIGHT_DTYPE: QuantType = QuantType.QUInt8


def configure_logging(verbose: bool) -> None:
    """Same handler shape as export_model.py for consistent output."""
    level = logging.DEBUG if verbose else logging.INFO
    logging.basicConfig(
        level=level,
        format="%(asctime)s [%(levelname)5s] %(message)s",
        datefmt="%H:%M:%S",
        stream=sys.stderr,
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Quantize an FP32 ONNX model to dynamic INT8.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "--input",
        type=Path,
        required=True,
        help="Path to the FP32 .onnx produced by export_model.py.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        required=True,
        help="Destination path for the INT8 .onnx (parent dir auto-created).",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Enable DEBUG-level logging.",
    )
    return parser.parse_args()


def quantize(input_path: Path, output_path: Path) -> None:
    """Run quantize_dynamic and report the size delta.

    The size delta is the most concrete demo of the win: a 9 MB FP32
    file becomes a ~2.5 MB INT8 file. That's the headline number
    benchmark.py later confirms with latency measurements.
    """
    log = logging.getLogger(__name__)

    if not input_path.is_file():
        raise FileNotFoundError(f"FP32 input not found: {input_path}")

    output_path.parent.mkdir(parents=True, exist_ok=True)

    log.info("Quantizing %s -> %s (weight type=%s)",
             input_path, output_path, WEIGHT_DTYPE.name)
    quantize_dynamic(
        model_input=str(input_path),
        model_output=str(output_path),
        weight_type=WEIGHT_DTYPE,
    )

    fp32_mb = input_path.stat().st_size / (1024.0 * 1024.0)
    int8_mb = output_path.stat().st_size / (1024.0 * 1024.0)
    reduction_pct = (1.0 - int8_mb / fp32_mb) * 100.0

    log.info("FP32: %.2f MB", fp32_mb)
    log.info("INT8: %.2f MB", int8_mb)
    log.info("Size reduction: %.1f%%", reduction_pct)


def main() -> int:
    args = parse_args()
    configure_logging(args.verbose)
    log = logging.getLogger(__name__)

    try:
        quantize(args.input, args.output)
    except Exception as exc:
        log.error("Quantization failed: %s", exc, exc_info=True)
        return 1

    log.info("Done. Next step: sanity_check.py to confirm INT8 outputs match FP32.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
