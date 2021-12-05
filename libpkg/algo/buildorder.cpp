#include "../data/config.h"

using namespace std;

namespace LibPkg {

/// \cond
struct TopoSortItem {
    explicit TopoSortItem(PackageSearchResult &&package, bool onlyDependency)
        : package(package)
        , onlyDependency(onlyDependency)
        , finished(false)
    {
    }

    PackageSearchResult package;
    bool onlyDependency;
    bool finished;
};
/// \endcond

/*!
 * \brief Adds \a dependency and its dependencies in topological order to \a finishedItems.
 *
 * Also populates \a allItems in arbitrary order which owns the items. Unknown \a dependencies (no matching package found in any of the databases) are
 * added to \a ignored and ignored.
 *
 * The variable \a cycleTracking is supposed to be empty in the beginning and internally used to keep track of the current chain. In case a cycle is detected,
 * \a cycleTracking is set to contain the cyclic dependency path. On normal exit it just contains the most recently processed dependency chain (not really
 * interesting).
 *
 * \remarks Dependency cycles are ignored if there are already binary packages of the involved dependencies. It is assumed that in this case it is possible to
 *          install the existing binaries for building new ones (e.g. just use the current GCC to build the new one instead of taking additional bootstrapping
 *          effort). We also ignore cycles affecting only indirect dependencies as long as there are binaries for them available (e.g. to build a package requiring
 *          mesa we just install mesa and don't care about its cyclic dependency with libglvnd).
 * \return Returns true if \a dependency could be added to \a finishedItems, has already been present or has been ignored. Returns false if a cycle has been detected.
 * \deprecated This function can likely be removed. The build preparation task has its own implementation (to compute batches) which is more useful.
 */
bool Config::addDepsRecursivelyInTopoOrder(std::vector<std::unique_ptr<TopoSortItem>> &allItems, std::vector<TopoSortItem *> &finishedItems,
    std::vector<std::string> &ignored, std::vector<PackageSearchResult> &cycleTracking, const Dependency &dependency, BuildOrderOptions options,
    bool onlyDependency)
{
    // skip if dependency is already present
    for (const auto &item : finishedItems) {
        if (item->package.pkg->providesDependency(dependency)) {
            // ensure the already finished dependency is not treated as dependency
            if (!onlyDependency) {
                item->onlyDependency = false;
            }
            return true;
        }
    }

    // search for a package providing the specified dependency (just using the first matching package for now)
    auto packageSearchResult = findPackage(dependency);
    auto &pkg = packageSearchResult.pkg;

    // ignore the dependency if we can't find a package which provides it
    if (!pkg) {
        ignored.emplace_back(dependency.toString());
        return true;
    }

    // check whether a topo sort item for the current dependency is already pending to detect cycles
    for (const auto &item : allItems) {
        const auto &itemPackage = item->package;
        if (std::get<Database *>(packageSearchResult.db) != std::get<Database *>(itemPackage.db) || packageSearchResult.id != itemPackage.id) {
            continue;
        }

        // skip package if it is only a dependency and there's already a binary package
        if (onlyDependency && pkg->packageInfo) {
            return true;
        }

        // report error: remove so far "healthy" path in the current chain so it contains only the cyclic path anymore
        for (auto i = cycleTracking.begin(), end = cycleTracking.end(); i != end; ++i) {
            if (const auto &visited = *i;
                std::get<Database *>(packageSearchResult.db) == std::get<Database *>(visited.db) && packageSearchResult.id == visited.id) {
                cycleTracking.erase(cycleTracking.begin(), i);
                break;
            }
        }
        return false;
    }

    // add package to the "current chain" (used to comprehend the path in case a cycle occurred)
    cycleTracking.emplace_back(packageSearchResult);

    // add topo sort item for dependency
    allItems.emplace_back(make_unique<TopoSortItem>(move(packageSearchResult), onlyDependency));
    auto *const currentItem = allItems.back().get();

    // add the dependencies of this dependency first
    const auto addBuildDependencies = (options & BuildOrderOptions::ConsiderBuildDependencies) && pkg->sourceInfo;
    const auto *const runtimeDependencies = &pkg->dependencies;
    const auto *const makeDependencies = addBuildDependencies ? &pkg->sourceInfo->makeDependencies : nullptr;
    const auto *const checkDependencies = addBuildDependencies ? &pkg->sourceInfo->checkDependencies : nullptr;
    for (const auto *dependenciesOfDependency : { runtimeDependencies, makeDependencies, checkDependencies }) {
        if (!dependenciesOfDependency) {
            continue;
        }
        for (const auto &dependencyOfDependency : *dependenciesOfDependency) {
            // skip dependencies provided by the current package itself (FIXME: right now python 3.n depends on python<3.(n+1) which should be fixed)
            if (pkg->providesDependency(dependencyOfDependency)) {
                continue;
            }

            if (!addDepsRecursivelyInTopoOrder(allItems, finishedItems, ignored, cycleTracking, dependencyOfDependency, options, true)) {
                // skip if a cycle has been detected
                return false;
            }
        }
    }

    // remove the package from the "current chain" again
    cycleTracking.pop_back();

    // mark the current item as visited and add it to finished items
    finishedItems.emplace_back(currentItem);
    return currentItem->finished = true;
}

BuildOrderResult Config::computeBuildOrder(const std::vector<std::string> &dependencyDenotations, BuildOrderOptions options)
{
    // setup variables to store results
    auto result = BuildOrderResult();
    auto allTopoSortItems = std::vector<std::unique_ptr<TopoSortItem>>();
    auto finishedTopoSortItems = std::vector<TopoSortItem *>();
    allTopoSortItems.reserve(dependencyDenotations.size());

    // add dependencies
    for (const auto &dependency : dependencyDenotations) {
        // continue adding dependencies as long as no cycles have been detected
        if (addDepsRecursivelyInTopoOrder(
                allTopoSortItems, finishedTopoSortItems, result.ignored, result.cycle, Dependency(std::string_view(dependency)), options, false)) {
            result.cycle.clear();
            continue;
        }

        // stop on the first cycle
        break;
    }

    // add finished items to result (even if we detected a cycle)
    result.order.reserve(allTopoSortItems.size());
    for (auto &item : finishedTopoSortItems) {
        if (!(options & BuildOrderOptions::IncludeAllDependencies)
            && (item->onlyDependency && ((options & BuildOrderOptions::IncludeSourceOnlyDependencies) || item->package.pkg->packageInfo))) {
            continue;
        }
        result.order.emplace_back(move(item->package));
    }

    result.success = result.cycle.empty() && result.ignored.empty();
    return result;
}

} // namespace LibPkg
