#ifndef LIBPKG_DATA_STORAGE_FWD_H
#define LIBPKG_DATA_STORAGE_FWD_H

//#include "./package.h"

#include <cstdint>

// forward declarations in accordance with lmdb-ttyped.hh
/*
struct nullindex_t;
template<typename T, class I1=nullindex_t, class I2=nullindex_t, class I3 = nullindex_t, class I4 = nullindex_t>
class TypedDBI;
template<class Class,typename Type,Type Class::*PtrToMember>
struct index_on;
*/

namespace LibPkg {

using StorageID = std::uint32_t;
//struct Package;
//using PackageStorage = TypedDBI<Package, index_on<Package, std::string, &Package::name>>;
struct StorageDistribution;
struct DatabaseStorage;

} // namespace LibPkg

#endif // LIBPKG_DATA_STORAGE_FWD_H
