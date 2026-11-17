// Minimal MD5 (RFC 1321). Godot's pack index stores an MD5 per file.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace mpml {
std::array<uint8_t, 16> md5(const uint8_t* data, size_t len);
}
