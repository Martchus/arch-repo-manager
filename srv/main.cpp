#include "../librepomgr/serversetup.h"

#include "../libpkg/data/config.h"

#include "resources/config.h"

#include <c++utilities/application/argumentparser.h>
#include <c++utilities/io/ansiescapecodes.h>

#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace std;
using namespace CppUtilities;
using namespace CppUtilities::EscapeCodes;
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
    ConfigValueArgument forceLoadingDBsArg("force-loading-dbs", 'f', "forces loading DBs, even if DB files have not been modified since last parse");
    runArg.setSubArguments({ &configFileArg, &forceLoadingDBsArg });
    runArg.setImplicit(true);
    runArg.setCallback([&setup, &exitCode, &configFileArg, &forceLoadingDBsArg](const ArgumentOccurrence &) {
        if (const auto configFilePath = configFileArg.firstValue()) {
            setup.configFilePath = configFilePath;
        }
        setup.building.forceLoadingDbs = forceLoadingDBsArg.isPresent();
        exitCode = setup.run();
    });
    OperationArgument fixDb("fix-db", '\0', "fixes the database files");
    fixDb.setSubArguments({ &configFileArg });
    fixDb.setCallback([&setup, &exitCode, &configFileArg](const ArgumentOccurrence &) {
        if (const auto configFilePath = configFileArg.firstValue()) {
            setup.configFilePath = configFilePath;
        }
        exitCode = setup.fixDb();
    });
    HelpArgument helpArg(parser);
    NoColorArgument noColorArg;
    parser.setMainArguments({ &runArg, &fixDb, &noColorArg, &helpArg });
    parser.setDefaultArgument(&runArg);
    parser.parseArgs(argc, argv);
    return exitCode;
}
