#include "png_writer.h"

#include <cstdio>
#include <cstring>

namespace wfsim {
namespace {

uint32_t crc_table[256];
bool crc_table_ready = false;

void make_crc_table() {
  for (uint32_t n = 0; n < 256; n++) {
    uint32_t c = n;
    for (int k = 0; k < 8; k++)
      c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    crc_table[n] = c;
  }
  crc_table_ready = true;
}

uint32_t crc32_of(const uint8_t *buf, size_t len, uint32_t crc = 0xFFFFFFFFu) {
  if (!crc_table_ready)
    make_crc_table();
  for (size_t n = 0; n < len; n++)
    crc = crc_table[(crc ^ buf[n]) & 0xFF] ^ (crc >> 8);
  return crc;
}

void push_be32(std::vector<uint8_t> &out, uint32_t v) {
  out.push_back(v >> 24);
  out.push_back((v >> 16) & 0xFF);
  out.push_back((v >> 8) & 0xFF);
  out.push_back(v & 0xFF);
}

void push_chunk(std::vector<uint8_t> &out, const char type[4], const std::vector<uint8_t> &data) {
  push_be32(out, static_cast<uint32_t>(data.size()));
  std::vector<uint8_t> typed(type, type + 4);
  typed.insert(typed.end(), data.begin(), data.end());
  out.insert(out.end(), typed.begin(), typed.end());
  push_be32(out, crc32_of(typed.data(), typed.size()) ^ 0xFFFFFFFFu);
}

}  // namespace

bool write_png(const std::string &path, int width, int height, const std::vector<uint8_t> &rgb) {
  if (width <= 0 || height <= 0)
    return false;
  if (rgb.size() != static_cast<size_t>(width) * height * 3)
    return false;

  // Raw scanlines with a zero filter byte in front of each.
  std::vector<uint8_t> raw;
  raw.reserve(static_cast<size_t>(height) * (1 + static_cast<size_t>(width) * 3));
  for (int y = 0; y < height; y++) {
    raw.push_back(0);
    const uint8_t *row = rgb.data() + static_cast<size_t>(y) * width * 3;
    raw.insert(raw.end(), row, row + static_cast<size_t>(width) * 3);
  }

  // zlib stream with stored deflate blocks.
  std::vector<uint8_t> z;
  z.push_back(0x78);
  z.push_back(0x01);
  size_t pos = 0;
  while (pos < raw.size()) {
    const size_t block = std::min<size_t>(65535, raw.size() - pos);
    const bool final_block = (pos + block) >= raw.size();
    z.push_back(final_block ? 1 : 0);
    z.push_back(block & 0xFF);
    z.push_back((block >> 8) & 0xFF);
    z.push_back(~block & 0xFF);
    z.push_back((~block >> 8) & 0xFF);
    z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + block);
    pos += block;
  }
  uint32_t a = 1, b = 0;
  for (uint8_t byte : raw) {
    a = (a + byte) % 65521;
    b = (b + a) % 65521;
  }
  push_be32(z, (b << 16) | a);

  std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  std::vector<uint8_t> ihdr;
  push_be32(ihdr, static_cast<uint32_t>(width));
  push_be32(ihdr, static_cast<uint32_t>(height));
  ihdr.push_back(8);  // bit depth
  ihdr.push_back(2);  // colour type: truecolour
  ihdr.push_back(0);
  ihdr.push_back(0);
  ihdr.push_back(0);
  push_chunk(out, "IHDR", ihdr);
  push_chunk(out, "IDAT", z);
  push_chunk(out, "IEND", {});

  FILE *f = fopen(path.c_str(), "wb");
  if (f == nullptr)
    return false;
  const size_t written = fwrite(out.data(), 1, out.size(), f);
  fclose(f);
  return written == out.size();
}

}  // namespace wfsim
