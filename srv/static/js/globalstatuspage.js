import * as AjaxHelper from './ajaxhelper.js';
import * as BuildActionsPage from './buildactionspage.js';
import * as CustomRendering from './customrendering.js';
import * as GenericRendering from './genericrendering.js';
import * as Utils from './utils.js';

const status = {repoNames: undefined};

export function queryGlobalStatus()
{
    AjaxHelper.queryRoute('GET', '/status', handleGlobalStatusUpdate, 'global');
    return true;
}

function handleGlobalStatusUpdate(ajaxRequest)
{
    const globalStatus = Utils.getAndEmptyElement('global-status');
    let responseText = ajaxRequest.responseText;
    if (ajaxRequest.status === 500) {
        responseText = 'internal server error';
    }
    if (ajaxRequest.status !== 200) {
        globalStatus.appendChild(document.createTextNode('unable to load global status: ' + responseText));
        return;
    }
    const responseJson = JSON.parse(responseText);
    const applicationVersion = responseJson.version;
    if (applicationVersion) {
        Utils.getAndEmptyElement('application-version').appendChild(document.createTextNode(applicationVersion));
    }
    const dbStats = responseJson.config.dbStats;
    const table = GenericRendering.renderTableFromJsonArray({
        rows: dbStats,
        columnHeaders: ['Arch', 'Database', 'Package count', 'Last update', 'Synced from mirror'],
        columnAccessors: ['arch', 'name', 'packageCount', 'lastUpdate', 'syncFromMirror'],
        customRenderer: {
            name: function (value, row) {
                return GenericRendering.renderLink(value, row, showRepository);
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
        },
    });
    globalStatus.appendChild(table);

    // update database selections
    const repoSelections = [
        Utils.getAndEmptyElement('build-action-source-repo', {'build-action-source-repo-none': 'keep'}),
        Utils.getAndEmptyElement('build-action-destination-repo', {'build-action-destination-repo-none': 'keep'}),
        Utils.getAndEmptyElement('package-search-db', {'package-search-db-any': 'keep'}),
    ];
    status.repoNames = [];
    dbStats.forEach(function (dbInfo) {
        const repoName = Utils.makeRepoName(dbInfo.name, dbInfo.arch);
        status.repoNames.push(repoName);
        repoSelections.forEach(function (selection) {
            const option = document.createElement('option');
            option.appendChild(document.createTextNode(repoName));
            selection.appendChild(option);
        });
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
    for (const [presetId, presetInfo] of Object.entries(presets.tasks)) {
        const category = presetInfo.category || 'Misc';
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

function showRepository(dbName, dbInfo)
{
    const mirror = dbInfo.mainMirror;
    if (!mirror) {
        window.alert('No mirror configured for ' + dbName + '.');
    } else {
        window.open(mirror);
    }
}

window.globalInfo = undefined;
window.functionsPostponedUntilGlobalInfo = [];
window.hasGlobalStatus = false;
window.buildActionStates = {};
window.buildActionResults = {};
window.buildActionTypes = {};
