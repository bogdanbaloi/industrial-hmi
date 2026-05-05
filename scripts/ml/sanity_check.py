"""Verify FP32 PyTorch == FP32 ONNX (and FP32 ONNX ~= INT8 ONNX).

What this script does:

    1. Loads MobileNetV2 from torchvision again (same way export_model
       does) -- this is our "ground truth" reference.
    2. Loads the exported FP32 .onnx through ONNX Runtime.
    3. Optionally loads the INT8 .onnx as well.
    4. Feeds the same preprocessed image through all three.
    5. Compares output tensors:
         - PyTorch  vs FP32 ONNX:  must be numerically near-identical
                                   (atol < 1e-4). Any drift here means
                                   the export is broken.
         - FP32 ONNX vs INT8 ONNX: allowed to differ; we compare top-1
                                   prediction (must match) and confidence
                                   (must be within 5 percentage points).

Why this matters:

    The most common deployment bug in vision pipelines is a preprocessing
    mismatch -- training used one normalization, inference used another,
    silently. The model still runs, accuracy quietly tanks. This script
    catches that in two ways:

        - We use the SAME preprocessing function for all three paths.
          If we got it wrong, all three are equally wrong, but PyTorch
          and ONNX FP32 will still agree (they share the operations),
          giving us false confidence. So the script is necessary but
          not sufficient -- you also need known-correct sample images
          with expected labels.

        - The script uses a real photo whose ImageNet class we know
          (e.g. "golden retriever"). If the predicted class isn't
          plausible, the preprocessing is probably wrong even if the
          numerical comparisons pass.

The "sample image" path is configurable so you can swap in your own
photos at QA time.

Talking points for an interview:

    Q: "Why does FP32 PyTorch not exactly match FP32 ONNX?"
    A: Two reasons. First, FP32 is not associative -- (a + b) + c can
       differ from a + (b + c) at the bit level when the values have
       different magnitudes. PyTorch and ONNX may schedule the same
       reduction (sum, mean) in different orders, producing values that
       differ in the last few mantissa bits. Second, constant folding
       during export pre-computes some operations at higher precision
       and stores the results -- those won't bit-match a runtime PyTorch
       computation. So we expect "close" (atol 1e-4 or 1e-5), not "exact".

    Q: "What's the right tolerance for INT8 vs FP32?"
    A: Don't compare pixel-level outputs -- they will differ. Compare
       what you care about. For classification, that's top-1 label match
       and confidence delta. For detection, that's IoU on bounding boxes
       and mAP delta on a held-out set. The principle: pick a metric
       that reflects the deployment-side decision the model drives, and
       set the tolerance based on what the business can tolerate.

    Q: "What if PyTorch and ONNX FP32 disagree?"
    A: That's a regression and the build should fail. Likely causes:
       wrong opset version (some ops have semantic changes between
       versions), unsupported PyTorch op that exporter approximated,
       or a custom layer not handled by torch.onnx.export. The fix
       is usually to bump opset, replace the offending op, or write
       a custom symbolic for it.
"""
from __future__ import annotations

import argparse
import logging
import sys
from pathlib import Path

import numpy as np
import onnxruntime as ort
import torch
from PIL import Image
from torchvision import models, transforms

# ImageNet normalization constants -- the values torchvision used when
# training MobileNetV2. Reproduced here verbatim because consumers must
# apply the EXACT same preprocessing or the model produces nonsense.
IMAGENET_MEAN: tuple[float, float, float] = (0.485, 0.456, 0.406)
IMAGENET_STD: tuple[float, float, float] = (0.229, 0.224, 0.225)
IMAGENET_INPUT_SIZE: int = 224
IMAGENET_RESIZE_SIZE: int = 256  # short-edge resize before center crop

# Tolerances for numerical comparisons.
#   FP32 PyTorch vs FP32 ONNX: should match to within float-rounding.
#   FP32 ONNX vs INT8 ONNX: only label and confidence matter.
FP32_ATOL: float = 1e-4
FP32_RTOL: float = 1e-4
INT8_CONFIDENCE_TOLERANCE: float = 0.05  # 5 percentage points


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
        description="Cross-validate PyTorch / FP32 ONNX / INT8 ONNX outputs.",
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
        help="Optional path to the INT8 .onnx; skipped if absent.",
    )
    parser.add_argument(
        "--image",
        type=Path,
        default=None,
        help=(
            "Image file to classify. If omitted, a synthetic noise tensor "
            "is used -- numerical checks still work but predicted classes "
            "are meaningless."
        ),
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Enable DEBUG-level logging.",
    )
    return parser.parse_args()


def build_preprocessor() -> transforms.Compose:
    """Reproduce torchvision's standard ImageNet inference pipeline.

    Resize the short edge to 256 px, center-crop 224x224, convert to
    a float tensor in [0, 1], then standardize with ImageNet mean/std.
    Matching this exactly is what makes inference outputs match training.
    """
    return transforms.Compose([
        transforms.Resize(IMAGENET_RESIZE_SIZE),
        transforms.CenterCrop(IMAGENET_INPUT_SIZE),
        transforms.ToTensor(),
        transforms.Normalize(mean=list(IMAGENET_MEAN), std=list(IMAGENET_STD)),
    ])


def prepare_input(image_path: Path | None) -> np.ndarray:
    """Produce a [1, 3, 224, 224] float32 numpy tensor.

    If the caller passed an image, decode + preprocess it. Otherwise
    fall back to a fixed-seed random tensor -- this lets the script
    still run as a smoke test on a fresh checkout where no sample
    photos have been added yet.
    """
    log = logging.getLogger(__name__)
    preprocess = build_preprocessor()

    if image_path is not None:
        log.info("Loading image: %s", image_path)
        with Image.open(image_path) as img:
            tensor = preprocess(img.convert("RGB"))
    else:
        log.info("No --image given; using deterministic random tensor.")
        rng = np.random.default_rng(seed=42)
        synthetic = rng.standard_normal(
            (3, IMAGENET_INPUT_SIZE, IMAGENET_INPUT_SIZE), dtype=np.float32)
        tensor = torch.from_numpy(synthetic)

    return tensor.unsqueeze(0).numpy().astype(np.float32)


def softmax(logits: np.ndarray) -> np.ndarray:
    """Numerically stable softmax over the last axis."""
    shifted = logits - np.max(logits, axis=-1, keepdims=True)
    exp = np.exp(shifted)
    return exp / np.sum(exp, axis=-1, keepdims=True)


def run_pytorch(input_array: np.ndarray) -> np.ndarray:
    """Forward pass with the original torchvision model in eval mode."""
    model = models.mobilenet_v2(weights="DEFAULT")
    model.eval()
    with torch.no_grad():
        out = model(torch.from_numpy(input_array))
    return out.numpy()


def run_onnx(model_path: Path, input_array: np.ndarray) -> np.ndarray:
    """Forward pass through an ONNX model via ONNX Runtime CPU EP."""
    session = ort.InferenceSession(
        str(model_path), providers=["CPUExecutionProvider"])
    input_name = session.get_inputs()[0].name
    return session.run(None, {input_name: input_array})[0]


def compare_fp32(
    pytorch_logits: np.ndarray,
    onnx_logits: np.ndarray,
) -> bool:
    """PyTorch and FP32 ONNX must agree to floating-point tolerance."""
    log = logging.getLogger(__name__)
    max_abs_diff = float(np.max(np.abs(pytorch_logits - onnx_logits)))
    log.info("FP32 PyTorch vs ONNX: max abs diff = %.6e", max_abs_diff)
    is_close = np.allclose(
        pytorch_logits, onnx_logits, atol=FP32_ATOL, rtol=FP32_RTOL)
    if not is_close:
        log.error(
            "FP32 mismatch above tolerance (atol=%g, rtol=%g) -- "
            "the export is suspect.", FP32_ATOL, FP32_RTOL)
    return is_close


def compare_quant(
    fp32_logits: np.ndarray,
    int8_logits: np.ndarray,
) -> bool:
    """INT8 must agree on top-1 class and stay within confidence tolerance."""
    log = logging.getLogger(__name__)

    fp32_probs = softmax(fp32_logits)[0]
    int8_probs = softmax(int8_logits)[0]

    fp32_top = int(np.argmax(fp32_probs))
    int8_top = int(np.argmax(int8_probs))

    fp32_conf = float(fp32_probs[fp32_top])
    int8_conf = float(int8_probs[int8_top])
    delta = abs(fp32_conf - int8_conf)

    log.info("FP32 top-1: class=%d  conf=%.4f", fp32_top, fp32_conf)
    log.info("INT8 top-1: class=%d  conf=%.4f", int8_top, int8_conf)
    log.info("Confidence delta: %.4f (tolerance %.2f)",
             delta, INT8_CONFIDENCE_TOLERANCE)

    label_match = fp32_top == int8_top
    conf_within = delta <= INT8_CONFIDENCE_TOLERANCE
    if not label_match:
        log.error("INT8 chose a different top-1 class -- quantization broke "
                  "the model on this input.")
    if not conf_within:
        log.warning(
            "INT8 confidence drift > tolerance; investigate with a "
            "validation set if this matters for the use case.")
    return label_match and conf_within


def main() -> int:
    args = parse_args()
    configure_logging(args.verbose)
    log = logging.getLogger(__name__)

    if not args.fp32_onnx.is_file():
        log.error("FP32 ONNX not found: %s", args.fp32_onnx)
        return 1

    input_array = prepare_input(args.image)

    log.info("Running PyTorch reference...")
    pytorch_logits = run_pytorch(input_array)

    log.info("Running FP32 ONNX...")
    fp32_logits = run_onnx(args.fp32_onnx, input_array)

    fp32_ok = compare_fp32(pytorch_logits, fp32_logits)
    if not fp32_ok:
        return 2

    if args.int8_onnx is not None:
        if not args.int8_onnx.is_file():
            log.error("INT8 ONNX not found: %s", args.int8_onnx)
            return 1
        log.info("Running INT8 ONNX...")
        int8_logits = run_onnx(args.int8_onnx, input_array)
        int8_ok = compare_quant(fp32_logits, int8_logits)
        if not int8_ok:
            return 3
    else:
        log.info("No --int8-onnx given; skipping quantization comparison.")

    log.info("All checks passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
