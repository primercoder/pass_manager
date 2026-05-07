#pragma once

#include <string>
#include <vector>
#include <cstdint>

#include "database.h"
#include "crypto.h"

class CLI {
public:
    CLI(Database& db, const std::string& defaultKeyPath);

    void run();

private:
    void printHeader();

    void menuAdd();
    void menuRemove();
    void menuShow();
    void menuList();
    void menuCount();
    void menuChangeKey();

    std::string readPassword(const std::string& prompt);
    std::string readLine(const std::string& prompt);
    std::string readOptionalLine(const std::string& prompt);

    bool copyToClipboard(const std::string& text);

    void printPasswordDetail(const PasswordEntry& entry,
                             const std::string& decryptedPassword,
                             bool showPassword);

    Database& db_;
    std::string defaultKeyPath_;
    std::string currentKeyPath_;
    std::vector<uint8_t> aesKey_;

    bool loadKeyFile();
    bool loadKeyFile(const std::string& path);
    void printKeyStatus();
};
