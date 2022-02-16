#include "./buildactionmeta.h"

#include "reflection/buildactionmeta.h"

namespace LibRepoMgr {

template <typename MapType, typename IteratableType, typename MemberPtr>
static MapType mapByName(const IteratableType &iteratable, MemberPtr memberPtr)
{
    MapType map;
    for (const auto &element : iteratable) {
        map.insert(std::pair(element.*memberPtr, std::cref(element)));
    }
    return map;
}

template <typename OutputIteratableType, typename InputIteratableType> static OutputIteratableType mapInto(const InputIteratableType &iteratable)
{
    OutputIteratableType mappings;
    mappings.reserve(iteratable.size());
    for (const auto &element : iteratable) {
        mappings.emplace_back(element);
    }
    return mappings;
}

BuildActionMetaInfo::BuildActionMetaInfo()
    : types({
        BuildActionTypeMetaInfo{
            .id = BuildActionType::Invalid,
            .name = "Invalid",
        },
        BuildActionTypeMetaInfo{
            .id = BuildActionType::RemovePackages,
            .category = "Repo management",
            .name = "Remove packages",
            .type = "remove-packages",
            .flags = {},
            .settings = {},
            .directory = true,
            .sourceDb = false,
            .destinationDb = true,
            .packageNames = true,
        },
        BuildActionTypeMetaInfo{
            .id = BuildActionType::MovePackages,
            .category = "Repo management",
            .name = "Move packages",
            .type = "move-packages",
            .flags = {
                BuildActionFlagMetaInfo{
                    .id = static_cast<BuildActionFlagType>(MovePackagesFlags::IgnoreExistingFiles),
                    .name = "Ignore existing files",
                    .desc = "Ignore copying errors caused by already existing files in the destination repository",
                    .param = "ignore-existing-files",
                },
            },
            .settings = {},
            .directory = true,
            .sourceDb = true,
            .destinationDb = true,
            .packageNames = true,
        },
        BuildActionTypeMetaInfo{
            .id = BuildActionType::CheckForUpdates,
            .category = "Repo management",
            .name = "Check for updates",
            .type = "check-updates",
            .flags = {
                BuildActionFlagMetaInfo{
                    .id = static_cast<BuildActionFlagType>(CheckForUpdatesFlags::ConsiderRegularPackage),
                    .name = "Consider regular package",
                    .desc = "When processing a variant package like mingw-w64-qt6-base compare with the regular qt6-base package as well",
                    .param = "consider-regular-package",
                },
            },
            .settings = {},
            .directory = false,
            .sourceDb = true,
            .destinationDb = true,
            .packageNames = false,
        },
        BuildActionTypeMetaInfo{
            .id = BuildActionType::ReloadDatabase,
            .category = "Repo management",
            .name = "Reload databases",
            .type = "reload-database",
            .flags = {
                BuildActionFlagMetaInfo{
                    .id = static_cast<BuildActionFlagType>(ReloadDatabaseFlags::ForceReload),
                    .name = "Force reload",
                    .desc = "Load the database even if its last modification data isn't newer than the last time the database was loaded",
                    .param = "force-reload",
                },
            },
            .settings = {},
            .directory = false,
            .sourceDb = false,
            .destinationDb = true,
            .packageNames = false,
        },
        BuildActionTypeMetaInfo{
            .id = BuildActionType::ReloadLibraryDependencies,
            .category = "Refresh data",
            .name = "Reload library dependencies",
            .type = "reload-library-dependencies",
            .flags = {
                BuildActionFlagMetaInfo{
                    .id = static_cast<BuildActionFlagType>(ReloadLibraryDependenciesFlags::ForceReload),
                    .name = "Force reload",
                    .desc = "Reload packages as well even though they have not changed on disk since the last reload",
                    .param = "force-reload",
                },
                BuildActionFlagMetaInfo{
                    .id = static_cast<BuildActionFlagType>(ReloadLibraryDependenciesFlags::SkipDependencies),
                    .name = "Skip dependencies",
                    .desc = "Do not take dependencies of the specified destination databases into account",
                    .param = "skip-dependencies",
                },
            },
            .settings = {
                BuildActionSettingMetaInfo{
                    .name = "Package exclude regex",
                    .desc = "Regular expression to match package names against; matching packages will be excluded.",
                    .param = "pkg-exclude-regex",
                },
            },
            .directory = false,
            .sourceDb = false,
            .destinationDb = true,
            .packageNames = false,
        },
        BuildActionTypeMetaInfo{
            .id = BuildActionType::PrepareBuild,
            .category = "Building",
            .name = "Prepare build",
            .type = "prepare-build",
            .flags = {
                BuildActionFlagMetaInfo{
                    .id = static_cast<BuildActionFlagType>(PrepareBuildFlags::ForceBumpPkgRel),
                    .name = "Force-bump pkgrel",
                    .desc = "Bump the pkgrel of the packages even if there is no existing version",
                    .param = "force-bump-pkgrel",
                },
                BuildActionFlagMetaInfo{
                    .id = static_cast<BuildActionFlagType>(PrepareBuildFlags::CleanSrcDir),
                    .name = "Clean source directory",
                    .desc = "Removes existing \"src\" sub-directories for the specified packages in the directory; use to update previously built packages",
                    .param = "clean-src-dir",
                },
                BuildActionFlagMetaInfo{
                    .id = static_cast<BuildActionFlagType>(PrepareBuildFlags::KeepOrder),
                    .name = "Keep dependency order",
                    .desc = "Build packages in the specified order",
                    .param = "keep-order",
                },
                BuildActionFlagMetaInfo{
                    .id = static_cast<BuildActionFlagType>(PrepareBuildFlags::KeepPkgRelAndEpoch),
                    .name = "Keep pkgrel/epoch",
                    .desc = "Never bumps pkgrel and epoch",
                    .param = "keep-pkgrel-and-epoch",
                },
                BuildActionFlagMetaInfo{
                    .id = static_cast<BuildActionFlagType>(PrepareBuildFlags::ResetChrootSettings),
                    .name = "Reset chroot settings",
                    .desc = "Resets chroot dir, chroot user and related flags",
                    .param = "reset-chroot-cfg",
                },
            },
            .settings = {
                BuildActionSettingMetaInfo{
                    .name = "PKGBUILDs directory",
                    .desc = "A colon separated list of PKGBUILDs directories to consider before checking the standard directories",
                    .param = "pkgbuilds-dir",
                },
            },
            .directory = true,
            .sourceDb = true,
            .destinationDb = true,
            .packageNames = true,
        },
        BuildActionTypeMetaInfo{
            .id = BuildActionType::ConductBuild,
            .category = "Building",
            .name = "Conduct build",
            .type = "conduct-build",
            .flags = {
                BuildActionFlagMetaInfo{
                    .id = static_cast<BuildActionFlagType>(ConductBuildFlags::BuildAsFarAsPossible),
                    .name = "Build as far as possible",
                    .desc = "By default the next batch is only considered when all packages in the previous batch succeeded; this option allows to build as far as possible instead",
                    .param = "build-as-far-as-possible",
                },
                BuildActionFlagMetaInfo{
                    .id = static_cast<BuildActionFlagType>(ConductBuildFlags::SaveChrootOfFailures),
                    .name = "Save chroot of failures",
                    .desc = "Renames the chroot working copy when a package failed to build so it will not be overridden by further builds and can be used for further investigation",
                    .param = "save-chroot-of-failures",
                },
                BuildActionFlagMetaInfo{
                    .id = static_cast<BuildActionFlagType>(ConductBuildFlags::UpdateChecksums),
                    .name = "Update checksums",
                    .desc = "Assumes that the checksums of the PKGBUILDs are outdated and will therefore update the checksums instead of using them for validation",
                    .param = "update-checksums",
                },
                BuildActionFlagMetaInfo{
                    .id = static_cast<BuildActionFlagType>(ConductBuildFlags::AutoStaging),
                    .name = "Auto-staging",
                    .desc = "Adds \"breaking\" packages only to the destination DB's staging repsitory and emits a rebuild list",
                    .param = "auto-staging",
                },
            },
            .settings = {
                BuildActionSettingMetaInfo{
                    .name = "Chroot directory",
                    .desc = "The chroot directory to use (instead of the globally configured one)",
                    .param = "chroot-dir",
                },
                BuildActionSettingMetaInfo{
                    .name = "Chroot default user",
                    .desc = "The default chroot user to use (instead of the globally configured one)",
                    .param = "chroot-dir",
                },
                BuildActionSettingMetaInfo{
                    .name = "CCache directory",
                    .desc = "The ccache directory to use (instead of the globally configured one)",
                    .param = "ccache-dir",
                },
                BuildActionSettingMetaInfo{
                    .name = "Package cache directory",
                    .desc = "The package cache directory to use (instead of the globally configured one)",
                    .param = "pkg-cache-dir",
                },
                BuildActionSettingMetaInfo{
                    .name = "Test files directory",
                    .desc = "The test files directory to use (instead of the globally configured one)",
                    .param = "test-files-dir",
                },
                BuildActionSettingMetaInfo{
                    .name = "GPG key to sign packages",
                    .desc = "The GPG key to sign packages (instead of the globally configured one)",
                    .param = "gpg-key",
                },
            },
            .directory = true,
            .sourceDb = false,
            .destinationDb = false,
            .packageNames = true,
            .implyPackagesFromPrevAction = true,
        },
        BuildActionTypeMetaInfo{
            .id = BuildActionType::MakeLicenseInfo,
            .category = "Misc",
            .name = "Make license info",
            .type = "make-license-info",
            .flags = {},
            .settings = {},
            .directory = false,
            .sourceDb = false,
            .destinationDb = false,
            .packageNames = true,
        },
        BuildActionTypeMetaInfo{
            .id = BuildActionType::ReloadConfiguration,
            .category = "Refresh data",
            .name = "Reload configuration",
            .type = "reload-configuration",
            .flags = {},
            .settings = {},
            .directory = false,
            .sourceDb = false,
            .destinationDb = false,
            .packageNames = false,
        },
        BuildActionTypeMetaInfo{
            .id = BuildActionType::CheckForProblems,
            .category = "Repo management",
            .name = "Check for problems",
            .type = "check-for-problems",
            .flags = {
                BuildActionFlagMetaInfo{
                    .id = static_cast<BuildActionFlagType>(CheckForProblemsFlags::RequirePackageSignatures),
                    .name = "Require package signatures",
                    .desc = "Checks whether package signatures are present",
                    .param = "require-pkg-signatures",
                },
            },
            .settings = {
                BuildActionSettingMetaInfo{
                    .name = "Dependencies to ignore",
                    .desc = "A white-space separated list of dependencies not to care about if missing (version constraints not supported)",
                    .param = "ignore-deps",
                },
                BuildActionSettingMetaInfo{
                    .name = "Libraries to ignore",
                    .desc = "A white-space separated list of library dependencies not to care about if missing",
                    .param = "ignore-libdeps",
                },
            },
            .directory = true,
            .sourceDb = false,
            .destinationDb = true,
            .packageNames = true,
        },
        BuildActionTypeMetaInfo{
            .id = BuildActionType::CleanRepository,
            .category = "Repo management",
            .name = "Clean repository",
            .type = "clean-repository",
            .flags = {
                BuildActionFlagMetaInfo{
                    .id = static_cast<BuildActionFlagType>(CleanRepositoryFlags::DryRun),
                    .name = "Dry run",
                    .desc = "Only record what would be done",
                    .param = "dry-run",
                },
            },
            .settings = {},
            .directory = true,
            .sourceDb = false,
            .destinationDb = true,
            .packageNames = true,
        },
        BuildActionTypeMetaInfo{
            .id = BuildActionType::DummyBuildAction,
            .category = "Misc",
            .name = "Dummy action for debugging",
            .type = "dummy",
            .flags = {},
            .settings = {},
            .directory = true,
            .sourceDb = false,
            .destinationDb = false,
            .packageNames = false,
        },
        BuildActionTypeMetaInfo{
            .id = BuildActionType::CustomCommand,
            .category = "Misc",
            .name = "Execute custom Bash command",
            .type = "custom-command",
            .flags = {},
            .settings = {
                BuildActionSettingMetaInfo{
                    .name = "Command",
                    .desc = "The command to execute via Bash",
                    .param = "cmd",
                },
                BuildActionSettingMetaInfo{
                    .name = "Shared locks",
                    .desc = "A comma-separated list of shared lock names to acquire",
                    .param = "shared-locks",
                },
                BuildActionSettingMetaInfo{
                    .name = "Unique locks",
                    .desc = "A comma-separated list of exclusive lock names to acquire",
                    .param = "exclusive-locks",
                },
            },
            .directory = true,
            .sourceDb = false,
            .destinationDb = false,
            .packageNames = false,
        },
    })
    , states({
        BuildActionStatusMetaInfo{
            .id = BuildActionStatus::Created,
            .name = "Created",
        },
        BuildActionStatusMetaInfo{
            .id = BuildActionStatus::Enqueued,
            .name = "Enqueued",
        },
        BuildActionStatusMetaInfo{
            .id = BuildActionStatus::AwaitingConfirmation,
            .name = "Awaiting confirmation",
        },
        BuildActionStatusMetaInfo{
            .id = BuildActionStatus::Running,
            .name = "Running",
        },
        BuildActionStatusMetaInfo{
            .id = BuildActionStatus::Finished,
            .name = "Finished",
        },
    })
    , results({
        BuildActionResultMetaInfo{
            .id = BuildActionResult::None,
            .name = "None",
        },
        BuildActionResultMetaInfo{
            .id = BuildActionResult::Success,
            .name = "Success",
        },
        BuildActionResultMetaInfo{
            .id = BuildActionResult::Failure,
            .name = "Failure",
        },
        BuildActionResultMetaInfo{
            .id = BuildActionResult::ConfirmationDeclined,
            .name = "ConfirmationDeclined",
        },
        BuildActionResultMetaInfo{
            .id = BuildActionResult::Aborted,
            .name = "Aborted",
        },
    })
    , m_typeInfoByName(mapByName<TypeInfoByName>(types, &BuildActionTypeMetaInfo::type))
    , m_mappings(mapInto<MetaMappingsForTypes>(types))
{
}

const BuildActionTypeMetaInfo &BuildActionMetaInfo::typeInfoForName(std::string_view name) const
{
    if (const auto i = m_typeInfoByName.find(name); i != m_typeInfoByName.end()) {
        return i->second;
    } else {
        return typeInfoForId(BuildActionType::Invalid);
    }
}

BuildActionTypeMetaMapping::BuildActionTypeMetaMapping(const BuildActionTypeMetaInfo &typeInfo)
    : flagInfoByName(mapByName<FlagMap>(typeInfo.flags, &BuildActionFlagMetaInfo::param))
    , settingInfoByName(mapByName<SettingMap>(typeInfo.settings, &BuildActionSettingMetaInfo::param))
{
}

} // namespace LibRepoMgr
