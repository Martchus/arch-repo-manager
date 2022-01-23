import * as GlobalStatusPage from './globalstatuspage.js';
import * as Utils from './utils.js';

export let sections = {};
export let sectionNames = [];

/// \brief 'main()' function which initializes the single page app.
export function initPage(pageSections)
{
    sections = pageSections;
    sectionNames = Object.keys(sections);
    handleHashChange();
    document.body.onhashchange = handleHashChange;
    document.getElementById('logo-link').onclick = function () {
        document.getElementById('about-dialog').style.display = 'block';
        return false;
    };
    GlobalStatusPage.queryGlobalStatus();
}

let preventHandlingHashChange = false;
let preventSectionInitializer = false;

/// \brief Shows the current section and hides other sections.
function handleHashChange()
{
    if (preventHandlingHashChange) {
        return;
    }

    const hashParts = Utils.splitHashParts();
    const currentSectionName = hashParts.shift() || 'global-section';
    if (!currentSectionName.endsWith('-section')) {
        return;
    }

    sectionNames.forEach(function (sectionName) {
        const sectionData = sections[sectionName];
        const sectionElement = document.getElementById(sectionName + '-section');
        if (sectionElement.id === currentSectionName) {
            const sectionInitializer = sectionData.initializer;
            if (sectionInitializer === undefined || preventSectionInitializer || sectionInitializer(sectionElement, sectionData, hashParts)) {
                sectionElement.style.display = 'block';
            }
        } else {
            const sectionDestructor = sectionData.destructor;
            if (sectionDestructor === undefined || sectionDestructor(sectionElement, sectionData, hashParts)) {
                sectionElement.style.display = 'none';
            }
        }
        const navLinkElement = document.getElementById(sectionName + '-nav-link');
        if (sectionElement.id === currentSectionName) {
            navLinkElement.classList.add('active');
        } else {
            navLinkElement.classList.remove('active');
        }
    });
}

/// \brief Updates the #hash without triggering the handler.
export function updateHashPreventingChangeHandler(newHash)
{
    preventHandlingHashChange = true;
    window.location.hash = newHash;
    preventHandlingHashChange = false;
}

/// \brief Updates the #hash without triggering the section initializer.
export function updateHashPreventingSectionInitializer(newHash)
{
    preventSectionInitializer = true;
    window.location.hash = newHash;
    preventSectionInitializer = false;
}
