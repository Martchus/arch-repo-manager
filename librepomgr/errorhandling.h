#ifndef LIBREPOMGR_ERROR_HANDLING_H
#define LIBREPOMGR_ERROR_HANDLING_H

#include <reflective_rapidjson/json/serializable.h>

#include <boost/beast/core/string.hpp>

// allow to serialize deserialization errors so we can return errors as JSON, too
REFLECTIVE_RAPIDJSON_MAKE_JSON_SERIALIZABLE(ReflectiveRapidJSON::JsonDeserializationError);

#endif // LIBREPOMGR_ERROR_HANDLING_H
