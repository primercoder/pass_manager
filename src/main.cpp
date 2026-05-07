#include "cli.h"
#include "database.h"
#include "crypto.h"

#include <iostream>
#include <string>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdlib>

static std::string getDefaultDir() {
    const char* home = getenv("HOME");
    if (!home) home = ".";
    return std::string(home) + "/.passmanager";
}

static void printUsage(const char* prog) {
    std::cout << "Password Manager v1.2.0\n\n"
              << "Usage: " << prog << " [OPTIONS] [OPERATION]\n\n"
              << "Options:\n"
              << "  -k, --key <path>        Key file path\n"
              << "                           (default: ~/.passmanager/default.key)\n"
              << "  -d, --db <path>         Database file path\n"
              << "                           (default: ~/.passmanager/passwords.db)\n"
              << "  -t, --tui               Enter interactive TUI mode\n"
              << "  -h, --help              Show this help message\n"
              << "\n"
              << "Operations:\n"
              << "  -a, --add               Add a new password\n"
              << "  -r, --remove            Remove a password\n"
              << "  -s, --show              Show password(s)\n"
              << "  -l, --list              List all passwords\n"
              << "  -e, --edit              Edit a password\n"
              << "\n"
              << "Parameters:\n"
              << "  -n, --name <name>       Password name/key\n"
              << "  --desc <desc>           Description\n"
              << "  --account <acct>        Account name\n"
              << "  -p, --password <pwd>    Password (visible in shell history!)\n"
              << "  --old-password <pwd>    Old password for --edit verification\n"
              << "  --reveal                Show password in plaintext (with --show)\n"
              << "\n"
              << "Missing required values will be prompted interactively.\n"
              << "\n"
              << "Examples:\n"
              << "  " << prog << " --tui\n"
              << "  " << prog << " --add -n github --desc \"GitHub\" --account user\n"
              << "  " << prog << " --show -n git --reveal\n"
              << "  " << prog << " --remove -n github\n"
              << "  " << prog << " --edit -n github --desc \"Work\"\n"
              << std::endl;
}

struct Args {
    std::string keyPath;
    std::string dbPath;

    bool tui = false;
    bool showHelp = false;

    std::string operation;

    std::string name;
    std::string desc;
    std::string account;
    std::string password;
    std::string oldPassword;

    bool reveal = false;
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
        } else if (a == "-t" || a == "--tui") {
            args.tui = true;
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
        } else if (a == "-e" || a == "--edit") {
            args.operation = "edit";

        } else if (a == "-n" || a == "--name") {
            if (i + 1 < argc) args.name = argv[++i];
            else { std::cerr << "Error: -n/--name requires a value.\n"; return false; }
        } else if (a == "--desc") {
            if (i + 1 < argc) args.desc = argv[++i];
            else { std::cerr << "Error: --desc requires a value.\n"; return false; }
        } else if (a == "--account") {
            if (i + 1 < argc) args.account = argv[++i];
            else { std::cerr << "Error: --account requires a value.\n"; return false; }
        } else if (a == "-p" || a == "--password") {
            if (i + 1 < argc) args.password = argv[++i];
            else { std::cerr << "Error: -p/--password requires a value.\n"; return false; }
        } else if (a == "--old-password") {
            if (i + 1 < argc) args.oldPassword = argv[++i];
            else { std::cerr << "Error: --old-password requires a value.\n"; return false; }
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

    if (!args.tui && args.operation.empty()) {
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

    if (args.tui || args.operation.empty()) {
        cli.run();
        db.close();
        return 0;
    }

    bool success = false;

    if (args.operation == "add") {
        success = cli.doAdd(args.name, args.desc, args.account, args.password);
    } else if (args.operation == "remove") {
        success = cli.doRemove(args.name, args.password);
    } else if (args.operation == "show") {
        success = cli.doShow(args.name, args.reveal);
    } else if (args.operation == "list") {
        success = cli.doList();
    } else if (args.operation == "edit") {
        success = cli.doEdit(args.name, args.oldPassword,
                             args.desc, args.account, args.password);
    }

    db.close();
    return success ? 0 : 1;
}
