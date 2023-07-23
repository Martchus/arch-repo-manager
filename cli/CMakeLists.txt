cmake_minimum_required(VERSION 3.17.0 FATAL_ERROR)

# add project files
set(HEADER_FILES)
set(SRC_FILES main.cpp config.cpp)
set(TEST_HEADER_FILES config.h)
set(TEST_SRC_FILES tests/cppunit.cpp)

# meta data
set(META_PROJECT_NAME repomgr)
set(META_PROJECT_TYPE application)
set(META_PROJECT_VARNAME BUILD_SERVICE_CLIENT)
set(META_APP_NAME "CLI for repo manager")
set(META_APP_DESCRIPTION "CLI tool to use the repository manager for Arch Linux")

# find c++utilities
set(CONFIGURATION_PACKAGE_SUFFIX
    ""
    CACHE STRING "sets the suffix for find_package() calls to packages configured via c++utilities")
find_package(c++utilities${CONFIGURATION_PACKAGE_SUFFIX} 5.0.0 REQUIRED)
use_cpp_utilities()

# find backend libraries
find_package(libpkg ${META_APP_VERSION} REQUIRED)
use_libpkg()

# find backend libraries
find_package(librepomgr ${META_APP_VERSION} REQUIRED)
use_librepomgr()

# use bundled tabulate
list(APPEND PRIVATE_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/tabulate/include/")

# include modules to apply configuration
include(BasicConfig)
include(WindowsResources)
include(AppTarget)
include(TestTarget)
include(ShellCompletion)
include(ConfigHeader)

# add install for config file template
install(
    FILES doc/client-config-example.conf
    DESTINATION "${CMAKE_INSTALL_PREFIX}/${META_DATA_DIR}/skel"
    COMPONENT config)
