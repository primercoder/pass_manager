#include "crypto.h"

#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/err.h>

#include <fstream>
#include <stdexcept>
#include <cstring>
#include <sys/stat.h>

std::vector<uint8_t> Crypto::deriveKey(const std::string& keyFilePath) {
    std::ifstream file(keyFilePath, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open key file: " + keyFilePath);
    }

    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());

    if (data.empty()) {
        throw std::runtime_error("Key file is empty: " + keyFilePath);
    }

    uint8_t hash[SHA256_DIGEST_LENGTH];
    SHA256(data.data(), data.size(), hash);

    return std::vector<uint8_t>(hash, hash + KEY_SIZE);
}

bool Crypto::generateKeyFile(const std::string& path) {
    std::vector<uint8_t> key(64);

    if (RAND_bytes(key.data(), static_cast<int>(key.size())) != 1) {
        return false;
    }

    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    file.write(reinterpret_cast<const char*>(key.data()),
               static_cast<std::streamsize>(key.size()));

    if (!file.good()) {
        return false;
    }

    chmod(path.c_str(), S_IRUSR | S_IWUSR);

    return true;
}

std::string Crypto::encrypt(const std::vector<uint8_t>& key,
                            const std::string& plaintext) {
    if (key.size() != static_cast<size_t>(KEY_SIZE)) {
        throw std::runtime_error("Invalid key size for encryption");
    }

    std::vector<uint8_t> iv(IV_SIZE);
    if (RAND_bytes(iv.data(), IV_SIZE) != 1) {
        throw std::runtime_error("Failed to generate IV");
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Failed to create cipher context");
    }

    int ret = 0;

    ret = EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
    if (ret != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_EncryptInit_ex failed");
    }

    ret = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, IV_SIZE, nullptr);
    if (ret != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_CTRL_GCM_SET_IVLEN failed");
    }

    ret = EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), iv.data());
    if (ret != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_EncryptInit_ex with key/IV failed");
    }

    std::vector<uint8_t> ciphertext(plaintext.size() + EVP_MAX_BLOCK_LENGTH);
    int outlen = 0;

    ret = EVP_EncryptUpdate(ctx, ciphertext.data(), &outlen,
                            reinterpret_cast<const uint8_t*>(plaintext.data()),
                            static_cast<int>(plaintext.size()));
    if (ret != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_EncryptUpdate failed");
    }

    int tmplen = 0;
    ret = EVP_EncryptFinal_ex(ctx, ciphertext.data() + outlen, &tmplen);
    if (ret != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_EncryptFinal_ex failed");
    }
    outlen += tmplen;

    std::vector<uint8_t> tag(TAG_SIZE);
    ret = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, TAG_SIZE, tag.data());
    if (ret != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_CTRL_GCM_GET_TAG failed");
    }

    EVP_CIPHER_CTX_free(ctx);

    ciphertext.resize(static_cast<size_t>(outlen));

    std::vector<uint8_t> combined;
    combined.reserve(IV_SIZE + TAG_SIZE + ciphertext.size());
    combined.insert(combined.end(), iv.begin(), iv.end());
    combined.insert(combined.end(), tag.begin(), tag.end());
    combined.insert(combined.end(), ciphertext.begin(), ciphertext.end());

    return base64Encode(combined);
}

std::string Crypto::decrypt(const std::vector<uint8_t>& key,
                            const std::string& encoded) {
    if (key.size() != static_cast<size_t>(KEY_SIZE)) {
        throw std::runtime_error("Invalid key size for decryption");
    }

    std::vector<uint8_t> combined = base64Decode(encoded);

    if (combined.size() < static_cast<size_t>(IV_SIZE + TAG_SIZE)) {
        throw std::runtime_error("Invalid encrypted data: too short");
    }

    const uint8_t* iv = combined.data();
    const uint8_t* tag = combined.data() + IV_SIZE;
    const uint8_t* ciphertext = combined.data() + IV_SIZE + TAG_SIZE;
    size_t ciphertextLen = combined.size() - IV_SIZE - TAG_SIZE;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Failed to create cipher context");
    }

    int ret = 0;

    ret = EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
    if (ret != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_DecryptInit_ex failed");
    }

    ret = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, IV_SIZE, nullptr);
    if (ret != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_CTRL_GCM_SET_IVLEN failed");
    }

    ret = EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), iv);
    if (ret != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_DecryptInit_ex with key/IV failed");
    }

    std::vector<uint8_t> plaintext(ciphertextLen);
    int outlen = 0;

    ret = EVP_DecryptUpdate(ctx, plaintext.data(), &outlen,
                            ciphertext, static_cast<int>(ciphertextLen));
    if (ret != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_DecryptUpdate failed");
    }

    ret = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, TAG_SIZE,
                              const_cast<uint8_t*>(tag));
    if (ret != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("EVP_CTRL_GCM_SET_TAG failed");
    }

    int tmplen = 0;
    ret = EVP_DecryptFinal_ex(ctx, plaintext.data() + outlen, &tmplen);
    if (ret <= 0) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error(
            "Decryption authentication failed: wrong key or corrupt data");
    }
    outlen += tmplen;

    plaintext.resize(static_cast<size_t>(outlen));
    EVP_CIPHER_CTX_free(ctx);

    return std::string(plaintext.begin(), plaintext.end());
}

std::string Crypto::base64Encode(const std::vector<uint8_t>& data) {
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* mem = BIO_new(BIO_s_mem());

    BIO_push(b64, mem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);

    BIO_write(b64, data.data(), static_cast<int>(data.size()));
    BIO_flush(b64);

    BUF_MEM* bufferPtr = nullptr;
    BIO_get_mem_ptr(b64, &bufferPtr);

    std::string result(bufferPtr->data, bufferPtr->length);

    BIO_free_all(b64);

    return result;
}

std::vector<uint8_t> Crypto::base64Decode(const std::string& encoded) {
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* mem = BIO_new_mem_buf(encoded.data(), static_cast<int>(encoded.size()));

    BIO_push(b64, mem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);

    size_t decodedLen = encoded.size();
    std::vector<uint8_t> result(decodedLen);

    int len = BIO_read(b64, result.data(), static_cast<int>(decodedLen));
    if (len < 0) {
        len = 0;
    }

    result.resize(static_cast<size_t>(len));
    BIO_free_all(b64);

    return result;
}
