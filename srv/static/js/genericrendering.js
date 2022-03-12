/// \brief Renders the specified \a value as text or a grey 'none' if the value is 'none' or empty.
export function renderNoneInGrey(value, row, elementName, noneText)
{
    const noValue = value === undefined || value === null || value === 'none' || value === 'None' ||
                    value === '' || value === 18446744073709552000;
    const element = document.createElement((noValue || elementName === undefined) ? 'span' : elementName);
    if (noValue) {
        element.appendChild(document.createTextNode(noneText || 'none'));
        element.style.color = 'grey';
        element.dataset.isNone = true;
    } else if (typeof value === 'boolean') {
        element.appendChild(document.createTextNode(value ? 'yes' : 'no'));
    } else {
        element.appendChild(document.createTextNode(value));
    }
    return element;
}

/// \brief Renders a standard table cell.
/// \remarks This is the default renderer used by renderTableFromJsonArray() and renderTableFromJsonObject().
export function renderStandardTableCell(data, allData, level)
{
    const dataType = typeof data;
    if (dataType !== 'object') {
        return renderNoneInGrey(data);
    }
    if (!Array.isArray(data)) {
        if (level !== undefined && level > 1) {
            return renderNoneInGrey('', data, undefined, 'rendering stopped at this level')
        }
        return renderTableFromJsonObject({data: data, level: level !== undefined ? level + 1 : 1});
    }
    if (data.length === 0) {
        return renderNoneInGrey('', data, undefined, 'empty array');
    }
    const ul = document.createElement('ul');
    data.forEach(function(element) {
        const li = document.createElement('li');
        li.appendChild(renderStandardTableCell(element));
        ul.appendChild(li);
    });
    return ul;
}

/// \brief Renders a custom list.
export function renderCustomList(array, customRenderer, compareFunction)
{
    if (!Array.isArray(array) || array.length < 1) {
        return renderNoneInGrey();
    }
    if (compareFunction !== undefined) {
        array.sort(compareFunction);
    }
    const ul = document.createElement('ul');
    array.forEach(function(arrayElement) {
        const li = document.createElement('li');
        const renderedDomElements = customRenderer(arrayElement);
        if (Array.isArray(renderedDomElements)) {
            renderedDomElements.forEach(function(renderedDomElement) {
                li.appendChild(renderedDomElement);
            });
        } else {
            li.appendChild(renderedDomElements);
        }
        ul.appendChild(li);
    });
    return ul;
}

/// \brief Renders a list of links.
export function renderLinkList(array, obj, handler)
{
    return renderCustomList(array, function(arrayElement) {
        return renderLink(array, obj, function() {
            handler(arrayElement, array, obj);
        });
    });
}

/// \brief Returns a 'time ago' string used by the time stamp rendering functions.
export function formatTimeAgoString(date)
{
    const seconds = Math.floor((new Date() - date) / 1000);
    let interval = Math.floor(seconds / 31536000);
    if (interval > 1) {
        return interval + ' y ago';
    }
    interval = Math.floor(seconds / 2592000);
    if (interval > 1) {
        return interval + ' m ago';
    }
    interval = Math.floor(seconds / 86400);
    if (interval > 1) {
        return interval + ' d ago';
    }
    interval = Math.floor(seconds / 3600);
    if (interval > 1) {
        return interval + ' h ago';
    }
    interval = Math.floor(seconds / 60);
    if (interval > 1) {
        return interval + ' min ago';
    }
    return Math.floor(seconds) + ' s ago';
}

/// \brief Returns a Date object from the specified time stamp.
export function dateFromTimeStamp(timeStamp)
{
    return new Date(timeStamp + 'Z');
}

/// \brief Renders a short time stamp, e.g. "12 hours ago" with the exact date as tooltip.
export function renderShortTimeStamp(timeStamp)
{
    const date = dateFromTimeStamp(timeStamp);
    if (date.getFullYear() === 1) {
        return document.createTextNode('not yet');
    }
    const span = document.createElement('span');
    span.appendChild(document.createTextNode(formatTimeAgoString(date)));
    span.title = timeStamp;
    return span;
}

/// \brief Renders a time stamp, e.g. "12 hours ago" with the exact date in brackets.
export function renderTimeStamp(timeStamp)
{
    const date = dateFromTimeStamp(timeStamp);
    if (date.getFullYear() === 1) {
        return document.createTextNode('not yet');
    }
    return document.createTextNode(formatTimeAgoString(date) + ' (' + timeStamp + ')');
}

/// \brief Renders a time delta from 2 time stamps.
export function renderTimeSpan(startTimeStamp, endTimeStamp)
{
    const startDate = dateFromTimeStamp(startTimeStamp);
    if (startDate.getFullYear() === 1) {
        return document.createTextNode('not yet');
    }
    let endDate = dateFromTimeStamp(endTimeStamp);
    if (endDate.getFullYear() === 1) {
        endDate = Date.now();
    }
    const elapsedMilliseconds = endDate - startDate;
    let text;
    if (elapsedMilliseconds >= (1000 * 60 * 60)) {
        text = Math.floor(elapsedMilliseconds / 1000 / 60 / 60) + ' h';
    } else if (elapsedMilliseconds >= (1000 * 60)) {
        text = Math.floor(elapsedMilliseconds / 1000 / 60) + ' min';
    } else if (elapsedMilliseconds >= (1000)) {
        text = Math.floor(elapsedMilliseconds / 1000) + ' s';
    } else {
        text = '< 1 s';
    }
    return document.createTextNode(text);
}

/// \brief Renders a link which will invoke the specified \a handler when clicked.
export function renderLink(value, row, handler, tooltip, href, middleClickHref)
{
    const linkElement = document.createElement('a');
    const linkText = typeof value === 'object' ? value : document.createTextNode(value);
    linkElement.appendChild(linkText);
    linkElement.href = middleClickHref || href || '#';
    if (tooltip !== undefined) {
        linkElement.title = tooltip;
    }
    linkElement.onclick = function () {
        handler(value, row);
        return false;
    };
    linkElement.onmouseup = function (e) {
        // treat middle-click as regular click
        if (e.which !== 2) {
            return true;
        }
        e.preventDefault();
        e.stopPropagation();
        if (!middleClickHref) {
            handler(value, row);
        }
        return false;
    };
    return linkElement;
}

/// \brief Renders the specified array as comma-separated string or 'none' if the array is empty.
export function renderArrayAsCommaSeparatedString(value)
{
    return renderNoneInGrey(!Array.isArray(value) || value.length <= 0 ? 'none' : value.join(', '));
}

/// \brief Renders the specified array as a possibly elided comma-separated string or 'none' if the array is empty.
export function renderArrayElidedAsCommaSeparatedString(value)
{
    if (!Array.isArray(value) || value.length <= 0) {
        return renderNoneInGrey('none');
    }
    return renderTextPossiblyElidingTheEnd(value.join(', '));
}

/// \brief Renders the specified value, possibly eliding the end.
export function renderTextPossiblyElidingTheEnd(value)
{
    const limit = 50;
    if (value.length < limit) {
        return document.createTextNode(value);
    }
    const element = document.createElement('span');
    const remainingText = document.createTextNode(value.substr(limit));
    const elipses = document.createTextNode('…');
    let expaned = false;
    element.appendChild(document.createTextNode(value.substr(0, limit)));
    element.appendChild(elipses);
    element.onclick = function () {
        element.removeChild(element.lastChild);
        ((expaned = !expaned)) ? element.appendChild(remainingText) : element.appendChild(elipses);
    };
    return element;
}

/// \brief Renders the specified \a sizeInByte using an appropriate unit.
export function renderDataSize(sizeInByte, row, includeBytes)
{
    if (typeof(sizeInByte) !== 'number') {
        return renderNoneInGrey('none');
    }
    let res;
    if (sizeInByte < 1024) {
        res = sizeInByte << " bytes";
    } else if (sizeInByte < 1048576) {
        res = (sizeInByte / 1024.0) + " KiB";
    } else if (sizeInByte < 1073741824) {
        res = (sizeInByte / 1048576.0) + " MiB";
    } else if (sizeInByte < 1099511627776) {
        res = (sizeInByte / 1073741824.0) + " GiB";
    } else {
        res = (sizeInByte / 1099511627776.0) + " TiB";
    }
    if (includeBytes && sizeInByte > 1024) {
        res += ' (' + sizeInByte + " byte)";
    }
    return document.createTextNode(res);
}

// \brief Accesses the property of the specified \a object denoted by \a accessor which is a string like 'foo.bar'.
// \returns Returns the propertie's value or undefined if it doesn't exist.
function accessProperty(object, accessor)
{
    if (accessor === undefined) {
        return;
    }
    const propertyNames = accessor.split(".");
    for (let i = 0, count = propertyNames.length; i !== count; ++i) {
        object = object[propertyNames[i]];
        if (object === undefined || object === null) {
            return;
        }
    }
    return object;
}

/// \brief Renders a checkbox for selecting a table row.
export function renderCheckBoxForTableRow(value, row, computeCheckBoxValue)
{
    const checkbox = document.createElement('input');
    checkbox.type = 'checkbox';
    checkbox.checked = row.selected;
    checkbox.value = computeCheckBoxValue(row);
    checkbox.onchange = function () { row.selected = this.checked };
    return checkbox;
}

/// \brief Returns a table for the specified JSON array.
export function renderTableFromJsonArray(args)
{
    // handle arguments
    const rows = args.rows;
    const columnHeaders = args.columnHeaders;
    const columnAccessors = args.columnAccessors;
    const columnSortAccessors = args.columnSortAccessors || [];
    const defaultRenderer = args.defaultRenderer || renderStandardTableCell;
    const customRenderer = args.customRenderer || {};
    const rowHandler = args.rowHandler;
    const maxPageButtons = args.maxPageButtons || 5;
    
    const container = document.createElement("div");

    // render note
    let noteRenderer = customRenderer.note;
    if (noteRenderer === undefined || typeof noteRenderer === 'string') {
        const note = document.createElement("p");
        note.appendChild(document.createTextNode(noteRenderer || "Showing " + rows.length + " results"));
        container.appendChild(note);
    } else {
        const note = noteRenderer(rows);
        if (note !== undefined) {
            container.appendChild(note);
        }
    }

    // add table
    const rowsPerPage = args.rowsPerPage;
    const table = document.createElement("table");
    table.data = rows;
    table.sortedData = rows;
    table.className = "table-from-json table-from-json-array";

    // define pagination stuff
    if (rowsPerPage !== undefined && rowsPerPage > 0) {
        table.hasPagination = true;
        table.rowsPerPage = args.rowsPerPage;
        table.currentPage = args.currentPage = 1;
        table.pageCount = Math.ceil(rows.length / rowsPerPage);
    }
    table.pageInfo = function() {
        if (!this.hasPagination) {
            return {begin: 0, end: this.data.length}; // no pagination
        }
        if (this.currentPage === undefined || this.currentPage <= 0) {
            return {begin: 0, end: 0}; // invalid page
        }
        return {
            begin: Math.min((this.currentPage - 1) * this.rowsPerPage, this.data.length),
            end: Math.min(this.currentPage * this.rowsPerPage, this.data.length),
        };
    };
    table.forEachRowOnPage = function(callback) {
        const pageInfo = this.pageInfo();
        if (isNaN(pageInfo.begin) || isNaN(pageInfo.end)) {
            return;
        }
        for (let i = pageInfo.begin, end = pageInfo.end; i != end; ++i) {
            const row = this.data[i];
            if (row === null || row === undefined) {
                continue;
            }
            callback(row, i, pageInfo, this);
        }
    };

    // render column header
    const thead = document.createElement("thead");
    const tr = document.createElement("tr");
    columnHeaders.forEach(function (columnHeader) {
        const th = document.createElement("th");
        const columnIndex = tr.children.length;
        th.columnAccessor = columnSortAccessors[columnIndex] || columnAccessors[columnIndex];
        th.descending = true;
        th.onclick = function () {
            table.sort(this.columnAccessor, this.descending = !this.descending);
        };
        th.style.cursor = "pointer";
        th.appendChild(document.createTextNode(columnHeader));
        tr.appendChild(th);
    });
    thead.appendChild(tr);
    table.appendChild(thead);

    // add pagination
    if (table.hasPagination) {
        const td = document.createElement("td");
        td.className = "pagination";
        td.colSpan = columnAccessors.length;
        const previousA = document.createElement("a");
        previousA.appendChild(document.createTextNode("<"));
        previousA.className = "prev";
        previousA.onclick = function() {
            const currentA = table.currentA;
            if (currentA) {
                const a = currentA.previousSibling;
                if (a !== undefined && a !== previousA) {
                    a.onclick();
                }
            }
            const pageNumInput = table.pageNumInput;
            if (pageNumInput && table.currentPage > 1) {
                pageNumInput.value = table.currentPage - 1;
                pageNumInput.onchange();
            }
        };
        const nextA = document.createElement("a");
        nextA.appendChild(document.createTextNode(">"));
        nextA.className = "next";
        nextA.onclick = function() {
            const currentA = table.currentA;
            if (currentA) {
                const a = currentA.nextSibling;
                if (a !== undefined && a !== nextA) {
                    a.onclick();
                }
            }
            const pageNumInput = table.pageNumInput;
            if (pageNumInput && table.currentPage < table.pageCount) {
                pageNumInput.value = table.currentPage + 1;
                pageNumInput.onchange();
            }
        };

        td.appendChild(previousA);

        if (table.pageCount <= maxPageButtons) {
            for (let pageNumber = 1; pageNumber <= table.pageCount; ++pageNumber) {
                const a = document.createElement("a");
                a.appendChild(document.createTextNode(pageNumber));
                a.onclick = function () {
                    table.currentA.className = '';
                    table.currentA = this;
                    table.currentA.className = 'current';
                    table.currentPage = pageNumber;
                    table.rerender();
                };
                if (pageNumber === table.currentPage) {
                    table.currentA = a;
                    a.className = 'current';
                }
                td.appendChild(a);
            }
        } else {
            const pageNumInput = document.createElement("input");
            pageNumInput.type = "number";
            pageNumInput.value = table.currentPage;
            pageNumInput.min = 1;
            pageNumInput.max = table.pageCount;
            pageNumInput.onchange = function() {
                const selectedPage = parseInt(this.value);
                if (!isNaN(selectedPage) && selectedPage) {
                    table.currentPage = selectedPage;
                    table.rerender();
                }
            };
            table.pageNumInput = pageNumInput;
            td.appendChild(pageNumInput);
            const totalSpan = document.createElement("span");
            totalSpan.appendChild(document.createTextNode(" of " + table.pageCount));
            td.appendChild(totalSpan);
        }

        td.appendChild(nextA);

        const tr = document.createElement("tr");
        const tfoot = document.createElement("tfoot");
        tr.appendChild(td);
        tfoot.appendChild(tr);
        table.appendChild(tfoot);
    }

    // render table contents
    const tbody = document.createElement("tbody");
    const renderNewRow = function (row) {
        const tr = document.createElement("tr");
        columnAccessors.forEach(function (columnAccessor) {
            const td = document.createElement("td");
            const renderer = customRenderer[columnAccessor];
            let data = accessProperty(row, columnAccessor);
            if (data === undefined) {
                data = "?";
            }
            const content = renderer ? renderer(data, row) : defaultRenderer(data, row);
            td.appendChild(content);
            tr.appendChild(td);
        });
        if (rowHandler) {
            rowHandler(row, tr);
        }
        tbody.appendChild(tr);
    };
    table.forEachRowOnPage(renderNewRow);
    table.appendChild(tbody);

    // define function to re-render the table's contents
    table.rerender = function() {
        const sortedData = this.sortedData;
        const trs = tbody.getElementsByTagName("tr");
        const pageInfo = table.pageInfo();
        let dataIndex = pageInfo.begin, dataEnd = pageInfo.end;
        for (let tr = tbody.firstChild; tr; ++dataIndex) {
            if (dataIndex >= dataEnd) {
                const nextTr = tr.nextSibling;
                tbody.removeChild(tr);
                tr = nextTr;
                continue;
            }
            const tds = tr.getElementsByTagName("td");
            const row = sortedData[dataIndex];
            for (let td = tr.firstChild, i = 0; td; td = td.nextSibling, ++i) {
                const columnAccessor = columnAccessors[i];
                while (td.firstChild) {
                    td.removeChild(td.firstChild);
                }
                const renderer = customRenderer[columnAccessor];
                const data = accessProperty(row, columnAccessor);
                const content = renderer !== undefined ? renderer(data, row) : defaultRenderer(data, row);
                td.appendChild(content);
            }
            if (rowHandler) {
                rowHandler(row, tr);
            }
            tr = tr.nextSibling;
        }
        for (; dataIndex < dataEnd; ++dataIndex) {
            renderNewRow(sortedData[dataIndex]);
        }
    };

    // define function to re-sort according to a specific column
    table.sort = function(columnAccessor, descending) {
        // sort the rows according to the column
        table.sortedData = rows.sort(function (a, b) {
            let aValue = accessProperty(a, columnAccessor);
            let bValue = accessProperty(b, columnAccessor);
            let aType = typeof aValue;
            let bType = typeof bValue;

            // handle undefined/null
            if (aValue === undefined || aValue === null) {
                return -1;
            }
            if (bValue === undefined || bValue === null) {
                return 1;
            }

            // handle numbers
            if (aType === "number" && bType === "number") {
                if (aValue < bValue) {
                    return descending ? 1 : -1;
                } else if (aValue > bValue) {
                    return descending ? -1 : 1;
                } else {
                    return 0;
                }
            }

            // handle arrays (sort them by length)
            if (aType === "array" && bType === "array") {
                if (aValue.length < bValue.length) {
                    return descending ? 1 : -1;
                } else if (aValue > bValue) {
                    return descending ? -1 : 1;
                } else {
                    return 0;
                }
            }

            // handle non-strings
            if (aType !== "string" || bType !== "string") {
                aValue = aValue.toString();
                bValue = bValue.toString();
                aType = bType = "string";
            }

            // compare strings
            return descending ? bValue.localeCompare(aValue) : aValue.localeCompare(bValue);
        });

        // re-render the table's contents
        table.rerender();
    };

    // FIXME: implement filter

    container.appendChild(table);
    container.table = table;
    return container;
}

/// \brief Returns a table for the specified JSON object.
export function renderTableFromJsonObject(args)
{
    // handle arguments
    const data = args.data;
    const relatedRow = args.relatedRow;
    const displayLabels = args.displayLabels || [];
    const fieldAccessors = args.fieldAccessors || Object.getOwnPropertyNames(data);
    const level = args.level;
    const defaultRenderer = args.defaultRenderer || renderStandardTableCell;
    const customRenderer = args.customRenderer || {};

    const container = document.createElement("div");

    // render table
    const table = document.createElement("table");
    table.className = "table-from-json table-from-json-object";

    // render table contents
    const tbody = document.createElement("tbody");
    fieldAccessors.forEach(function (fieldAccessor) {
        const tr = document.createElement("tr");
        const th = document.createElement("th");
        const displayLabel = displayLabels[tbody.children.length] || fieldAccessor;
        if (displayLabel !== undefined) {
            th.appendChild(document.createTextNode(displayLabel));
        }
        tr.appendChild(th);
        const td = document.createElement("td");
        const renderer = customRenderer[fieldAccessor];
        let fieldData = accessProperty(data, fieldAccessor);
        if (fieldData === undefined) {
            fieldData = "?";
        }
        const content = renderer ? renderer(fieldData, data, level, relatedRow) : defaultRenderer(fieldData, data, level, relatedRow);
        if (Array.isArray(content)) {
            content.forEach(function(contentElement) {
                td.appendChild(contentElement);
            });
        } else {
            td.appendChild(content);
        }
        tr.appendChild(td);
        tbody.appendChild(tr);
    });
    table.appendChild(tbody);

    container.appendChild(table);
    return container;
}

/// \brief Returns a heading for each key and values via renderStandardTableCell().
export function renderObjectWithHeadings(object, row, level)
{
    const elements = [];
    for (const [key, value] of Object.entries(object)) {
        const heading = document.createElement('h4');
        heading.className = 'compact-heading';
        heading.appendChild(document.createTextNode(key));
        elements.push(heading, renderStandardTableCell(value, object, level));
    }
    return elements;
}
