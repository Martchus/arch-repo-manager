// Created via CMake from template global.h.in
// WARNING! Any changes to this file will be overwritten by the next CMake run!

#ifndef LIBREPOMGR_GLOBAL
#define LIBREPOMGR_GLOBAL

#include "librepomgr-definitions.h"
#include <c++utilities/application/global.h>

#ifdef LIBREPOMGR_STATIC
#define LIBREPOMGR_EXPORT
#define LIBREPOMGR_IMPORT
#else
#define LIBREPOMGR_EXPORT CPP_UTILITIES_GENERIC_LIB_EXPORT
#define LIBREPOMGR_IMPORT CPP_UTILITIES_GENERIC_LIB_IMPORT
#endif

/*!
 * \def LIBREPOMGR_EXPORT
 * \brief Marks the symbol to be exported by the librepomgr library.
 */

/*!
 * \def LIBREPOMGR_IMPORT
 * \brief Marks the symbol to be imported from the librepomgr library.
 */

#endif // LIBREPOMGR_GLOBAL
