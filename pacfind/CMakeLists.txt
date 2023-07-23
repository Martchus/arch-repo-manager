cmake_minimum_required(VERSION 3.17.0 FATAL_ERROR)

# add project files
set(HEADER_FILES)
set(SRC_FILES main.cpp)
set(TEST_HEADER_FILES)
set(TEST_SRC_FILES tests/cppunit.cpp tests/check.cpp)

# meta data
set(META_PROJECT_NAME pacfind)
set(META_PROJECT_TYPE application)
set(META_PROJECT_VARNAME REPO_CLEAN)
set(META_APP_NAME "Package finder")
set(META_APP_AUTHOR "Martchus")
set(META_APP_DESCRIPTION "Tool to find the package containing a certain file")
set(META_VERSION_MAJOR 0)
set(META_VERSION_MINOR 0)
set(META_VERSION_PATCH 1)

# find c++utilities
set(CONFIGURATION_PACKAGE_SUFFIX
    ""
    CACHE STRING "sets the suffix for find_package() calls to packages configured via c++utilities")
find_package(c++utilities${CONFIGURATION_PACKAGE_SUFFIX} 5.0.0 REQUIRED)
use_cpp_utilities()

# find backend libraries
find_package(libpkg ${META_APP_VERSION} REQUIRED)
use_libpkg()

list(APPEND PRIVATE_LIBRARIES pthread)

# include modules to apply configuration
include(BasicConfig)
include(WindowsResources)
include(AppTarget)
include(TestTarget)
include(ShellCompletion)
include(ConfigHeader)
