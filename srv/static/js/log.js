function initLog()
{
    const hashParts = hashAsObject();
    const id = hashParts.id;
    const name = hashParts.name;
    const mainElement = getAndEmptyElement('log-container');
    if (id === undefined || id === '' || name === undefined || name === '') {
        document.title += ' - logfile';
        mainElement.appendChild(document.createTextNode('id or name invalid'));
        return;
    }
    const path = '/build-action/logfile?id=' + encodeURIComponent(id) + '&name=' + encodeURIComponent(name);
    const terminal = makeTerminal();
    const ajaxRequest = streamRouteIntoTerminal('GET', path, terminal);
    document.title += ' - ' + name;
    terminal.resize(180, 500);
    window.setTimeout(function() {
        addSearchToTerminal(terminal, mainElement);
        terminal.open(mainElement);
    });
}