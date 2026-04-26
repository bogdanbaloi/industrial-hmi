#pragma once

#include "src/model/Product.h"

#include <istream>
#include <ostream>
#include <string>
#include <vector>

namespace app::integration {

/// Format-agnostic serializer for domain objects.
///
/// SOLID:
///   * S -- one job per concrete: format conversion, nothing else.
///     No file I/O, no network, no localisation policy.
///   * O -- adding a new format (XML, MessagePack, Parquet) is one
///     subclass; existing call sites stay untouched.
///   * L -- any concrete is a drop-in replacement: callers depend on
///     the abstract Serializer reference.
///   * I -- the interface stays narrow on purpose. Network protocols
///     do NOT implement Serializer; they have their own
///     `IntegrationBackend` interface (introduced when TCP/MQTT
///     backends land).
///   * D -- View / Presenter layers depend on Serializer&, never on a
///     concrete CsvSerializer or JsonSerializer.
///
/// Streams (not file paths) are the I/O primitive so the same code
/// works for files, in-memory test buffers, and TCP socket streambufs.
class Serializer {
public:
    virtual ~Serializer() = default;

    Serializer(const Serializer&) = delete;
    Serializer& operator=(const Serializer&) = delete;
    Serializer(Serializer&&) = delete;
    Serializer& operator=(Serializer&&) = delete;

    /// Write products to `out`. Concrete formats decide whether headers /
    /// metadata / framing accompany the records (CSV writes a header row;
    /// JSON wraps in an array; MessagePack would write a length prefix).
    virtual void writeProducts(std::ostream& out,
                               const std::vector<model::Product>& products) = 0;

    /// Read products from `in`. Returns an empty vector on a stream that
    /// holds only the format's metadata (e.g. CSV with header but no
    /// rows, JSON `[]`). Throws `std::runtime_error` on malformed input.
    [[nodiscard]] virtual std::vector<model::Product>
        readProducts(std::istream& in) = 0;

    /// Display-friendly name (e.g. "CSV", "JSON"). Used in UI dropdowns
    /// and log messages. Stable -- do not localise.
    [[nodiscard]] virtual std::string formatName() const = 0;

    /// File extension WITHOUT a leading dot (e.g. "csv", "json").
    /// Used by file save dialogs to suggest a default name.
    [[nodiscard]] virtual std::string fileExtension() const = 0;

    /// IANA media type (e.g. "text/csv", "application/json"). Used by
    /// future TCP / MQTT backends as the Content-Type header.
    [[nodiscard]] virtual std::string mimeType() const = 0;

protected:
    Serializer() = default;
};

}  // namespace app::integration
