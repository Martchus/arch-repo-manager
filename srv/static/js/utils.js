
export function splitHashParts()
{
    const currentHash = location.hash.substr(1);
    const hashParts = currentHash.split('?');
    for (let i = 0, len = hashParts.length; i != len; ++i) {
        hashParts[i] = decodeURIComponent(hashParts[i]);
    }
    return hashParts;
}

export function hashAsObject()
{
    const hashObject = {};
    location.hash.substr(1).split('&').forEach(function(hashPart) {
        const parts = hashPart.split('=', 2);
        if (parts.length < 1) {
            return;
        }
        hashObject[decodeURIComponent(parts[0])] = parts.length > 1 ? decodeURIComponent(parts[1]) : undefined;
    });
    return hashObject;
}

export function getAndEmptyElement(elementId, specialActionsById)
{
    return emptyDomElement(document.getElementById(elementId), specialActionsById);
}

export function emptyDomElement(domElement, specialActionsById)
{
    let child = domElement.firstChild;
    while (child) {
        let specialAction = specialActionsById ? specialActionsById[child.id] : undefined;
        let nextSibling = child.nextSibling;
        if (specialAction !== 'keep') {
            domElement.removeChild(child);
        }
        child = nextSibling;
    }
    return domElement;
}

export function alterFormSelection(form, command)
{
    // modify form elements
    const elements = form.elements;
    for (let i = 0, len = elements.length; i != len; ++i) {
        const element = elements[i];
        if (element.type !== 'checkbox') {
            continue;
        }
        switch (command) {
        case 'uncheck-all':
            element.checked = false;
            break;
        case 'check-all':
            element.checked = true;
            break;
        }
    }
    // modify the actual data
    const tables = form.getElementsByTagName('table');
    for (let i = 0, len = tables.length; i != len; ++i) {
        const data = tables[i].data;
        if (!Array.isArray(data)) {
            return;
        }
        data.forEach(function (row) {
             switch (command) {
            case 'uncheck-all':
                row.selected = false;
                break;
            case 'check-all':
                row.selected = true;
                break;
            }
        });
    }
}

export function getProperty(object, property, fallback)
{
    if (typeof object !== 'object') {
        return fallback;
    }
    const value = object[property];
    return value !== undefined ? value : fallback;
}

export function makeRepoName(dbName, dbArch)
{
    return dbArch && dbArch !== 'x86_64' ? dbName + '@' + dbArch : dbName;
}

/// \brief Returns the table row data for the table within the element with the specified ID.
export function getFormTableData(formId)
{
    const formElement = document.getElementById(formId);
    const tableElement = formElement.getElementsByTagName('table')[0];
    if (tableElement === undefined) {
        return;
    }
    const data = tableElement.data;
    return Array.isArray(data) ? data : undefined;
}

/// \brief Returns the cell values of selected rows.
/// \remarks The row data needs to be passed. The cell is determined by the specified \a propertyName.
export function getSelectedRowProperties(data, propertyName)
{
    const propertyValues = [];
    data.forEach(function (row) {
        if (row.selected) {
            propertyValues.push(row[propertyName]);
        }
    });
    return propertyValues;
}
