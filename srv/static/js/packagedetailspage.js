import * as AjaxHelper from './ajaxhelper.js';
import * as CustomRendering from './customrendering.js';
import * as GenericRendering from './genericrendering.js';
import * as SinglePageHelper from './singlepage.js';
import * as Utils from './utils.js';

export function initPackageDetails(sectionElement, sectionData, newPackages)
{
    const currentPackage = sectionData.state.package;
    const hasNewPackages = newPackages.length >= 1;
    if (!hasNewPackages) {
        if (currentPackage !== undefined) {
            SinglePageHelper.updateHashPreventingChangeHandler('#package-details-section?' + encodeURIComponent(currentPackage));
        }
        return true;
    }
    const packageStr = newPackages[0];
    if (currentPackage === packageStr) {
        return true;
    }
    const packageParts = packageStr.split('/');
    const packageObj = {
        db: packageParts[0],
        name: packageParts[1]
    };
    AjaxHelper.queryRoute('GET', '/packages?details=1&name=' + encodeURIComponent(packageStr), function(ajaxRequest) {
        showPackageDetails(ajaxRequest, packageObj);
    }, 'package-details');
    return true;
}

function makePackageID(row)
{
    return row.db + (row.dbArch ? '@' + row.dbArch : '') + '/' + row.name;
}

export function queryPackageDetails(value, row)
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

function showPackageDetails(ajaxRequest, row)
{
    const packageID = makePackageID(row);
    const packageDetailsContainer = Utils.getAndEmptyElement('package-details-container');
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
    const packageObj = responseJson[0];
    const heading = document.createElement('h3');
    heading.appendChild(document.createTextNode(packageObj.name));
    heading.appendChild(document.createTextNode(' ' + packageObj.version));
    packageDetailsContainer.appendChild(heading);
    packageObj.db = row.db;
    packageObj.dbArch = row.dbArch;
    packageDetailsContainer.appendChild(CustomRendering.renderPackage(packageObj, true));

    switchToPackageDetails(packageID);
}
