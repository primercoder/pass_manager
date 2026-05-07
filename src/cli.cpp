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

static void getPasswordInput(const std::string& prompt, std::string& out) {
    std::cout << prompt;
    std::cout.flush();
    setEcho(false);
    std::getline(std::cin, out);
    setEcho(true);
    std::cout << std::endl;
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
        std::cerr << "[ERROR] Failed to load key file: " << e.what() << std::endl;
        keyLoaded_ = false;
        return false;
    }
}

bool CLI::doAdd(const std::string& name,
                const std::string& desc,
                const std::string& account,
                const std::string& password) {
    std::string n = name;
    if (n.empty()) {
        std::cout << "Name: " << std::flush;
        std::getline(std::cin, n);
        if (n.empty()) {
            std::cerr << "[ERROR] Name is required.\n";
            return false;
        }
    }

    auto existing = db_.getPassword(n);
    if (existing.has_value()) {
        std::cerr << "[ERROR] Password with name '" << n << "' already exists.\n";
        return false;
    }

    std::string d = desc;
    if (d.empty() && !assumeYes_) {
        std::cout << "Description (optional): " << std::flush;
        std::getline(std::cin, d);
    }

    std::string a = account;
    if (a.empty() && !assumeYes_) {
        std::cout << "Account (optional): " << std::flush;
        std::getline(std::cin, a);
    }

    std::string pwd = password;
    if (pwd.empty()) {
        std::string confirm;
        while (true) {
            getPasswordInput("Password: ", pwd);
            if (pwd.empty()) {
                std::cerr << "[ERROR] Password cannot be empty.\n";
                return false;
            }
            getPasswordInput("Confirm:  ", confirm);
            if (pwd == confirm) break;
            std::cerr << "[ERROR] Passwords do not match.\n\n";
        }
    }

    std::string encrypted;
    try {
        encrypted = Crypto::encrypt(aesKey_, pwd);
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Encryption failed: " << e.what() << "\n";
        return false;
    }

    if (!db_.addPassword(n, d, a, encrypted)) {
        std::cerr << "[ERROR] Failed to add password.\n";
        return false;
    }

    std::cout << "[OK] Password '" << n << "' added.\n";
    return true;
}

bool CLI::doRemove(const std::string& name, const std::string& password) {
    std::string n = name;
    if (n.empty()) {
        std::cout << "Name: " << std::flush;
        std::getline(std::cin, n);
        if (n.empty()) {
            std::cerr << "[ERROR] Name is required.\n";
            return false;
        }
    }

    auto entry = db_.getPassword(n);
    if (!entry.has_value()) {
        std::cerr << "[ERROR] No password found with name '" << n << "'.\n";
        return false;
    }

    std::string stored;
    try {
        stored = Crypto::decrypt(aesKey_, entry->passwordEncrypted);
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Decryption failed: " << e.what() << "\n";
        return false;
    }

    std::string verify = password;
    if (verify.empty()) {
        getPasswordInput("Verify password: ", verify);
    }
    if (verify != stored) {
        std::cerr << "[ERROR] Password does not match. Deletion aborted.\n";
        return false;
    }

    if (!db_.removePassword(n)) {
        std::cerr << "[ERROR] Failed to remove password.\n";
        return false;
    }

    std::cout << "[OK] Password '" << n << "' deleted.\n";
    return true;
}

bool CLI::doShow(const std::string& name, bool reveal) {
    std::string searchName = name;
    bool exact = !name.empty();

    if (searchName.empty()) {
        std::cout << "Name: " << std::flush;
        std::getline(std::cin, searchName);
        if (searchName.empty()) {
            std::cerr << "[ERROR] Name is required.\n";
            return false;
        }
    }

    PasswordEntry target;

    if (exact) {
        auto entry = db_.getPassword(searchName);
        if (!entry.has_value()) {
            std::cout << "[NOT FOUND] No password named '" << searchName << "'.\n";
            return false;
        }
        target = entry.value();
    } else {
        auto results = db_.searchPasswords(searchName);
        if (results.empty()) {
            std::cout << "[NOT FOUND] No matches for '" << searchName << "'.\n";
            return false;
        }

        if (results.size() == 1) {
            target = results[0];
        } else {
            std::cout << "\n  " << results.size() << " matches:\n\n";
            for (size_t i = 0; i < results.size(); ++i) {
                std::cout << "  " << std::right << std::setw(3) << (i + 1)
                          << ". " << std::left << std::setw(22) << results[i].name;
                if (!results[i].account.empty()) {
                    std::cout << "(" << results[i].account << ")";
                }
                std::cout << "\n";
            }
            std::string sel;
            while (true) {
                std::cout << "\n  Select (1-" << results.size() << "): " << std::flush;
                std::getline(std::cin, sel);
                int idx = 0;
                try { idx = std::stoi(sel); } catch (...) { idx = 0; }
                if (idx >= 1 && static_cast<size_t>(idx) <= results.size()) {
                    target = results[static_cast<size_t>(idx - 1)];
                    break;
                }
                std::cout << "  Invalid selection.\n";
            }
        }
    }

    std::string decrypted;
    try {
        decrypted = Crypto::decrypt(aesKey_, target.passwordEncrypted);
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Decrypt failed: " << e.what() << "\n";
        return false;
    }

    printPasswordDetail(target, decrypted, reveal);

    if (copyToClipboard(decrypted)) {
        std::cout << "[OK] Password copied to clipboard.\n";
    } else {
        std::cout << "[WARN] Clipboard not available.\n";
    }

    return true;
}

bool CLI::doList() {
    auto results = db_.listAllPasswords();
    if (results.empty()) {
        std::cout << "No passwords stored.\n";
        return true;
    }

    std::cout << "\n";
    std::cout << std::setw(4) << "No."
              << "  " << std::left << std::setw(22) << "Name"
              << std::setw(24) << "Description"
              << std::setw(22) << "Created"
              << "Updated\n";
    std::cout << std::string(90, '-') << "\n";

    for (size_t i = 0; i < results.size(); ++i) {
        std::string desc = results[i].description;
        if (desc.length() > 21) desc = desc.substr(0, 18) + "...";
        std::cout << std::setw(4) << (i + 1)
                  << "  " << std::left << std::setw(22) << results[i].name.substr(0, 21)
                  << std::setw(24) << desc
                  << std::setw(22) << results[i].createdAt
                  << results[i].updatedAt << "\n";
    }

    std::cout << "\nTotal: " << results.size() << " password(s)\n";
    return true;
}

bool CLI::doEdit(const std::string& name,
                 const std::string& oldPassword,
                 const std::string& newDesc,
                 const std::string& newAccount,
                 const std::string& newPassword) {
    std::string n = name;
    if (n.empty()) {
        std::cout << "Name: " << std::flush;
        std::getline(std::cin, n);
        if (n.empty()) {
            std::cerr << "[ERROR] Name is required.\n";
            return false;
        }
    }

    auto entry = db_.getPassword(n);
    if (!entry.has_value()) {
        std::cerr << "[ERROR] No password found with name '" << n << "'.\n";
        return false;
    }

    std::string stored;
    try {
        stored = Crypto::decrypt(aesKey_, entry->passwordEncrypted);
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Decryption failed: " << e.what() << "\n";
        return false;
    }

    std::string verify = oldPassword;
    if (verify.empty()) {
        getPasswordInput("Verify original password: ", verify);
    }
    if (verify != stored) {
        std::cerr << "[ERROR] Original password verification failed.\n";
        return false;
    }

    std::string desc = newDesc;
    if (desc.empty() && !assumeYes_) {
        std::cout << "New description [" << entry->description << "]: " << std::flush;
        std::getline(std::cin, desc);
    }
    if (desc.empty()) desc = entry->description;

    std::string account = newAccount;
    if (account.empty() && !assumeYes_) {
        std::cout << "New account [" << entry->account << "]: " << std::flush;
        std::getline(std::cin, account);
    }
    if (account.empty()) account = entry->account;

    std::string pwd = newPassword;
    if (pwd.empty() && !assumeYes_) {
        std::string changePwd;
        std::cout << "Change password? (y/N): " << std::flush;
        std::getline(std::cin, changePwd);
        if (changePwd == "y" || changePwd == "Y") {
            std::string confirm;
            while (inputAvailable()) {
                getPasswordInput("New password: ", pwd);
                if (pwd.empty()) break;
                getPasswordInput("Confirm:      ", confirm);
                if (pwd == confirm) break;
                std::cerr << "[ERROR] Passwords do not match.\n\n";
            }
        }
    }

    std::string encrypted;
    if (!pwd.empty()) {
        try {
            encrypted = Crypto::encrypt(aesKey_, pwd);
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] Encryption failed: " << e.what() << "\n";
            return false;
        }
    } else {
        encrypted = entry->passwordEncrypted;
    }

    if (!db_.updatePassword(n, desc, account, encrypted)) {
        std::cerr << "[ERROR] Failed to update password.\n";
        return false;
    }

    std::cout << "[OK] Password '" << n << "' updated.\n";
    return true;
}

void CLI::printHeader() {
    std::cout << "\033[1;36m"
              << "╔══════════════════════════════════╗\n"
              << "║        PASSWORD MANAGER          ║\n"
              << "╚══════════════════════════════════╝\033[0m\n";
}

void CLI::printKeyStatus() {
    std::cout << "Key: " << keyPath_ << std::endl;
}

void CLI::printHelp() {
    std::cout << "\n";
    std::cout << "  add      Add a new password\n";
    std::cout << "  delete   Delete a password\n";
    std::cout << "  show     Search and reveal a password\n";
    std::cout << "  list     List all passwords with details\n";
    std::cout << "  edit     Edit a password\n";
    std::cout << "  key      Change key file\n";
    std::cout << "  help     Show this help\n";
    std::cout << "  exit     Quit the program\n";
}

void CLI::run() {
    if (!keyLoaded_ && !loadKeyFile()) {
        std::cerr << "Cannot start without a valid key file." << std::endl;
        std::cerr << "Use -k <path> to specify a key file." << std::endl;
        return;
    }

    printHeader();
    printKeyStatus();
    printHelp();

    std::string cmd;
    while (inputAvailable()) {
        std::cout << "\npass_manager> " << std::flush;
        if (!std::getline(std::cin, cmd)) break;
        if (cmd.empty()) continue;

        if (cmd == "add") {
            cmdAdd();
        } else if (cmd == "delete" || cmd == "remove") {
            cmdDelete();
        } else if (cmd == "show") {
            cmdShow();
        } else if (cmd == "list") {
            cmdList();
        } else if (cmd == "edit") {
            cmdEdit();
        } else if (cmd == "key") {
            cmdKey();
        } else if (cmd == "help") {
            printHelp();
        } else if (cmd == "exit" || cmd == "quit") {
            std::cout << "Goodbye.\n" << std::endl;
            break;
        } else {
            std::cout << "Unknown command: " << cmd
                      << " (type 'help' for commands)\n";
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

void CLI::cmdAdd() {
    std::string name = readLine("  Name: ");
    if (name.empty()) {
        std::cout << "[ERROR] Name is required.\n";
        return;
    }

    auto existing = db_.getPassword(name);
    if (existing.has_value()) {
        std::cout << "[ERROR] '" << name << "' already exists.\n";
        return;
    }

    std::string desc, account;
    std::cout << "  Description: " << std::flush;
    std::getline(std::cin, desc);
    std::cout << "  Account: " << std::flush;
    std::getline(std::cin, account);

    std::string password, confirm;
    while (inputAvailable()) {
        getPasswordInput("  Password: ", password);
        if (password.empty()) {
            std::cout << "[ERROR] Password cannot be empty.\n";
            continue;
        }
        getPasswordInput("  Confirm:  ", confirm);
        if (password == confirm) break;
        std::cout << "[ERROR] Passwords do not match.\n\n";
    }

    std::string encrypted;
    try {
        encrypted = Crypto::encrypt(aesKey_, password);
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Encryption failed: " << e.what() << std::endl;
        return;
    }

    if (db_.addPassword(name, desc, account, encrypted)) {
        std::cout << "[OK] '" << name << "' added.\n";
    } else {
        std::cout << "[ERROR] Failed to add password.\n";
    }
}

void CLI::cmdDelete() {
    std::string name = readLine("  Name: ");
    if (name.empty()) {
        std::cout << "[ERROR] Name is required.\n";
        return;
    }

    auto entry = db_.getPassword(name);
    if (!entry.has_value()) {
        std::cout << "[ERROR] No password found: '" << name << "'.\n";
        return;
    }

    std::string stored;
    try {
        stored = Crypto::decrypt(aesKey_, entry->passwordEncrypted);
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Decryption failed: " << e.what() << std::endl;
        return;
    }

    std::string verify = readPassword("  Verify password: ");
    if (verify != stored) {
        std::cout << "[ERROR] Password does not match. Deletion aborted.\n";
        return;
    }

    if (db_.removePassword(name)) {
        std::cout << "[OK] '" << name << "' deleted.\n";
    } else {
        std::cout << "[ERROR] Failed to delete password.\n";
    }
}

void CLI::printPasswordDetail(const PasswordEntry& entry,
                              const std::string& decryptedPassword,
                              bool showPassword) {
    std::cout << "\n";
    std::cout << "  ┌────────────────────────────────────────┐\n";
    std::cout << "  │ " << std::left << std::setw(38) << entry.name << " │\n";
    std::cout << "  ├────────────────────────────────────────┤\n";
    if (!entry.account.empty()) {
        std::cout << "  │ Account:  " << std::left << std::setw(28)
                  << entry.account << " │\n";
    }
    if (!entry.description.empty()) {
        std::cout << "  │ Desc:     " << std::left << std::setw(28)
                  << entry.description << " │\n";
    }
    std::cout << "  │ Created:  " << std::left << std::setw(28)
              << entry.createdAt << " │\n";
    std::cout << "  │ Updated:  " << std::left << std::setw(28)
              << entry.updatedAt << " │\n";
    if (showPassword) {
        std::cout << "  │ Password: " << std::left << std::setw(28)
                  << decryptedPassword << " │\n";
    } else {
        std::cout << "  │ Password: " << std::left << std::setw(28)
                  << "******** (hidden)" << " │\n";
    }
    std::cout << "  └────────────────────────────────────────┘\n";
}

void CLI::cmdShow() {
    std::string search = readLine("  Search: ");
    if (search.empty()) return;

    auto results = db_.searchPasswords(search);
    if (results.empty()) {
        std::cout << "[NOT FOUND] No matches for '" << search << "'.\n";
        return;
    }

    const PasswordEntry* selected = nullptr;
    std::string currentSearch = search;

    while (!selected) {
        if (results.size() == 1) {
            selected = &results[0];
            break;
        }

        std::cout << "\n  " << results.size() << " matches:\n\n";
        for (size_t i = 0; i < results.size(); ++i) {
            std::cout << "  " << std::right << std::setw(3) << (i + 1) << ". "
                      << std::left << std::setw(22) << results[i].name;
            if (!results[i].account.empty()) {
                std::cout << "(" << results[i].account << ")";
            }
            std::cout << "\n";
        }

        std::cout << "\n  Select number, or type to narrow search";
        std::cout << " [" << currentSearch << "]: " << std::flush;

        std::string input;
        std::getline(std::cin, input);

        if (input.empty()) return;

        int num = 0;
        bool isNum = true;
        try {
            num = std::stoi(input);
        } catch (...) {
            isNum = false;
        }

        if (isNum && num >= 1 && static_cast<size_t>(num) <= results.size()) {
            selected = &results[static_cast<size_t>(num - 1)];
            break;
        }

        currentSearch += input;
        results = db_.searchPasswords(currentSearch);
        if (results.empty()) {
            std::cout << "[NOT FOUND] No matches for '" << currentSearch << "'.\n";
            currentSearch = search;
            results = db_.searchPasswords(search);
        }
    }

    std::string decrypted;
    try {
        decrypted = Crypto::decrypt(aesKey_, selected->passwordEncrypted);
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Decrypt failed: " << e.what() << std::endl;
        std::cerr << "The key file may have changed.\n";
        return;
    }

    std::string showChoice = readLine("\n  Show password? (y/N): ");
    bool showPassword = (showChoice == "y" || showChoice == "Y");

    printPasswordDetail(*selected, decrypted, showPassword);

    if (copyToClipboard(decrypted)) {
        std::cout << "[OK] Password copied to clipboard.\n";
    } else {
        std::cout << "[WARN] Clipboard not available.\n";
    }
}

void CLI::cmdList() {
    doList();
}

void CLI::cmdEdit() {
    std::string name = readLine("  Name: ");
    if (name.empty()) {
        std::cout << "[ERROR] Name is required.\n";
        return;
    }

    auto entry = db_.getPassword(name);
    if (!entry.has_value()) {
        std::cout << "[ERROR] No password found: '" << name << "'.\n";
        return;
    }

    std::string stored;
    try {
        stored = Crypto::decrypt(aesKey_, entry->passwordEncrypted);
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Decryption failed: " << e.what() << std::endl;
        return;
    }

    std::string verify = readPassword("  Verify original password: ");
    if (verify != stored) {
        std::cout << "[ERROR] Password verification failed. Edit aborted.\n";
        return;
    }

    std::cout << "  (leave blank to keep current value)\n";

    std::string newDesc;
    std::cout << "  Description [" << entry->description << "]: " << std::flush;
    std::getline(std::cin, newDesc);
    if (newDesc.empty()) newDesc = entry->description;

    std::string newAccount;
    std::cout << "  Account [" << entry->account << "]: " << std::flush;
    std::getline(std::cin, newAccount);
    if (newAccount.empty()) newAccount = entry->account;

    std::string newPassword;
    std::string changePwd;
    std::cout << "  Change password? (y/N): " << std::flush;
    std::getline(std::cin, changePwd);
    if (changePwd == "y" || changePwd == "Y") {
        std::string confirm;
        while (inputAvailable()) {
            getPasswordInput("  New password: ", newPassword);
            if (newPassword.empty()) {
                std::cout << "[ERROR] Password cannot be empty. Keeping original.\n";
                newPassword.clear();
                break;
            }
            getPasswordInput("  Confirm:      ", confirm);
            if (newPassword == confirm) break;
            std::cout << "[ERROR] Passwords do not match.\n\n";
        }
    }

    std::string encrypted;
    if (!newPassword.empty()) {
        try {
            encrypted = Crypto::encrypt(aesKey_, newPassword);
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] Encryption failed: " << e.what() << std::endl;
            return;
        }
    } else {
        encrypted = entry->passwordEncrypted;
    }

    if (db_.updatePassword(name, newDesc, newAccount, encrypted)) {
        std::cout << "[OK] '" << name << "' updated.\n";
    } else {
        std::cout << "[ERROR] Failed to update password.\n";
    }
}

void CLI::cmdKey() {
    std::cout << "  Current key: " << keyPath_ << "\n\n";

    std::string newPath;
    std::cout << "  New key path (empty to cancel): " << std::flush;
    std::getline(std::cin, newPath);
    if (newPath.empty()) {
        std::cout << "  Key unchanged.\n";
        return;
    }

    struct stat st;
    if (stat(newPath.c_str(), &st) != 0) {
        std::string create;
        std::cout << "  Key file not found. Create? (y/N): " << std::flush;
        std::getline(std::cin, create);
        if (create == "y" || create == "Y") {
            if (Crypto::generateKeyFile(newPath)) {
                std::cout << "[OK] Key created at: " << newPath << "\n";
            } else {
                std::cout << "[ERROR] Failed to create key file.\n";
                return;
            }
        } else {
            std::cout << "  Key unchanged.\n";
            return;
        }
    }

    if (loadKeyFile(newPath)) {
        std::cout << "[OK] Key changed.\n";
    } else {
        std::cerr << "[ERROR] Failed to load new key. Falling back.\n";
        loadKeyFile(keyPath_);
    }
}
