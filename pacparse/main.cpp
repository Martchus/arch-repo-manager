#include "../libpkg/data/package.h"
#include "../libpkg/parser/binary.h"

#include "resources/config.h"

#include <c++utilities/application/argumentparser.h>
#include <c++utilities/conversion/stringbuilder.h>
#include <c++utilities/io/path.h>
#include <c++utilities/misc/math.h>
#include <c++utilities/misc/parseerror.h>

#include <reflective_rapidjson/json/reflector.h>

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace CppUtilities;

struct BinaryInfo : public ReflectiveRapidJSON::JsonSerializable<BinaryInfo> {
    std::string name;
    std::string architecture;
    std::set<std::string> symbols;
    std::set<std::string> requiredLibs;
    std::string rpath;
    std::string prefix;
    bool isBigEndian = false;
};

struct PacparseResults : public ReflectiveRapidJSON::JsonSerializable<PacparseResults> {
    std::vector<std::shared_ptr<LibPkg::Package>> packages;
    std::vector<BinaryInfo> binaries;
};

int main(int argc, const char *argv[])
{
    SET_APPLICATION_INFO;

    // read cli args
    auto parser = ArgumentParser();
    auto packagesArg = ConfigValueArgument("packages", 'p', "specifies the paths of the package files to parse", { "path" });
    packagesArg.setRequiredValueCount(Argument::varValueCount);
    packagesArg.setImplicit(true);
    auto binariesArg = ConfigValueArgument("binaries", 'b', "specifies the paths of the binaries", { "path" });
    binariesArg.setRequiredValueCount(Argument::varValueCount);
    parser.setMainArguments({ &packagesArg, &binariesArg, &parser.helpArg() });
    parser.setDefaultArgument(&parser.helpArg());
    parser.parseArgs(argc, argv);
    if (parser.helpArg().isPresent()) {
        return EXIT_SUCCESS;
    }

    auto res = PacparseResults();
    auto packageMutex = std::mutex();
    auto binaryMutex = std::mutex();
    auto returnCode = std::atomic<int>(EXIT_SUCCESS);

    auto pi = std::vector<const char *>::const_iterator();
    auto pend = std::vector<const char *>::const_iterator();
    auto bi = std::vector<const char *>::const_iterator();
    auto bend = std::vector<const char *>::const_iterator();
    auto imutex = std::mutex();
    if (packagesArg.isPresent()) {
        const auto &packagePaths = packagesArg.values();
        pi = packagePaths.begin();
        pend = packagePaths.end();
        res.packages.reserve(packagePaths.size());
    }
    if (binariesArg.isPresent()) {
        const auto &binaryPaths = binariesArg.values();
        bi = binaryPaths.begin();
        bend = binaryPaths.end();
        res.binaries.reserve(binaryPaths.size());
    }

    const auto processPackage = [&] {
        for (;;) {
            // get next package path
            const char *path = nullptr;
            auto isBinary = false;
            auto ilock = std::unique_lock<std::mutex>(imutex);
            if (pi != pend) {
                path = *pi++;
            } else if (bi != bend) {
                isBinary = true;
                path = *bi++;
            } else {
                return;
            }
            ilock.unlock();

            // parse package
            try {
                if (isBinary) {
                    auto binary = LibPkg::Binary();
                    binary.load(path);
                    auto binaryLock = std::unique_lock<std::mutex>(binaryMutex);
                    auto &binaryInfo = res.binaries.emplace_back();
                    binaryInfo.prefix = binary.addPrefix(std::string_view());
                    binaryInfo.name = std::move(binary.name);
                    binaryInfo.architecture = std::move(binary.architecture);
                    binaryInfo.isBigEndian = binary.isBigEndian;
                    binaryInfo.rpath = std::move(binary.rpath);
                    binaryInfo.symbols = std::move(binary.symbols);
                    binaryInfo.requiredLibs = std::move(binary.requiredLibs);
                } else {
                    auto package = LibPkg::Package::fromPkgFile(path);
                    auto binaryLock = std::unique_lock<std::mutex>(packageMutex);
                    res.packages.emplace_back(std::move(package));
                }
            } catch (const std::runtime_error &e) {
                std::cerr << "Unable to parse \"" << path << "\": " << e.what() << '\n';
                returnCode = EXIT_FAILURE;
            }
        }
    };

    auto threads = std::vector<std::thread>(
        std::max<std::size_t>(std::min<std::size_t>(std::thread::hardware_concurrency(), res.packages.capacity() + res.binaries.capacity()), 1u) - 1);
    for (auto &t : threads) {
        t = std::thread(processPackage);
    }
    processPackage();
    for (auto &t : threads) {
        t.join();
    }

    const auto json = ReflectiveRapidJSON::JsonReflector::toJson(res);
    std::cout << std::string_view(json.GetString(), json.GetSize());
    return returnCode;
}

#include "reflection/main.h"
