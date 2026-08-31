#include "core/Error.hpp"

#include <type_traits>

namespace ktxcmp {
namespace {

// If <expected> is ever unavailable on a target, this fires first and points at
// the alias in Error.hpp rather than at a wall of unrelated template errors.
static_assert(std::is_same_v<Result<int>, std::expected<int, Error>>);
static_assert(std::is_default_constructible_v<Error>);

}  // namespace

const char* categoryName(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::Io:                return "I/O";
        case ErrorCode::NotAContainer:     return "not a KTX file";
        case ErrorCode::UnsupportedFormat: return "unsupported format";
        case ErrorCode::Malformed:         return "malformed";
        case ErrorCode::Internal:          return "internal";
    }
    return "internal";
}

}  // namespace ktxcmp
