import * as AjaxHelper from './ajaxhelper.js';
import * as GenericRendering from './genericrendering.js';
import * as PackageRendering from './packagerendering.js';
import * as SinglePageHelper from './singlepage.js';
import * as Utils from './utils.js';

export function initPackageSearch(sectionElement, sectionData, newParams)
{
    const searchForm = document.getElementById('package-search-form');
    if (!searchForm.dataset.initialized) {
        searchForm.onsubmit = function() {
            searchForPackages();
            return false;
        };
        const packageResultsFormElements = document.getElementById('package-results-form').elements;
        packageResultsFormElements.selectall.onclick = function () {
            Utils.alterFormSelection(this.form, 'check-all');
        };
        packageResultsFormElements.unselectall.onclick = function () {
            Utils.alterFormSelection(this.form, 'uncheck-all');
        };
        packageResultsFormElements.startselected.onclick = function () {
            fillBuildActionFromPackageSearch();
        };
        searchForm.dataset.initialized = true;
    }
    let currentParams = sectionData.state.params;
    if (currentParams && currentParams.startsWith('?')) {
        currentParams = currentParams.substr(1);
    }
    const hasNewParams = newParams.length >= 1;
    if (!hasNewParams) {
        if (currentParams !== undefined) {
            SinglePageHelper.updateHashPreventingChangeHandler('#package-search-section?' + encodeURIComponent(currentParams));
        }
        return true;
    }
    const searchParams = sectionData.state.params = newParams[0];
    if (currentParams === searchParams) {
        return true;
    }
    if (!window.globalInfo) {
        window.functionsPostponedUntilGlobalInfo.push(searchForPackagesFromParams.bind(this, searchParams));
    } else {
        searchForPackagesFromParams(searchParams);
    }
    return true;
}

function fillBuildActionFromPackageSearch()
{
    const packageNamesTextArea = document.getElementById('build-action-form')['package-names'];
    const data = Utils.getFormTableData('package-results-form');
    if (data === undefined) {
        return;
    }
    packageNamesTextArea.value = Utils.getSelectedRowProperties(data, 'name').join(' ');
    location.hash = '#build-action-section';
}

function searchForPackagesFromParams(searchParams)
{
    const form = document.getElementById('package-search-form');
    form.reset();
    for (const [key, value] of Object.entries(Utils.hashAsObject(searchParams))) {
        const formElement = form[key];
        if (!formElement) {
            return;
        }
        if (formElement.multiple) {
            Array.from(formElement.options).forEach(function(optionElement) {
                if (optionElement.value === value) {
                    optionElement.selected = true;
                    return;
                }
            });
        } else {
            formElement.value = value;
        }
    }
    const res = AjaxHelper.startFormQueryEx('package-search-form', showPackageSearchResults);
    SinglePageHelper.sections['package-search'].state.params = res.params;
    return res;
}

function searchForPackages()
{
    const res = AjaxHelper.startFormQueryEx('package-search-form', showPackageSearchResults);
    const params = SinglePageHelper.sections['package-search'].state.params = res.params.substr(1);
    SinglePageHelper.updateHashPreventingSectionInitializer('#package-search-section?' + encodeURIComponent(params));
    return res.ajaxRequest;
}

function showPackageSearchResults(ajaxRequest)
{
    const packageSearchResults = Utils.getAndEmptyElement('package-search-results');
    if (ajaxRequest.status !== 200) {
        packageSearchResults.appendChild(document.createTextNode('unable search for packages: ' + ajaxRequest.responseText));
        return;
    }
    const responseJson = JSON.parse(ajaxRequest.responseText);
    const table = GenericRendering.renderTableFromJsonArray({
        rows: responseJson,
        columnHeaders: ['', 'Arch', 'Repo', 'Name', 'Version', 'Description', 'Build date'],
        columnAccessors: ['checkbox', 'arch', 'db', 'name', 'version', 'description', 'buildDate'],
        rowsPerPage: 40,
        customRenderer: {
            name: function (value, row) {
                return PackageRendering.renderPackageDetailsLink(row);
            },
            checkbox: function(value, row) {
                return GenericRendering.renderCheckBoxForTableRow(value, row, function(row) {
                    return [row.db, row.name].join('/');
                });
            },
            note: function (rows) {
                const note = document.createElement("p");
                note.appendChild(document.createTextNode("Found " + rows.length + " packages"));
                return note;
            },
        },
    });
    packageSearchResults.appendChild(table);
}
