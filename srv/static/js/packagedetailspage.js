function initPackageDetails(sectionElement, sectionData, newPackages)
{
    const currentPackage = sectionData.state.package;
    const hasNewPackages = newPackages.length >= 1;
    if (!hasNewPackages) {
        if (currentPackage !== undefined) {
            window.preventHandlingHashChange = true;
            window.location.hash = '#package-details-section&' + encodeURIComponent(currentPackage);
            window.preventHandlingHashChange = false;
        }
        return true;
    }
    const packageStr = newPackages[0];
    if (currentPackage === packageStr) {
        return true;
    }
    const packageParts = packageStr.split('/');
    const package = {db: packageParts[0], name: packageParts[1]};
    queryRoute('GET', '/packages?details=1&name=' + encodeURIComponent(packageStr), function(ajaxRequest) {
        showPackageDetails(ajaxRequest, package);
    });
    return true;
}

function makePackageID(row)
{
    return row.db + (row.dbArch ? '@' + row.dbArch : '') + '/' + row.name;
}

function queryPackageDetails(value, row)
{
    queryRoute('GET', '/packages?details=1&name=' + encodeURIComponent(makePackageID(row)), function(ajaxRequest) {
        showPackageDetails(ajaxRequest, row);
    });
}

function switchToPackageDetails(packageID)
{
    sections['package-details'].state.package = packageID;
    window.preventSectionInitializer = true;
    window.location.hash = '#package-details-section&' + encodeURIComponent(packageID);
    window.preventSectionInitializer = false;
}

function showPackageDetails(ajaxRequest, row)
{
    const packageID = makePackageID(row);
    const packageDetailsContainer = getAndEmptyElement('package-details-container');
    if (ajaxRequest.status !== 200) {
        packageDetailsContainer.appendChild(document.createTextNode('unable query package details: ' + ajaxRequest.responseText));
        return;
    }
    const responseJson = JSON.parse(ajaxRequest.responseText);
    if (!Array.isArray(responseJson) || responseJson.length !== 1) {
        switchToPackageDetails(packageID);
        packageDetailsContainer.appendChild(document.createTextNode('unable query package details: package not present'));
        return;
    }
    const package = responseJson[0];
    const heading = document.createElement('h3');
    heading.appendChild(document.createTextNode(package.name));
    heading.appendChild(document.createTextNode(' ' + package.version));
    packageDetailsContainer.appendChild(heading);
    package.db = row.db;
    package.dbArch = row.dbArch;
    packageDetailsContainer.appendChild(renderPackage(package, true));

    switchToPackageDetails(packageID);
}

const labelsWithoutBasics = ['Architecture', 'Repository', 'Description', 'Upstream URL', 'License(s)', 'Groups', 'Package size', 'Installed size', 'Packager', 'Build date', 'Dependencies', 'Optional dependencies', 'Make dependencies', 'Check dependencies', 'Provides', 'Replaces', 'Conflicts', 'Contained libraries', 'Needed libraries', 'Files'];
const fieldsWithoutBasics = ['packageInfo.arch', 'db', 'description', 'upstreamUrl', 'licenses', 'groups', 'packageInfo.size', 'installInfo.installedSize', 'packageInfo.packager', 'packageInfo.buildDate', 'dependencies', 'optionalDependencies', 'sourceInfo.makeDependencies', 'sourceInfo.checkDependencies', 'provides', 'replaces', 'conflicts', 'libprovides', 'libdepends', 'packageInfo.files'];
const labelsWithBasics = ['Name', 'Version', ...labelsWithoutBasics];
const fieldsWithBasics = ['name', 'version', ...fieldsWithoutBasics];

function renderPackage(package, withoutBasics)
{
    const table = renderTableFromJsonObject({
        data: package,
        displayLabels: withoutBasics ? labelsWithoutBasics : labelsWithBasics,
        fieldAccessors: withoutBasics ? fieldsWithoutBasics : fieldsWithBasics,
        customRenderer: {
            db: function(value, row) {
                return document.createTextNode(makeRepoName(value, row.dbArch));
            },
            upstreamUrl: function (value, row) {
                return renderLink(value, row, function (value) {
                    window.open(value);
                });
            },
            licenses: renderArrayAsCommaSeparatedString,
            groups: renderArrayAsCommaSeparatedString,
            dependencies: renderDependency,
            optionalDependencies: renderDependency,
            provides: renderDependency,
            replaces: renderDependency,
            conflicts: renderDependency,
            libprovides: renderArrayAsCommaSeparatedString,
            libdepends: renderArrayAsCommaSeparatedString,
            'sourceInfo.makeDependencies': renderDependency,
            'sourceInfo.checkDependencies': renderDependency,
            'packageInfo.arch': function (value, row) {
                const sourceInfo = row.sourceInfo;
                const sourceArchs = sourceInfo !== undefined ? sourceInfo.archs : undefined;
                if (Array.isArray(sourceArchs) && sourceArchs.length) {
                    return renderArrayAsCommaSeparatedString(sourceArchs);
                } else {
                    return renderNoneInGrey(value);
                }
            },
            'packageInfo.size': renderDataSize,
            'installInfo.installedSize': renderDataSize,
        },
    });
    table.className = 'package-details-table';
    return table;
}