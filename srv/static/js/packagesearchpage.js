function searchForPackages()
{
    return startFormQuery('package-search-form', showPackageSearchResults);
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
                    '#package-details-section&' + encodeURIComponent(row.db + (row.dbArch ? '@' + row.dbArch : '') + '/' + value));
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
