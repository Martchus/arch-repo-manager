import * as AjaxHelper from './ajaxhelper.js';
import * as CustomRendering from './customrendering.js';
import * as GenericRendering from './genericrendering.js';
import * as PackageRendering from './packagerendering.js';
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
        PackageRendering.showPackageDetails(ajaxRequest, packageObj);
    }, 'package-details');
    return true;
}
