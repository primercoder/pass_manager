#include "cli.h"
#include "database.h"
#include "crypto.h"

#include <iostream>
#include <string>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <cstdlib>

static std::string getDefaultDir() {
    const char* home = getenv("HOME");
    if (!home) {
        home = ".";
    }
    return std::string(home) + "/.passmanager";
}

static void printUsage(const char* prog) {
    std::cout << "Password Manager v1.1.0\n\n"
              << "Usage: " << prog << " [OPTIONS] [OPERATION]\n\n"
              << "Options:\n"
              << "  -k, --key <path>        Key file path\n"
              << "                           (default: ~/.passmanager/default.key)\n"
              << "  -d, --db <path>         Database file path\n"
              << "                           (default: ~/.passmanager/passwords.db)\n"
              << "  -h, --help              Show this help message\n"
              << "\n"
              << "Operations (non-TUI):\n"
              << "  -a, --add               Add a new password\n"
              << "  -r, --remove            Remove a password\n"
              << "  -s, --show              Show password(s)\n"
              << "  -l, --list              List all passwords\n"
              << "  -c, --count             Show password count\n"
              << "  -e, --edit              Edit a password\n"
              << "\n"
              << "Operation parameters:\n"
              << "  --name <name>           Password name/key\n"
              << "  --desc <desc>           Description\n"
              << "  --account <acct>        Account name\n"
              << "  --password <pwd>        Password (visible in shell history!)\n"
              << "  --password-stdin        Read password from stdin (secure)\n"
              << "  --verify <pwd>          Password for verification (remove/edit)\n"
              << "  --verify-stdin          Read verify password from stdin\n"
              << "  --new-desc <desc>       New description (for --edit)\n"
              << "  --new-account <acct>    New account (for --edit)\n"
              << "  --new-password <pwd>    New password (for --edit)\n"
              << "  --new-password-stdin    Read new password from stdin (--edit)\n"
              << "  --reveal                Show password in plaintext (with --show)\n"
              << "\n"
              << "Examples:\n"
              << "  " << prog << "\n"
              << "  " << prog << " --add --name github --desc \"GitHub\""
              <<            " --account user --password-stdin\n"
              << "  " << prog << " --show --name git --reveal\n"
              << "  " << prog << " --remove --name github --verify-stdin\n"
              << "  " << prog << " --edit --name github --verify-stdin --new-desc \"Work GitHub\"\n"
              << std::endl;
}

struct Args {
    std::string keyPath;
    std::string dbPath;
    std::string operation;

    std::string name;
    std::string desc;
    std::string account;
    std::string password;
    std::string verify;
    std::string newDesc;
    std::string newAccount;
    std::string newPassword;

    bool reveal = false;
    bool passwordStdin = false;
    bool verifyStdin = false;
    bool newPasswordStdin = false;
    bool showHelp = false;
};

static bool parseArgs(int argc, char* argv[], Args& args) {
    std::string defaultDir = getDefaultDir();
    args.keyPath = defaultDir + "/default.key";
    args.dbPath = defaultDir + "/passwords.db";

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];

        if (a == "-h" || a == "--help") {
            args.showHelp = true;
            return true;
        } else if (a == "-k" || a == "--key") {
            if (i + 1 < argc) args.keyPath = argv[++i];
            else { std::cerr << "Error: -k requires a path.\n"; return false; }
        } else if (a == "-d" || a == "--db") {
            if (i + 1 < argc) args.dbPath = argv[++i];
            else { std::cerr << "Error: -d requires a path.\n"; return false; }
        } else if (a == "-a" || a == "--add") {
            args.operation = "add";
        } else if (a == "-r" || a == "--remove") {
            args.operation = "remove";
        } else if (a == "-s" || a == "--show") {
            args.operation = "show";
        } else if (a == "-l" || a == "--list") {
            args.operation = "list";
        } else if (a == "-c" || a == "--count") {
            args.operation = "count";
        } else if (a == "-e" || a == "--edit") {
            args.operation = "edit";

        } else if (a == "--name") {
            if (i + 1 < argc) args.name = argv[++i];
            else { std::cerr << "Error: --name requires a value.\n"; return false; }
        } else if (a == "--desc") {
            if (i + 1 < argc) args.desc = argv[++i];
            else { std::cerr << "Error: --desc requires a value.\n"; return false; }
        } else if (a == "--account") {
            if (i + 1 < argc) args.account = argv[++i];
            else { std::cerr << "Error: --account requires a value.\n"; return false; }
        } else if (a == "--password") {
            if (i + 1 < argc) args.password = argv[++i];
            else { std::cerr << "Error: --password requires a value.\n"; return false; }
        } else if (a == "--password-stdin") {
            args.passwordStdin = true;
        } else if (a == "--verify") {
            if (i + 1 < argc) args.verify = argv[++i];
            else { std::cerr << "Error: --verify requires a value.\n"; return false; }
        } else if (a == "--verify-stdin") {
            args.verifyStdin = true;
        } else if (a == "--new-desc") {
            if (i + 1 < argc) args.newDesc = argv[++i];
            else { std::cerr << "Error: --new-desc requires a value.\n"; return false; }
        } else if (a == "--new-account") {
            if (i + 1 < argc) args.newAccount = argv[++i];
            else { std::cerr << "Error: --new-account requires a value.\n"; return false; }
        } else if (a == "--new-password") {
            if (i + 1 < argc) args.newPassword = argv[++i];
            else { std::cerr << "Error: --new-password requires a value.\n"; return false; }
        } else if (a == "--new-password-stdin") {
            args.newPasswordStdin = true;
        } else if (a == "--reveal") {
            args.reveal = true;
        } else {
            std::cerr << "Error: Unknown option: " << a << "\n";
            return false;
        }
    }

    return true;
}

static bool ensureKeyFile(std::string& keyPath) {
    struct stat st;
    if (stat(keyPath.c_str(), &st) != 0) {
        std::string keyDir = keyPath;
        size_t pos = keyDir.find_last_of('/');
        if (pos != std::string::npos) {
            keyDir = keyDir.substr(0, pos);
            mkdir(keyDir.c_str(), 0755);
        }

        std::cerr << "Key file not found. Generating a new one at: "
                  << keyPath << std::endl;
        if (!Crypto::generateKeyFile(keyPath)) {
            std::cerr << "Error: Failed to generate key file.\n";
            return false;
        }
        std::cerr << "Key file generated successfully.\n";
        std::cerr << "KEEP THIS FILE SAFE! Without it, passwords cannot be decrypted.\n\n";
    } else {
        mode_t mode = st.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO);
        if (mode != (S_IRUSR | S_IWUSR)) {
            std::cerr << "Warning: Key file has insecure permissions. Fixing...\n";
            chmod(keyPath.c_str(), S_IRUSR | S_IWUSR);
        }
    }
    return true;
}

int main(int argc, char* argv[]) {
    Args args;
    if (!parseArgs(argc, argv, args)) {
        printUsage(argv[0]);
        return 1;
    }

    if (args.showHelp) {
        printUsage(argv[0]);
        return 0;
    }

    std::string defaultDir = getDefaultDir();
    mkdir(defaultDir.c_str(), 0700);

    if (!ensureKeyFile(args.keyPath)) {
        return 1;
    }

    Database db;
    if (!db.open(args.dbPath)) {
        std::cerr << "Error: Failed to open database at: " << args.dbPath << "\n";
        return 1;
    }

    CLI cli(db, args.keyPath);

    if (!cli.loadKeyFile()) {
        db.close();
        return 1;
    }

    if (args.operation.empty()) {
        cli.run();
    } else {
        if (args.passwordStdin) {
            std::cerr << "Enter password: ";
            std::getline(std::cin, args.password);
        }
        if (args.verifyStdin) {
            std::cerr << "Enter verify password: ";
            std::getline(std::cin, args.verify);
        }
        if (args.newPasswordStdin) {
            std::cerr << "Enter new password: ";
            std::getline(std::cin, args.newPassword);
        }

        bool success = false;

        if (args.operation == "add") {
            success = cli.doAdd(args.name, args.desc, args.account, args.password);
        } else if (args.operation == "remove") {
            success = cli.doRemove(args.name, args.verify);
        } else if (args.operation == "show") {
            success = cli.doShow(args.name, args.reveal);
        } else if (args.operation == "list") {
            success = cli.doList();
        } else if (args.operation == "count") {
            success = cli.doCount();
        } else if (args.operation == "edit") {
            success = cli.doEdit(args.name, args.verify,
                                 args.newDesc, args.newAccount, args.newPassword);
        } else {
            std::cerr << "Error: Unknown operation: " << args.operation << "\n";
        }

        db.close();

        if (!success) {
            return 1;
        }
    }

    db.close();

    return 0;
}
