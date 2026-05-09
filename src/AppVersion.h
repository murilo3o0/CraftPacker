#pragma once

#include <QString>

#include "CraftPackerVersion.h"

inline QString craftPackerVersionString()
{
    return QLatin1String(CRAFTPACKER_VERSION_STRING);
}

inline QString craftPackerModrinthUserAgent()
{
    return QLatin1String("helloworldx64/CraftPacker/") + craftPackerVersionString();
}
