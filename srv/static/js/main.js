import * as BuildActionsPage from './buildactionspage.js';
import * as PackageDetailsPage from './packagedetailspage.js';
import * as PackageSearchPage from './packagesearchpage.js';
import * as SinglePage from './singlepage.js';

SinglePage.initPage({
    'global': {
    },
    'package-search': {
        initializer: PackageSearchPage.initPackageSearch,
        state: {params: undefined},
    },
    'package-details': {
        initializer: PackageDetailsPage.initPackageDetails,
        state: {package: undefined},
    },
    'build-action': {
        initializer: BuildActionsPage.initBuildActionsForm,
    },
    'build-action-details': {
        initializer: BuildActionsPage.initBuildActionDetails,
        state: {id: undefined},
    },
});