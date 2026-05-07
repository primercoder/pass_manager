#pragma once

#include <string>
#include <vector>
#include <cstdint>

#include "database.h"
#include "crypto.h"

class CLI {
public:
    CLI(Database& db, const std::string& keyPath);

    void run();

    bool loadKeyFile();
    bool loadKeyFile(const std::string& path);

    bool doAdd(const std::string& name,
               const std::string& desc,
               const std::string& account,
               const std::string& password);
    bool doRemove(const std::string& name, const std::string& password);
    bool doShow(const std::string& name, bool reveal);
    bool doList();
    bool doEdit(const std::string& name,
                const std::string& oldPassword,
                const std::string& newDesc,
                const std::string& newAccount,
                const std::string& newPassword);

private:
    void printHelp();
    void printHeader();

    void cmdAdd();
    void cmdDelete();
    void cmdShow();
    void cmdList();
    void cmdEdit();
    void cmdKey();

    std::string readPassword(const std::string& prompt);
    std::string readLine(const std::string& prompt);

    bool copyToClipboard(const std::string& text);

    void printPasswordDetail(const PasswordEntry& entry,
                             const std::string& decryptedPassword,
                             bool showPassword);

    Database& db_;
    std::string keyPath_;
    std::vector<uint8_t> aesKey_;
    bool keyLoaded_ = false;

    void printKeyStatus();
};
