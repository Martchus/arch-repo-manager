import * as AjaxHelper from './ajaxhelper.js';
import * as GenericRendering from './genericrendering.js';
import * as CustomRendering from './customrendering.js';
import * as SinglePageHelper from './singlepage.js';
import * as Utils from './utils.js';

export function renderPackageDetailsLink(row)
{
    return GenericRendering.renderLink(row.name, row, queryPackageDetails, 'Show package details', undefined,
                                       '#package-details-section?' + encodeURIComponent(row.db + (row.dbArch ? '@' + row.dbArch : '') + '/' + row.name));
}

const modeTooltip = {depends: 'dependency', provides: 'dependency', requires: 'dependency', libdepends: 'library', libprovides: 'library'};

export function renderPackageSearchLink(name, mode, text)
{
    const tooltip = 'Search for ' + (mode !== undefined ? modeTooltip[mode] : 'package');
    if (mode === undefined) {
        mode = 'name';
    }
    const params = '#package-search-section?name=' + encodeURIComponent(name) + '&mode=' + encodeURIComponent(mode);
    return GenericRendering.renderLink(text || name, undefined, undefined, tooltip, undefined, params);
}

/// \brief Renders library names.
export function renderLibraries(mode, value)
{
    if (!Array.isArray(value) || value.length < 1) {
        return GenericRendering.renderArrayAsCommaSeparatedString(value);
    }
    const containerElement = document.createElement('span');
    value.forEach(function (libraryName) {
        if (containerElement.firstChild) {
            containerElement.appendChild(document.createTextNode(' '));
        }
        containerElement.appendChild(renderPackageSearchLink(libraryName, mode));
    });
    return containerElement;
}

/// \brief Renders a dependency object.
export function renderDependency(mode, value)
{
    if (!Array.isArray(value) || value.length < 1) {
        return GenericRendering.renderArrayAsCommaSeparatedString(value);
    }
    const list = document.createElement('ul');
    list.className = 'dependency-list';
    value.forEach(function (dependency) {
        const item = document.createElement('li');
        let res = dependency.name;
        if (dependency.version) {
            const modes = [undefined, undefined, '=', '>=', '<=', '>', '<'];
            const mode = modes[dependency.mode];
            if (mode !== undefined) {
                res += mode + dependency.version;
            }
        }
        item.appendChild(renderPackageSearchLink(res, mode || 'provides'));
        if (dependency.description) {
            const descriptionSpan = document.createElement('span');
            descriptionSpan.appendChild(document.createTextNode(' - ' + dependency.description));
            descriptionSpan.style.fontStyle = 'italics';
            item.appendChild(descriptionSpan);
        }
        list.appendChild(item);
    });
    return list;
}

const labelsWithoutBasics = ['Architecture', 'Repository', 'Description', 'Upstream URL', 'License(s)', 'Groups', 'Package size', 'Installed size', 'Packager', 'Build date', 'Dependencies', 'Optional dependencies', 'Make dependencies', 'Check dependencies', 'Provides', 'Replaces', 'Conflicts', 'Contained libraries', 'Needed libraries', 'Files'];
const fieldsWithoutBasics = ['arch', 'db', 'description', 'upstreamUrl', 'licenses', 'groups', 'packageInfo.size', 'installInfo.installedSize', 'packageInfo.packager', 'buildDate', 'dependencies', 'optionalDependencies', 'sourceInfo.makeDependencies', 'sourceInfo.checkDependencies', 'provides', 'replaces', 'conflicts', 'libprovides', 'libdepends', 'packageInfo.files'];
const labelsWithBasics = ['Name', 'Version', ...labelsWithoutBasics];
const fieldsWithBasics = ['name', 'version', ...fieldsWithoutBasics];

export function renderPackage(packageObj, withoutBasics)
{
    const pkgInfo = packageObj.pkg;
    if (pkgInfo) {
        pkgInfo.id = packageObj.id;
        packageObj = pkgInfo;
    }
    const table = GenericRendering.renderTableFromJsonObject({
        data: packageObj,
        displayLabels: withoutBasics ? labelsWithoutBasics : labelsWithBasics,
        fieldAccessors: withoutBasics ? fieldsWithoutBasics : fieldsWithBasics,
        customRenderer: {
            db: function(value, row) {
                return document.createTextNode(Utils.makeRepoName(value, row.dbArch));
            },
            upstreamUrl: function(value, row) {
                return GenericRendering.renderLink(value, row, function(value) {
                    window.open(value);
                });
            },
            licenses: GenericRendering.renderArrayAsCommaSeparatedString,
            groups: GenericRendering.renderArrayAsCommaSeparatedString,
            dependencies: renderDependency.bind(undefined, 'provides'),
            optionalDependencies: renderDependency.bind(undefined, 'provides'),
            provides: renderDependency.bind(undefined, 'depends'),
            replaces: renderDependency.bind(undefined, 'provides'),
            conflicts: renderDependency.bind(undefined, 'provides'),
            libprovides: renderLibraries.bind(undefined, 'libdepends'),
            libdepends: renderLibraries.bind(undefined, 'libprovides'),
            'sourceInfo.makeDependencies': renderDependency.bind(undefined, 'provides'),
            'sourceInfo.checkDependencies': renderDependency.bind(undefined, 'provides'),
            'packageInfo.arch': function(value, row) {
                const sourceInfo = row.sourceInfo;
                const sourceArchs = sourceInfo !== undefined ? sourceInfo.archs : undefined;
                if (Array.isArray(sourceArchs) && sourceArchs.length) {
                    return GenericRendering.renderArrayAsCommaSeparatedString(sourceArchs);
                } else {
                    return GenericRendering.renderNoneInGrey(value);
                }
            },
            'packageInfo.size': GenericRendering.renderDataSize,
            'installInfo.installedSize': GenericRendering.renderDataSize,
        },
    });
    table.className = 'package-details-table';
    return table;
}

function makePackageID(row)
{
    return row.db + (row.dbArch ? '@' + row.dbArch : '') + '/' + row.name;
}

function queryPackageDetails(value, row)
{
    AjaxHelper.queryRoute('GET', '/packages?details=1&name=' + encodeURIComponent(makePackageID(row)), function(ajaxRequest) {
        showPackageDetails(ajaxRequest, row);
    }, 'package-details');
}

function switchToPackageDetails(packageID)
{
    SinglePageHelper.sections['package-details'].state.package = packageID;
    SinglePageHelper.updateHashPreventingSectionInitializer('#package-details-section?' + encodeURIComponent(packageID));
}

function renderPackageActions(pkg)
{
    const container = document.createElement('span');
    container.appendChild(CustomRendering.renderIconLink('graph', undefined, undefined, 'Show dependend packages',
        undefined, '#package-search-section?name=' + encodeURIComponent(pkg.name) + '&mode=depends'));
    container.appendChild(CustomRendering.renderIconLink('table-refresh', undefined, function() {
        queryPackageDetails(undefined, pkg);
        return false;
    }, 'Refresh package details', undefined, '#package-details-section?' + encodeURIComponent(makePackageID(pkg))));
    return container;
}

export function showPackageDetails(ajaxRequest, row)
{
    const packageID = makePackageID(row);
    const packageDetailsContainer = Utils.getAndEmptyElement('package-details-container');
    if (ajaxRequest.status !== 200) {
        packageDetailsContainer.appendChild(document.createTextNode('unable query package details: ' + ajaxRequest.responseTextDisplay));
        return;
    }
    const responseJson = JSON.parse(ajaxRequest.responseText);
    if (!Array.isArray(responseJson) || responseJson.length !== 1) {
        switchToPackageDetails(packageID);
        packageDetailsContainer.appendChild(document.createTextNode('unable query package details: package not present'));
        return;
    }
    const packageObj = responseJson[0];
    const heading = document.createElement('h3');
    heading.appendChild(document.createTextNode(packageObj.name));
    heading.appendChild(document.createTextNode(' ' + packageObj.version));
    packageDetailsContainer.appendChild(heading);
    packageObj.db = row.db;
    packageObj.dbArch = row.dbArch;
    packageDetailsContainer.appendChild(renderPackage(packageObj, true));
    const packageActions = Utils.getAndEmptyElement('package-details-actions');
    packageActions.appendChild(renderPackageActions(packageObj));

    switchToPackageDetails(packageID);
}

