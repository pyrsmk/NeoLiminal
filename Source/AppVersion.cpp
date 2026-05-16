#include "AppVersion.h"

static const int kBuild = 1;

static const std::string kVersionString = "v" + std::string(VERSION) + " build " + std::to_string(kBuild);

const std::string& getVersionString()
{
    return kVersionString;
}
