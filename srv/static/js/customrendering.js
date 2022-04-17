import * as GenericRendering from './genericrendering.js';
import * as Utils from './utils.js';

/// \brief Renders a "Reload" button invoking the specified \a handler when clicked.
export function renderReloadButton(handler)
{
    const reloadButton = document.createElement('button');
    reloadButton.className = 'icon-button icon-reload';
    reloadButton.type = 'button';
    reloadButton.onclick = function () { handler() };
    reloadButton.oncontextmenu = function () { return false };
    reloadButton.onmouseup = function (e) {
        if (e.which === 3 || e.button === 2) { // right click
            handler(window.prompt('Enter additional query parameters:'));
        }
    }
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
