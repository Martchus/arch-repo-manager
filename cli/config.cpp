#include "./config.h"

#include "resources/config.h"

#include <c++utilities/conversion/stringbuilder.h>
#include <c++utilities/io/inifile.h>
#include <c++utilities/io/nativefilestream.h>

#include <stdexcept>

using namespace CppUtilities;

void ClientConfig::parse(const Argument &configFileArg, const Argument &instanceArg)
{
    // parse connfig file
    path = configFileArg.firstValue();
    if (!path || !*path) {
        path = "/etc/buildservice" PROJECT_CONFIG_SUFFIX "/client.conf";
    }
    auto configFile = NativeFileStream();
    configFile.exceptions(std::ios_base::badbit | std::ios_base::failbit);
    configFile.open(path, std::ios_base::in);
    auto configIni = AdvancedIniFile();
    configIni.parse(configFile);
    configFile.close();

    // read innstance
    if (instanceArg.isPresent()) {
        instance = instanceArg.values().front();
    }
    for (const auto &section : configIni.sections) {
        if (!section.name.starts_with("instance/")) {
            continue;
        }
        if (!instance.empty() && instance != std::string_view(section.name.data() + 9, section.name.size() - 9)) {
            continue;
        }
        instance = section.name;
        if (const auto url = section.findField("url"); url != section.fieldEnd()) {
            this->url = std::move(url->value);
        } else {
            throw std::runtime_error("Config is invalid: No \"url\" specified within \"" % section.name + "\".");
        }
        if (const auto user = section.findField("user"); user != section.fieldEnd()) {
            this->userName = std::move(user->value);
        }
        break;
    }
    if (url.empty()) {
        throw std::runtime_error("Config is invalid: Instance configuration insufficient.");
    }

    // read user data
    if (userName.empty()) {
        return;
    }
    if (const auto userSection = configIni.findSection("user/" + userName); userSection != configIni.sectionEnd()) {
        if (const auto password = userSection->findField("password"); password != userSection->fieldEnd()) {
            this->password = std::move(password->value);
        } else {
            throw std::runtime_error("Config is invalid: No \"password\" specified within \"" % userSection->name + "\".");
        }
    } else {
        throw std::runtime_error("Config is invalid: User \"" % userName + "\" referenced in instance configuration not found.");
    }
}
