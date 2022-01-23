import * as Terminal from './terminal.js';
import * as Utils from './utils.js';

function initLog()
{
    const hashParts = Utils.hashAsObject();
    const id = hashParts.id;
    const name = hashParts.name;
    const mainElement = Utils.getAndEmptyElement('log-container');
    if (id === undefined || id === '' || name === undefined || name === '') {
        document.title += ' - logfile';
        mainElement.appendChild(document.createTextNode('id or name invalid'));
        return;
    }
    const path = '/build-action/logfile?id=' + encodeURIComponent(id) + '&name=' + encodeURIComponent(name);
    const terminal = Terminal.makeTerminal();
    const ajaxRequest = Terminal.streamRouteIntoTerminal('GET', path, terminal);
    document.title += ' - ' + name;
    terminal.resize(180, 500);
    window.setTimeout(function() {
        Terminal.addSearchToTerminal(terminal, mainElement);
        terminal.open(mainElement);
    });
}

document.body.onhashchange = initLog;
initLog();
