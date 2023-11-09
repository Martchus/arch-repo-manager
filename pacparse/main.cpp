#include "../libpkg/parser/binary.h"
#include "../libpkg/parser/package.h"

#include "resources/config.h"

#include <c++utilities/application/argumentparser.h>
#include <c++utilities/conversion/stringbuilder.h>
#include <c++utilities/io/path.h>
#include <c++utilities/misc/math.h>
#include <c++utilities/misc/parseerror.h>

#include <reflective_rapidjson/json/reflector.h>

#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

using namespace CppUtilities;

int main(int argc, const char *argv[])
{
    SET_APPLICATION_INFO;

    // read cli args
    auto parser = ArgumentParser();
    auto packagesArg = Argument("packages", 'p', "specifies the paths of the package files to parse");
    packagesArg.setRequiredValueCount(Argument::varValueCount);
    packagesArg.setValueNames({ "path" });
    packagesArg.setImplicit(true);
    parser.setMainArguments({ &packagesArg, &parser.helpArg() });
    parser.setDefaultArgument(&parser.helpArg());
    parser.parseArgs(argc, argv);

    auto packages = std::vector<std::shared_ptr<LibPkg::Package>>();
    auto packageMutex = std::mutex();

    auto pi = std::vector<const char *>::const_iterator();
    auto pend = std::vector<const char *>::const_iterator();
    auto piMutex = std::mutex();
    if (packagesArg.isPresent()) {
        const auto &packagePaths = packagesArg.values();
        pi = packagePaths.begin();
        pend = packagePaths.end();
        packages.reserve(packagePaths.size());
    }

    const auto processPackage = [&] {
        for (;;) {
            // get next package path
            auto piLock = std::unique_lock<std::mutex>(piMutex);
            if (pi == pend) {
                return;
            }
            auto packagePath = *(pi++);
            piLock.unlock();

            // parse package
            try {
                auto package = LibPkg::Package::fromPkgFile(packagePath);
                auto packageLock = std::unique_lock<std::mutex>(packageMutex);
                packages.emplace_back(std::move(package));
            } catch (const std::runtime_error &e) {
                std::cerr << "Unable to parse \"" << packagePath << "\": " << e.what() << '\n';
            }
        }
    };

    auto threads
        = std::vector<std::thread>(std::max<std::size_t>(std::min<std::size_t>(std::thread::hardware_concurrency(), packages.capacity()), 1u) - 1);
    for (auto &t : threads) {
        t = std::thread(processPackage);
    }
    processPackage();
    for (auto &t : threads) {
        t.join();
    }

    const auto json = ReflectiveRapidJSON::JsonReflector::toJson(packages);
    std::cout << std::string_view(json.GetString(), json.GetSize());
    return 0;
}
