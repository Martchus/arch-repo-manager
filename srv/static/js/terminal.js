import * as AjaxHelper from './ajaxhelper.js';

/// \brief Returns a new terminal created via xterm.js.
export function makeTerminal()
{
    const terminal = new Terminal({
        disableStdin: true,
        convertEol: true,
        scrollback: 500000,
        cols: 120,
    });
    return terminal;
}

/// \brief Adds a search for the specified \a terminal to the specified \a targetElement.
export function addSearchToTerminal(terminal, targetElement)
{
    const searchAddon = new SearchAddon();
    // FIXME: import the search addon correctly
    //import('../node_modules/xterm-addon-search/lib/xterm-addon-search.js').then(function(module) {
    //    const searchAddon = new module.SearchAddon();
    //    terminal.loadAddon(searchAddon);
    //});
    terminal.loadAddon(searchAddon);

    const searchInput = document.createElement('input');
    searchInput.placeholder = 'Search';
    searchInput.style.width = '100%';
    searchInput.style.boxSizing = 'border-box';
    searchInput.onkeyup = function(event) {
        event.preventDefault();
        if (event.keyCode === 13) {
            const res = (event.shiftKey ? searchAddon.findPrevious(searchInput.value) : searchAddon.findNext(searchInput.value));
            searchInput.style.backgroundColor = (res ? '#83d883' : '#d5aaaf');
        }
    };
    targetElement.appendChild(searchInput);

    return searchAddon;
}

/// \brief Initializes the specified \a terminal within the specified \a targetElement writing the specified initial \a value
///        to the terminal.
/// \remarks This initialization only works if \a targetElement is already part of the rendered HTML page. Hence this function
///          uses window.setTimeout to ensure \a targetElement is rendered.
export function setupTerminalLater(terminal, targetElement, value)
{
    window.setTimeout(function() {
        terminal.open(targetElement);
        if (value !== undefined) {
            terminal.write(value);
        }
        addSearchToTerminal(terminal, targetElement);
        // ensure the scroll bar on the right side is not clipped
        targetElement.style.minWidth = terminal.element.scrollWidth + 20 + 'px';
    }, 100);
}

/// \brief Makes an AJAX query and writes the received data to the specified \a terminal.
/// \remarks If the server responds in chunks, each chunk is written as soon as it arrives.
export function streamRouteIntoTerminal(method, path, terminal)
{
    const ajaxRequest = new XMLHttpRequest();
    let responseWritten = 0;
    ajaxRequest.onreadystatechange = function() {
        const response = this.response;
        if (this.readyState === 3 || this.readyState === 4) {
            terminal.write(response.substr(responseWritten));
            responseWritten = response.length;
        }
        if (this.readyState === 4) {
            if (this.status !== 200) {
                terminal.write('\r\nUnable to query ' + path + ': ' + this.status + ' response');
            }
        }
    };
    ajaxRequest.open(method, AjaxHelper.apiPrefix + path, true);
    ajaxRequest.send();
    return ajaxRequest;
}
