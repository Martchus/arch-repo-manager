import * as AjaxHelper from './ajaxhelper.js';
import * as BuildActionsPage from './buildactionspage.js';
import * as PackageSearchPage from './packagesearchpage.js';
import * as CustomRendering from './customrendering.js';
import * as GenericRendering from './genericrendering.js';
import * as Utils from './utils.js';

const status = {repoNames: undefined, defaultArch: undefined};

export function queryGlobalStatus(additionalParams)
{
    additionalParams = additionalParams === undefined ? '' : '?' + additionalParams;
    AjaxHelper.queryRoute('GET', '/status' + additionalParams, handleGlobalStatusUpdate, 'global');
    return true;
}

function handleGlobalStatusUpdate(ajaxRequest)
{
    const globalStatus = Utils.getAndEmptyElement('global-status');
    let responseText = ajaxRequest.responseText;
    if (ajaxRequest.status !== 200) {
        globalStatus.appendChild(document.createTextNode('unable to load global status: ' + ajaxRequest.responseTextDisplay));
        globalStatus.appendChild(document.createTextNode(' '));
        globalStatus.appendChild(CustomRendering.renderReloadButton(queryGlobalStatus));
        return;
    }
    const responseJson = JSON.parse(responseText);
    const applicationVersion = responseJson.version;
    if (applicationVersion) {
        Utils.getAndEmptyElement('application-version').appendChild(document.createTextNode(applicationVersion));
    }
    const applicationURL = responseJson.url;
    if (applicationURL) {
        document.getElementById('source-code-repo-link').href = applicationURL;
    }
    const dbStats = responseJson.config.dbStats;
    const dbTable = GenericRendering.renderTableFromJsonArray({
        rows: dbStats,
        columnHeaders: ['Arch', 'Database', 'Package count', 'Last update', 'Synced from mirror'],
        columnAccessors: ['arch', 'name', 'packageCount', 'lastUpdate', 'syncFromMirror'],
        customRenderer: {
            name: function (value, row) {
                return GenericRendering.renderLink(value, row, searchRepository, undefined, undefined, hashToSearchRepository(row));
            },
            lastUpdate: GenericRendering.renderShortTimeStamp,
            note: function(rows) {
                const note = document.createElement('div');
                const totalPackageCount = rows.reduce((acc, cur) => acc + cur.packageCount, 0);
                note.className = 'form-row';
                note.appendChild(document.createTextNode(rows.length + ' databases and ' + totalPackageCount + ' packages '));
                note.appendChild(CustomRendering.renderReloadButton(queryGlobalStatus));
                return note;
            },
            syncFromMirror: function (value, row) {
                const mirror = row.mainMirror;
                if (mirror) {
                    const link = GenericRendering.renderLink(value, row, undefined, mirror, mirror);
                    link.target = 'blank';
                    return link;
                } else {
                    return GenericRendering.renderNoneInGrey(value);
                }
            },
        },
    });
    globalStatus.appendChild(dbTable);

    const resourceUsageHeading = document.createElement('h2');
    resourceUsageHeading.appendChild(document.createTextNode('Resource usage'));
    globalStatus.appendChild(resourceUsageHeading);
    const resTable = GenericRendering.renderTableFromJsonObject({
        data: responseJson.resourceUsage,
        displayLabels: [
            'Virtual memory', 'Resident set size', 'Peak resident set size', 'Shared resident set size',
            'Package-DB size', 'Actions-DB size', 'Cached packages', 'Actions', 'Running actions',
        ],
        fieldAccessors: [
            'virtualMemory', 'residentSetSize', 'peakResidentSetSize', 'sharedResidentSetSize',
            'packageDbSize', 'actionsDbSize', 'cachedPackages', 'actionsCount', 'runningActionsCount',
        ],
        customRenderer: {
            virtualMemory: GenericRendering.renderDataSize,
            residentSetSize: GenericRendering.renderDataSize,
            peakResidentSetSize: GenericRendering.renderDataSize,
            sharedResidentSetSize: GenericRendering.renderDataSize,
            packageDbSize: GenericRendering.renderDataSize,
            actionsDbSize: GenericRendering.renderDataSize,
        },
    });
    globalStatus.appendChild(resTable);

    // update database selections
    const repoSelections = [
        Utils.getAndEmptyElement('build-action-source-repo', {'build-action-source-repo-none': 'keep'}),
        Utils.getAndEmptyElement('build-action-destination-repo', {'build-action-destination-repo-none': 'keep'}),
        Utils.getAndEmptyElement('package-search-db', {'package-search-db-any': 'keep'}),
    ];
    status.repoNames = [];
    status.defaultArch = responseJson.defaultArch || 'x86_64';
    dbStats.forEach(function (dbInfo) {
        const repoName = Utils.makeRepoName(dbInfo.name, dbInfo.arch);
        status.repoNames.push(repoName);
        repoSelections.forEach(function (selection) {
            const id = selection.id;
            const option = document.createElement('option');
            option.text = repoName;
            option.dataset.arch = dbInfo.arch;
            selection.appendChild(option);
            const filterSel = document.getElementById(id + '-arch-filter');
            if (!filterSel) {
                return;
            }
            const filterOptId = filterSel.id + dbInfo.arch;
            if (!filterSel.options.namedItem(filterOptId)) {
                const filterOpt = document.createElement('option');
                filterOpt.id = filterOptId;
                filterOpt.text = dbInfo.arch;
                filterOpt.defaultSelected = dbInfo.arch === status.defaultArch;
                filterSel.add(filterOpt);
            }
        });
    });

    // make arch filters actually do something
    repoSelections.forEach(function (selection) {
        const filterSel = document.getElementById(selection.id + '-arch-filter');
        if (!filterSel) {
            return;
        }
        filterSel.onchange = function() {
            const filterVal = !filterSel.selectedIndex ? undefined : filterSel.value;
            Array.from(selection.options).forEach(function(option) {
                option.selected = filterVal ? (filterVal === option.dataset.arch) : (!option.dataset.arch);
                option.style.display = !filterVal || filterVal === option.dataset.arch ? 'block' : 'none';
            });
        };
        filterSel.onchange();
    });

    // update lists of build action states, results and types
    const globalInfo = window.globalInfo = {};
    const actions = responseJson.actions;
    [{jsonKey: 'states', variable: 'buildActionStates'},
     {jsonKey: 'results', variable: 'buildActionResults'},
     {jsonKey: 'types', variable: 'buildActionTypes'},
     {jsonKey: 'types', variable: 'buildActionTypesByParam', field: 'type'},
    ].forEach(function (mapping) {
        const variable = globalInfo[mapping.variable] = {};
        actions[mapping.jsonKey].forEach(function(entry) {
            variable[entry[mapping.field || 'id']] = entry;
        });
    });

    // update build action form and make settingNames/settingParams arrays for build action details
    const buildActionTypeSelect = Utils.getAndEmptyElement('build-action-type');
    const buildActionFlagsContainer = Utils.getAndEmptyElement('build-action-flags');
    const buildActionSettingsTable = Utils.getAndEmptyElement('build-action-settings');
    let optgroupElements = {};
    const makeOrReuseOptgroup = function (label) {
        const existingOptgroupElement = optgroupElements[label];
        if (existingOptgroupElement) {
            return existingOptgroupElement;
        }
        const optgroupElement = document.createElement('optgroup');
        optgroupElement.label = label;
        return optgroupElements[label] = optgroupElement;
    };
    actions.types.forEach(function (typeInfo) {
        const typeId = typeInfo.id;
        if (typeId === 0) {
            return; // skip invalid type
        }
        const type = typeInfo.type;
        const category = typeInfo.category || 'Misc';
        const optionElement = document.createElement('option');
        optionElement.value = type;
        optionElement.dataset.id = typeId;
        optionElement.appendChild(document.createTextNode(typeInfo.name));
        const optgroupElement = makeOrReuseOptgroup(category);
        optgroupElement.appendChild(optionElement);
        if (!optgroupElement.parentElement) {
            buildActionTypeSelect.appendChild(optgroupElement);
        }
        typeInfo.flags.forEach(function (flag) {
            const param = flag.param;
            const input = document.createElement('input');
            input.id = 'build-action-' + type + '-' + param;
            input.type = 'checkbox';
            input.name = param;
            input.value = 1;
            input.dataset.id = flag.id;
            const label = document.createElement('label');
            label.htmlFor = input.id;
            label.dataset.relevantType = type;
            label.appendChild(document.createTextNode(flag.name));
            label.title = flag.desc;
            buildActionFlagsContainer.appendChild(input);
            buildActionFlagsContainer.appendChild(label);
        });
        const settingNames = typeInfo.settingNames = [];
        const settingParams = typeInfo.settingParams = [];
        typeInfo.settings.forEach(function (setting) {
            const name = setting.name;
            const param = setting.param;
            const label = document.createElement('label');
            const input = document.createElement('input');
            input.id = 'build-action-' + type + '-' + param;
            input.type = 'text';
            input.name = param;
            label.htmlFor = input.id;
            label.appendChild(document.createTextNode(name + ': '));
            label.title = setting.desc;
            const labelTh = document.createElement('th');
            labelTh.style.fontWeight = 'normal';
            labelTh.appendChild(label);
            const inputTd = document.createElement('td');
            inputTd.appendChild(input);
            const tr = document.createElement('tr');
            tr.appendChild(labelTh);
            tr.appendChild(inputTd);
            tr.dataset.relevantType = type;
            buildActionSettingsTable.appendChild(tr);
            settingNames.push(name);
            settingParams.push(param);
        });
    });

    // update presets/tasks form
    const buildActionPresetSelect = Utils.getAndEmptyElement('build-action-task', {'build-action-task-none': 'keep'});
    const presets = responseJson.presets;
    globalInfo.presets = presets;
    optgroupElements = {};
    const sortedTasks = Object.entries(presets.tasks).sort(([,a],[,b]) => {
        const categoryCmp = a.category.localeCompare(b.category);
        return categoryCmp !== 0 ? categoryCmp : a.name.localeCompare(b.name);
    });
    for (const [presetId, presetInfo] of sortedTasks) {
        const category = presetInfo.category ?? 'Misc';
        const optionElement = document.createElement('option');
        optionElement.value = presetId;
        optionElement.appendChild(document.createTextNode(presetInfo.name));
        const optgroupElement = makeOrReuseOptgroup(category);
        optgroupElement.appendChild(optionElement);
        if (!optgroupElement.parentElement) {
            buildActionPresetSelect.appendChild(optgroupElement);
        }
    }

    window.functionsPostponedUntilGlobalInfo.forEach(function(f) { f(); });
    window.functionsPostponedUntilGlobalInfo = [];

    BuildActionsPage.handleBuildActionTypeChange();
    BuildActionsPage.handleBuildActionPresetChange();
}

function searchRepository(value, dbInfo)
{
    const form = PackageSearchPage.initSearchForm();
    form.name.value = '';
    form.db.value = Utils.makeRepoName(dbInfo.name, dbInfo.arch);
    form.mode.value = 'name-contains'; 
    PackageSearchPage.searchForPackages();
}

function hashToSearchRepository(dbInfo)
{
    return '#package-search-section?'
        + encodeURIComponent('name=&mode=name-contains&db='
        + encodeURIComponent(Utils.makeRepoName(dbInfo.name, dbInfo.arch)));
}

window.globalInfo = undefined;
window.functionsPostponedUntilGlobalInfo = [];
window.hasGlobalStatus = false;
window.buildActionStates = {};
window.buildActionResults = {};
window.buildActionTypes = {};
