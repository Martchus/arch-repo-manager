cmake_minimum_required(VERSION 3.1.0 FATAL_ERROR)

# add project files
set(HEADER_FILES
    errorhandling.h
    serversetup.h
    resourceusage.h
    helper.h
    json.h
    logcontext.h
    logging.h
    multisession.h
    globallock.h
    authentication.h
    webapi/server.h
    webapi/session.h
    webapi/render.h
    webapi/routes.h
    webapi/routeid.h
    webapi/typedefs.h
    webapi/repository.h
    webapi/params.h
    webclient/aur.h
    webclient/database.h
    webclient/session.h
    buildactions/buildactionmeta.h
    buildactions/buildaction.h
    buildactions/buildactionfwd.h
    buildactions/buildactiontemplate.h
    buildactions/subprocess.h
    buildactions/subprocessfwd.h)
set(SRC_FILES
    json.cpp
    errorhandling.cpp
    serversetup.cpp
    resourceusage.cpp
    globallock.cpp
    authentication.cpp
    webapi/server.cpp
    webapi/session.cpp
    webapi/routes.cpp
    webapi/routes_buildaction.cpp
    webapi/render.cpp
    webapi/params.cpp
    webapi/repository.cpp
    webclient/aur.cpp
    webclient/database.cpp
    webclient/session.cpp
    buildactions/buildactionmeta.cpp
    buildactions/buildactionlivestreaming.cpp
    buildactions/buildaction.cpp
    buildactions/buildactiontemplate.cpp
    buildactions/buildactionprivate.h
    buildactions/customcommand.cpp
    buildactions/updatecheck.cpp
    buildactions/makelicenseinfo.cpp
    buildactions/reloaddatabase.cpp
    buildactions/reloadlibrarydependencies.cpp
    buildactions/reloadconfiguration.cpp
    buildactions/repomanagement.cpp
    buildactions/preparebuild.cpp
    buildactions/conductbuild.cpp)
set(TEST_HEADER_FILES tests/parser_helper.h)
set(TEST_SRC_FILES tests/cppunit.cpp tests/buildactions.cpp tests/utils.cpp tests/webapi.cpp tests/parser_helper.cpp)

# meta data
set(META_PROJECT_NAME librepomgr)
set(META_PROJECT_TYPE library)
set(META_PROJECT_VARNAME LIBREPOMGR)
set(META_APP_AUTHOR "Martchus")
set(META_APP_NAME "Unofficial Arch Linux repository management library")
set(META_APP_DESCRIPTION "Library for managing custom Arch Linux repositories")
set(META_VERSION_MAJOR 0)
set(META_VERSION_MINOR 0)
set(META_VERSION_PATCH 1)
set(META_VERSION_CACHE 11)
set(LINK_TESTS_AGAINST_APP_TARGET ON)

# find c++utilities
set(CONFIGURATION_PACKAGE_SUFFIX
    ""
    CACHE STRING "sets the suffix for find_package() calls to packages configured via c++utilities")
find_package(c++utilities${CONFIGURATION_PACKAGE_SUFFIX} 5.11.0 REQUIRED)
use_cpp_utilities(VISIBILITY PUBLIC)

# find passwordfile
find_package(passwordfile${CONFIGURATION_PACKAGE_SUFFIX} 5.0.0 REQUIRED)
use_password_file()

# find boost libraries
option(BOOST_STATIC_LINKAGE "${STATIC_LINKAGE}" "link statically against Boost (instead of dynamically)")
set(Boost_USE_MULTITHREADED ON)
if (BOOST_STATIC_LINKAGE)
    set(Boost_USE_STATIC_LIBS ON)
endif ()
set(BOOST_ARGS "REQUIRED;COMPONENTS;system;filesystem;iostreams")
use_package(TARGET_NAME Boost::system PACKAGE_NAME Boost PACKAGE_ARGS "${BOOST_ARGS}")
use_package(TARGET_NAME Boost::filesystem PACKAGE_NAME Boost PACKAGE_ARGS "${BOOST_ARGS}")
use_package(TARGET_NAME Boost::iostreams PACKAGE_NAME Boost PACKAGE_ARGS "${BOOST_ARGS}")
option(BOOST_ASIO_IO_URING ON "enable use of io_uring")
if (BOOST_ASIO_IO_URING)
    message(STATUS "Using io_uring")
    list(APPEND META_PUBLIC_COMPILE_DEFINITIONS BOOST_ASIO_HAS_IO_URING BOOST_ASIO_DISABLE_EPOLL)
    use_pkg_config_module(PKG_CONFIG_MODULES "liburing" VISIBILITY PUBLIC)
endif ()

# find reflective-rapidjson
find_package(reflective_rapidjson${CONFIGURATION_PACKAGE_SUFFIX} REQUIRED)
use_reflective_rapidjson(VISIBILITY PUBLIC)

# find backend libraries
find_package(libpkg ${META_APP_VERSION} REQUIRED)
use_libpkg(VISIBILITY PUBLIC)

# find lmdb-safe
find_package(lmdb-safe${CONFIGURATION_PACKAGE_SUFFIX} REQUIRED)
use_lmdb_safe()

# link against crypto and SSL library from OpenSSL
use_openssl(VISIBILITY PUBLIC)

# link against pthread
list(APPEND PUBLIC_LIBRARIES pthread)

# avoid fatal warning about potential null pointer deref from GCC 12 about code included from Boost.Asio
list(APPEND META_PUBLIC_COMPILE_OPTIONS "-Wno-error=null-dereference")

# apply basic configuration
include(BasicConfig)

# add cache version to config header
string(APPEND META_CUSTOM_CONFIG "#define ${META_PROJECT_VARNAME}_CACHE_VERSION \"${META_VERSION_CACHE}\"\n")

# trigger code generator for tests because the tests already contain structs to be (de)serialized
include(ReflectionGenerator)
add_reflection_generator_invocation(
    INPUT_FILES
    errorhandling.h
    serversetup.h
    resourceusage.h
    buildactions/buildaction.h
    buildactions/buildactionmeta.h
    buildactions/buildactiontemplate.h
    CLANG_OPTIONS_FROM_TARGETS
    "${META_TARGET_NAME}"
    CLANG_OPTIONS_FROM_DEPENDENCIES
    "${PUBLIC_LIBRARIES};${PRIVATE_LIBRARIES}"
    GENERATORS
    json
    binary
    OUTPUT_LISTS
    SRC_FILES
    JSON_VISIBILITY
    ${META_PROJECT_VARNAME}_EXPORT
    BINARY_VISBILITY
    ${META_PROJECT_VARNAME}_EXPORT)

# disable Boost's support for concepts to avoid compile errors
# /usr/include/boost/asio/async_result.hpp:70:20: error: concept cannot have associated constraints
# BOOST_ASIO_CONCEPT completion_handler_for =
#                    ^
# /usr/include/boost/asio/async_result.hpp:492:20: error: concept cannot have associated constraints
# BOOST_ASIO_CONCEPT completion_token_for = requires(T&& t)
#                    ^
list(APPEND META_PUBLIC_COMPILE_DEFINITIONS BOOST_ASIO_DISABLE_CONCEPTS)

# include modules to apply configuration
include(WindowsResources)
include(LibraryTarget)
include(TestTarget)
include(ConfigHeader)

# configure dummy build action
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(DUMMY_BUILD_ACTION_ENABLED_BY_DEFAULT ON)
endif ()
option(DUMMY_BUILD_ACTION_ENABLED "enable dummy build action" ${DUMMY_BUILD_ACTION_ENABLED_BY_DEFAULT})
if (DUMMY_BUILD_ACTION_ENABLED)
    set_property(
        SOURCE buildactions/buildaction.cpp
        APPEND
        PROPERTY COMPILE_DEFINITIONS ${META_PROJECT_VARNAME_UPPER}_DUMMY_BUILD_ACTION_ENABLED)
endif ()

# configure test helper shared with libpkg (FIXME: create a separate test helper library)
set_property(
    SOURCE tests/parser_helper.cpp
    APPEND
    PROPERTY COMPILE_DEFINITIONS ${META_PROJECT_VARNAME_UPPER}_BUILD)
