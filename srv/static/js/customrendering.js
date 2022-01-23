import * as GenericRendering from './genericrendering.js';
import * as Utils from './utils.js';

/// \brief Renders a dependency object.
export function renderDependency(value)
{
    if (value.length < 1) {
        return GenericRendering.renderArrayAsCommaSeparatedString(value);
    }
    const list = document.createElement('ul');
    list.className = 'dependency-list';
    value.forEach(function (dependency) {
        const item = document.createElement('li');
        let res = dependency.name;
        if (dependency.version) {
            const modes = [undefined, undefined, '=', '>=', '<=', '>', '<'];
            const mode = modes[dependency.mode];
            if (mode !== undefined) {
                res += mode + dependency.version;
            }
        }
        item.appendChild(document.createTextNode(res));
        if (dependency.description) {
            const descriptionSpan = document.createElement('span');
            descriptionSpan.appendChild(document.createTextNode(' - ' + dependency.description));
            descriptionSpan.style.fontStyle = 'italics';
            item.appendChild(descriptionSpan);
        }
        list.appendChild(item);
    });
    return list;
}

/// \brief Renders a "Reload" button invoking the specified \a handler when clicked.
export function renderReloadButton(handler)
{
    const reloadButton = document.createElement('button');
    reloadButton.className = 'icon-button icon-reload';
    reloadButton.type = 'button';
    reloadButton.onclick = handler;
    reloadButton.appendChild(document.createTextNode('Reload'));
    return reloadButton;
}

/// \brief Renders an icon.
export function renderIcon(iconName)
{
    const icon = document.createElement('span');
    icon.className = 'icon icon-' + iconName;
    return icon;
}

/// \brief Renders an icon link which will invoke the specified \a handler when clicked.
export function renderIconLink(value, row, handler, tooltip, href, middleClickHref)
{
    const link = GenericRendering.renderLink(renderIcon(value), row, handler, tooltip, href, middleClickHref);
    link.className = 'icon-link';
    return link;
}

const labelsWithoutBasics = ['Architecture', 'Repository', 'Description', 'Upstream URL', 'License(s)', 'Groups', 'Package size', 'Installed size', 'Packager', 'Build date', 'Dependencies', 'Optional dependencies', 'Make dependencies', 'Check dependencies', 'Provides', 'Replaces', 'Conflicts', 'Contained libraries', 'Needed libraries', 'Files'];
const fieldsWithoutBasics = ['packageInfo.arch', 'db', 'description', 'upstreamUrl', 'licenses', 'groups', 'packageInfo.size', 'installInfo.installedSize', 'packageInfo.packager', 'packageInfo.buildDate', 'dependencies', 'optionalDependencies', 'sourceInfo.makeDependencies', 'sourceInfo.checkDependencies', 'provides', 'replaces', 'conflicts', 'libprovides', 'libdepends', 'packageInfo.files'];
const labelsWithBasics = ['Name', 'Version', ...labelsWithoutBasics];
const fieldsWithBasics = ['name', 'version', ...fieldsWithoutBasics];

export function renderPackage(packageObj, withoutBasics)
{
    const table = GenericRendering.renderTableFromJsonObject({
        data: packageObj,
        displayLabels: withoutBasics ? labelsWithoutBasics : labelsWithBasics,
        fieldAccessors: withoutBasics ? fieldsWithoutBasics : fieldsWithBasics,
        customRenderer: {
            db: function(value, row) {
                return document.createTextNode(Utils.makeRepoName(value, row.dbArch));
            },
            upstreamUrl: function(value, row) {
                return GenericRendering.renderLink(value, row, function(value) {
                    window.open(value);
                });
            },
            licenses: GenericRendering.renderArrayAsCommaSeparatedString,
            groups: GenericRendering.renderArrayAsCommaSeparatedString,
            dependencies: renderDependency,
            optionalDependencies: renderDependency,
            provides: renderDependency,
            replaces: renderDependency,
            conflicts: renderDependency,
            libprovides: GenericRendering.renderArrayAsCommaSeparatedString,
            libdepends: GenericRendering.renderArrayAsCommaSeparatedString,
            'sourceInfo.makeDependencies': renderDependency,
            'sourceInfo.checkDependencies': renderDependency,
            'packageInfo.arch': function(value, row) {
                const sourceInfo = row.sourceInfo;
                const sourceArchs = sourceInfo !== undefined ? sourceInfo.archs : undefined;
                if (Array.isArray(sourceArchs) && sourceArchs.length) {
                    return GenericRendering.renderArrayAsCommaSeparatedString(sourceArchs);
                } else {
                    return GenericRendering.renderNoneInGrey(value);
                }
            },
            'packageInfo.size': GenericRendering.renderDataSize,
            'installInfo.installedSize': GenericRendering.renderDataSize,
        },
    });
    table.className = 'package-details-table';
    return table;
}
