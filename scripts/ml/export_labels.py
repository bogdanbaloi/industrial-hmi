"""Export the canonical 1000 ImageNet class names to a flat text file.

What this script does:

    1. Downloads the canonical PyTorch hub `imagenet_classes.txt` --
       the same list the torchvision reference implementations use to
       resolve a 0-999 prediction index to a human-readable label.
    2. Writes it as `assets/models/imagenet_labels.txt`, one label per
       line, UTF-8.

The C++ inference side reads this file via `app::ml::ImageNetLabels`.

Why a separate one-shot script and not a CMake fetch step:

    Labels are a stable artifact: the 1000-class taxonomy has not changed
    since 2012. Vendoring the file in the repo means the C++ test suite,
    the CI integration job, and a developer running the binary off a
    fresh clone all see the same labels without a network round-trip.
    The script exists so the "where do these come from?" question has
    an executable answer rather than a hand-wavy comment.

Usage:

    python export_labels.py \\
        --output ../../assets/models/imagenet_labels.txt
"""
from __future__ import annotations

import argparse
import logging
import sys
import urllib.request
from pathlib import Path

# Canonical labels file maintained by the PyTorch team. Stable URL,
# pinned by branch (master == live); regenerating produces the same
# bytes barring upstream typo fixes that we WANT to absorb.
LABELS_URL: str = (
    "https://raw.githubusercontent.com/pytorch/hub/master/"
    "imagenet_classes.txt"
)

DEFAULT_OUTPUT_PATH: Path = Path("assets/models/imagenet_labels.txt")


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
        description="Export the canonical 1000 ImageNet class labels.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=DEFAULT_OUTPUT_PATH,
        help="Destination text file (one label per line).",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Enable DEBUG-level logging.",
    )
    return parser.parse_args()


def fetch_labels() -> list[str]:
    """Download the canonical labels list and return one entry per line.

    Strips trailing whitespace + blank lines so the on-disk file is
    exactly 1000 lines, in the order the model emits its logits.
    """
    log = logging.getLogger(__name__)
    log.info("Fetching %s", LABELS_URL)
    with urllib.request.urlopen(LABELS_URL, timeout=30) as response:
        raw = response.read().decode("utf-8")
    labels = [line.strip() for line in raw.splitlines() if line.strip()]
    if len(labels) != 1000:
        raise RuntimeError(
            f"Expected 1000 labels, got {len(labels)}. "
            "Upstream file format may have changed."
        )
    return labels


def write_labels(labels: list[str], output_path: Path) -> None:
    log = logging.getLogger(__name__)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("w", encoding="utf-8") as out:
        for label in labels:
            out.write(label + "\n")
    log.info("Wrote %d labels to %s", len(labels), output_path)


def main() -> int:
    args = parse_args()
    configure_logging(args.verbose)
    log = logging.getLogger(__name__)

    try:
        labels = fetch_labels()
        write_labels(labels, args.output)
    except Exception as exc:
        log.error("Export failed: %s", exc, exc_info=True)
        return 1

    log.info("Done.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
