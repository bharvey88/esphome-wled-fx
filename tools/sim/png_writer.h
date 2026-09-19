#pragma once

// Minimal PNG writer: 8 bit RGB, one IDAT of uncompressed deflate blocks. No
// third-party dependency, which keeps the simulator buildable with nothing but a
// C++17 compiler.

#include <cstdint>
#include <string>
#include <vector>

namespace wfsim {

bool write_png(const std::string &path, int width, int height, const std::vector<uint8_t> &rgb);

}  // namespace wfsim
