#ifndef LIBPKG_DATA_SIGLEVEL_H
#define LIBPKG_DATA_SIGLEVEL_H

#include "../global.h"

#include <reflective_rapidjson/binary/serializable.h>
#include <reflective_rapidjson/json/serializable.h>

#include <c++utilities/misc/flagenumclass.h>

#include <string>
#include <string_view>

namespace LibPkg {

enum class SignatueScope {
    Package,
    Database,
};

/*!
 * \brief The DatabaseSignatureLevel enum represents a database's "SigLevel".
 * \sa https://www.archlinux.org/pacman/pacman.conf.5.html#SC
 */
enum class SignatureLevel {
    Invalid = 0, /*! Indicates that the signature level could not be parsed by signatureLevelToString(). */
    Never = (1 << 0), /*! All signature checking is suppressed, even if signatures are present. */
    Optional = (1
        << 1), /*! Signatures are checked if present; absence of a signature is not an error. An invalid signature is a fatal error, as is a signature from a key not in the keyring. */
    Required = (1
        << 2), /*! Signatures are required; absence of a signature or an invalid signature is a fatal error, as is a signature from a key not in the keyring. */
    TrustedOnly = (1 << 3), /*! If a signature is checked, it must be in the keyring and fully trusted; marginal trust does not meet this criteria. */
    TrustAll = (1
        << 4), /*! If a signature is checked, it must be in the keyring, but is not required to be assigned a trust level (e.g., unknown or marginal trust). */
    Default = SignatureLevel::Optional | SignatureLevel::TrustedOnly, /*! The default signature level. */
};

LIBPKG_EXPORT std::string signatureLevelToString(SignatureLevel sigLevel, std::string_view prefix = std::string_view());

} // namespace LibPkg

CPP_UTILITIES_MARK_FLAG_ENUM_CLASS(LibPkg, LibPkg::SignatureLevel)

namespace LibPkg {

struct LIBPKG_EXPORT SignatureLevelConfig : public ReflectiveRapidJSON::JsonSerializable<SignatureLevelConfig>,
                                            public ReflectiveRapidJSON::BinarySerializable<SignatureLevelConfig> {
    explicit SignatureLevelConfig();
    explicit SignatureLevelConfig(SignatureLevel levelForAllScopes);
    explicit SignatureLevelConfig(SignatureLevel levelForDbScope, SignatureLevel levelForPackageScope);
    static SignatureLevelConfig fromString(std::string_view str);
    std::string toString() const;
    bool isValid() const;
    bool operator==(const SignatureLevelConfig &other) const;

    SignatureLevel databaseScope = SignatureLevel::Default;
    SignatureLevel packageScope = SignatureLevel::Default;
};

inline SignatureLevelConfig::SignatureLevelConfig()
{
}

inline SignatureLevelConfig::SignatureLevelConfig(SignatureLevel levelForAllScopes)
    : databaseScope(levelForAllScopes)
    , packageScope(levelForAllScopes)
{
}

inline SignatureLevelConfig::SignatureLevelConfig(SignatureLevel levelForDbScope, SignatureLevel levelForPackageScope)
    : databaseScope(levelForDbScope)
    , packageScope(levelForPackageScope)
{
}

inline bool SignatureLevelConfig::isValid() const
{
    return databaseScope != SignatureLevel::Invalid && packageScope != SignatureLevel::Invalid;
}

inline bool SignatureLevelConfig::operator==(const SignatureLevelConfig &other) const
{
    return databaseScope == other.databaseScope && packageScope == other.packageScope;
}

inline std::ostream &operator<<(std::ostream &o, const SignatureLevelConfig &signatureLevelConfig)
{
    return o << signatureLevelConfig.toString();
}

} // namespace LibPkg

#endif // LIBPKG_DATA_LOCKABLE_H
