cmake_minimum_required(VERSION 3.1.0 FATAL_ERROR)

# add project files
set(HEADER_FILES
    data/package.h
    data/database.h
    data/config.h
    data/lockable.h
    data/siglevel.h
    parser/aur.h
    parser/package.h
    parser/database.h
    parser/config.h
    parser/utils.h
    parser/binary.h
    lmdb-safe/lmdb-safe.hh
    lmdb-safe/lmdb-typed.hh)
set(SRC_FILES
    data/package.cpp
    data/database.cpp
    data/config.cpp
    data/lockable.cpp
    algo/search.cpp
    algo/buildorder.cpp
    algo/licenses.cpp
    parser/aur.cpp
    parser/package.cpp
    parser/database.cpp
    parser/config.cpp
    parser/utils.cpp
    parser/binary.cpp
    parser/siglevel.cpp
    lmdb-safe/lmdb-safe.cc
    lmdb-safe/lmdb-typed.cc)
set(TEST_HEADER_FILES tests/parser_helper.h)
set(TEST_SRC_FILES tests/cppunit.cpp tests/parser.cpp tests/parser_binary.cpp tests/parser_helper.cpp tests/data.cpp
                   tests/utils.cpp)

# meta data
set(META_PROJECT_NAME libpkg)
set(META_PROJECT_TYPE library)
set(META_PROJECT_VARNAME LIBPKG)
set(META_APP_NAME "Unofficial Arch Linux package library")
set(META_APP_DESCRIPTION "C++ library to parse Arch Linux packages and databases")
set(META_VERSION_MAJOR 0)
set(META_VERSION_MINOR 0)
set(META_VERSION_PATCH 1)
set(META_TIDY_EXCLUDE_REGEX "lmdb-safe/.*")

# find c++utilities
set(CONFIGURATION_PACKAGE_SUFFIX
    ""
    CACHE STRING "sets the suffix for find_package() calls to packages configured via c++utilities")
find_package(c++utilities${CONFIGURATION_PACKAGE_SUFFIX} 5.2.0 REQUIRED)
use_cpp_utilities(VISIBILITY PUBLIC)

# use std::filesystem
include(3rdParty)
use_standard_filesystem(VISIBILITY PUBLIC)

# find reflective-rapidjson
find_package(reflective_rapidjson${CONFIGURATION_PACKAGE_SUFFIX} REQUIRED)
use_reflective_rapidjson(VISIBILITY PUBLIC)

# find lmdb
use_pkg_config_module(PKG_CONFIG_MODULES "lmdb" VISIBILITY PUBLIC)

# configure lmdb-safe
set_source_files_properties(lmdb-safe/lmdb-safe.cc lmdb-safe/lmdb-typed.cc PROPERTIES COMPILE_FLAGS "-Wno-error")

# find 3rd party libraries zlib
use_zlib()
# libarchive
find_package(LibArchive)
if (NOT LibArchive_FOUND)
    message(FATAL_ERROR "Unable to find libarchive.")
endif ()
add_library(libarchive UNKNOWN IMPORTED)
set_property(TARGET libarchive PROPERTY IMPORTED_LOCATION "${LibArchive_LIBRARIES}")
set_property(TARGET libarchive PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${LibArchive_INCLUDE_DIRS}")
use_target(TARGET_NAME libarchive)

# apply basic configuration
include(BasicConfig)

# trigger code generator for tests because the tests already contain structs to be (de)serialized
include(ReflectionGenerator)
add_reflection_generator_invocation(
    INPUT_FILES
    data/database.h
    data/package.h
    data/config.h
    data/siglevel.h
    parser/aur.h
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

# include modules to apply configuration
include(WindowsResources)
include(LibraryTarget)
include(TestTarget)
include(Doxygen)
include(ConfigHeader)
