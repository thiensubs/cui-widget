// Internal image decoding for libcui-widget (not part of the public C ABI).
//
// The host contract allows only `data:` URLs — no network, no file paths — so an
// <img> carries its bytes inline as `data:image/png;base64,…`. This module turns
// that into raw RGBA pixels: base64-decode the payload, then decode the PNG/JPEG
// bytes with stb_image (vendored by CUI, header-only). The renderer uploads the
// pixels to a texture and draws them; this file never touches CUI.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cuiw_img {

// Decodes a `data:[<mediatype>;]base64,<payload>` URL into RGBA8 pixels (row-
// major, top-left origin, stride = width*4). Returns false if the URL isn't a
// base64 data URL or the payload isn't a decodable image. `out` is emptied on
// failure.
bool decode_data_url(const std::string& url, std::vector<uint8_t>& out,
                     int& width, int& height);

}  // namespace cuiw_img
