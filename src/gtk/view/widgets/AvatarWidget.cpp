#include "src/gtk/view/widgets/AvatarWidget.h"

#include <giomm/memoryinputstream.h>
#include <gdkmm/pixbuf.h>
#include <pangomm/cairofontmap.h>
#include <pangomm/fontdescription.h>
#include <pangomm/layout.h>
#include <pango/pangocairo.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <stdexcept>

namespace app::view {

namespace {

// Visual constants. Named so the clang-tidy magic-numbers lint stays
// clean and so a future visual tweak (slightly larger radius, bolder
// initials font) lands in one place.
constexpr double kRgbScale            = 255.0;  // 0..255 -> 0..1 for Cairo
constexpr double kCornerRadiusRatio   = 0.20;   // rounded-square corners
constexpr double kFontSizeRatio       = 0.45;   // initials height as
                                                // fraction of the tile
constexpr double kTextAlphaWhite      = 1.0;

// Pre-session placeholder colour -- mid-grey, distinguishable from
// the user-derived palette colours so it visibly reads as "no user
// yet" during the brief window between widget construction and the
// first setUser() / clear() call.
constexpr std::uint8_t kEmptyRgbChannel = 0x6B;
constexpr app::auth::AvatarColor kEmptyColor{
    kEmptyRgbChannel, kEmptyRgbChannel, kEmptyRgbChannel};

}  // namespace

AvatarWidget::AvatarWidget(int sizePx) {
    color_ = kEmptyColor;
    set_content_width(sizePx);
    set_content_height(sizePx);
    set_draw_func(sigc::mem_fun(*this, &AvatarWidget::onDraw));
}

void AvatarWidget::setUser(
        const app::auth::User&                  user,
        const std::optional<app::auth::Avatar>& avatar) {
    initials_ = app::auth::computeInitials(user.displayName, user.username);
    color_    = app::auth::pickPaletteColor(user.username);

    if (avatar.has_value() && !avatar->bytes.empty()) {
        pixbuf_ = decodeAvatar(avatar->bytes);
    } else {
        pixbuf_.reset();
    }
    queue_draw();
}

void AvatarWidget::clear() {
    initials_ = "?";
    color_    = kEmptyColor;
    pixbuf_.reset();
    queue_draw();
}

Glib::RefPtr<Gdk::Pixbuf>
AvatarWidget::decodeAvatar(const std::vector<std::uint8_t>& bytes) {
    if (bytes.empty()) return {};
    try {
        // Wrap the raw blob in a GBytes (zero copy -- GBytes references
        // the caller-owned buffer until the RefPtr drops). Feed the
        // bytes into Gio::MemoryInputStream so Gdk::Pixbuf's stream
        // loader can sniff format from the magic bytes (PNG / JPEG).
        auto gbytes = Glib::Bytes::create(bytes.data(), bytes.size());
        auto stream = Gio::MemoryInputStream::create();
        stream->add_bytes(gbytes);
        return Gdk::Pixbuf::create_from_stream(stream);
    } catch (const Glib::Error&) {
        // Corrupted / unsupported format -- fall back to initials.
        return {};
    } catch (const std::exception&) {
        return {};
    }
}

void AvatarWidget::onDraw(const Cairo::RefPtr<Cairo::Context>& cr,
                          int width, int height) {
    const auto w = static_cast<double>(width);
    const auto h = static_cast<double>(height);

    // Clip the entire draw to a rounded-square path so both the
    // pixbuf path and the initials path get the same outer shape.
    // (cairo doesn't have a "rounded rect" primitive; we hand-build
    // the path from four quarter-circles + four lines.)
    const double radius = std::min(w, h) * kCornerRadiusRatio;
    cr->save();
    cr->begin_new_path();
    constexpr double kHalfPi  = std::numbers::pi / 2.0;
    constexpr double kFullPi  = std::numbers::pi;
    cr->arc(radius,     radius,     radius, kFullPi,        kFullPi + kHalfPi);
    cr->arc(w - radius, radius,     radius, kFullPi + kHalfPi, 0.0);
    cr->arc(w - radius, h - radius, radius, 0.0,            kHalfPi);
    cr->arc(radius,     h - radius, radius, kHalfPi,        kFullPi);
    cr->close_path();
    cr->clip();

    if (pixbuf_) {
        // Scale the pixbuf to fill the tile -- avatars come in many
        // resolutions but the badge is fixed size. Gdk::Cairo helper
        // does the matrix work for us via gdk_cairo_set_source_pixbuf
        // after a scale_simple, which is cheaper than a full transform
        // for a one-shot draw.
        const int pw = pixbuf_->get_width();
        const int ph = pixbuf_->get_height();
        if (pw > 0 && ph > 0) {
            const double sx = w / static_cast<double>(pw);
            const double sy = h / static_cast<double>(ph);
            cr->scale(sx, sy);
            Gdk::Cairo::set_source_pixbuf(cr, pixbuf_, 0.0, 0.0);
            cr->paint();
        }
        cr->restore();
        return;
    }

    // Initials path: fill with the palette colour, then draw the
    // initials centred in white using a Pango layout (handles font
    // metrics + multibyte safely, future-proof for non-ASCII names).
    cr->set_source_rgb(static_cast<double>(color_.r) / kRgbScale,
                       static_cast<double>(color_.g) / kRgbScale,
                       static_cast<double>(color_.b) / kRgbScale);
    cr->paint();

    auto layout = Pango::Layout::create(cr);
    Pango::FontDescription desc("Sans Bold");
    // Pango sizes are in fractional points; the multiplication keeps
    // the integer cast clean.
    desc.set_size(static_cast<int>(h * kFontSizeRatio * PANGO_SCALE));
    layout->set_font_description(desc);
    layout->set_text(initials_);

    int textWidth  = 0;
    int textHeight = 0;
    layout->get_pixel_size(textWidth, textHeight);
    const double tx = (w - static_cast<double>(textWidth))  / 2.0;
    const double ty = (h - static_cast<double>(textHeight)) / 2.0;

    cr->set_source_rgba(1.0, 1.0, 1.0, kTextAlphaWhite);
    cr->move_to(tx, ty);
    layout->show_in_cairo_context(cr);

    cr->restore();
}

}  // namespace app::view
