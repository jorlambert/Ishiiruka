// Copyright 2016 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Core/WiiRoot.h"

#include <string>

#include "Common/CommonPaths.h"
#include "Common/FileUtil.h"
#include "Common/Logging/Log.h"
#include "Common/NandPaths.h"
#include "Common/StringUtil.h"
#include "Common/SysConf.h"
#include "Core/ConfigManager.h"
#include "Core/Movie.h"
#include "Core/NetPlayClient.h"

namespace Core
{
static std::string s_temp_wii_root;

static void InitializeDeterministicWiiSaves()
{
  std::string save_path =
      Common::GetTitleDataPath(SConfig::GetInstance().GetTitleID(), Common::FROM_SESSION_ROOT);
  std::string user_save_path =
      Common::GetTitleDataPath(SConfig::GetInstance().GetTitleID(), Common::FROM_CONFIGURED_ROOT);
  if (Movie::IsRecordingInput())
  {
    if (NetPlay::IsNetPlayRunning() && !SConfig::GetInstance().bCopyWiiSaveNetplay)
    {
      Movie::SetClearSave(true);
    }
    else
    {
      // TODO: Check for the actual save data
      Movie::SetClearSave(!File::Exists(user_save_path + "banner.bin"));
    }
  }

  if ((NetPlay::IsNetPlayRunning() && SConfig::GetInstance().bCopyWiiSaveNetplay) ||
      (Movie::IsMovieActive() && !Movie::IsStartingFromClearSave()))
  {
    // Copy the current user's save to the Blank NAND
    if (File::Exists(user_save_path + "banner.bin"))
    {
      File::CopyDir(user_save_path, save_path);
    }
  }
}

void InitializeWiiRoot(bool use_temporary)
{
  if (use_temporary)
  {
    File::DeleteDirRecursively(File::GetUserPath(D_USER_IDX) + "WiiSession" DIR_SEP);
    s_temp_wii_root = File::GetUserPath(D_USER_IDX) + "WiiSession" DIR_SEP;
    WARN_LOG(IOS_FILEIO, "Using temporary directory %s for minimal Wii FS", s_temp_wii_root.c_str());

    if (s_temp_wii_root.empty())
    {
      ERROR_LOG(IOS_FILEIO, "Could not create temporary directory");
      return;
    }

    // If directory exists, make a backup
    if (File::Exists(s_temp_wii_root))
    {
      const std::string backup_path =
        s_temp_wii_root.substr(0, s_temp_wii_root.size() - 1) + ".backup" DIR_SEP;
      WARN_LOG(IOS_FILEIO, "Temporary Wii FS directory exists, moving to backup...");

      // If backup exists, delete it as we don't want a mess
      if (File::Exists(backup_path))
      {
        WARN_LOG(IOS_FILEIO, "Temporary Wii FS backup directory exists, deleting...");
        File::DeleteDirRecursively(backup_path);
      }

      File::CopyDir(s_temp_wii_root, backup_path, true);
    }
    File::CopyDir(File::GetSysDirectory() + WII_USER_DIR, s_temp_wii_root);
    WARN_LOG(IOS_FILEIO, "Using temporary directory %s for minimal Wii FS",
             s_temp_wii_root.c_str());
    File::SetUserPath(D_SESSION_WIIROOT_IDX, s_temp_wii_root);
    // Generate a SYSCONF with default settings for the temporary Wii NAND.
    SysConf sysconf{Common::FromWhichRoot::FROM_SESSION_ROOT};
    sysconf.Save();

    InitializeDeterministicWiiSaves();
  }
  else
  {
    File::SetUserPath(D_SESSION_WIIROOT_IDX, File::GetUserPath(D_WIIROOT_IDX));
  }
}

void ShutdownWiiRoot()
{
  std::string s_brawl_temp_save = File::GetUserPath(D_USER_IDX) + "WiiSession" DIR_SEP + "title" DIR_SEP + "00010000" DIR_SEP + "52534245" DIR_SEP + "data" DIR_SEP;
  std::string replay_data = "./ReplayData";

  if (File::Exists(s_brawl_temp_save + "collect.vff"))
  {
    if (!File::Exists(replay_data))
    {
      File::CreateDir(replay_data);
    }
    
    time_t rawtime;
    struct tm* timeinfo;
    char buffer[80];

    time(&rawtime);
    timeinfo = localtime(&rawtime);

    strftime(buffer, sizeof(buffer), "%d-%m-%Y %H_%M_%S", timeinfo);
    std::string date(buffer);

    std::string replay_file = s_brawl_temp_save + "collect.vff" + " " + date;
    std::string replay_file_backup = replay_data + DIR_SEP "collect.vff" + " " + date;

    WARN_LOG(IOS_FILEIO, "Attempting to backup replay data", s_brawl_temp_save.c_str());
    File::Rename(s_brawl_temp_save + "collect.vff", replay_file);
    File::Copy(replay_file, replay_file_backup);

    if (File::Exists(replay_file_backup))
      WARN_LOG(IOS_FILEIO, "Replay file was backed up");
    else
      ERROR_LOG(IOS_FILEIO, "Could not backup replay save file");
  }

  if (!s_temp_wii_root.empty())
  {
    const u64 title_id = SConfig::GetInstance().GetTitleID();
    std::string save_path = Common::GetTitleDataPath(title_id, Common::FROM_SESSION_ROOT);
    std::string user_save_path = Common::GetTitleDataPath(title_id, Common::FROM_CONFIGURED_ROOT);
    std::string user_backup_path = File::GetUserPath(D_BACKUP_IDX) +
                                   StringFromFormat("%08x/%08x/", static_cast<u32>(title_id >> 32),
                                                    static_cast<u32>(title_id));
    if (File::Exists(save_path + "banner.bin") && SConfig::GetInstance().bEnableMemcardSdWriting)
    {
      // Backup the existing save just in case it's still needed.
      if (File::Exists(user_save_path + "banner.bin"))
      {
        if (File::Exists(user_backup_path))
          File::DeleteDirRecursively(user_backup_path);
        File::CopyDir(user_save_path, user_backup_path);
        File::DeleteDirRecursively(user_save_path);
      }
      File::CopyDir(save_path, user_save_path);
      File::DeleteDirRecursively(save_path);
    }
    File::DeleteDirRecursively(s_temp_wii_root);
    s_temp_wii_root.clear();
  }
}
}
