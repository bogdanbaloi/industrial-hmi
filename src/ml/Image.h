#pragma once

#include <cstdint>
#include <vector>

namespace app::ml {

/// Decoded raster image in row-major, channel-interleaved layout.
///
/// One unit = one pixel byte. For an RGB image of width W and height H the
/// `pixels` buffer has size `W * H * 3`, ordered:
///
///     R(0,0) G(0,0) B(0,0)  R(1,0) G(1,0) B(1,0)  ...  R(W-1,0) ...
///     R(0,1) G(0,1) B(0,1)  ...
///
/// This matches what `stb_image` produces and what every common decoder /
/// renderer agrees on, so it is the cheapest interchange format inside the
/// pipeline. Conversion to NCHW float tensors happens in the preprocessor;
/// nothing in this struct knows about ML.
///
/// SOLID:
///   * S -- the struct is pure data. No I/O, no decoding, no semantics.
///   * D -- consumers depend on this layout abstraction; producers
///         (file decoder, in-memory decoder, future camera capture)
///         each fill it in their own way.
struct Image {
    /// Image width in pixels. Strictly positive after a successful decode.
    int width = 0;

    /// Image height in pixels. Strictly positive after a successful decode.
    int height = 0;

    /// Channel count: 1 = grayscale, 3 = RGB, 4 = RGBA. The preprocessor
    /// will reject anything other than 3 -- ImageNet pre-trained models
    /// expect three colour channels.
    int channels = 0;

    /// Row-major, channel-interleaved pixel buffer. Size MUST equal
    /// `width * height * channels`; producers are responsible for
    /// upholding that invariant.
    std::vector<std::uint8_t> pixels;
};

}  // namespace app::ml
