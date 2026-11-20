#include "cui_img.h"

// stb_image is vendored by CUI as a single header. Compile the implementation
// once, here, with static functions so it never clashes with libcui's own copy.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace cuiw_img {

namespace {

// Standard base64 alphabet; '=' is padding. Returns -1 for any other byte.
int b64val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

// Decodes the base64 payload (padding and embedded whitespace tolerated) into
// raw bytes. Returns false if a character outside the alphabet appears.
bool base64_decode(const std::string& in, std::vector<uint8_t>& out) {
    out.clear();
    int val = 0, bits = 0;
    for (char ch : in) {
        if (ch == '=') break;                       // padding: payload is done
        if (std::isspace((unsigned char)ch)) continue;
        int v = b64val(ch);
        if (v < 0) return false;
        val = (val << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((uint8_t)((val >> bits) & 0xFF));
        }
    }
    return true;
}

}  // namespace

bool decode_data_url(const std::string& url, std::vector<uint8_t>& out,
                     int& width, int& height) {
    out.clear();
    width = 0; height = 0;
    // Only base64 data URLs carry inline bytes; url()/file paths are not allowed
    // by the host contract, so anything else is skipped.
    const std::string mark = "base64,";
    size_t comma = url.find(mark);
    if (url.rfind("data:", 0) != 0 || comma == std::string::npos) return false;

    std::vector<uint8_t> bytes;
    if (!base64_decode(url.substr(comma + mark.size()), bytes)) return false;
    if (bytes.empty()) return false;

    // stbi_load_from_memory always returns 4 channels (RGBA) when asked to.
    int w = 0, h = 0, n = 0;
    uint8_t* px = stbi_load_from_memory(bytes.data(), (int)bytes.size(),
                                        &w, &h, &n, 4);
    if (!px || w <= 0 || h <= 0) {
        stbi_image_free(px);
        return false;
    }
    out.assign(px, px + (size_t)w * h * 4);
    stbi_image_free(px);
    width = w; height = h;
    return true;
}

}  // namespace cuiw_img
