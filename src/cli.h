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
    bool doCount();
    bool doEdit(const std::string& name,
                const std::string& oldPassword,
                const std::string& newDesc,
                const std::string& newAccount,
                const std::string& newPassword);

private:
    void printHeader();

    void menuAdd();
    void menuRemove();
    void menuShow();
    void menuList();
    void menuCount();
    void menuChangeKey();
    void menuEdit();

    std::string searchPasswordName();

    std::string readPassword(const std::string& prompt);
    std::string readLine(const std::string& prompt);
    std::string readOptionalLine(const std::string& prompt);

    bool copyToClipboard(const std::string& text);

    void printPasswordDetail(const PasswordEntry& entry,
                             const std::string& decryptedPassword,
                             bool showPassword);

    std::string encryptPassword(const std::string& password);
    std::string decryptPassword(const std::string& encrypted);

    Database& db_;
    std::string keyPath_;
    std::vector<uint8_t> aesKey_;
    bool keyLoaded_ = false;

    void printKeyStatus();
};
