cmake_minimum_required(VERSION 3.1.0 FATAL_ERROR)

# add project files
set(HEADER_FILES)
set(SRC_FILES main.cpp)
set(TEST_HEADER_FILES)
set(TEST_SRC_FILES)
set(HTML_FILES static/index.html static/log.html)
set(JAVA_SCRIPT_FILES
    static/js/ajaxhelper.js
    static/js/buildactionspage.js
    static/js/customrendering.js
    static/js/genericrendering.js
    static/js/packagerendering.js
    static/js/globalstatuspage.js
    static/js/log.js
    static/js/packagedetailspage.js
    static/js/packagesearchpage.js
    static/js/singlepage.js
    static/js/terminal.js
    static/js/utils.js
    static/js/main.js
    static/node_modules/xterm/lib/xterm.js
    static/node_modules/xterm-addon-search/lib/xterm-addon-search.js)
set(CSS_FILES static/css/basics.css static/css/genericrendering.css static/css/layout.css static/css/log.css
              static/css/specifics.css static/node_modules/xterm/css/xterm.css)
set(IMG_FILES
    static/img/logo.svg
    static/img/icon/content-save.svg
    static/img/icon/chevron-left.svg
    static/img/icon/power.svg
    static/img/icon/play.svg
    static/img/icon/magnify.svg
    static/img/icon/select-all.svg
    static/img/icon/select-off.svg
    static/img/icon/plus.svg
    static/img/icon/delete.svg
    static/img/icon/undo-variant.svg
    static/img/icon/restart.svg
    static/img/icon/reload.svg
    static/img/icon/dock-window.svg
    static/img/icon/download.svg
    static/img/icon/stop.svg
    static/img/icon/database-white.svg
    static/img/icon/magnify-white.svg
    static/img/icon/package-variant-white.svg
    static/img/icon/cogs-white.svg
    static/img/icon/console-line-white.svg
    static/img/icon/table-refresh.svg
    static/img/icon/graph.svg)
set(CMAKE_MODULE_FILES cmake/modules/SystemdService.cmake)
set(CMAKE_TEMPLATE_FILES cmake/templates/systemd.service.in cmake/templates/sysusers.conf.in
                         cmake/templates/tmpfiles.conf.in)

# meta data
set(META_PROJECT_NAME buildservice)
set(META_PROJECT_TYPE application)
set(META_PROJECT_VARNAME BUILD_SERVICE)
set(META_APP_NAME "Repo manager and package builder for Arch Linux")
set(META_APP_AUTHOR "Martchus")
set(META_APP_DESCRIPTION
    "Daemon and web application for building Arch Linux packages and managing custom Arch Linux repositories")
set(META_VERSION_MAJOR 0)
set(META_VERSION_MINOR 0)
set(META_VERSION_PATCH 1)

# include CMake modules from own project directory
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules" "${CMAKE_MODULE_PATH}")

# find c++utilities
set(CONFIGURATION_PACKAGE_SUFFIX
    ""
    CACHE STRING "sets the suffix for find_package() calls to packages configured via c++utilities")
find_package(c++utilities${CONFIGURATION_PACKAGE_SUFFIX} 5.4.0 REQUIRED)
use_cpp_utilities()

# find backend libraries
find_package(librepomgr ${META_APP_VERSION} REQUIRED)
use_librepomgr()

# apply basic configuration
include(BasicConfig)
include(WindowsResources)
include(AppTarget)
include(ShellCompletion)
include(ConfigHeader)

# add install target for web files
set(WEB_FILES ${HTML_FILES} ${JAVA_SCRIPT_FILES} ${CSS_FILES} ${IMG_FILES})
set(WEB_FILES_INSTALL_DESTINATION "${CMAKE_INSTALL_PREFIX}/${META_DATA_DIR}/web")
foreach (WEB_FILE ${WEB_FILES})
    get_filename_component(WEB_DIR "${WEB_FILE}" DIRECTORY)
    install(
        FILES "${WEB_FILE}"
        DESTINATION "${WEB_FILES_INSTALL_DESTINATION}/${WEB_DIR}"
        COMPONENT web-files)
endforeach ()
if (NOT TARGET install-web-files)
    add_custom_target(install-web-files COMMAND "${CMAKE_COMMAND}" -DCMAKE_INSTALL_COMPONENT=web-files -P
                                                "${CMAKE_BINARY_DIR}/cmake_install.cmake")
endif ()

# add installs for config file template
install(
    FILES doc/server-config-example.conf doc/presets-example.json doc/nginx-config-example.conf
    DESTINATION "${CMAKE_INSTALL_PREFIX}/${META_DATA_DIR}/skel"
    COMPONENT config)

# make files for systemd service
include(SystemdService)
add_systemd_service(ARGUMENTS run -c "/etc/${META_PROJECT_NAME}${META_CONFIG_SUFFIX}/server.conf")
