#ifndef LIBREPOMGR_BUILD_ACTION_META_H
#define LIBREPOMGR_BUILD_ACTION_META_H

#include "../global.h"

#include "../../libpkg/data/lockable.h"

#include <c++utilities/misc/flagenumclass.h>

#include <reflective_rapidjson/json/serializable.h>

#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace LibRepoMgr {

enum class BuildActionStatus : std::uint64_t {
    Created,
    Enqueued,
    AwaitingConfirmation,
    Running,
    Finished,
};

enum class BuildActionResult : std::uint64_t {
    None,
    Success,
    Failure,
    ConfirmationDeclined,
    Aborted,
};

enum class BuildActionType : std::uint64_t {
    Invalid,
    RemovePackages,
    MovePackages,
    CheckForUpdates,
    ReloadDatabase,
    ReloadLibraryDependencies,
    PrepareBuild,
    ConductBuild,
    MakeLicenseInfo,
    ReloadConfiguration,
    CheckForProblems,
    CleanRepository,
    DummyBuildAction,
    CustomCommand,
    LastType = CustomCommand,
};

using BuildActionFlagType = std::uint64_t;
constexpr BuildActionFlagType noBuildActionFlags = 0;
enum class MovePackagesFlags : BuildActionFlagType {
    None,
    IgnoreExistingFiles = (1 << 0),
};
enum class CheckForUpdatesFlags : BuildActionFlagType {
    None,
    ConsiderRegularPackage = (1 << 0), // be consistent with LibPkg::UpdateCheckOptions here
};
enum class ReloadDatabaseFlags : BuildActionFlagType {
    None,
    ForceReload = (1 << 0),
};
enum class ReloadLibraryDependenciesFlags : BuildActionFlagType {
    None,
    ForceReload = (1 << 0),
    SkipDependencies = (1 << 1),
};
enum class PrepareBuildFlags : BuildActionFlagType {
    None,
    ForceBumpPkgRel = (1 << 0),
    CleanSrcDir = (1 << 1),
    KeepOrder = (1 << 2),
    KeepPkgRelAndEpoch = (1 << 3),
    ResetChrootSettings = (1 << 4),
};
enum class ConductBuildFlags : BuildActionFlagType {
    None,
    BuildAsFarAsPossible = (1 << 0),
    SaveChrootOfFailures = (1 << 1),
    UpdateChecksums = (1 << 2),
    AutoStaging = (1 << 3),
    UseContainer = (1 << 4),
};
enum class CheckForProblemsFlags : BuildActionFlagType {
    None,
    RequirePackageSignatures = (1 << 0),
};
enum class CleanRepositoryFlags : BuildActionFlagType {
    None,
    DryRun = (1 << 0),
};
enum class ReloadLibraryDependenciesSettings : std::size_t { PackageExcludeRegex };
enum class CheckForProblemsSettings : std::size_t { IgnoreDeps, IgnoreLibDeps };
enum class PrepareBuildSettings : std::size_t { PKGBUILDsDirs };
enum class ConductBuildSettings : std::size_t { ChrootDir, ChrootDefaultUser, CCacheDir, PackageCacheDir, TestFilesDir, GpgKey };
enum class MakeLicenseInfoSettings : std::size_t { OutputFilePath };
enum class CustomCommandSettings : std::size_t { Command, SharedLocks, ExclusiveLocks };

struct LIBREPOMGR_EXPORT BuildActionFlagMetaInfo : public ReflectiveRapidJSON::JsonSerializable<BuildActionFlagMetaInfo> {
    const BuildActionFlagType id = 0;
    const std::string_view name;
    const std::string_view desc;
    const std::string_view param;
};
struct LIBREPOMGR_EXPORT BuildActionSettingMetaInfo : public ReflectiveRapidJSON::JsonSerializable<BuildActionSettingMetaInfo> {
    const std::string_view name;
    const std::string_view desc;
    const std::string_view param;
};
struct LIBREPOMGR_EXPORT BuildActionTypeMetaInfo : public ReflectiveRapidJSON::JsonSerializable<BuildActionTypeMetaInfo> {
    const BuildActionType id = BuildActionType::Invalid;
    const std::string_view category;
    const std::string_view name;
    const std::string_view type;
    const std::vector<BuildActionFlagMetaInfo> flags;
    const std::vector<BuildActionSettingMetaInfo> settings;
    const bool directory = true;
    const bool sourceDb = true;
    const bool destinationDb = true;
    const bool packageNames = true;
    const bool implyPackagesFromPrevAction = false;
};
struct LIBREPOMGR_EXPORT BuildActionStatusMetaInfo : public ReflectiveRapidJSON::JsonSerializable<BuildActionStatusMetaInfo> {
    const BuildActionStatus id = BuildActionStatus::Created;
    const std::string_view name;
};
struct LIBREPOMGR_EXPORT BuildActionResultMetaInfo : public ReflectiveRapidJSON::JsonSerializable<BuildActionResultMetaInfo> {
    const BuildActionResult id = BuildActionResult::None;
    const std::string_view name;
};
struct BuildActionMetaInfo;
struct LIBREPOMGR_EXPORT BuildActionTypeMetaMapping {
    using FlagMap = std::unordered_map<std::string_view, std::reference_wrapper<const BuildActionFlagMetaInfo>>;
    using SettingMap = std::unordered_map<std::string_view, std::reference_wrapper<const BuildActionSettingMetaInfo>>;

    explicit BuildActionTypeMetaMapping(const BuildActionTypeMetaInfo &typeInfo);

    const FlagMap flagInfoByName;
    const SettingMap settingInfoByName;
};
struct LIBREPOMGR_EXPORT BuildActionMetaInfo : public ReflectiveRapidJSON::JsonSerializable<BuildActionMetaInfo>, public LibPkg::Lockable {
    using TypeInfoByName = std::unordered_map<std::string_view, std::reference_wrapper<const BuildActionTypeMetaInfo>>;
    using MetaMappingsForTypes = std::vector<BuildActionTypeMetaMapping>;

    explicit BuildActionMetaInfo();
    const TypeInfoByName &typeInfoByName() const;
    const MetaMappingsForTypes &mappings() const;
    bool isTypeIdValid(BuildActionType id) const;
    const BuildActionTypeMetaInfo &typeInfoForId(BuildActionType id) const;
    const BuildActionTypeMetaInfo &typeInfoForName(std::string_view name) const;
    const BuildActionTypeMetaMapping &mappingForId(BuildActionType id) const;

    const std::vector<BuildActionTypeMetaInfo> types;
    const std::vector<BuildActionStatusMetaInfo> states;
    const std::vector<BuildActionResultMetaInfo> results;

private:
    const TypeInfoByName m_typeInfoByName;
    const MetaMappingsForTypes m_mappings;
};

inline const BuildActionMetaInfo::TypeInfoByName &BuildActionMetaInfo::typeInfoByName() const
{
    return m_typeInfoByName;
}

inline const BuildActionMetaInfo::MetaMappingsForTypes &BuildActionMetaInfo::mappings() const
{
    return m_mappings;
}

inline bool BuildActionMetaInfo::isTypeIdValid(BuildActionType id) const
{
    return static_cast<std::size_t>(id) < types.size();
}

inline const BuildActionTypeMetaInfo &BuildActionMetaInfo::typeInfoForId(BuildActionType id) const
{
    return types[static_cast<std::size_t>(id)];
}

inline const BuildActionTypeMetaMapping &BuildActionMetaInfo::mappingForId(BuildActionType id) const
{
    return m_mappings[static_cast<std::size_t>(id)];
}

} // namespace LibRepoMgr

CPP_UTILITIES_MARK_FLAG_ENUM_CLASS(LibRepoMgr, LibRepoMgr::MovePackagesFlags)
CPP_UTILITIES_MARK_FLAG_ENUM_CLASS(LibRepoMgr, LibRepoMgr::ReloadDatabaseFlags)
CPP_UTILITIES_MARK_FLAG_ENUM_CLASS(LibRepoMgr, LibRepoMgr::ReloadLibraryDependenciesFlags)
CPP_UTILITIES_MARK_FLAG_ENUM_CLASS(LibRepoMgr, LibRepoMgr::PrepareBuildFlags)
CPP_UTILITIES_MARK_FLAG_ENUM_CLASS(LibRepoMgr, LibRepoMgr::ConductBuildFlags)
CPP_UTILITIES_MARK_FLAG_ENUM_CLASS(LibRepoMgr, LibRepoMgr::CleanRepositoryFlags)
CPP_UTILITIES_MARK_FLAG_ENUM_CLASS(LibRepoMgr, LibRepoMgr::CheckForProblemsFlags)

#endif // LIBREPOMGR_BUILD_ACTION_META_H
