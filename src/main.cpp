#include "cli.h"
#include "database.h"
#include "crypto.h"

#include <iostream>
#include <string>
#include <cstring>
#include <sys/stat.h>
#include <libgen.h>
#include <unistd.h>
#include <limits.h>

static void printUsage(const char* prog) {
    std::cout << "Password Manager v1.0.0\n\n"
              << "Usage: " << prog << " [OPTIONS]\n\n"
              << "Options:\n"
              << "  -k, --key <path>    Path to key file\n"
              << "                       (default: ./key/default.key)\n"
              << "  -d, --db <path>     Path to database file\n"
              << "                       (default: ./passwords.db)\n"
              << "  -h, --help          Show this help message\n"
              << std::endl;
}

int main(int argc, char* argv[]) {
    std::string defaultKeyPath = "./key/default.key";
    std::string defaultDbPath = "./passwords.db";

    std::string keyPath = defaultKeyPath;
    std::string dbPath = defaultDbPath;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "-k" || arg == "--key") {
            if (i + 1 < argc) {
                keyPath = argv[++i];
            } else {
                std::cerr << "Error: -k/--key requires a path argument.\n";
                return 1;
            }
        } else if (arg == "-d" || arg == "--db") {
            if (i + 1 < argc) {
                dbPath = argv[++i];
            } else {
                std::cerr << "Error: -d/--db requires a path argument.\n";
                return 1;
            }
        } else {
            std::cerr << "Error: Unknown option: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    struct stat st;
    if (stat(keyPath.c_str(), &st) != 0) {
        std::string keyDir = keyPath;
        size_t pos = keyDir.find_last_of('/');
        if (pos != std::string::npos) {
            keyDir = keyDir.substr(0, pos);
            mkdir(keyDir.c_str(), 0755);
        }

        std::cout << "Key file not found. Generating a new one at: "
                  << keyPath << std::endl;
        if (!Crypto::generateKeyFile(keyPath)) {
            std::cerr << "Error: Failed to generate key file.\n";
            return 1;
        }
        std::cout << "Key file generated successfully.\n";
        std::cout << "KEEP THIS FILE SAFE! Without it, passwords cannot be decrypted.\n\n";
    } else {
        mode_t mode = st.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO);
        if (mode != (S_IRUSR | S_IWUSR)) {
            std::cout << "Warning: Key file has insecure permissions ("
                      << std::oct << mode << "). "
                      << "Restricting to owner read/write only.\n";
            chmod(keyPath.c_str(), S_IRUSR | S_IWUSR);
        }
    }

    Database db;
    if (!db.open(dbPath)) {
        std::cerr << "Error: Failed to open database at: " << dbPath << "\n";
        return 1;
    }

    CLI cli(db, keyPath);

    cli.run();

    db.close();

    return 0;
}
