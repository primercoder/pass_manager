#pragma once

#include <string>
#include <vector>
#include <cstdint>

class Crypto {
public:
    static std::vector<uint8_t> deriveKey(const std::string& keyFilePath);

    static bool generateKeyFile(const std::string& path);

    static std::string encrypt(const std::vector<uint8_t>& key,
                               const std::string& plaintext);

    static std::string decrypt(const std::vector<uint8_t>& key,
                               const std::string& encoded);

private:
    static std::string base64Encode(const std::vector<uint8_t>& data);
    static std::vector<uint8_t> base64Decode(const std::string& encoded);

    static constexpr int KEY_SIZE = 32;
    static constexpr int IV_SIZE = 12;
    static constexpr int TAG_SIZE = 16;
};
