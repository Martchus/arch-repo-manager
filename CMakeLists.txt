cmake_minimum_required(VERSION 3.1.0 FATAL_ERROR)

# metadata
set(META_PROJECT_NAME repomgr)
set(META_PROJECT_TYPE application)
set(META_APP_AUTHOR "Martchus")
set(META_APP_URL "https://github.com/${META_APP_AUTHOR}/${META_PROJECT_NAME}")
set(META_APP_DESCRIPTION "Repository manager and package builder for Arch Linux")
set(META_APP_CATEGORIES "System;Utility;Network;FileTransfer")
set(META_VERSION_MAJOR 0)
set(META_VERSION_MINOR 0)
set(META_VERSION_PATCH 2)
set(META_VERSION_EXACT_SONAME ON)
set(META_CXX_STANDARD 20)

project(${META_PROJECT_NAME})

# allow using non-default configuration
set(CONFIGURATION_PACKAGE_SUFFIX "" CACHE STRING "sets the suffix for find_package() calls to packages configured via c++utilities")

enable_testing()

# add subdirectories
option(INCLUDE_TABULATE_PROJECT "includes the CMake project of tabulate" OFF)
if (INCLUDE_TABULATE_PROJECT)
    add_subdirectory(3rdparty/tabulate)
endif ()
option(INCLUDE_LMDB_SAFE_PROJECT "includes the CMake project of lmdb-safe" ON)
if (INCLUDE_LMDB_SAFE_PROJECT)
    set(META_NO_INSTALL_TARGETS ON)
    set(LMDB_SAFE_BUILD_SHARED_LIBS OFF CACHE BOOL "whether to build lmdb-safe as shared library")
    add_subdirectory(3rdparty/lmdb-safe)
    set(${NAMESPACE_PREFIX}lmdb-safe${CONFIGURATION_PACKAGE_SUFFIX}_DIR "${CMAKE_CURRENT_BINARY_DIR}/3rdparty/lmdb-safe")
    unset(META_NO_INSTALL_TARGETS)
endif ()
add_subdirectory(libpkg)
link_directories(${LIBPKG_BINARY_DIR})
add_subdirectory(librepomgr)
link_directories(${LIBREPOMGR_BINARY_DIR})
add_subdirectory(srv)
add_subdirectory(cli)
add_subdirectory(pacfind)

