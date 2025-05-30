cmake_minimum_required(VERSION 3.17.0 FATAL_ERROR)

# add project files
set(HEADER_FILES)
set(SRC_FILES main.cpp)
set(TEST_HEADER_FILES)
set(TEST_SRC_FILES tests/cppunit.cpp tests/check.cpp)

# meta data
set(META_PROJECT_NAME pacparse)
set(META_PROJECT_TYPE application)
set(META_PROJECT_VARNAME REPO_CLEAN)
set(META_APP_NAME "Package parser")
set(META_APP_AUTHOR "Martchus")
set(META_APP_DESCRIPTION "Tool to parse an Arch Linux package printing the results as JSON")

# find c++utilities
set(CONFIGURATION_PACKAGE_SUFFIX
    ""
    CACHE STRING "sets the suffix for find_package() calls to packages configured via c++utilities")
find_package(c++utilities${CONFIGURATION_PACKAGE_SUFFIX} 5.0.0 REQUIRED)
use_cpp_utilities()

# find reflective-rapidjson
find_package(reflective_rapidjson${CONFIGURATION_PACKAGE_SUFFIX} REQUIRED)
use_reflective_rapidjson(VISIBILITY PUBLIC)

# find backend libraries
find_package(libpkg ${META_APP_VERSION} REQUIRED)
use_libpkg()

list(APPEND PRIVATE_LIBRARIES pthread)

# apply basic configuration
include(BasicConfig)

# add reflection generator invocation
include(ReflectionGenerator)
add_reflection_generator_invocation(
    INPUT_FILES
    main.cpp
    CLANG_OPTIONS_FROM_TARGETS
    "${META_TARGET_NAME}"
    CLANG_OPTIONS_FROM_DEPENDENCIES
    "${PUBLIC_LIBRARIES};${PRIVATE_LIBRARIES}"
    GENERATORS
    json
    OUTPUT_LISTS
    SRC_FILES
    ERROR_RESILIENT)

# apply further configuration
include(WindowsResources)
include(AppTarget)
include(TestTarget)
include(ShellCompletion)
include(ConfigHeader)
