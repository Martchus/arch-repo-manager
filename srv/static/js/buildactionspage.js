function initBuildActionsForm()
{
    const buildActionsForm = document.getElementById('build-action-form');
    if (buildActionsForm.dataset.initialized) {
        return true;
    }
    queryBuildActions();
    handleBuildActionTypeChange();
    buildActionsForm.dataset.initialized = true;
    return true;
}

function queryBuildActions()
{
    queryRoute('GET', '/build-action', showBuildActions);
    return true;
}

function queryBuildActionDetails(ids)
{
    queryRoute('GET', '/build-action/details?' + makeIdParams(ids), showBuildActionDetails);
    return true;
}

function cloneBuildAction(ids)
{
    queryRoute('POST', '/build-action/clone?' + makeIdParams(ids), function (xhr, success) {
        if (!success) {
            return showAjaxError(xhr, 'clone build action');
        }
        const cloneIDs = JSON.parse(xhr.responseText);
        if (cloneIDs.length === 1 && typeof cloneIDs[0] === 'number') {
            if (window.confirm('Build action cloned as ' + cloneIDs[0] + '. Show cloned action?')) {
                queryBuildActionDetails(cloneIDs[0]);
            }
        } else {
            window.alert('Build action(s) have been cloned.');
        }
    });
    return true;
}

function deleteBuildAction(ids)
{
    queryRoute('DELETE', '/build-action?' + makeIdParams(ids), function (xhr, success) {
        success ? window.alert('Build action has been deleted.') : showAjaxError(xhr, 'delete build action');
    });
    return true;
}

function stopBuildAction(ids)
{
    queryRoute('POST', '/build-action/stop?' + makeIdParams(ids), function (xhr, success) {
        success ? window.alert('Build action has been stopped.') : showAjaxError(xhr, 'stop build action');
    });
    return true;
}

function startBuildAction(ids)
{
    queryRoute('POST', '/build-action/start?' + makeIdParams(ids), function (xhr, success) {
        success ? window.alert('Build action has been started.') : showAjaxError(xhr, 'start build action');
    });
    return true;
}

function selectOptions(selectElement, values)
{
    Array.from(selectElement.options).forEach(function(option) {
        option.selected = values.includes(option.value);
    });
}

function prepareBuildActionBasedOnExistingOne(existingBuildAction)
{
    if (!globalInfo) {
        window.alert('Global info needs to be retrieved first.');
        return;
    }
    const typeId = existingBuildAction.type;
    const typeInfo = globalInfo.buildActionTypes[typeId];
    const typeName = typeInfo.type;
    document.getElementById('build-action-task-none').selected = true;
    document.getElementById('build-action-type').value = typeName;
    handleBuildActionPresetChange();
    handleBuildActionTypeChange();
    document.getElementById('build-action-directory').value = existingBuildAction.directory;
    document.getElementById('build-action-package-names').value = existingBuildAction.packageNames.join(', ');
    selectOptions(document.getElementById('build-action-source-repo'), existingBuildAction.sourceDbs);
    selectOptions(document.getElementById('build-action-destination-repo'), existingBuildAction.destinationDbs);
    const presentFlags = existingBuildAction.flags;
    typeInfo.flags.forEach(function (flag) {
        const input = document.getElementById('build-action-' + typeName + '-' + flag.param);
        input.checked = parseInt(input.dataset.id) & presentFlags;
    });
    typeInfo.settings.forEach(function (setting) {
        document.getElementById('build-action-' + typeName + '-' + setting.param).value = existingBuildAction.settings[setting.param] || '';
    });
}

function handleBuildActionTypeChange()
{
    if (!globalInfo) {
        return;
    }
    const buildActionType = document.getElementById('build-action-type').value;
    const typeInfo = globalInfo.buildActionTypesByParam[buildActionType];
    document.getElementById('build-action-directory').disabled = !typeInfo.directory;
    document.getElementById('build-action-source-repo').disabled = !typeInfo.sourceDb;
    document.getElementById('build-action-destination-repo').disabled = !typeInfo.destinationDb;
    document.getElementById('build-action-package-names').disabled = !typeInfo.packageNames;
    Array.from(document.getElementById('build-action-flags').getElementsByTagName('label')).forEach(function(label) {
        const relevantType = label.dataset.relevantType;
        if (!relevantType) {
            return;
        }
        label.control.style.display = label.style.display = buildActionType === relevantType ? 'inline' : 'none';
    });
    Array.from(document.getElementById('build-action-settings').getElementsByTagName('tr')).forEach(function(tr) {
        const relevantType = tr.dataset.relevantType;
        if (!relevantType) {
            return;
        }
        const isRelevant = buildActionType === relevantType;
        tr.style.display = isRelevant ? 'table-row' : 'none';
        Array.from(tr.getElementsByTagName('input')).forEach(function(input) {
            input.style.display = isRelevant ? 'inline' : 'none';
        });
    });
}

function handleBuildActionPresetChange()
{
    if (!globalInfo) {
        return;
    }
    const task = document.getElementById('build-action-task').value;
    const taskInfo = globalInfo.presets.tasks[task];
    const taskInfoElement = getAndEmptyElement('build-action-task-info');
    const actionSelect = document.getElementById('build-action-type');
    if (!taskInfo) {
        actionSelect.disabled = false;
        taskInfoElement.style.fontStyle = 'italic';
        taskInfoElement.appendChild(document.createTextNode('Start a single action (no predefined task selected)'));
        handleBuildActionTypeChange();
        return;
    }
    actionSelect.disabled = true;
    taskInfoElement.style.fontStyle = 'normal';
    taskInfoElement.appendChild(document.createTextNode(taskInfo.desc || taskInfo.name));
    document.getElementById('build-action-directory').disabled = false;
    document.getElementById('build-action-source-repo').disabled = true;
    document.getElementById('build-action-destination-repo').disabled = true;
    document.getElementById('build-action-package-names').disabled = false;
    Array.from(document.getElementById('build-action-flags').getElementsByTagName('label')).forEach(function(label) {
        label.control.style.display = label.style.display = 'none';
    });
    Array.from(document.getElementById('build-action-settings').getElementsByTagName('tr')).forEach(function(tr) {
        tr.style.display = 'none';
        Array.from(tr.getElementsByTagName('input')).forEach(function(input) {
            input.style.display = 'none';
        });
    });
}

function renderBuildActionActions(actionValue, buildAction, refresh)
{
    const container = document.createElement('span');
    if (!refresh) {
        container.className = 'table-row-actions';
    }
    const id = buildAction.id;
    container.appendChild(renderIconLink(refresh ? 'table-refresh' : 'magnify', buildAction, function() {
        queryBuildActionDetails(id);
        return false;
    }, refresh ? 'Refresh details table' : 'Show details', undefined, '#build-action-details-section&' + id));
    container.appendChild(renderIconLink('restart', buildAction, function() {
        if (window.confirm('Do you really want to clone/restart action ' + id + '?')) {
            cloneBuildAction(id);
        }
    }, 'Clone ' + id));
    container.appendChild(renderIconLink('plus', buildAction, function() {
        prepareBuildActionBasedOnExistingOne(buildAction);
        switchToBuildActions();
    }, 'Create new build action based on ' + id));
    container.appendChild(renderIconLink('delete', buildAction, function() {
        if (window.confirm('Do you really want to delete action ' + id + '?')) {
            deleteBuildAction(id);
        }
    }, 'Delete ' + id));
    if (buildAction.status !== 0 && buildAction.status !== 4) {
        container.appendChild(renderIconLink('stop', buildAction, function() {
            if (window.confirm('Do you really want to stop/decline action ' + id + '?')) {
                stopBuildAction(id);
            }
        }, 'Stop/decline ' + id));
    }
    if (buildAction.status === 0) {
        container.appendChild(renderIconLink('play', buildAction, function() {
            if (window.confirm('Do you really want to start action ' + id + '?')) {
                startBuildAction(id);
            }
        }, 'Start ' + id));
    }
    return container;
}

function showBuildActions(ajaxRequest)
{
    if (!window.globalInfo) {
        window.functionsPostponedUntilGlobalInfo.push(showBuildActions.bind(this, ...arguments));
        return;
    }
    const buildActionsList = getAndEmptyElement('build-actions-list');
    if (ajaxRequest.status !== 200) {
        buildActionsList.appendChild(document.createTextNode('Unable to load build actions: ' + ajaxRequest.responseText));
        buildActionsList.appendChild(renderReloadButton(queryBuildActions));
        return;
    }
    const responseJson = JSON.parse(ajaxRequest.responseText);
    if (!Array.isArray(responseJson)) {
        buildActionsList.appendChild(document.createTextNode('Unable to load build actions: response is no array'));
        buildActionsList.appendChild(renderReloadButton(queryBuildActions));
        return;
    }

    // compute clusters of actions which run in sequence
    const buildActionsById = {};
    responseJson.forEach(function(buildAction) {
        buildActionsById[buildAction.id] = buildAction;
    });
    responseJson.forEach(function(buildAction) {
        if (!Array.isArray(buildAction.startAfter)) {
            return;
        }
        const currentActionId = buildAction.id;
        let clusterId = buildAction._clusterId;
        let clusterActions = buildAction._clusterActions;
        buildAction.startAfter.forEach(function(previousActionId) {
            const previousAction = buildActionsById[previousActionId];
            if (!previousAction) {
                return;
            }
            const previousActionClusterId = previousAction._clusterId;
            const previousClusterActions = previousAction._clusterActions;
            if (clusterId !== undefined) {
                if (previousActionClusterId === undefined) {
                    clusterActions.splice(clusterActions.indexOf(buildAction), 0, previousAction);
                    previousAction._clusterId = clusterId;
                    previousAction._clusterActions = clusterActions;
                } else if (previousActionClusterId !== clusterId) {
                    clusterActions.splice(clusterActions.indexOf(buildAction), 0, ...previousClusterActions);
                    previousClusterActions.forEach(function(previousClusterAction) {
                        previousClusterAction._clusterId = clusterId;
                        previousClusterAction._clusterActions = clusterActions;
                    });
                }
            } else if (previousActionClusterId !== undefined) {
                clusterId = buildAction._clusterId = previousAction._clusterId;
                clusterActions = buildAction._clusterActions = previousAction._clusterActions;
                clusterActions.push(buildAction);
            } else {
                clusterId = buildAction._clusterId = previousAction._clusterId = currentActionId;
                clusterActions = buildAction._clusterActions = previousAction._clusterActions = [previousAction, buildAction];
            }
        });
    });

    // compute cluster offsets and make up a color for each cluster
    responseJson.forEach(function(buildAction) {
        const clusterActions = buildAction._clusterActions;
        const clusterOffset = buildAction._clusterOffset = clusterActions ? clusterActions.indexOf(buildAction) : 0;
        buildAction._cc = buildAction.created + ':' + clusterOffset.toString().padStart(4, '0');
        if (!clusterActions) { return; }
        if (buildAction._clusterColor) {
            return;
        }
        const clusterColor = '#' + Math.floor((Math.abs(Math.sin(buildAction._clusterId) * 16777215)) % 16777215).toString(16);
        clusterActions.forEach(a => a._clusterColor = clusterColor);
    });

    // define function to toggle a row
    const toggleRow = function(trElement) {
        trElement.cells[0].getElementsByTagName('input')[0].click();
    };

    // render the table
    const container = renderTableFromJsonArray({
        rows: responseJson,
        rowsPerPage: 10,
        columnHeaders: ['', 'ID', 'Task', 'Type', 'Status', 'Result', 'Created', 'Started', 'Runtime', 'Directory', 'Source repo', 'Destination repo', 'Packages', 'Actions'],
        columnAccessors: ['checkbox', 'id', 'taskName', 'type', 'status', 'result', 'created', 'started', 'finished', 'directory', 'sourceDbs', 'destinationDbs', 'packageNames', 'actions'],
        columnSortAccessors: [null, null, null, null, null, null, '_cc'],
        customRenderer: {
            checkbox: function(value, row) {
                return renderCheckBoxForTableRow(value, row, function(row) {
                    return row.name;
                });
            },
            actions: renderBuildActionActions,
            id: function(value, row) {
                return renderLink(value, row, function() {
                    queryBuildActionDetails(row.id);
                    return false;
                }, 'Show details', undefined, '#build-action-details-section&' + row.id);
            },
            taskName: function (value) {
                if (!value) {
                    return renderNoneInGrey();
                }
                return document.createTextNode(getProperty(globalInfo.presets.tasks[value], 'name', value));
            },
            status: function(value) {
                return document.createTextNode(getProperty(globalInfo.buildActionStates[value], 'name', 'Invalid/unknown'));
            },
            result: function(value) {
                return renderNoneInGrey(getProperty(globalInfo.buildActionResults[value], 'name', 'Invalid/unknown'));
            },
            type: function(value) {
                return document.createTextNode(getProperty(globalInfo.buildActionTypes[value], 'name', 'Invalid/debugging'));
            },
            created: renderShortTimeStamp,
            started: renderShortTimeStamp,
            finished: function (value, row) {
                return renderTimeSpan(row.started, value);
            },
            sourceDbs: renderArrayElidedAsCommaSeparatedString,
            destinationDbs: renderArrayElidedAsCommaSeparatedString,
            packageNames: function(value) {
                return renderNoneInGrey(!Array.isArray(value) || value.length <= 0 ? 'none' : value.length);
            },
            note: function(rows) {
                const note = document.createElement('p');
                note.appendChild(document.createTextNode(rows.length + ' build actions present '));
                note.appendChild(renderReloadButton(queryBuildActions));
                return note;
            },
        },
        rowHandler: function(row, tr) {
            // toggle checkbox on click; toggle checkbox for whole cluster when clicking edge
            const dataset = tr.dataset;
            dataset.id = row.id;
            if (!dataset.initialized) {
                dataset.initialized = true;
                tr.onclick = function(e) {
                    if (e.defaultPrevented || e.target.type === 'checkbox') { return; }
                    const dataset = tr.dataset;
                    const clusterClass = e.clientX <= 30 ? dataset.clusterClass : undefined;
                    if (!clusterClass) { return toggleRow(tr); }
                    const relevantAction = buildActionsById[dataset.id];
                    const newSelected = !relevantAction.selected;
                    const clusterActions = relevantAction._clusterActions || [];
                    clusterActions.forEach(clusterAction => clusterAction.selected = newSelected);
                    Array.from(tr.parentElement.getElementsByClassName(clusterClass)).forEach(toggleRow);
                };
            }

            // apply highlighting of clusters
            const oldClusterClass = dataset.clusterClass;
            const clusterId = row._clusterId;
            const className = clusterId === undefined ? undefined : 'ba-tr-cluster-' + clusterId;
            if (oldClusterClass === className) {
                return;
            }
            if (oldClusterClass) {
                delete tr.dataset.clusterClass;
                tr.classList.remove(oldClusterClass);
                tr.firstChild.style.borderLeft = '';
            }
            if (clusterId === undefined) {
                return;
            }
            tr.dataset.clusterClass = className;
            tr.classList.add(className);
            tr.firstChild.style.borderLeft = '10px solid ' + row._clusterColor;
            if (oldClusterClass) {
                return;
            }
            const updateRowHighlighting = function(row, highlight) {
                const clusterClass = row.dataset.clusterClass;
                if (!clusterClass) {
                    return;
                }
                const fn = highlight ? 'add' : 'remove';
                Array.from(document.getElementsByClassName(clusterClass)).forEach(function(tr) {
                    tr.classList[fn]('table-row-highlighted');
                });
            }
            tr.addEventListener('mouseover', function() { updateRowHighlighting(this, true); });
            tr.addEventListener('mouseout', function() { updateRowHighlighting(this, false); });
        },
    });
    container.table.sort('_cc', true);
    buildActionsList.appendChild(container);
}

function switchToBuildActionDetails(buildActionIds)
{
    sections['build-action-details'].state.ids = buildActionIds;
    window.preventSectionInitializer = true;
    if (!Array.isArray(buildActionIds) || buildActionIds.length === 0) {
        window.location.hash = '#build-action-details-section';
    } else {
        window.location.hash = '#build-action-details-section&' + encodeURIComponent(buildActionIds.join(','));
    }
    window.preventSectionInitializer = false;
}

function switchToBuildActions()
{
    window.preventSectionInitializer = true;
    window.location.hash = '#build-action-section';
    window.preventSectionInitializer = false;
}

function showBuildActionDetails(ajaxRequest)
{
    if (!window.globalInfo) {
        window.functionsPostponedUntilGlobalInfo.push(showBuildActionDetails.bind(this, ...arguments));
        return;
    }
    if (checkForAjaxError(ajaxRequest, 'show build action')) {
        return;
    }
    const responseJSON = JSON.parse(ajaxRequest.responseText);
    return showBuildActionDetails2(Array.isArray(responseJSON) ? responseJSON : [responseJSON]);
}

function showBuildActionDetails2(buildActions)
{
    const buildActionResults = getAndEmptyElement('build-action-results');
    let buildActionActions = getAndEmptyElement('build-action-details-actions');
    buildActions.forEach(function (buildActionDetails) {
        if (!buildActionActions) {
            buildActionActions = document.createElement('span');
            buildActionActions.className = 'heading-actions';
            buildActionResults.appendChild(buildActionActions);
        }
        buildActionResults.appendChild(renderBuildActionDetailsTable(buildActionDetails));
        buildActionActions.appendChild(renderBuildActionActions(undefined, buildActionDetails, true));
        buildActionActions = undefined;
    });
    switchToBuildActionDetails(buildActions.map(buildAction => buildAction.id));
}

function renderBuildActionDetailsTable(buildActionDetails)
{
    return renderTableFromJsonObject({
        data: buildActionDetails,
        displayLabels: ['ID', 'Task', 'Type', 'Status', 'Result', 'Result data', 'Created', 'Started', 'Finished', 'Start after', 'Directory', 'Source repo', 'Destination repo', 'Packages', 'Flags', 'Settings', 'Log files', 'Artefacts', 'Output'],
        fieldAccessors: ['id', 'taskName', 'type', 'status', 'result', 'resultData', 'created', 'started', 'finished', 'startAfter', 'directory', 'sourceDbs', 'destinationDbs', 'packageNames', 'flags', 'settings', 'logfiles', 'artefacts', 'output'],
        customRenderer: {
            taskName: function (value) {
                if (!value) {
                    return renderNoneInGrey();
                }
                return document.createTextNode(getProperty(globalInfo.presets.tasks[value], 'name', value));
            },
            status: function(value) {
                return document.createTextNode(getProperty(globalInfo.buildActionStates[value], 'name', 'Invalid/unknown'));
            },
            result: function(value) {
                return renderNoneInGrey(getProperty(globalInfo.buildActionResults[value], 'name', 'Invalid/unknown'));
            },
            type: function(value) {
                return document.createTextNode(getProperty(globalInfo.buildActionTypes[value], 'name', 'Invalid/debugging'));
            },
            created: renderTimeStamp,
            started: renderTimeStamp,
            finished: renderTimeStamp,
            startAfter: renderArrayAsCommaSeparatedString,
            sourceDbs: renderArrayAsCommaSeparatedString,
            destinationDbs: renderArrayAsCommaSeparatedString,
            packageNames: renderArrayAsCommaSeparatedString,
            flags: function(value, row) {
                const flagNames = [];
                const typeInfo = globalInfo.buildActionTypes[row.type];
                typeInfo.flags.forEach(function(flag) {
                    if (flag.id & value) {
                        flagNames.push(flag.name);
                    }
                });
                return renderArrayAsCommaSeparatedString(flagNames);
            },
            settings: function(value, row) {
                const typeInfo = globalInfo.buildActionTypes[row.type];
                if (typeInfo.settingNames.length === 0) {
                    return renderNoneInGrey();
                }
                return renderTableFromJsonObject({
                    data: value,
                    displayLabels: typeInfo.settingNames,
                    fieldAccessors: typeInfo.settingParams,
                    defaultRenderer: function (arg1, arg2, arg3) {
                        return renderNoneInGrey(arg1, arg2, arg3, 'default/none');
                    },
                });
            },
            resultData: function(value, row) {
                switch(value.index) {
                case 3: // update info
                    const formElement = document.createElement('form');
                    formElement.className = 'update-info-form';
                    formElement.appendChild(renderTableFromJsonObject({
                        data: value.data,
                        relatedRow: row,
                        displayLabels: ['Version updates', 'Package updates', 'Downgrades', 'Orphans'],
                        fieldAccessors: ['versionUpdates', 'packageUpdates', 'downgrades', 'orphans'],
                        customRenderer: {
                            orphans: renderOrphanPackage,
                            versionUpdates: renderUpdateOrDowngrade,
                            packageUpdates: renderUpdateOrDowngrade,
                            downgrades: renderUpdateOrDowngrade,
                        },
                    }));
                    const addSelectedInput = document.createElement('input');
                    addSelectedInput.type = 'button';
                    addSelectedInput.value = 'Add selected packages for new build action';
                    addSelectedInput.onclick = function () {
                        const buildActionForm = document.getElementById('build-action-form');
                        const packageNamesTextArea = buildActionForm['package-names'];
                        const elements = formElement.elements;
                        const packageNames = [];
                        for (let i = 0, count = elements.length; i != count; ++i) {
                            const element = elements[i];
                            if (element.type === 'checkbox' && element.checked) {
                                packageNames.push(element.value);
                            }
                        }
                        if (packageNamesTextArea.value !== '') {
                            packageNamesTextArea.value += ' ';
                        }
                        packageNamesTextArea.value += packageNames.join(' ');
                    };
                    formElement.appendChild(addSelectedInput);
                    return formElement;
                case 4: // build preparation info
                    return renderTableFromJsonObject({
                        data: value.data,
                        displayLabels: ['Error', 'Warnings', 'Database config', 'Batches', 'Cyclic leftovers', 'Build data'],
                        fieldAccessors: ['error', 'warnings', 'dbConfig', 'batches', 'cyclicLeftovers', 'buildData'],
                        customRenderer: {
                            buildData: renderBuildPreparationResultData,
                            cyclicLeftovers: renderArrayAsCommaSeparatedString,
                            batches: function(batch) {
                                return renderCustomList(batch, renderArrayAsCommaSeparatedString);
                            },
                        },
                    });
                case 7: // reporitory problems
                    const container = document.createElement('div');
                    container.className = 'repo-problems';
                    for (const [database, problems] of Object.entries(value.data)) {
                        const table = renderTableFromJsonArray({
                            rows: problems,
                            columnHeaders: ['Related package', 'Problem description'],
                            columnAccessors: ['pkg', 'desc'],
                            customRenderer: {
                                note: problems.length + ' problems in reporitory ' + database,
                                desc: function(value) {
                                    switch(value.index) {
                                    case 1:
                                        return renderTableFromJsonObject({
                                            data: value.data,
                                            displayLabels: ['Missing dependencies', 'Missing libraries'],
                                            fieldAccessors: ['deps', 'libs'],
                                            customRenderer: {
                                                deps: renderDependency,
                                            },
                                        });
                                     default:
                                        return renderStandardTableCell(value.data);
                                     }
                                },
                            },
                        });
                        table.table.sort('pkg', false);
                        container.appendChild(table);
                    }
                    return container;
                default:
                    return renderStandardTableCell(value.data);
                }
            },
            logfiles: renderBuildActionLogFiles,
            artefacts: renderBuildActionArtefacts,
            output: function(value, row) {
                const isFinished = row.status === 4;
                if (!value && isFinished) {
                    return renderNoneInGrey();
                }
                const targetElement = document.createElement('div');
                if (isFinished) {
                    const terminal = makeTerminal();
                    setupTerminalLater(terminal, targetElement, value);
                    return targetElement;
                }
                const streamingSetup = setupTerminalForStreaming({
                    id: 'output-' + row.id,
                    targetElement: targetElement,
                    path: '/build-action/output?id=' + encodeURIComponent(row.id) + '&offset=' + encodeURIComponent(value.length),
                });
                streamingSetup.startStreaming();
                const terminal = streamingSetup.terminal();
                terminal.setOption('convertEol', true);
                terminal.write(value);
                return streamingSetup.elements;
            },
        },
    });
}

function setupTerminalForStreaming(args)
{
    const id = args.id; // a page-unique ID to make up DOM element IDs as needed
    const targetElement = args.targetElement; // the DOM element to stream contents into
    const path = args.path; // the path to GET contents from via streamRouteIntoTerminal()
    let terminal;
    let ajaxRequest;
    return {
        elements: [targetElement],
        startStreaming: function () {
            terminal = makeTerminal();
            ajaxRequest = streamRouteIntoTerminal('GET', path, terminal);
            setupTerminalLater(terminal, targetElement);
        },
        stopStreaming: function () {
            ajaxRequest.abort();
            ajaxRequest = undefined;
            terminal.dispose();
            terminal = undefined;
        },
        isStreaming: function () {
            return ajaxRequest !== undefined;
        },
        terminal: function () {
            return terminal;
        },
    };
}

function fillBuildActionFromPackageSearch()
{
    const packageNamesTextArea = document.getElementById('build-action-form')['package-names'];
    const data = getFormTableData('package-results-form');
    if (data === undefined) {
        return;
    }
    packageNamesTextArea.value = getSelectedRowProperties(data, 'name').join(' ');
    location.hash = '#build-action-section';
}

function submitBuildAction()
{
    startFormQuery('build-action-form', handleBuildActionResponse);
}

function handleBuildActionResponse(ajaxRequest)
{
    const results = getAndEmptyElement('build-action-results');
    if (ajaxRequest.status !== 200) {
        results.appendChild(document.createTextNode('unable to create build action: ' + ajaxRequest.responseText));
        switchToBuildActionDetails();
        return;
    }
    showBuildActionDetails(ajaxRequest);
}

function renderBuildActionLogFiles(array, obj)
{
    return renderCustomList(array, function(arrayElement) {
        const params = 'id=' + encodeURIComponent(obj.id) + '&name=' + encodeURIComponent(arrayElement);
        const logFilePath = '/build-action/logfile?' + params;
        const newWindowPath = 'log.html#' + params;
        const targetElement = document.createElement('div');
        const streamingSetup = setupTerminalForStreaming({
            id: 'logfile-' + obj.id + '-' + arrayElement,
            targetElement: targetElement,
            path: logFilePath,
        });
        const basicElements = streamingSetup.elements;
        const openInNewWindowLinkElement = renderIconLink('dock-window', obj, function() { window.open(newWindowPath); }, 'Open in new window');
        const downloadLinkElement = renderIconLink('download', obj, function() { window.open(apiPrefix + logFilePath); }, 'Download log');
        const stopStreamingLinkElement = renderIconLink('stop', obj, function() {
            if (!streamingSetup.isStreaming()) {
                return;
            }
            streamingSetup.stopStreaming();
            targetElement.style.display = stopStreamingLinkElement.style.display = 'none';
            emptyDomElement(targetElement);
        }, 'Close log');
        const startStreamingLinkElement = renderLink(arrayElement, obj, function() {
            if (streamingSetup.isStreaming()) {
                return;
            }
            emptyDomElement(targetElement);
            streamingSetup.startStreaming();
            targetElement.style.display = 'block';
            stopStreamingLinkElement.style.display = 'inline-block';
        }, 'Show log file', apiPrefix + logFilePath);
        targetElement.style.display = stopStreamingLinkElement.style.display = 'none';
        [downloadLinkElement, stopStreamingLinkElement].forEach(function(element) {
            element.classList.add('streaming-link');
        });
        return [startStreamingLinkElement, stopStreamingLinkElement, downloadLinkElement, openInNewWindowLinkElement, ...basicElements];
    });
}

function renderBuildActionArtefacts(array, obj)
{
    return renderCustomList(array, function(arrayElement) {
        const path = apiPrefix + '/build-action/artefact?id=' + encodeURIComponent(obj.id) + '&name=' + encodeURIComponent(arrayElement);
        return renderLink(arrayElement, obj, function() {
            window.open(path);
        }, 'Download artefact', path);
    });
}

function isBuildActionSourceAur(buildActionInfo)
{
    const sourceDbs = typeof buildActionInfo === 'object' ? buildActionInfo.sourceDbs : undefined;
    return Array.isArray(sourceDbs) && sourceDbs.length === 1 && sourceDbs[0] == 'aur';
}

function renderUpdateInfoWithCheckbox(id, packageName, versionInfo, sourceFromAur)
{
    const inputElement = document.createElement('input');
    inputElement.type = 'checkbox';
    inputElement.id = id;
    inputElement.value = packageName;
    const labelElement = document.createElement('label');
    labelElement.htmlFor = id;
    if (sourceFromAur) {
        const packageNameLink = document.createElement('a');
        packageNameLink.href = 'https://aur.archlinux.org/packages/' + packageName;
        packageNameLink.target = '_blank';
        packageNameLink.appendChild(document.createTextNode(packageName));
        labelElement.appendChild(packageNameLink);
        labelElement.appendChild(document.createTextNode(': ' + versionInfo));
    } else {
        labelElement.appendChild(document.createTextNode(packageName + ': ' + versionInfo));
    }
    return [inputElement, labelElement];
}

function renderPackageList(packageList)
{
    return renderCustomList(packageList, renderPackage);
}

function renderBuildPreparationBuildData(buildDataForPackage)
{
    return renderTableFromJsonObject({
        data: buildDataForPackage,
        displayLabels: ['Error', 'Warnings', 'Has source', 'Source directory', 'Original/local source directory', 'New packages', 'Existing packages', 'Specified index'],
        fieldAccessors: ['error', 'warnings', 'hasSource', 'sourceDirectory', 'originalSourceDirectory', 'packages', 'existingPackages', 'specifiedIndex'],
        customRenderer: {
            packages: renderPackageList,
            existingPackages: renderPackageList,
        },
    });
}

function makeVersionsString(packages)
{
    const versions = packages.map(package => package.version);
    if (versions.length === 0) {
        return '?';
    } else if (versions.length === 1) {
        return versions[0];
    } else {
        return '(' + versions.join(', ') + ')';
    }
}

function renderBuildPreparationResultData(buildPreparationData)
{
    const elements = [];
    for (const [packageName, buildDataForPackage] of Object.entries(buildPreparationData)) {
        const heading = document.createElement('h4');
        let table;
        heading.className = 'compact-heading';
        heading.appendChild(renderLink(packageName, undefined, function() {
            if (table === undefined) {
                table = renderBuildPreparationBuildData(buildDataForPackage);
                heading.insertAdjacentElement('afterEnd', table);
            } else {
                table.style.display = table.style.display === 'none' ? 'table' : 'none';
            }
        }));
        const versionSpan = document.createElement('span');
        const newVersions = makeVersionsString(buildDataForPackage.packages);
        const existingVersions = makeVersionsString(buildDataForPackage.existingPackages);
        versionSpan.style.fontWeight = 'lighter';
        versionSpan.style.float = 'right';
        versionSpan.appendChild(document.createTextNode(' ' + existingVersions + ' → ' + newVersions));
        heading.appendChild(versionSpan);
        elements.push(heading);
    }
    return elements;
}

function renderOrphanPackage(value, obj, level, row)
{
    return renderCustomList(value, function(package) {
        const packageName = package.name;
        return renderUpdateInfoWithCheckbox(
            'update-info-checkbox-' + packageName + '-' + package.version,
            packageName,
            package.version
        );
        return document.createTextNode();
    }, function(package1, package2) {
        return package1.name.localeCompare(package2.name);
    });
}

function renderUpdateOrDowngrade(value, obj, level, row)
{
    const sourceFromAur = isBuildActionSourceAur(row);
    return renderCustomList(value, function(updateInfo) {
        const oldVersion = updateInfo.oldVersion;
        const newVersion = updateInfo.newVersion;
        const packageName = oldVersion.name;
        return renderUpdateInfoWithCheckbox(
            'update-info-checkbox-' + packageName + '-' + oldVersion.version + '-' + newVersion.version,
            packageName,
            oldVersion.version + ' → ' + newVersion.version,
            sourceFromAur
        );
    }, function(updateInfo1, updateInfo2) {
        return updateInfo1.oldVersion.name.localeCompare(updateInfo2.oldVersion.name);
    });
}

function triggerToolbarAction(toolbarElement)
{
    const confirmQuestion = toolbarElement.dataset.confirmation;
    if (confirmQuestion !== undefined && !window.confirm(confirmQuestion)) {
        return false;
    }
    toolbarElement.disabled = true;
    queryRoute(toolbarElement.dataset.method, toolbarElement.dataset.action, function(ajaxRequest) {
        window.alert(ajaxRequest.responseText);
        toolbarElement.disabled = false;
    });
    return false;
}

function deleteSelectedActions()
{
    const data = getFormTableData('build-actions-list');
    if (data === undefined) {
        return;
    }
    const ids = getSelectedRowProperties(data, 'id');
    if (ids.length && window.confirm('Do you really want to delete the build action(s) ' + ids.join(', ') + '?')) {
        deleteBuildAction(ids);
    }
}

function showSelectedActions()
{
    const data = getFormTableData('build-actions-list');
    if (data === undefined) {
        return;
    }
    const ids = getSelectedRowProperties(data, 'id');
    if (ids.length) {
        queryBuildActionDetails(ids);
    }
}

function initBuildActionDetails(sectionElement, sectionData, newHashParts)
{
    const currentBuildActionIds = sectionData.state.ids;
    const hasCurrentlyBuildActions = Array.isArray(currentBuildActionIds) && currentBuildActionIds.length !== 0;
    if (!newHashParts.length) {
        if (hasCurrentlyBuildActions) {
            window.preventHandlingHashChange = true;
            window.location.hash = '#build-action-details-section&' + encodeURIComponent(currentBuildActionIds.join(','));
            window.preventHandlingHashChange = false;
        }
        return true;
    }
    const newBuildActionIds = newHashParts[0].split(',');
    if (!hasCurrentlyBuildActions || newBuildActionIds.some(id => !currentBuildActionIds.find(currentId => id == currentId))) { // possible type conversion wanted
        queryBuildActionDetails(newBuildActionIds);
    }
    return true;
}