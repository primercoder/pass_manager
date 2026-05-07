#include "database.h"

#include <sqlite3.h>
#include <stdexcept>
#include <cstring>

Database::~Database() {
    close();
}

bool Database::open(const std::string& dbPath) {
    if (db_) {
        close();
    }

    int rc = sqlite3_open(dbPath.c_str(), reinterpret_cast<sqlite3**>(&db_));
    if (rc != SQLITE_OK) {
        return false;
    }

    sqlite3_exec(reinterpret_cast<sqlite3*>(db_),
                 "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(reinterpret_cast<sqlite3*>(db_),
                 "PRAGMA foreign_keys=ON;", nullptr, nullptr, nullptr);

    return createTables();
}

void Database::close() {
    if (db_) {
        sqlite3_close(reinterpret_cast<sqlite3*>(db_));
        db_ = nullptr;
    }
}

bool Database::createTables() {
    const char* sql = R"SQL(
        CREATE TABLE IF NOT EXISTS passwords (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            name            TEXT    NOT NULL UNIQUE,
            description     TEXT    NOT NULL DEFAULT '',
            account         TEXT    NOT NULL DEFAULT '',
            password_enc    TEXT    NOT NULL,
            created_at      TEXT    NOT NULL DEFAULT (datetime('now','localtime')),
            updated_at      TEXT    NOT NULL DEFAULT (datetime('now','localtime'))
        );
    )SQL";

    char* errMsg = nullptr;
    int rc = sqlite3_exec(reinterpret_cast<sqlite3*>(db_), sql,
                          nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        if (errMsg) {
            sqlite3_free(errMsg);
        }
        return false;
    }

    return true;
}

bool Database::addPassword(const std::string& name,
                           const std::string& description,
                           const std::string& account,
                           const std::string& encryptedPassword) {
    const char* sql = R"SQL(
        INSERT INTO passwords (name, description, account, password_enc)
        VALUES (?, ?, ?, ?);
    )SQL";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(db_), sql, -1,
                                &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, encryptedPassword.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

bool Database::removePassword(const std::string& name) {
    const char* sql = "DELETE FROM passwords WHERE name = ?;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(db_), sql, -1,
                                &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    int changes = sqlite3_changes(reinterpret_cast<sqlite3*>(db_));
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE && changes > 0;
}

std::optional<PasswordEntry> Database::getPassword(const std::string& name) {
    const char* sql = R"SQL(
        SELECT id, name, description, account, password_enc, created_at, updated_at
        FROM passwords
        WHERE name = ?;
    )SQL";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(db_), sql, -1,
                                &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<PasswordEntry> result = std::nullopt;

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        PasswordEntry entry;
        entry.id = sqlite3_column_int(stmt, 0);
        entry.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        entry.description = reinterpret_cast<const char*>(
            sqlite3_column_text(stmt, 2));
        entry.account = reinterpret_cast<const char*>(
            sqlite3_column_text(stmt, 3));
        entry.passwordEncrypted = reinterpret_cast<const char*>(
            sqlite3_column_text(stmt, 4));
        entry.createdAt = reinterpret_cast<const char*>(
            sqlite3_column_text(stmt, 5));
        entry.updatedAt = reinterpret_cast<const char*>(
            sqlite3_column_text(stmt, 6));
        result = std::move(entry);
    }

    sqlite3_finalize(stmt);
    return result;
}

std::vector<PasswordEntry> Database::searchPasswords(
    const std::string& partialName) {

    const char* sql = R"SQL(
        SELECT id, name, description, account, password_enc, created_at, updated_at
        FROM passwords
        WHERE name LIKE ? ESCAPE '\'
        ORDER BY name ASC;
    )SQL";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(db_), sql, -1,
                                &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return {};
    }

    std::string pattern = "%" + partialName + "%";

    for (size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == '%' && i > 0 && pattern[i - 1] == '\\') {
            continue;
        }
        if (pattern[i] == '_') {
            pattern.insert(i, "\\");
            ++i;
        }
    }

    sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);

    std::vector<PasswordEntry> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PasswordEntry entry;
        entry.id = sqlite3_column_int(stmt, 0);
        entry.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        entry.description = reinterpret_cast<const char*>(
            sqlite3_column_text(stmt, 2));
        entry.account = reinterpret_cast<const char*>(
            sqlite3_column_text(stmt, 3));
        entry.passwordEncrypted = reinterpret_cast<const char*>(
            sqlite3_column_text(stmt, 4));
        entry.createdAt = reinterpret_cast<const char*>(
            sqlite3_column_text(stmt, 5));
        entry.updatedAt = reinterpret_cast<const char*>(
            sqlite3_column_text(stmt, 6));
        results.push_back(std::move(entry));
    }

    sqlite3_finalize(stmt);
    return results;
}

std::vector<PasswordEntry> Database::listAllPasswords() {
    const char* sql = R"SQL(
        SELECT id, name, description, account, password_enc, created_at, updated_at
        FROM passwords
        ORDER BY name ASC;
    )SQL";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(db_), sql, -1,
                                &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return {};
    }

    std::vector<PasswordEntry> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PasswordEntry entry;
        entry.id = sqlite3_column_int(stmt, 0);
        entry.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        entry.description = reinterpret_cast<const char*>(
            sqlite3_column_text(stmt, 2));
        entry.account = reinterpret_cast<const char*>(
            sqlite3_column_text(stmt, 3));
        entry.passwordEncrypted = reinterpret_cast<const char*>(
            sqlite3_column_text(stmt, 4));
        entry.createdAt = reinterpret_cast<const char*>(
            sqlite3_column_text(stmt, 5));
        entry.updatedAt = reinterpret_cast<const char*>(
            sqlite3_column_text(stmt, 6));
        results.push_back(std::move(entry));
    }

    sqlite3_finalize(stmt);
    return results;
}

int Database::getPasswordCount() {
    const char* sql = "SELECT COUNT(*) FROM passwords;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(db_), sql, -1,
                                &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return -1;
    }

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return count;
}

bool Database::updatePassword(const std::string& name,
                              const std::string& description,
                              const std::string& account,
                              const std::string& encryptedPassword) {
    const char* sql = R"SQL(
        UPDATE passwords
        SET description = ?, account = ?, password_enc = ?,
            updated_at = datetime('now','localtime')
        WHERE name = ?;
    )SQL";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(db_), sql, -1,
                                &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, encryptedPassword.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, name.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    int changes = sqlite3_changes(reinterpret_cast<sqlite3*>(db_));
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE && changes > 0;
}
