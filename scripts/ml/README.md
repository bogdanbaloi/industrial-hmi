# ML Pipeline (Phase 0)

Python-only pipeline that prepares an ONNX model the C++ inference side
(Phase 1) consumes. Nothing in this folder runs at production time --
it's the build chain that produces the `.onnx` artifacts.

## What gets produced

```
assets/models/
  mobilenetv2_fp32.onnx     ~9 MB   FP32 weights, the export reference
  mobilenetv2_int8.onnx     ~2.5 MB INT8 dynamic quantization, deployment target
benchmark-results.json              Latency stats from the last benchmark run
```

The `.onnx` files are .gitignored -- binary artifacts don't belong in
version control, and they regenerate from the scripts in under a minute.

## Setup

```bash
cd scripts/ml
python -m venv .venv
source .venv/bin/activate          # Linux / WSL / Git Bash
# or:
.venv\Scripts\activate.bat         # Windows cmd.exe / PowerShell

pip install -r requirements.txt
```

The first install pulls ~2 GB (PyTorch wheels are large). The venv keeps
those out of the system Python.

## Running the pipeline

```bash
# 1. Download MobileNetV2 weights and export FP32 ONNX.
python export_model.py \
    --output ../../assets/models/mobilenetv2_fp32.onnx

# 2. Compress to INT8 dynamic quantization.
python quantize_model.py \
    --input  ../../assets/models/mobilenetv2_fp32.onnx \
    --output ../../assets/models/mobilenetv2_int8.onnx

# 3. Verify FP32 ONNX matches PyTorch reference, INT8 matches FP32 (top-1 + confidence).
python sanity_check.py \
    --fp32-onnx ../../assets/models/mobilenetv2_fp32.onnx \
    --int8-onnx ../../assets/models/mobilenetv2_int8.onnx
# (optionally pass --image path/to/photo.jpg for a known-class spot-check)

# 4. Benchmark latency. 200 iterations + 20 warmup; takes ~10 seconds.
python benchmark.py \
    --fp32-onnx ../../assets/models/mobilenetv2_fp32.onnx \
    --int8-onnx ../../assets/models/mobilenetv2_int8.onnx \
    --output benchmark-results.json
```

Expected output from step 4 (example, your numbers will vary by hardware):

```
Model                       Size MB   p50 ms   p95 ms   p99 ms   Iter
mobilenetv2_fp32.onnx          8.74    11.80    14.50    28.40    200
mobilenetv2_int8.onnx          2.45     6.30     8.10    17.90    200
```

## What each script does

| Script | Purpose |
|---|---|
| `export_model.py` | Downloads MobileNetV2 from torchvision, traces the forward pass, writes ONNX FP32. |
| `quantize_model.py` | Reads FP32 ONNX, rewrites Linear / MatMul weights to INT8, writes the quantized ONNX. |
| `sanity_check.py` | Cross-validates: PyTorch == FP32 ONNX (numerical), INT8 ONNX top-1 match + confidence delta. |
| `benchmark.py` | Warmup + measure inference latency; reports p50/p90/p95/p99 + writes JSON. |

Each script is self-documenting -- the docstring at the top explains
why it exists and the implementation explains how.

## Why the choices we made

The architectural decisions are documented inline in each script's
docstring. Brief summary:

- **MobileNetV2** -- 3.5M params, designed for edge deployment. Recognized
  baseline for "industrial CNN classification" stories.
- **ONNX Runtime** -- 50 MB deployment vs 2 GB libtorch; works on CPU,
  GPU, and embedded NPU through the same C++ API.
- **Dynamic INT8 quantization** -- one function call, no retraining
  needed, ~75 % size reduction with sub-1 % accuracy drop on ImageNet.
- **CPU benchmarks** -- industrial PCs typically lack GPUs; reporting
  GPU numbers would mislead about deployment-side performance.

## What this is NOT

This pipeline does **not** train a model. The weights are pre-trained on
ImageNet's 1000 classes (cats, dogs, household objects, vehicles). The
goal of Phase 0 is to demonstrate the **deployment infrastructure**, not
to solve domain-specific quality inspection -- that requires a domain
dataset (e.g. MVTec AD) and is the natural next phase.

## Next phases

1. **Phase 1 -- C++ inference**: ONNX Runtime C++ API wrapper, integrated
   into the existing presenter pipeline behind a new `ImageClassifier`
   abstract interface.
2. **Phase 2 -- HMI integration**: New `QualityInspectionPage` with file
   picker, image preview, and result widget.
3. **Phase 3 -- Domain fine-tuning**: Replace generic ImageNet weights
   with a model fine-tuned on MVTec AD (defect detection dataset). The
   pipeline above is unchanged -- only the training step is added.
