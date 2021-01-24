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

using namespace std;
using namespace LibPkg;
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
    ConfigValueArgument fileNameArg("file-name", 'f', "specifies the file name to seach for", { "name" });
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
    listArg.setSubArguments({ &packageArg, &dbFileArg, &loadPacmanConfigArg });
    parser.setMainArguments({ &searchArg, &listArg, &helpArg });
    parser.setDefaultArgument(&helpArg);
    parser.parseArgs(argc, argv);

    // init config from pacman config to get relevant dbs
    Config cfg;
    if (loadPacmanConfigArg.isPresent()) {
        try {
            cfg.loadPacmanConfig("/etc/pacman.conf");
        } catch (const runtime_error &e) {
            cerr << "Unable to load pacman config." << endl;
            exit(1);
        }
    }

    // allow adding custom db paths
    if (dbFileArg.isPresent()) {
        for (const char *dbPath : dbFileArg.values(0)) {
            Database &db = cfg.databases.emplace_back(string(), dbPath);
            db.name = std::filesystem::path(db.path).stem().string();
            db.localPkgDir = directory(db.path);
        }
    }

    if (cfg.databases.empty()) {
        cerr << "No databases configured." << endl;
        exit(2);
    }

    // load all packages for the dbs
    for (Database &db : cfg.databases) {
        try {
            if (endsWith(db.path, ".files")) {
                db.filesPath = db.path;
            } else {
                db.filesPath = db.filesPathFromRegularPath();
            }
            db.loadPackages(true);
        } catch (const runtime_error &e) {
            cerr << "Unable to load database \"" << db.name << "\": " << e.what() << '\n';
        }
    }

    // print file list for certain package
    if (packageArg.isPresent()) {
        const auto pkgs = cfg.findPackages(packageArg.firstValue());
        for (const auto &pkg : pkgs) {
            if (const auto *const db = std::get<Database *>(pkg.db); !db->name.empty()) {
                cout << db->name << '/';
            }
            cout << pkg.pkg->name;
            if (!pkg.pkg->packageInfo) {
                cout << '\n';
                continue;
            }
            for (const auto &path : pkg.pkg->packageInfo->files) {
                cout << "\n - " << path;
            }
            cout << '\n';
        }
        return 0;
    }

    // search databases for relevant packages
    const char *const searchTerm = fileNameArg.firstValue();
    const auto negate = negateArg.isPresent();
    auto regex = std::optional<std::regex>();
    if (regexArg.isPresent()) {
        regex = std::regex(searchTerm, std::regex::egrep);
    }
    for (const Database &db : cfg.databases) {
        for (const auto &pkg : db.packages) {
            const auto &pkgInfo = pkg.second->packageInfo;
            if (!pkgInfo) {
                continue;
            }
            auto foundOne = false;
            for (const string &file : pkgInfo->files) {
                const auto found = regex.has_value() ? std::regex_match(file, regex.value()) : file.find(searchTerm) != string::npos;
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
                        cout << db.name << '/';
                    }
                    cout << pkg.first << '\n';
                    foundOne = true;
                }
                cout << " - " << file << '\n';
            }
            if (negate && !foundOne) {
                if (!db.name.empty()) {
                    cout << db.name << '/';
                }
                cout << pkg.first << '\n';
            }
        }
    }

    return 0;
}
