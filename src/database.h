#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>

struct PasswordEntry {
    int id = 0;
    std::string name;
    std::string description;
    std::string account;
    std::string passwordEncrypted;
    std::string createdAt;
    std::string updatedAt;
};

class Database {
public:
    Database() = default;
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    bool open(const std::string& dbPath);
    void close();

    bool addPassword(const std::string& name,
                     const std::string& description,
                     const std::string& account,
                     const std::string& encryptedPassword);

    bool removePassword(const std::string& name);

    std::optional<PasswordEntry> getPassword(const std::string& name);

    std::vector<PasswordEntry> searchPasswords(const std::string& partialName);

    std::vector<PasswordEntry> listAllPasswords();

    int getPasswordCount();

    bool updatePassword(const std::string& name,
                        const std::string& description,
                        const std::string& account,
                        const std::string& encryptedPassword);

private:
    bool createTables();
    void* db_ = nullptr;
};
