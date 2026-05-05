"""Export a pre-trained MobileNetV2 from torchvision to ONNX FP32.

What this script does (in plain language):

    1. Downloads MobileNetV2 weights pre-trained on ImageNet (1000 classes,
       1.4M images). torchvision caches the file under ~/.cache/torch/.
    2. Switches the network into 'evaluation' mode (turns off dropout
       and batch-norm running-stat updates -- those only matter during
       training).
    3. Asks torch.onnx.export() to trace the network with a dummy input
       and write the resulting graph to disk as .onnx.
    4. Validates the produced file with onnx.checker -- catches malformed
       graphs early instead of during inference.

The exported file is the foundation for everything downstream:
quantize_model.py compresses it; sanity_check.py verifies it produces
the same outputs as the original PyTorch model; benchmark.py measures
its latency. The C++ inference side (Phase 1) loads it identically.

Why MobileNetV2 specifically:
    - 3.5M parameters vs ResNet50's 25M. ~9 MB on disk in FP32.
    - Designed by Google for edge/mobile deployment -- depthwise
      separable convolutions reduce compute 8-9x versus standard
      convolutions for similar accuracy.
    - 72% top-1 accuracy on ImageNet -- a few percentage points below
      ResNet50's 76% but with one-fourth the inference cost.
    - Standard baseline; recognized by every ML interviewer.

Usage:

    python export_model.py --output ../../assets/models/mobilenetv2_fp32.onnx

The output path is relative to the script's working directory; the
caller is responsible for ensuring the parent folder exists (we do
mkdir -p as a courtesy).

Talking points for an interview:

    Q: "What does torch.onnx.export actually do?"
    A: It runs a forward pass with the dummy input you give it,
       recording every tensor operation as it happens. That recording
       becomes the ONNX graph. The output is a static computation
       graph -- branches that depended on input values are baked in
       at trace time. That's why dynamic axes (see DYNAMIC_AXES below)
       have to be declared explicitly: any dimension that should stay
       variable at inference time must be marked, otherwise the
       exporter freezes whatever value the dummy input had.

    Q: "Why opset 17?"
    A: ONNX has versioned operator sets. Newer opsets add ops and
       refine semantics; older runtimes may not know about them.
       Opset 17 is the highest level supported by ONNX Runtime 1.20
       (our target), and it covers everything MobileNetV2 needs.
       Pinning a known-supported opset keeps the model portable.

    Q: "Why eval() before export?"
    A: PyTorch modules behave differently in train() vs eval() mode.
       In train() dropout layers randomly zero activations and batch
       normalization updates running statistics from the current batch.
       Both are wrong for inference: dropout would inject randomness
       into predictions, BN would corrupt its averaged stats with a
       single dummy input. eval() switches them to deterministic
       inference behaviour.
"""
from __future__ import annotations

import argparse
import logging
import sys
from pathlib import Path

import onnx
import torch
from torchvision import models

# All knobs live as named constants so the script reads as
# configuration rather than a wall of magic numbers. Each is pinned
# to a value that has a specific reason -- documented inline.

# 224x224 is the resolution every torchvision ImageNet checkpoint was
# trained on. Feeding any other size to the FP32 model would silently
# work (convolutions are size-agnostic) but would produce nonsense
# logits because the fully-connected head expects exactly this many
# spatial features.
INPUT_HEIGHT: int = 224
INPUT_WIDTH: int = 224

# RGB; 3 channels is the only sensible value for an ImageNet model.
INPUT_CHANNELS: int = 3

# Single image at a time -- batch dimension is marked dynamic below
# so the same .onnx file accepts batches of any size at inference.
EXPORT_BATCH_SIZE: int = 1

# ONNX opset version. 17 is the highest fully supported by ONNX
# Runtime 1.20 (our target). Pinning forward keeps the model usable
# by older deployment runtimes if needed.
ONNX_OPSET_VERSION: int = 17

# Names exposed in the ONNX graph -- referenced by both quantize_model.py
# and the C++ inference side. Stable so consumers don't have to rediscover
# them per export.
INPUT_TENSOR_NAME: str = "input"
OUTPUT_TENSOR_NAME: str = "logits"

# Mark batch dimension dynamic so a single .onnx file serves both
# single-image latency benchmarks and batched throughput runs.
DYNAMIC_AXES: dict[str, dict[int, str]] = {
    INPUT_TENSOR_NAME:  {0: "batch"},
    OUTPUT_TENSOR_NAME: {0: "batch"},
}

# Default output path; callers can override via --output.
DEFAULT_OUTPUT_PATH: Path = Path("assets/models/mobilenetv2_fp32.onnx")


def configure_logging(verbose: bool) -> None:
    """Wire up a single root-level handler with a clean line format.

    Standard library logging prints `WARNING:root:...` by default;
    overriding the format keeps script output readable when piped to
    files or compared in CI.
    """
    level = logging.DEBUG if verbose else logging.INFO
    logging.basicConfig(
        level=level,
        format="%(asctime)s [%(levelname)5s] %(message)s",
        datefmt="%H:%M:%S",
        stream=sys.stderr,
    )


def parse_args() -> argparse.Namespace:
    """Command-line interface.

    Two knobs:
        --output: where to write the .onnx file. Default lands inside
            the project's assets/models/ directory which is .gitignored
            (binary artifacts don't belong in version control).
        --verbose: bumps log level to DEBUG; otherwise INFO is enough.
    """
    parser = argparse.ArgumentParser(
        description="Export pre-trained MobileNetV2 to ONNX FP32.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=DEFAULT_OUTPUT_PATH,
        help="Destination .onnx path (parent dir auto-created).",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Enable DEBUG-level logging.",
    )
    return parser.parse_args()


def load_pretrained_model() -> torch.nn.Module:
    """Fetch MobileNetV2 weights from torchvision's checkpoint zoo.

    `weights="DEFAULT"` resolves to whatever is currently the recommended
    checkpoint for this architecture (currently MobileNet_V2_Weights.
    IMAGENET1K_V2 for torchvision >= 0.13). Using the symbolic name
    rather than pinning a version means we automatically pick up any
    accuracy improvements in newer torchvision releases without code
    changes.

    The model is moved to eval() mode here, NOT by the caller. Forgetting
    that step is the most common bug I see in inference scripts; making
    it part of "loading" prevents the mistake.
    """
    log = logging.getLogger(__name__)
    log.info("Downloading MobileNetV2 ImageNet weights via torchvision...")
    model = models.mobilenet_v2(weights="DEFAULT")
    model.eval()
    log.info("Model loaded; switched to eval() mode.")
    return model


def export_to_onnx(model: torch.nn.Module, output_path: Path) -> None:
    """Trace the model with a dummy input and serialise to ONNX.

    The dummy input shape MUST match what the model expects -- here that's
    [batch=1, channels=3, height=224, width=224]. The trace records every
    op the forward pass touches; downstream consumers (ONNX Runtime,
    quantization tools) operate on that recording.

    `do_constant_folding=True` lets the exporter pre-compute parts of
    the graph that depend only on constants (like fixed-shape padding
    operations). The exported file is smaller and inference is faster
    because the runtime skips the folded ops.
    """
    log = logging.getLogger(__name__)

    # Dummy input -- contents don't matter (we just need shape + dtype),
    # but using zeros rather than randn keeps the export deterministic.
    dummy_input = torch.zeros(
        EXPORT_BATCH_SIZE, INPUT_CHANNELS, INPUT_HEIGHT, INPUT_WIDTH,
        dtype=torch.float32,
    )

    output_path.parent.mkdir(parents=True, exist_ok=True)
    log.info("Exporting to %s ...", output_path)

    torch.onnx.export(
        model,
        dummy_input,
        str(output_path),
        input_names=[INPUT_TENSOR_NAME],
        output_names=[OUTPUT_TENSOR_NAME],
        dynamic_axes=DYNAMIC_AXES,
        opset_version=ONNX_OPSET_VERSION,
        do_constant_folding=True,
    )

    size_mb = output_path.stat().st_size / (1024.0 * 1024.0)
    log.info("Wrote %s (%.2f MB)", output_path, size_mb)


def validate_onnx_model(output_path: Path) -> None:
    """Run ONNX's structural checker against the exported file.

    onnx.checker.check_model walks the graph and verifies that:
        - every operator has the inputs / outputs it expects
        - tensor shapes are consistent across consumers
        - referenced opsets exist and the ops in them are valid
    Catching this here (loud, at export time) is much friendlier than
    catching it in Phase 1 when ONNX Runtime emits a cryptic load error.
    """
    log = logging.getLogger(__name__)
    log.info("Running ONNX structural validation...")
    model_proto = onnx.load(str(output_path))
    onnx.checker.check_model(model_proto)
    log.info("Validation passed -- model graph is well-formed.")


def main() -> int:
    args = parse_args()
    configure_logging(args.verbose)
    log = logging.getLogger(__name__)

    try:
        model = load_pretrained_model()
        export_to_onnx(model, args.output)
        validate_onnx_model(args.output)
    except Exception as exc:
        log.error("Export failed: %s", exc, exc_info=True)
        return 1

    log.info("Done. Next step: quantize_model.py to produce the INT8 variant.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
