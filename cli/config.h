#ifndef REPOMGR_CLIENT_H
#define REPOMGR_CLIENT_H

#include <c++utilities/application/argumentparser.h>

#include <string>

struct ClientConfig {
    void parse(const CppUtilities::Argument &configFileArg, const CppUtilities::Argument &instanceArg);

    const char *path = nullptr;
    std::string instance;
    std::string url;
    std::string userName;
    std::string password;
};

#endif // REPOMGR_CLIENT_H
