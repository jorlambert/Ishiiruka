// Copyright 2018 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included

#include "Core/Config/UISettings.h"

namespace Config
{
// UI.General

const ConfigInfo<bool> MAIN_USE_DISCORD_PRESENCE{{System::Main, "General", "UseDiscordPresence"},
                                                 true};
const ConfigInfo<bool> MAIN_UPDATE_CHECK{{System::UI, "General", "CheckUpdate"}, true};

}  // namespace Config
