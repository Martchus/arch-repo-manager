// Created via CMake from template global.h.in
// WARNING! Any changes to this file will be overwritten by the next CMake run!

#ifndef LIBPKG_GLOBAL
#define LIBPKG_GLOBAL

#include <c++utilities/application/global.h>

#ifdef LIBPKG_STATIC
#define LIBPKG_EXPORT
#define LIBPKG_IMPORT
#else
#define LIBPKG_EXPORT CPP_UTILITIES_GENERIC_LIB_EXPORT
#define LIBPKG_IMPORT CPP_UTILITIES_GENERIC_LIB_IMPORT
#endif

/*!
 * \def LIBPKG_EXPORT
 * \brief Marks the symbol to be exported by the libpkg library.
 */

/*!
 * \def LIBPKG_IMPORT
 * \brief Marks the symbol to be imported from the libpkg library.
 */

#endif // LIBPKG_GLOBAL
