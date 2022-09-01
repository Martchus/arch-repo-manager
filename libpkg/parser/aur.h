#ifndef LIBPKG_PARSER_AUR_H
#define LIBPKG_PARSER_AUR_H

#include "../global.h"

#include <reflective_rapidjson/json/serializable.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace LibPkg {

struct LIBPKG_EXPORT AurRpcResult : public ReflectiveRapidJSON::JsonSerializable<AurRpcResult> {
    std::int64_t ID;
    std::string Name;
    std::int64_t PackageBaseID;
    std::string PackageBase;
    std::string Version;
    std::string Description;
    std::string URL;
    std::int64_t NumVotes;
    double Popularity = 0.0;
    std::unique_ptr<std::int64_t> OutOfDate;
    std::string Maintainer;
    std::int64_t FirstSubmitted = 0;
    std::int64_t LastModified = 0;
    std::string URLPath;
    std::vector<std::string> Conflicts;
    std::vector<std::string> Provides;
    std::vector<std::string> Replaces;
    std::vector<std::string> Depends;
    std::vector<std::string> MakeDepends;
    std::vector<std::string> CheckDepends;
    std::vector<std::string> OptDepends;
    std::vector<std::string> License;
    std::vector<std::string> Groups;
    std::vector<std::string> Keywords;
};

struct LIBPKG_EXPORT AurRpcMultiInfo : public ReflectiveRapidJSON::JsonSerializable<AurRpcMultiInfo> {
    std::int64_t version = -1;
    std::int64_t resultcount = -1;
    std::string type;
    std::vector<AurRpcResult> results;
};

} // namespace LibPkg

#endif // LIBPKG_PARSER_CONFIG_H
