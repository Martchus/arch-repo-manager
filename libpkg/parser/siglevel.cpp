#include "../data/siglevel.h"

#include <c++utilities/conversion/stringbuilder.h>
#include <c++utilities/conversion/stringconversion.h>

#include <vector>

using namespace CppUtilities;

namespace LibPkg {

SignatureLevelConfig SignatureLevelConfig::fromString(std::string_view str)
{
    auto parts = splitStringSimple<std::vector<std::string_view>>(str, std::string_view(" "));
    auto whatToCheckPkg = SignatureLevel::Optional;
    auto whatIsAllowedPkg = SignatureLevel::TrustedOnly;
    auto whatToCheckDb = SignatureLevel::Optional;
    auto whatIsAllowedDb = SignatureLevel::TrustedOnly;
    for (auto &part : parts) {
        if (part.empty()) {
            continue;
        }
        enum SignatureScope {
            All,
            Package,
            Database,
        } scope
            = All;
        if (startsWith(part, "Package")) {
            part = part.substr(7);
            scope = Package;
        } else if (startsWith(part, "Database")) {
            part = part.substr(8);
            scope = All;
        }
        SignatureLevel whatToCheck = SignatureLevel::Invalid, whatIsAllowed = SignatureLevel::Invalid;
        if (part == "Never") {
            whatToCheck = SignatureLevel::Never;
        } else if (part == "Optional") {
            whatToCheck = SignatureLevel::Optional;
        } else if (part == "Required") {
            whatToCheck = SignatureLevel::Required;
        } else if (part == "TrustedOnly") {
            whatIsAllowed = SignatureLevel::TrustedOnly;
        } else if (part == "TrustAll") {
            whatIsAllowed = SignatureLevel::TrustAll;
        } else {
            return SignatureLevelConfig(SignatureLevel::Invalid);
        }
        switch (scope) {
        case All:
            if (whatToCheck != SignatureLevel::Invalid) {
                whatToCheckDb = whatToCheckPkg = whatToCheck;
            } else if (whatIsAllowed != SignatureLevel::Invalid) {
                whatIsAllowedDb = whatIsAllowedPkg = whatIsAllowed;
            }
            break;
        case Package:
            if (whatToCheck != SignatureLevel::Invalid) {
                whatToCheckPkg = whatToCheck;
            } else if (whatIsAllowed != SignatureLevel::Invalid) {
                whatIsAllowedPkg = whatIsAllowed;
            }
            break;
        case Database:
            if (whatToCheck != SignatureLevel::Invalid) {
                whatToCheckDb = whatToCheck;
            } else if (whatIsAllowed != SignatureLevel::Invalid) {
                whatIsAllowedDb = whatIsAllowed;
            }
            break;
        }
    }
    return SignatureLevelConfig(whatToCheckDb | whatIsAllowedDb, whatToCheckPkg | whatIsAllowedPkg);
}

std::string signatureLevelToString(SignatureLevel sigLevel, std::string_view prefix)
{
    if (sigLevel == SignatureLevel::Invalid) {
        return std::string();
    }
    const char *whatToCheck = "Optional";
    const char *whatIsAllowed = "TrustedOnly";
    if (sigLevel & SignatureLevel::Required) {
        whatToCheck = "Required";
    } else if (sigLevel & SignatureLevel::Optional) {
        whatToCheck = "Optional";
    } else if (sigLevel & SignatureLevel::Never) {
        whatToCheck = "Never";
    }
    if (sigLevel & SignatureLevel::TrustedOnly) {
        whatIsAllowed = "TrustedOnly";
    } else if (sigLevel & SignatureLevel::TrustAll) {
        whatIsAllowed = "TrustAll";
    }
    return argsToString(prefix, whatToCheck, ' ', prefix, whatIsAllowed);
}

std::string SignatureLevelConfig::toString() const
{
    if (databaseScope == SignatureLevel::Invalid && packageScope == SignatureLevel::Invalid) {
        return std::string();
    } else if (databaseScope == packageScope || packageScope == SignatureLevel::Invalid) {
        return signatureLevelToString(databaseScope);
    } else if (databaseScope == SignatureLevel::Invalid) {
        return signatureLevelToString(packageScope);
    } else {
        return argsToString(signatureLevelToString(databaseScope, std::string_view("Database")), ' ',
            signatureLevelToString(packageScope, std::string_view("Package")));
    }
}

} // namespace LibPkg

#include "reflection/siglevel.h"
