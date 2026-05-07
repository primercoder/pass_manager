#include "cli.h"

#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>
#include <termios.h>
#include <limits>

static void setEcho(bool enable) {
    if (!isatty(STDIN_FILENO)) return;
    struct termios tty;
    if (tcgetattr(STDIN_FILENO, &tty) != 0) return;
    if (enable) {
        tty.c_lflag |= ECHO;
    } else {
        tty.c_lflag &= ~ECHO;
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &tty);
}

static void setRawMode(bool enable) {
    if (!isatty(STDIN_FILENO)) return;
    static struct termios saved;
    if (enable) {
        tcgetattr(STDIN_FILENO, &saved);
        struct termios raw = saved;
        raw.c_lflag &= ~(ICANON | ECHO);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    } else {
        tcsetattr(STDIN_FILENO, TCSANOW, &saved);
    }
}

static void getPasswordInput(const std::string& prompt, std::string& out) {
    std::cout << prompt;
    std::cout.flush();
    setEcho(false);
    std::getline(std::cin, out);
    setEcho(true);
    std::cout << std::endl;
}

static bool isInteractive() {
    static bool tty = isatty(STDIN_FILENO);
    return tty;
}

static void pauseForEnter(const std::string& msg = "Press Enter to continue...") {
    if (!isInteractive()) {
        std::cout << msg << "\n";
        return;
    }
    std::cout << msg;
    std::cout.flush();
    std::string dummy;
    std::getline(std::cin, dummy);
}

static bool inputAvailable() {
    return std::cin.good() && !std::cin.eof();
}

CLI::CLI(Database& db, const std::string& keyPath)
    : db_(db), keyPath_(keyPath) {
}

bool CLI::loadKeyFile() {
    return loadKeyFile(keyPath_);
}

bool CLI::loadKeyFile(const std::string& path) {
    try {
        aesKey_ = Crypto::deriveKey(path);
        keyPath_ = path;
        keyLoaded_ = true;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "\n[ERROR] Failed to load key file: " << e.what() << std::endl;
        keyLoaded_ = false;
        return false;
    }
}

bool CLI::doAdd(const std::string& name,
                const std::string& desc,
                const std::string& account,
                const std::string& password) {
    if (name.empty() || password.empty()) {
        std::cerr << "[ERROR] Name and password are required.\n";
        return false;
    }

    auto existing = db_.getPassword(name);
    if (existing.has_value()) {
        std::cerr << "[ERROR] Password with name '" << name << "' already exists.\n";
        return false;
    }

    std::string encrypted;
    try {
        encrypted = Crypto::encrypt(aesKey_, password);
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Encryption failed: " << e.what() << "\n";
        return false;
    }

    if (!db_.addPassword(name, desc, account, encrypted)) {
        std::cerr << "[ERROR] Failed to add password.\n";
        return false;
    }

    std::cout << "[OK] Password '" << name << "' added.\n";
    return true;
}

bool CLI::doRemove(const std::string& name, const std::string& password) {
    if (name.empty()) {
        std::cerr << "[ERROR] Name is required.\n";
        return false;
    }

    auto entry = db_.getPassword(name);
    if (!entry.has_value()) {
        std::cerr << "[ERROR] No password found with name '" << name << "'.\n";
        return false;
    }

    std::string stored;
    try {
        stored = Crypto::decrypt(aesKey_, entry->passwordEncrypted);
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Decryption failed: " << e.what() << "\n";
        return false;
    }

    if (password != stored) {
        std::cerr << "[ERROR] Password verification failed. Deletion aborted.\n";
        return false;
    }

    if (!db_.removePassword(name)) {
        std::cerr << "[ERROR] Failed to remove password.\n";
        return false;
    }

    std::cout << "[OK] Password '" << name << "' removed.\n";
    return true;
}

bool CLI::doShow(const std::string& name, bool reveal) {
    if (name.empty()) {
        std::cerr << "[ERROR] Name is required.\n";
        return false;
    }

    auto results = db_.searchPasswords(name);
    if (results.empty()) {
        std::cerr << "[NOT FOUND] No password matching '" << name << "'.\n";
        return false;
    }

    for (const auto& entry : results) {
        std::string decrypted;
        try {
            decrypted = Crypto::decrypt(aesKey_, entry.passwordEncrypted);
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] Decrypt failed for '" << entry.name
                      << "': " << e.what() << "\n";
            continue;
        }

        printPasswordDetail(entry, decrypted, reveal);

        if (copyToClipboard(decrypted)) {
            std::cout << "[OK] Password copied to clipboard.\n";
        } else {
            std::cout << "[WARN] Clipboard not available.\n";
        }
    }

    return true;
}

bool CLI::doList() {
    auto results = db_.listAllPasswords();
    if (results.empty()) {
        std::cout << "No passwords stored.\n";
        return true;
    }

    std::cout << std::left
              << std::setw(4) << "No."
              << std::setw(22) << "Name"
              << std::setw(22) << "Account"
              << std::setw(22) << "Created"
              << "Description\n";
    std::cout << std::string(90, '-') << "\n";

    for (size_t i = 0; i < results.size(); ++i) {
        std::string desc = results[i].description;
        if (desc.length() > 18) {
            desc = desc.substr(0, 15) + "...";
        }
        std::cout << std::left
                  << std::setw(4) << (i + 1)
                  << std::setw(22) << results[i].name.substr(0, 21)
                  << std::setw(22) << results[i].account.substr(0, 21)
                  << std::setw(22) << results[i].createdAt
                  << desc << "\n";
    }

    std::cout << "\nTotal: " << results.size() << " password(s)\n";
    return true;
}

bool CLI::doCount() {
    int count = db_.getPasswordCount();
    std::cout << "Total passwords stored: " << count << "\n";

    auto results = db_.listAllPasswords();
    if (!results.empty()) {
        std::cout << "\nPassword names:\n";
        for (size_t i = 0; i < results.size(); ++i) {
            std::cout << "  " << (i + 1) << ". " << results[i].name;
            if (!results[i].description.empty()) {
                std::cout << " -- " << results[i].description;
            }
            std::cout << "\n";
        }
    }
    return true;
}

bool CLI::doEdit(const std::string& name,
                 const std::string& oldPassword,
                 const std::string& newDesc,
                 const std::string& newAccount,
                 const std::string& newPassword) {
    if (name.empty()) {
        std::cerr << "[ERROR] Name is required.\n";
        return false;
    }

    auto entry = db_.getPassword(name);
    if (!entry.has_value()) {
        std::cerr << "[ERROR] No password found with name '" << name << "'.\n";
        return false;
    }

    std::string stored;
    try {
        stored = Crypto::decrypt(aesKey_, entry->passwordEncrypted);
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Decryption failed: " << e.what() << "\n";
        return false;
    }

    if (oldPassword != stored) {
        std::cerr << "[ERROR] Original password verification failed.\n";
        return false;
    }

    std::string desc = newDesc.empty() ? entry->description : newDesc;
    std::string account = newAccount.empty() ? entry->account : newAccount;

    std::string encrypted;
    if (!newPassword.empty()) {
        try {
            encrypted = Crypto::encrypt(aesKey_, newPassword);
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] Encryption failed: " << e.what() << "\n";
            return false;
        }
    } else {
        encrypted = entry->passwordEncrypted;
    }

    if (!db_.updatePassword(name, desc, account, encrypted)) {
        std::cerr << "[ERROR] Failed to update password.\n";
        return false;
    }

    std::cout << "[OK] Password '" << name << "' updated.\n";
    return true;
}

void CLI::printHeader() {
    std::cout << "\n\033[1;36m"
              << "╔══════════════════════════════════╗\n"
              << "║        PASSWORD MANAGER          ║\n"
              << "╚══════════════════════════════════╝\033[0m\n";
}

void CLI::printKeyStatus() {
    std::cout << "Key file: " << keyPath_ << std::endl;
}

void CLI::run() {
    if (!keyLoaded_ && !loadKeyFile()) {
        std::cerr << "Cannot start without a valid key file." << std::endl;
        std::cerr << "Use -k <path> to specify a key file." << std::endl;
        return;
    }

    std::string choice;
    while (inputAvailable()) {
        printHeader();
        printKeyStatus();
        std::cout << "\n";
        std::cout << "  1. Add Password        (新增密码)\n";
        std::cout << "  2. Remove Password     (移除密码)\n";
        std::cout << "  3. Show Password       (查询密码)\n";
        std::cout << "  4. List All Passwords  (密码列表)\n";
        std::cout << "  5. Password Count      (密码数量)\n";
        std::cout << "  6. Edit Password       (修改密码)\n";
        std::cout << "  7. Change Key File     (更换密钥)\n";
        std::cout << "  0. Exit                (退出)\n";
        std::cout << "\n";
        std::cout << "Choice: ";
        std::cout.flush();

        if (!std::getline(std::cin, choice)) {
            break;
        }

        if (choice == "1") {
            menuAdd();
        } else if (choice == "2") {
            menuRemove();
        } else if (choice == "3") {
            menuShow();
        } else if (choice == "4") {
            menuList();
        } else if (choice == "5") {
            menuCount();
        } else if (choice == "6") {
            menuEdit();
        } else if (choice == "7") {
            menuChangeKey();
        } else if (choice == "0") {
            std::cout << "\nGoodbye.\n" << std::endl;
            break;
        } else {
            std::cout << "\n[ERROR] Invalid choice.";
            pauseForEnter();
        }
    }
}

std::string CLI::readLine(const std::string& prompt) {
    std::cout << prompt;
    std::cout.flush();
    std::string line;
    std::getline(std::cin, line);
    return line;
}

std::string CLI::readOptionalLine(const std::string& prompt) {
    std::cout << prompt;
    std::cout.flush();
    std::string line;
    std::getline(std::cin, line);
    return line;
}

std::string CLI::readPassword(const std::string& prompt) {
    std::string password;
    getPasswordInput(prompt, password);
    return password;
}

bool CLI::copyToClipboard(const std::string& text) {
    FILE* pipe = popen("xclip -selection clipboard 2>/dev/null", "w");
    if (pipe) {
        fwrite(text.c_str(), 1, text.size(), pipe);
        int rc = pclose(pipe);
        if (rc == 0) return true;
    }

    pipe = popen("wl-copy 2>/dev/null", "w");
    if (pipe) {
        fwrite(text.c_str(), 1, text.size(), pipe);
        int rc = pclose(pipe);
        if (rc == 0) return true;
    }

    return false;
}

std::string CLI::encryptPassword(const std::string& password) {
    return Crypto::encrypt(aesKey_, password);
}

std::string CLI::decryptPassword(const std::string& encrypted) {
    return Crypto::decrypt(aesKey_, encrypted);
}

void CLI::menuAdd() {
    printHeader();
    std::cout << "=== Add Password ===\n\n";

    std::string name = readLine("Name (required, unique): ");
    if (name.empty()) {
        std::cout << "\n[ERROR] Name is required.\n";
        pauseForEnter();
        return;
    }

    auto existing = db_.getPassword(name);
    if (existing.has_value()) {
        std::cout << "\n[ERROR] A password with name '" << name
                  << "' already exists.\n";
        pauseForEnter();
        return;
    }

    std::string description = readOptionalLine("Description (optional): ");
    std::string account = readOptionalLine("Account (optional): ");

    std::string password, confirmPassword;
    while (inputAvailable()) {
        password = readPassword("Password (required, input hidden): ");
        if (password.empty()) {
            std::cout << "[ERROR] Password cannot be empty.\n";
            continue;
        }
        confirmPassword = readPassword("Confirm password: ");
        if (password == confirmPassword) {
            break;
        }
        std::cout << "[ERROR] Passwords do not match. Please try again.\n\n";
    }

    std::string encrypted;
    try {
        encrypted = Crypto::encrypt(aesKey_, password);
    } catch (const std::exception& e) {
        std::cerr << "\n[ERROR] Encryption failed: " << e.what() << std::endl;
        pauseForEnter();
        return;
    }

    if (db_.addPassword(name, description, account, encrypted)) {
        std::cout << "\n[OK] Password '" << name << "' added successfully.\n";
    } else {
        std::cout << "\n[ERROR] Failed to add password to database.\n";
    }

    pauseForEnter();
}

void CLI::menuRemove() {
    printHeader();
    std::cout << "=== Remove Password ===\n\n";
    std::cout << "You must enter the password to confirm deletion.\n\n";

    std::string name = readLine("Password name to remove: ");
    if (name.empty()) {
        std::cout << "\n[ERROR] Name is required.\n";
        pauseForEnter();
        return;
    }

    auto entry = db_.getPassword(name);
    if (!entry.has_value()) {
        std::cout << "\n[ERROR] No password found with name '" << name << "'.\n";
        pauseForEnter();
        return;
    }

    std::string storedPassword;
    try {
        storedPassword = Crypto::decrypt(aesKey_, entry->passwordEncrypted);
    } catch (const std::exception& e) {
        std::cerr << "\n[ERROR] Failed to decrypt password: " << e.what() << std::endl;
        pauseForEnter();
        return;
    }

    std::string confirmPassword = readPassword(
        "Enter the current password to confirm deletion: ");

    if (confirmPassword != storedPassword) {
        std::cout << "\n[ERROR] Password does not match. Deletion aborted.\n";
        pauseForEnter();
        return;
    }

    if (db_.removePassword(name)) {
        std::cout << "\n[OK] Password '" << name << "' removed successfully.\n";
    } else {
        std::cout << "\n[ERROR] Failed to remove password.\n";
    }

    pauseForEnter();
}

void CLI::printPasswordDetail(const PasswordEntry& entry,
                              const std::string& decryptedPassword,
                              bool showPassword) {
    std::cout << "\n";
    std::cout << "┌─────────────────────────────────────────┐\n";
    std::cout << "│ " << std::left << std::setw(39) << entry.name << "│\n";
    std::cout << "├─────────────────────────────────────────┤\n";
    if (!entry.account.empty()) {
        std::cout << "│ Account:    " << std::left << std::setw(28)
                  << entry.account << "│\n";
    }
    if (!entry.description.empty()) {
        std::cout << "│ Desc:       " << std::left << std::setw(28)
                  << entry.description << "│\n";
    }
    std::cout << "│ Created:    " << std::left << std::setw(28)
              << entry.createdAt << "│\n";
    std::cout << "│ Updated:    " << std::left << std::setw(28)
              << entry.updatedAt << "│\n";
    if (showPassword) {
        std::cout << "│ Password:   " << std::left << std::setw(28)
                  << decryptedPassword << "│\n";
    } else {
        std::cout << "│ Password:   " << std::left << std::setw(28)
                  << "******** (hidden)" << "│\n";
    }
    std::cout << "└─────────────────────────────────────────┘\n";
}

std::string CLI::searchPasswordName() {
    if (!isInteractive()) {
        return readLine("Enter password name (partial OK): ");
    }

    std::cout << "  Enter password name (ESC to cancel)\n";
    std::cout << "  > " << std::flush;

    std::cout << "\033[s" << std::flush;

    std::string buffer;
    setRawMode(true);

    while (true) {
        int ch = std::cin.get();

        if (ch == 27) {
            setRawMode(false);
            std::cout << "\033[u\033[J\n[CANCELLED]\n";
            return "";
        }

        if (ch == '\n' || ch == '\r') {
            break;
        }

        if (ch == 127 || ch == '\b') {
            if (!buffer.empty()) {
                buffer.pop_back();
            }
        } else if (ch >= 32 && ch <= 126) {
            buffer += static_cast<char>(ch);
        } else {
            continue;
        }

        std::cout << "\r> ";
        std::cout << "\033[K";
        std::cout << buffer << std::flush;

        std::cout << "\n\033[J";

        if (!buffer.empty()) {
            auto results = db_.searchPasswords(buffer);
            int count = static_cast<int>(results.size());
            int show = std::min(10, count);

            if (show > 0) {
                std::cout << "  \033[90m── matches (" << count << ") ──\033[0m\n";
                for (int i = 0; i < show; ++i) {
                    std::cout << "  " << std::setw(3) << (i + 1) << ". "
                              << std::left << std::setw(22) << results[static_cast<size_t>(i)].name;
                    if (!results[static_cast<size_t>(i)].account.empty()) {
                        std::cout << "(" << results[static_cast<size_t>(i)].account << ")";
                    }
                    std::cout << "\033[K\n";
                }
                if (count > 10) {
                    std::cout << "  \033[90m... and " << (count - 10)
                              << " more (type to narrow)\033[0m\033[K\n";
                }
            } else {
                std::cout << "  \033[90m(no matches)\033[0m\033[K\n";
            }
        }

        std::cout << "\033[u" << std::flush;
        std::cout << buffer << std::flush;
    }

    setRawMode(false);

    std::cout << "\033[u\033[J";
    std::cout << buffer << "\n" << std::flush;

    return buffer;
}

void CLI::menuShow() {
    printHeader();
    std::cout << "=== Show Password ===\n\n";

    std::string partial = searchPasswordName();
    if (partial.empty()) {
        pauseForEnter();
        return;
    }

    auto results = db_.searchPasswords(partial);
    if (results.empty()) {
        std::cout << "\n[NOT FOUND] No password matching '" << partial << "'.\n";
        pauseForEnter();
        return;
    }

    const PasswordEntry* selected = nullptr;

    if (results.size() == 1) {
        selected = &results[0];
    } else {
        std::cout << "\nFound " << results.size() << " matching passwords:\n\n";
        std::cout << std::left
                  << std::setw(4) << "No."
                  << std::setw(25) << "Name"
                  << std::setw(25) << "Account"
                  << "Description\n";
        std::cout << std::string(78, '-') << "\n";

        for (size_t i = 0; i < results.size(); ++i) {
            std::string desc = results[i].description;
            if (desc.length() > 20) {
                desc = desc.substr(0, 17) + "...";
            }
            std::cout << std::left
                      << std::setw(4) << (i + 1)
                      << std::setw(25) << results[i].name.substr(0, 24)
                      << std::setw(25) << results[i].account.substr(0, 24)
                      << desc << "\n";
        }
        std::cout << "\n";

        std::string sel = readLine("Select a number (0 to cancel): ");
        int idx = 0;
        try {
            idx = std::stoi(sel);
        } catch (...) {
            idx = 0;
        }

        if (idx < 1 || static_cast<size_t>(idx) > results.size()) {
            std::cout << "\n[CANCELLED] Selection cancelled.\n";
            pauseForEnter();
            return;
        }
        selected = &results[static_cast<size_t>(idx - 1)];
    }

    std::string decryptedPassword;
    try {
        decryptedPassword = Crypto::decrypt(aesKey_,
                                            selected->passwordEncrypted);
    } catch (const std::exception& e) {
        std::cerr << "\n[ERROR] Failed to decrypt password: "
                  << e.what() << std::endl;
        std::cerr << "The key file may have changed since this password was stored.\n";
        pauseForEnter();
        return;
    }

    std::string showChoice = readLine("\nShow password in plaintext? (y/N): ");
    bool showPassword = (showChoice == "y" || showChoice == "Y");

    printPasswordDetail(*selected, decryptedPassword, showPassword);

    if (copyToClipboard(decryptedPassword)) {
        std::cout << "\n[OK] Password copied to clipboard.";
    } else {
        std::cout << "\n[WARN] Could not copy to clipboard (xclip/wl-copy not found).";
    }

    pauseForEnter("\n\nPress Enter to continue...");
}

void CLI::menuList() {
    printHeader();
    std::cout << "=== Password List ===\n\n";

    auto results = db_.listAllPasswords();
    if (results.empty()) {
        std::cout << "No passwords stored yet.\n";
        pauseForEnter("\nPress Enter to continue...");
        return;
    }

    std::cout << std::left
              << std::setw(4) << "No."
              << std::setw(22) << "Name"
              << std::setw(22) << "Account"
              << std::setw(22) << "Created"
              << "Description\n";
    std::cout << std::string(90, '-') << "\n";

    for (size_t i = 0; i < results.size(); ++i) {
        std::string desc = results[i].description;
        if (desc.length() > 18) {
            desc = desc.substr(0, 15) + "...";
        }
        std::cout << std::left
                  << std::setw(4) << (i + 1)
                  << std::setw(22) << results[i].name.substr(0, 21)
                  << std::setw(22) << results[i].account.substr(0, 21)
                  << std::setw(22) << results[i].createdAt
                  << desc << "\n";
    }

    std::cout << "\nTotal: " << results.size() << " password(s)\n";
    pauseForEnter("\nPress Enter to continue...");
}

void CLI::menuCount() {
    printHeader();
    std::cout << "=== Password Count ===\n\n";

    int count = db_.getPasswordCount();
    std::cout << "Total passwords stored: " << count << "\n";

    auto results = db_.listAllPasswords();
    if (!results.empty()) {
        std::cout << "\nPassword names:\n";
        for (size_t i = 0; i < results.size(); ++i) {
            std::cout << "  " << (i + 1) << ". " << results[i].name;
            if (!results[i].description.empty()) {
                std::cout << " -- " << results[i].description;
            }
            std::cout << "\n";
        }
    }

    pauseForEnter("\nPress Enter to continue...");
}

void CLI::menuEdit() {
    printHeader();
    std::cout << "=== Edit Password ===\n\n";
    std::cout << "You must enter the original password to confirm changes.\n\n";

    std::string name = readLine("Password name to edit: ");
    if (name.empty()) {
        std::cout << "\n[ERROR] Name is required.\n";
        pauseForEnter();
        return;
    }

    auto entry = db_.getPassword(name);
    if (!entry.has_value()) {
        std::cout << "\n[ERROR] No password found with name '" << name << "'.\n";
        pauseForEnter();
        return;
    }

    std::string storedPassword;
    try {
        storedPassword = Crypto::decrypt(aesKey_, entry->passwordEncrypted);
    } catch (const std::exception& e) {
        std::cerr << "\n[ERROR] Failed to decrypt password: " << e.what() << std::endl;
        pauseForEnter();
        return;
    }

    std::string verifyPassword = readPassword(
        "Enter the original password to verify: ");
    if (verifyPassword != storedPassword) {
        std::cout << "\n[ERROR] Password verification failed. Edit aborted.\n";
        pauseForEnter();
        return;
    }

    std::cout << "\n[Leave blank to keep current value]\n";

    std::string newDesc = readOptionalLine(
        "New description [" + entry->description + "]: ");
    if (newDesc.empty()) {
        newDesc = entry->description;
    }

    std::string newAccount = readOptionalLine(
        "New account [" + entry->account + "]: ");
    if (newAccount.empty()) {
        newAccount = entry->account;
    }

    std::string newPassword;
    std::string changePwd = readLine("Change password? (y/N): ");
    if (changePwd == "y" || changePwd == "Y") {
        std::string confirmNew;
        while (inputAvailable()) {
            newPassword = readPassword("New password (input hidden): ");
            if (newPassword.empty()) {
                std::cout << "[ERROR] Password cannot be empty. Using original.\n";
                newPassword.clear();
                break;
            }
            confirmNew = readPassword("Confirm new password: ");
            if (newPassword == confirmNew) {
                break;
            }
            std::cout << "[ERROR] Passwords do not match. Try again.\n\n";
        }
    }

    std::string encrypted;
    if (!newPassword.empty()) {
        try {
            encrypted = Crypto::encrypt(aesKey_, newPassword);
        } catch (const std::exception& e) {
            std::cerr << "\n[ERROR] Encryption failed: " << e.what() << std::endl;
            pauseForEnter();
            return;
        }
    } else {
        encrypted = entry->passwordEncrypted;
    }

    if (db_.updatePassword(name, newDesc, newAccount, encrypted)) {
        std::cout << "\n[OK] Password '" << name << "' updated successfully.\n";
    } else {
        std::cout << "\n[ERROR] Failed to update password.\n";
    }

    pauseForEnter();
}

void CLI::menuChangeKey() {
    printHeader();
    std::cout << "=== Change Key File ===\n\n";
    std::cout << "Current key file: " << keyPath_ << "\n\n";

    std::string newPath = readLine("Enter new key file path (empty to keep current): ");
    if (newPath.empty()) {
        std::cout << "\nKey file unchanged.\n";
        pauseForEnter();
        return;
    }

    struct stat st;
    if (stat(newPath.c_str(), &st) != 0) {
        std::string createNew = readLine(
            "Key file does not exist. Create a new one? (y/N): ");
        if (createNew == "y" || createNew == "Y") {
            if (Crypto::generateKeyFile(newPath)) {
                std::cout << "\n[OK] New key file created at: " << newPath << "\n";
            } else {
                std::cout << "\n[ERROR] Failed to create key file.\n";
                pauseForEnter();
                return;
            }
        } else {
            std::cout << "\n[CANCELLED] Key file unchanged.\n";
            pauseForEnter();
            return;
        }
    }

    if (loadKeyFile(newPath)) {
        std::cout << "\n[OK] Key file changed successfully.\n";
    } else {
        if (!loadKeyFile(keyPath_)) {
            std::cerr << "\n[FATAL] Cannot fall back to previous key file either.\n";
        }
    }

    pauseForEnter("\nPress Enter to continue...");
}
