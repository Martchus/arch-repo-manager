#include "../libpkg/data/config.h"

#include "resources/config.h"

#include <c++utilities/application/argumentparser.h>
#include <c++utilities/conversion/stringbuilder.h>
#include <c++utilities/io/path.h>
#include <c++utilities/misc/parseerror.h>

#include <filesystem>
#include <iostream>
#include <optional>
#include <regex>

using namespace CppUtilities;

int main(int argc, const char *argv[])
{
    SET_APPLICATION_INFO;

    // read cli args
    ArgumentParser parser;
    Argument dbFileArg("db-files", 'd', "specifies the repository database files");
    dbFileArg.setRequiredValueCount(Argument::varValueCount);
    dbFileArg.setValueNames({ "path" });
    Argument loadPacmanConfigArg("pacman-config", 'c', "specifies whether to load all databases from pacman config");
    loadPacmanConfigArg.setCombinable(true);
    ConfigValueArgument pacmanConfigPathArg("pacman-config-path", '\0', "specifies the path of the pacman config file", { "path" });
    loadPacmanConfigArg.setSubArguments({ &pacmanConfigPathArg });
    ConfigValueArgument fileNameArg("file-name", 'f', "specifies the file name to search for", { "name" });
    fileNameArg.setImplicit(true);
    fileNameArg.setRequired(true);
    Argument regexArg("regex", 'r', "use regex to match the file name (--file-name is considered a regex)");
    regexArg.setCombinable(true);
    Argument negateArg("negate", 'n', "lists only packages which do NOT contain --file-name");
    negateArg.setCombinable(true);
    OperationArgument searchArg("search", '\0', "searches for packages containing the specified file");
    searchArg.setImplicit(true);
    searchArg.setSubArguments({ &fileNameArg, &regexArg, &negateArg, &dbFileArg, &loadPacmanConfigArg });
    HelpArgument helpArg(parser);
    OperationArgument listArg("list", '\0', "lists the files contained within the specified package");
    ConfigValueArgument packageArg("package", '\0', "the name of the package", { "name" });
    packageArg.setImplicit(true);
    packageArg.setRequired(true);
    listArg.setSubArguments({ &packageArg, &dbFileArg, &loadPacmanConfigArg });
    parser.setMainArguments({ &searchArg, &listArg, &helpArg });
    parser.setDefaultArgument(&helpArg);
    parser.parseArgs(argc, argv);

    // init config from pacman config to get relevant dbs
    auto cfg = LibPkg::Config();
    if (loadPacmanConfigArg.isPresent()) {
        try {
            cfg.loadPacmanConfig("/etc/pacman.conf");
        } catch (const std::runtime_error &e) {
            std::cerr << "Unable to load pacman config: " << e.what() << std::endl;
            std::exit(1);
        }
    }

    // allow adding custom db paths
    if (dbFileArg.isPresent()) {
        for (const char *const dbPath : dbFileArg.values()) {
            auto &db = cfg.databases.emplace_back(std::string(), dbPath);
            db.name = std::filesystem::path(db.path).stem().string();
            db.localPkgDir = directory(db.path);
        }
    }

    if (cfg.databases.empty()) {
        std::cerr << "No databases configured." << std::endl;
        std::exit(2);
    }

    // load all packages for the dbs
    auto ec = std::error_code();
    auto tmpDir = std::filesystem::temp_directory_path(ec);
    if (ec) {
        std::cerr << "Unable to locate temp directory path: " << ec.message() << std::endl;
        std::exit(4);
    }
    try {
        cfg.initStorage((tmpDir.string() + "/pacfind.db").data());
    } catch (const std::runtime_error &e) {
        std::cerr << "Unable to initialize temporary storage: " << e.what() << std::endl;
        std::exit(1);
    }
    for (auto &db : cfg.databases) {
        try {
            if (endsWith(db.path, ".files")) {
                db.filesPath = db.path;
            } else {
                db.filesPath = db.filesPathFromRegularPath();
            }
            db.loadPackagesFromConfiguredPaths(true, true);
        } catch (const std::runtime_error &e) {
            std::cerr << "Unable to load database \"" << db.name << "\": " << e.what() << '\n';
        }
    }

    // print file list for certain package
    if (listArg.isPresent()) {
        const auto pkgs = cfg.findPackages(packageArg.firstValue());
        for (const auto &pkg : pkgs) {
            if (const auto *const db = std::get<LibPkg::Database *>(pkg.db); !db->name.empty()) {
                std::cout << db->name << '/';
            }
            std::cout << pkg.pkg->name;
            if (!pkg.pkg->packageInfo) {
                std::cout << '\n';
                continue;
            }
            for (const auto &path : pkg.pkg->packageInfo->files) {
                std::cout << "\n - " << path;
            }
            std::cout << '\n';
        }
        return 0;
    }

    // search databases for relevant packages
    const char *const searchTerm = fileNameArg.firstValue();
    const auto negate = negateArg.isPresent();
    auto regex = std::optional<std::regex>();
    if (regexArg.isPresent()) {
        try {
            regex = std::regex(searchTerm, std::regex::egrep);
        } catch (const std::regex_error &e) {
            std::cerr << "Specified regex is invalid: " << e.what() << std::endl;
            std::exit(3);
        }
    }
    for (auto &db : cfg.databases) {
        db.allPackages([&](LibPkg::StorageID, const std::shared_ptr<LibPkg::Package> &package) {
            const auto &pkgInfo = package->packageInfo;
            if (!pkgInfo) {
                return false;
            }
            auto foundOne = false;
            for (const auto &file : pkgInfo->files) {
                const auto found = regex.has_value() ? std::regex_match(file, regex.value()) : file.find(searchTerm) != std::string::npos;
                if (negate) {
                    if (found) {
                        foundOne = true;
                        break;
                    } else {
                        continue;
                    }
                }
                if (!found) {
                    continue;
                }
                if (!foundOne) {
                    if (!db.name.empty()) {
                        std::cout << db.name << '/';
                    }
                    std::cout << package->name << '\n';
                    foundOne = true;
                }
                std::cout << " - " << file << '\n';
            }
            if (negate && !foundOne) {
                if (!db.name.empty()) {
                    std::cout << db.name << '/';
                }
                std::cout << package->name << '\n';
            }
            return false;
        });
    }

    return 0;
}
