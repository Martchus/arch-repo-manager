export const apiPrefix = 'api/v0';
let authError = false;
let ongoingRequests = {};

/// \brief Makes an AJAX query with basic error handling.
export function queryRoute(method, path, callback, type)
{
    if (type) {
        const ongoingRequest = ongoingRequests[type];
        if (ongoingRequest) {
            ongoingRequest.abort();
        }
        const navElement = document.getElementById(type + '-nav-link');
        if (navElement) {
            navElement.classList.add('progress');
        }
    }

    const ajaxRequest = new XMLHttpRequest();
    ajaxRequest.onreadystatechange = function() {
        if (this.readyState === 4) {
            if (type) {
                if (ongoingRequests[type] !== ajaxRequest) {
                    return;
                }
                delete ongoingRequests[type];
                const navElement = document.getElementById(type + '-nav-link');
                if (navElement) {
                    navElement.classList.remove('progress');
                }
            }
            const status = this.status;
            authError = status === 403;
            switch (status) {
            case 401:
                return queryRoute(method, path, callback);
            case 403:
                return window.alert('Authentication failed. Try again.');
            default:
                try {
                    return callback(this, status === 200);
                } catch (e) {
                    window.alert('Unable to process server response: ' + e);
                    throw e;
                }
            }
        }
    };
    const args = [method, apiPrefix + path, true];
    if (authError) {
        args.push('try', 'again');
    }
    ajaxRequest.open(...args);
    ajaxRequest.send();
    if (type) {
        ongoingRequests[type] = ajaxRequest;
    }
    return ajaxRequest;
}

/// \brief Makes an AJAX query for the specified form.
export function startFormQuery(formId, handler)
{
    return startFormQueryEx(formId, handler).ajaxRequest;
}

/// \brief Makes an AJAX query for the specified form.
export function startFormQueryEx(formId, handler)
{
    const form = document.getElementById(formId);
    const params = makeFormQueryParameter(form);
    const queryType = formId.endsWith('-form') ? formId.substr(0, formId.length - 5) : formId;
    return {
        ajaxRequest: queryRoute(form.method, form.getAttribute('action') + params, handler, queryType),
        form: form,
        params, params,
    };
}

/// \brief Returns the query parameter for the specified \a form element.
function makeFormQueryParameter(form)
{
    const params = [];
    const formElements = form.elements;
    for (let i = 0, count = formElements.length; i != count; ++i) {
        const formElement = formElements[i];
        if (formElement.disabled || formElement.style.display === 'none') {
            continue; // if we disable a form element or hide it via CSS we also don't want to submit its data
        }
        const type = formElement.type;
        if ((type === 'checkbox' || type === 'radio') && !formElement.checked) {
            continue;
        }
        const name = formElement.name;
        if (name === undefined || name.length < 1) {
            continue;
        }
        else if (name === 'package-names') {
            const packageNames = formElement.value.split(/[,\s]+/);
            packageNames.forEach(function (packageName) {
                if (packageName.length <= 0) {
                    return;
                }
                params.push(['package', encodeURIComponent(packageName)].join("="));
            });
            continue;
        }
        const values = [];
        const options = formElement.options;
        if (options !== undefined) {
            for (let i = 0, end = options.length; i != end; ++i) {
                const option = options[i];
                if (option.selected && option.dataset.ignore) {
                    continue;
                }
                const value = option.value;
                if (option.selected && value !== undefined && value !== 'none' && value !== 'None') {
                    values.push(value);
                }
            }
        } else if (formElement.value !== undefined) {
            values.push(formElement.value);
        }
        values.forEach(function(value) {
            params.push([encodeURIComponent(name), encodeURIComponent(value)].join("="));
        });
    }
    if (params.length < 1) {
        return "";
    }
    return "?" + params.join("&");
}

export function makeIdParams(ids)
{
    if (!Array.isArray(ids)) {
        ids = [ids];
    }
    return ids.map(id => 'id=' + encodeURIComponent(id)).join('&');
}

/// \brief Shows an alert for the specified AJAX request.
export function showAjaxError(xhr, action)
{
    let errorMessage;
    try {
        errorMessage = JSON.parse(xhr.responseText).error;
    } catch (e) {
        errorMessage = xhr.responseText;
    }
    if (!errorMessage) {
        errorMessage = 'unknown error';
    }
    window.alert('Unable to ' + action + ': ' + errorMessage);
}

/// \brief Returns whether the specified AJAX request failed and shows an alert it it did.
export function checkForAjaxError(xhr, action)
{
    if (xhr.status === 200) {
        return false;
    }
    showAjaxError(xhr, action);
    return true;
}
