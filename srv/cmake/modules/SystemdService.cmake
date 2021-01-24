cmake_minimum_required(VERSION 3.3.0 FATAL_ERROR)

# prevent multiple inclusion
if (DEFINED SYSTEMD_SERVICE_LOADED)
    return()
endif ()
set(SYSTEMD_SERVICE_LOADED YES)

include(CMakeParseArguments)
include(TemplateFinder)
include(ListToString)

function (add_systemd_service)
    # parse arguments
    set(OPTIONAL_ARGS
        SERVICE_NAME
        SERVICE_DESCRIPTION
        USER_NAME
        USER_ID
        USER_DISPLAY_NAME
        EXECUTABLE
        SERVICE_TEMPLATE
        SYSUSERS_TEMPLATE
        TMPFILES_TEMPLATE)
    set(ONE_VALUE_ARGS)
    set(MULTI_VALUE_ARGS ARGUMENTS)
    cmake_parse_arguments(ARGS "${OPTIONAL_ARGS}" "${ONE_VALUE_ARGS}" "${MULTI_VALUE_ARGS}" ${ARGN})
    if (NOT ARGS_SERVICE_NAME)
        set(ARGS_SERVICE_NAME "${META_PROJECT_NAME}${META_CONFIG_SUFFIX}")
    endif ()
    if (NOT ARGS_SERVICE_DESCRIPTION)
        if (META_APP_DESCRIPTION)
            set(ARGS_SERVICE_DESCRIPTION "${META_APP_DESCRIPTION}")
        else ()
            set(ARGS_SERVICE_DESCRIPTION "${ARGS_SERVICE_NAME} service")
        endif ()
    endif ()
    if (NOT ARGS_USER_NAME)
        set(ARGS_USER_NAME "${ARGS_SERVICE_NAME}")
    endif ()
    if (NOT ARGS_USER_ID)
        set(ARGS_USER_ID "-")
    endif ()
    if (NOT ARGS_USER_DISPLAY_NAME)
        if (META_APP_NAME)
            set(ARGS_USER_DISPLAY_NAME "${META_APP_NAME}")
        else ()
            set(ARGS_USER_DISPLAY_NAME "${ARGS_SERVICE_NAME} user")
        endif ()
    endif ()
    if (NOT ARGS_EXECUTABLE)
        set(ARGS_EXECUTABLE "${TARGET_EXECUTABLE}")
    endif ()
    if (NOT ARGS_SERVICE_TEMPLATE)
        set(ARGS_SERVICE_TEMPLATE "systemd.service")
    endif ()
    if (NOT ARGS_SYSUSERS_TEMPLATE)
        set(ARGS_SYSUSERS_TEMPLATE "sysusers.conf")
    endif ()
    if (NOT ARGS_TMPFILES_TEMPLATE)
        set(ARGS_TMPFILES_TEMPLATE "tmpfiles.conf")
    endif ()

    # find templates, make paths for outputs
    find_template_file("${ARGS_SERVICE_TEMPLATE}" ${META_PROJECT_VARNAME} SERVICE_TEMPLATE_FILE)
    find_template_file("${ARGS_SYSUSERS_TEMPLATE}" ${META_PROJECT_VARNAME} SYSUSERS_TEMPLATE_FILE)
    find_template_file("${ARGS_TMPFILES_TEMPLATE}" ${META_PROJECT_VARNAME} TMPFILES_TEMPLATE_FILE)
    set(SERVICE_TARGET_FILE "${CMAKE_CURRENT_BINARY_DIR}/resources/systemd/system/${ARGS_SERVICE_NAME}.service")
    set(SYSUSERS_TARGET_FILE "${CMAKE_CURRENT_BINARY_DIR}/resources/sysusers.d/${ARGS_SERVICE_NAME}.conf")
    set(TMPFILES_TARGET_FILE "${CMAKE_CURRENT_BINARY_DIR}/resources/tmpfiles.d/${ARGS_SERVICE_NAME}.conf")

    # set variables used in templates
    set(SYSTEMD_SERVICE_NAME "${ARGS_SERVICE_NAME}")
    set(SYSTEMD_SERVICE_DESCRIPTION "${ARGS_SERVICE_DESCRIPTION}")
    set(SYSTEMD_SERVICE_USER_NAME "${ARGS_USER_NAME}")
    set(SYSTEMD_SERVICE_USER_ID "${ARGS_USER_ID}")
    set(SYSTEMD_SERVICE_USER_DISPLAY_NAME "${ARGS_USER_DISPLAY_NAME}")
    if (ARGS_ARGUMENTS)
        list_to_string(" " "" "" "${ARGS_ARGUMENTS}" SYSTEMD_SERVICE_EXEC)
        set(SYSTEMD_SERVICE_EXEC "${ARGS_EXECUTABLE} ${SYSTEMD_SERVICE_EXEC}")
    else ()
        set(SYSTEMD_SERVICE_EXEC "${ARGS_EXECUTABLE}")
    endif ()

    # configure files
    configure_file("${SERVICE_TEMPLATE_FILE}" "${SERVICE_TARGET_FILE}")
    configure_file("${SYSUSERS_TEMPLATE_FILE}" "${SYSUSERS_TARGET_FILE}")
    configure_file("${TMPFILES_TEMPLATE_FILE}" "${TMPFILES_TARGET_FILE}")

    # add installs
    install(
        FILES "${SERVICE_TARGET_FILE}"
        DESTINATION lib/systemd/system
        COMPONENT service)
    install(
        FILES "${SYSUSERS_TARGET_FILE}"
        DESTINATION lib/sysusers.d
        COMPONENT service)
    install(
        FILES "${TMPFILES_TARGET_FILE}"
        DESTINATION lib/tmpfiles.d
        COMPONENT service)
    if (NOT TARGET install-service)
        add_custom_target(install-service COMMAND "${CMAKE_COMMAND}" -DCMAKE_INSTALL_COMPONENT=service -P
                                                  "${CMAKE_BINARY_DIR}/cmake_install.cmake")
    endif ()
endfunction ()
