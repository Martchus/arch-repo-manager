function initPackageSearch(sectionElement, sectionData, newParams)
{
    const currentParams = sectionData.state.params;
    const hasNewParams = newParams.length >= 1;
    if (!hasNewParams) {
        if (currentParams !== undefined) {
            updateHashPreventingChangeHandler('#package-search-section?' + encodeURIComponent(currentParams));
        }
        return true;
    }
    const searchParams = sections['package-search'].state.params = newParams[0];
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

function searchForPackagesFromParams(searchParams)
{
    const params = new URLSearchParams(searchParams);
    const form = document.getElementById('package-search-form');
    form.reset();
    params.forEach(function(value, key) {
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
    });
    const res = startFormQueryEx('package-search-form', showPackageSearchResults);
    sections['package-search'].state.params = res.params;
    return res;
}

function searchForPackages()
{
    const res = startFormQueryEx('package-search-form', showPackageSearchResults);
    const params = sections['package-search'].state.params = res.params.substr(1);
    updateHashPreventingSectionInitializer('#package-search-section?' + encodeURIComponent(params));
    return res.ajaxRequest;
}

function showPackageSearchResults(ajaxRequest)
{
    const packageSearchResults = getAndEmptyElement('package-search-results');
    if (ajaxRequest.status !== 200) {
        packageSearchResults.appendChild(document.createTextNode('unable search for packages: ' + ajaxRequest.responseText));
        return;
    }
    const responseJson = JSON.parse(ajaxRequest.responseText);
    const table = renderTableFromJsonArray({
        rows: responseJson,
        columnHeaders: ['', 'Arch', 'Repo', 'Name', 'Version', 'Description', 'Build date'],
        columnAccessors: ['checkbox', 'arch', 'db', 'name', 'version', 'description', 'buildDate'],
        rowsPerPage: 40,
        customRenderer: {
            name: function (value, row) {
                return renderLink(value, row, queryPackageDetails, 'Show package details', undefined,
                    '#package-details-section?' + encodeURIComponent(row.db + (row.dbArch ? '@' + row.dbArch : '') + '/' + value));
            },
            checkbox: function(value, row) {
                return renderCheckBoxForTableRow(value, row, function(row) {
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
