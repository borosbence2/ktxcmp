#pragma once

// Error type for the non-UI layers. CLAUDE.md: std::expected-style returns, no
// exceptions across layer boundaries, and no libktx error code allowed to leak
// upward.

#include <expected>
#include <string>
#include <utility>

namespace ktxcmp {

enum class ErrorCode {
    Io,                 // missing, unreadable, or truncated file
    NotAContainer,      // magic matches neither KTX1 nor KTX2
    UnsupportedFormat,  // valid container, format outside the supported subset
    Malformed,          // header or level index is self-inconsistent
    Internal,           // libktx returned something we did not anticipate
};

struct Error {
    ErrorCode code = ErrorCode::Internal;
    std::string message;  // shown to the user, so name the format or the file
};

// The single seam for the error-return mechanism. If a target's standard
// library turns out to lack <expected>, this alias changes and no signature in
// any layer does.
template <typename T>
using Result = std::expected<T, Error>;

[[nodiscard]] inline std::unexpected<Error> fail(ErrorCode code, std::string message) {
    return std::unexpected(Error{code, std::move(message)});
}

// Short category label, for prefixing a message in the UI.
[[nodiscard]] const char* categoryName(ErrorCode code) noexcept;

}  // namespace ktxcmp
