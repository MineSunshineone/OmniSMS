// 可移植 SHA-256 / HMAC-SHA256 / Base64：钉钉、飞书推送签名用。
// core 自带实现，端口不必再桥接 mbedTLS/OpenSSL；每次调用数据量为几十字节，
// 性能无关紧要，正确性由 RFC 测试向量单测保证。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace omnisms {

// out 需 32 字节
void sha256(const uint8_t* data, size_t len, uint8_t out[32]);
void hmac_sha256(const uint8_t* key, size_t key_len, const uint8_t* data, size_t data_len,
                 uint8_t out[32]);

std::string base64_encode(const uint8_t* data, size_t len);
// Strict RFC 4648 decoder. Rejects invalid alphabet, misplaced padding and non-canonical tails.
bool base64_decode(const std::string& encoded, std::string& out);

// HMAC-SHA256 后 Base64——与固件 mbedTLS 版本同签名语义
std::string hmac_sha256_base64(const std::string& data, const std::string& key);

}  // namespace omnisms
