#include "../librepomgr/serversetup.h"

#include "../libpkg/data/config.h"

#include "resources/config.h"

#include <c++utilities/application/argumentparser.h>

#include <filesystem>

using namespace CppUtilities;
using namespace LibRepoMgr;
using namespace LibPkg;

int main(int argc, const char *argv[])
{
    SET_APPLICATION_INFO;

    // define default server setup
    auto exitCode = 0;
    auto setup = ServiceSetup();

    // read cli args
    ArgumentParser parser;
    OperationArgument runArg("run", 'r', "runs the server");
    ConfigValueArgument configFileArg("config-file", 'c', "specifies the path of the config file", { "path" });
    configFileArg.setEnvironmentVariable(PROJECT_VARNAME_UPPER "_CONFIG_FILE");
    configFileArg.setRequiredValueCount(Argument::varValueCount);
    ConfigValueArgument forceLoadingDBsArg("force-loading-dbs", 'f', "forces loading DBs, even if DB files have not been modified since last parse");
    runArg.setSubArguments({ &configFileArg, &forceLoadingDBsArg });
    runArg.setImplicit(true);
    const auto assignConfigFiles = [&configFileArg, &setup]() {
        if (configFileArg.isPresent()) {
            const auto &values = configFileArg.values();
            setup.configFilePaths.reserve(values.size());
            for (const auto &value : values) {
                try {
                    const auto path = std::filesystem::path(value);
                    if (std::filesystem::is_directory(path)) {
                        for (auto it = std::filesystem::directory_iterator(path, std::filesystem::directory_options::follow_directory_symlink | std::filesystem::directory_options::skip_permission_denied); const auto &entry : it) {
                            if (!entry.is_directory()) {
                                setup.configFilePaths.emplace_back(std::filesystem::absolute(entry.path()));
                            }
                        }
                    } else {
                        setup.configFilePaths.emplace_back(std::filesystem::absolute(path));
                    }
                } catch (const std::filesystem::filesystem_error &e) {
                    std::cerr << EscapeCodes::Phrases::ErrorMessage << "Unable to locate config file \"" << value << "\": " << e.what() << EscapeCodes::Phrases::End;
                }
            }
        }
    };
    runArg.setCallback([&setup, &exitCode, &assignConfigFiles, &forceLoadingDBsArg](const ArgumentOccurrence &) {
        assignConfigFiles();
        setup.building.forceLoadingDbs = forceLoadingDBsArg.isPresent();
        exitCode = setup.run();
    });
    OperationArgument fixDb("fix-db", '\0', "fixes the database files");
    fixDb.setSubArguments({ &configFileArg });
    fixDb.setCallback([&setup, &exitCode, &assignConfigFiles](const ArgumentOccurrence &) {
        assignConfigFiles();
        exitCode = setup.fixDb();
    });
    OperationArgument dumpDb("dump-db", '\0', "dumps package database entries");
    ConfigValueArgument filterRegexArg("filter-regex", 'r', "dump only packages which name matches the specified regex", { "regex" });
    dumpDb.setSubArguments({ &filterRegexArg, &configFileArg });
    dumpDb.setCallback([&setup, &exitCode, &filterRegexArg, &assignConfigFiles](const ArgumentOccurrence &) {
        assignConfigFiles();
        exitCode = setup.dumpDb(filterRegexArg.isPresent() ? std::string_view(filterRegexArg.firstValue()) : std::string_view());
    });
    HelpArgument helpArg(parser);
    NoColorArgument noColorArg;
    parser.setMainArguments({ &runArg, &fixDb, &dumpDb, &noColorArg, &helpArg });
    parser.setDefaultArgument(&runArg);
    parser.parseArgs(argc, argv);
    return exitCode;
}
