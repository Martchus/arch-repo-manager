/// \brief Renders a dependency object.
function renderDependency(value)
{
    if (value.length < 1) {
        return renderArrayAsCommaSeparatedString(value);
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
function renderReloadButton(handler)
{
    const reloadButton = document.createElement('button');
    reloadButton.className = 'icon-button icon-reload';
    reloadButton.type = 'button';
    reloadButton.onclick = handler;
    reloadButton.appendChild(document.createTextNode('Reload'));
    return reloadButton;
}

/// \brief Renders an icon.
function renderIcon(iconName)
{
    const icon = document.createElement('span');
    icon.className = 'icon icon-' + iconName;
    return icon;
}

/// \brief Renders an icon link which will invoke the specified \a handler when clicked.
function renderIconLink(value, row, handler, tooltip, href, middleClickHref)
{
    const link = renderLink(renderIcon(value), row, handler, tooltip, href, middleClickHref);
    link.className = 'icon-link';
    return link;
}