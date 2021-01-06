// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <OptionParser.h>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <locale>

#include <wx/app.h>
#include <wx/buffer.h>
#include <wx/cmdline.h>
#include <wx/evtloop.h>
#include <wx/image.h>
#include <wx/imagpng.h>
#include <wx/intl.h>
#include <wx/language.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/thread.h>
#include <wx/timer.h>
#include <wx/tooltip.h>
#include <wx/utils.h>
#include <wx/window.h>

#if defined(_WIN32) || defined(__APPLE__)
#include <picojson/picojson.h>
#endif

#include "Common/CPUDetect.h"
#include "Common/CommonPaths.h"
#include "Common/CommonTypes.h"
#include "Common/FileUtil.h"
#include "Common/IniFile.h"
#include "Common/Logging/LogManager.h"
#include "Common/MsgHandler.h"
#include "Common/Thread.h"
#include "Common/Version.h"

#include "Core/Analytics.h"
#include "Core/Boot/Boot.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/HW/Wiimote.h"
#include "Core/Host.h"
#include "Core/NetPlayProto.h"
#include "Core/Movie.h"

#include "DolphinWX/Debugger/CodeWindow.h"
#include "DolphinWX/Debugger/JitWindow.h"
#include "DolphinWX/Frame.h"
#include "DolphinWX/Globals.h"
#include "DolphinWX/Main.h"
#include "DolphinWX/NetPlay/NetWindow.h"
#include "DolphinWX/SoftwareVideoConfigDialog.h"
#include "DolphinWX/UINeedsControllerState.h"
#include "DolphinWX/VideoConfigDiag.h"
#include "DolphinWX/WxUtils.h"

#include "UICommon/DiscordPresence.h"
#include "Core/Config/UISettings.h"

#include "UICommon/CommandLineParse.h"
#include "UICommon/UICommon.h"

#include "VideoCommon/VideoBackendBase.h"
#include "VideoCommon/OnScreenDisplay.h"

#if defined HAVE_X11 && HAVE_X11
#include <X11/Xlib.h>
#endif

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#endif

#ifdef _WIN32

// Applications exporting this symbol with this value will be automatically
// directed to the high-performance GPU on Nvidia Optimus systems with
// up-to-date drivers
//
__declspec(dllexport) DWORD NvOptimusEnablement = 1;

// Applications exporting this symbol with this value will be automatically
// directed to the high-performance GPU on AMD PowerXpress systems with
// up-to-date drivers
//
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;

#endif

// ------------
//  Main window

IMPLEMENT_APP(DolphinApp)

bool wxMsgAlert(const char*, const char*, bool, MsgType);
std::string wxStringTranslator(const char*);

CFrame* main_frame = nullptr;

#if defined(_WIN32) || defined(__APPLE__)
bool DolphinApp::updateAvailable = false;
#endif

bool DolphinApp::Initialize(int& c, wxChar** v)
{
#if defined HAVE_X11 && HAVE_X11
  XInitThreads();
#endif
  return wxApp::Initialize(c, v);
}

// The 'main program' equivalent that creates the main window and return the main frame

void DolphinApp::OnInitCmdLine(wxCmdLineParser& parser)
{
  parser.SetCmdLine("");
}

int DolphinApp::FilterEvent(wxEvent& event)
{
  wxKeyEvent& kev = reinterpret_cast<wxKeyEvent&>(event);

  if (main_frame && main_frame->RendererHasFocus())
  {
    if (event.GetEventType() == wxEVT_CHAR)
    {
      if (kev.GetKeyCode() == WXK_BACK)
      {
        if (OSD::Chat::current_msg.size() > 0)
          OSD::Chat::current_msg.pop_back();
      }
      else
      {
        std::string result = wxString(kev.GetUnicodeKey()).ToStdString();
        std::string filtered;

        for (char c : result)
        {
          if (std::isalnum(c, std::locale::classic()) || std::ispunct(c, std::locale::classic()) || c == ' ')
            filtered += c;
        }

        OSD::Chat::current_msg += filtered;
      }
    }
  }

  return -1;
}

bool DolphinApp::OnCmdLineParsed(wxCmdLineParser& parser)
{
  return true;
}

bool DolphinApp::OnInit()
{
  if (!wxApp::OnInit())
    return false;

  Bind(wxEVT_QUERY_END_SESSION, &DolphinApp::OnEndSession, this);
  Bind(wxEVT_END_SESSION, &DolphinApp::OnEndSession, this);
  Bind(wxEVT_IDLE, &DolphinApp::OnIdle, this);
  Bind(wxEVT_ACTIVATE_APP, &DolphinApp::OnActivate, this);

  // Register message box and translation handlers
  RegisterMsgAlertHandler(&wxMsgAlert);
  RegisterStringTranslator(&wxStringTranslator);

#if wxUSE_ON_FATAL_EXCEPTION
  wxHandleFatalExceptions(true);
#endif

#ifdef _WIN32
  const bool console_attached = AttachConsole(ATTACH_PARENT_PROCESS) != FALSE;
  HANDLE stdout_handle = ::GetStdHandle(STD_OUTPUT_HANDLE);
  if (console_attached && stdout_handle)
  {
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
  }
#endif

  ParseCommandLine();

#ifdef _WIN32
  FreeConsole();
#endif

  UICommon::SetUserDirectory(m_user_path.ToStdString());
  UICommon::CreateDirectories();
  InitLanguageSupport();  // The language setting is loaded from the user directory
  UICommon::Init();

  if (m_select_video_backend && !m_video_backend_name.empty())
    SConfig::GetInstance().m_strVideoBackend = WxStrToStr(m_video_backend_name);

  if (m_select_audio_emulation)
    SConfig::GetInstance().bDSPHLE = (m_audio_emulation_name.Upper() == "HLE");

  VideoBackendBase::ActivateBackend(SConfig::GetInstance().m_strVideoBackend);

  DolphinAnalytics::Instance()->ReportDolphinStart("wx");

  wxToolTip::Enable(!SConfig::GetInstance().m_DisableTooltips);

  // Enable the PNG image handler for screenshots
  wxImage::AddHandler(new wxPNGHandler);

  // Silent PNG warnings from some homebrew banners: "iCCP: known incorrect sRGB profile"
  wxImage::SetDefaultLoadFlags(wxImage::GetDefaultLoadFlags() & ~wxImage::Load_Verbose);

#ifdef __APPLE__
  typedef Boolean(*SecTranslocateIsTranslocatedURL)(CFURLRef path, bool* isTranslocated, CFErrorRef * error);
  typedef CFURLRef(*SecTranslocateCreateOriginalPathForURL)(CFURLRef translocatedPath, CFErrorRef * error);

  void* security_framework = dlopen("/System/Library/Frameworks/Security.framework/Security", RTLD_NOW);

  if (security_framework)
  {
    SecTranslocateIsTranslocatedURL SecTranslocateIsTranslocatedURL_func =
      (SecTranslocateIsTranslocatedURL)dlsym(security_framework, "SecTranslocateIsTranslocatedURL");
    SecTranslocateCreateOriginalPathForURL SecTranslocateCreateOriginalPathForURL_func =
      (SecTranslocateCreateOriginalPathForURL)dlsym(security_framework, "SecTranslocateCreateOriginalPathForURL");

    if (SecTranslocateIsTranslocatedURL_func && SecTranslocateCreateOriginalPathForURL_func)
    {
      CFStringRef path = CFStringCreateWithCString(NULL, File::GetBundleDirectory().c_str(), kCFStringEncodingUTF8);
      CFURLRef url = CFURLCreateWithFileSystemPath(NULL, path, kCFURLPOSIXPathStyle, 0);
      CFURLRef translocated_original = SecTranslocateCreateOriginalPathForURL_func(url, nullptr);

      bool is_translocated = false;
      SecTranslocateIsTranslocatedURL_func(url, &is_translocated, nullptr);

      if (is_translocated)
      {
        // https://stackoverflow.com/questions/28860033/convert-from-cfurlref-or-cfstringref-to-stdstring
        CFIndex bufferSize = CFStringGetLength(CFURLGetString(translocated_original)) + 1; // The +1 is for having space for the string to be NUL terminated
        char buffer[bufferSize];

        // CFStringGetCString is documented to return a false if the buffer is too small
        // (which shouldn't happen in this example) or if the conversion generally fails
        if (CFStringGetCString(CFURLGetString(translocated_original), buffer, bufferSize, kCFStringEncodingUTF8))
        {
          std::string cppString(buffer);
          cppString.erase(0, std::string("file://").size());

          if (system(("xattr -r -d com.apple.quarantine \"" + cppString + "\"").c_str()) == EXIT_SUCCESS)
          {
            system(("\"" + cppString + "/Contents/MacOS/Dolphin\" &disown").c_str());
            exit(EXIT_SUCCESS);
          }
        }

        wxMessageBox("This app is quarantined! Move it to your Applications folder and reopen it.\nAsk in #support-dolphin or check the subreddit for further help.", "An error occured", wxOK | wxCENTRE | wxICON_WARNING);
        exit(EXIT_SUCCESS);
        }
      }
    }

    dlclose(security_framework);
#endif

  // We have to copy the size and position out of SConfig now because CFrame's OnMove
  // handler will corrupt them during window creation (various APIs like SetMenuBar cause
  // event dispatch including WM_MOVE/WM_SIZE)
  wxRect window_geometry(SConfig::GetInstance().iPosX, SConfig::GetInstance().iPosY,
                         SConfig::GetInstance().iWidth, SConfig::GetInstance().iHeight);
  main_frame = new CFrame(nullptr, wxID_ANY, StrToWxStr(Common::scm_rev_str), window_geometry,
                          m_use_debugger, m_batch_mode, m_use_logger);
  SetTopWindow(main_frame);

  AfterInit();

  return true;
}

void DolphinApp::ParseCommandLine()
{
  auto parser = CommandLineParse::CreateParser(CommandLineParse::ParserOptions::IncludeGUIOptions);

  // Manually convert each argument to a UTF-8 std::string, because the implicit
  // conversion of wxCmdLineArgsArray to char** calls ToAscii (which is undesired).
  std::vector<std::string> utf8_argv;
  for (int i = 1; i < argc; ++i)
    utf8_argv.emplace_back(argv[i].utf8_str());
  optparse::Values& options = CommandLineParse::ParseArguments(parser.get(), utf8_argv);

  std::vector<std::string> args = parser->args();

  if (options.is_set("exec"))
  {
    m_boot = BootParameters::GenerateFromFile(static_cast<const char*>(options.get("exec")));
  }
  else if (options.is_set("nand_title"))
  {
    const std::string hex_string = static_cast<const char*>(options.get("nand_title"));
    if (hex_string.length() == 16)
    {
      const u64 title_id = std::stoull(hex_string, nullptr, 16);
      m_boot = std::make_unique<BootParameters>(BootParameters::NANDTitle{ title_id });
    }
    else
    {
      WxUtils::ShowErrorDialog(_("The title ID is invalid."));
    }
  }
  else if (args.size())
  {
    m_boot = BootParameters::GenerateFromFile(args.front());
    args.erase(args.begin());
  }

  m_use_debugger = options.is_set("debugger");
  m_use_logger = options.is_set("logger");
  m_batch_mode = options.is_set("batch");

  m_confirm_stop = options.is_set("confirm");
  m_confirm_setting = options.get("confirm");

  m_select_video_backend = options.is_set("video_backend");
  m_video_backend_name = static_cast<const char*>(options.get("video_backend"));

  m_select_audio_emulation = options.is_set("audio_emulation");
  m_audio_emulation_name = static_cast<const char*>(options.get("audio_emulation"));

  m_play_movie = options.is_set("movie");
  m_movie_file = static_cast<const char*>(options.get("movie"));

  m_user_path = static_cast<const char*>(options.get("user"));
}

#ifdef __APPLE__
void DolphinApp::MacOpenFile(const wxString& fileName)
{
  main_frame->StartGame(BootParameters::GenerateFromFile(fileName.ToStdString()));
}
#endif

void DolphinApp::AfterInit()
{
#ifdef USE_DISCORD_PRESENCE
  if (Config::Get(Config::MAIN_USE_DISCORD_PRESENCE))
  {
    Discord::Init();
  }
#endif

#if defined(USE_ANALYTICS) && USE_ANALYTICS
  if (!SConfig::GetInstance().m_analytics_permission_asked)
  {
    int answer =
      wxMessageBox(_("If authorized, Dolphin can collect data on its performance, "
        "feature usage, and configuration, as well as data on your system's "
        "hardware and operating system.\n\n"
        "No private data is ever collected. This data helps us understand "
        "how people and emulated games use Dolphin and prioritize our "
        "efforts. It also helps us identify rare configurations that are "
        "causing bugs, performance and stability issues.\n"
        "This authorization can be revoked at any time through Dolphin's "
        "settings.\n\n"
        "Do you authorize Dolphin to report this information to Dolphin's "
        "developers?"),
        _("Usage statistics reporting"), wxYES_NO, main_frame);

    SConfig::GetInstance().m_analytics_permission_asked = true;
    SConfig::GetInstance().m_analytics_enabled = (answer == wxYES);
    SConfig::GetInstance().SaveSettings();

    DolphinAnalytics::Instance()->ReloadConfig();
  }
#endif

#ifdef _WIN32
  if (File::Exists("./Updater-temp.exe") && File::Exists("./Updater.exe"))
  {
    File::Delete("./Updater-temp.exe");
  }
  else if (File::Exists("./Updater-temp.exe") && !File::Exists("./Updater.exe"))
  {
    File::Rename("./Updater-temp.exe", "./Updater.exe");
  }
#endif

#ifdef __APPLE__
  if (File::Exists("./Dolphin.app/Contents/Resources/Updater-temp") && File::Exists("./Dolphin.app/Contents/Resources/Updater"))
  {
    File::Delete("./Dolphin.app/Contents/Resources/Updater-temp");
  }
  else if (File::Exists("./Dolphin.app/Contents/Resources/Updater-temp") && !File::Exists("./Dolphin.app/Contents/Resources/Updater"))
  {
    File::Rename("./Dolphin.app/Contents/Resources/Updater-temp", "./Dolphin.app/Contents/Resources/Updater");
  }
#endif

#if defined(_WIN32) || defined(__APPLE__)
  if (Config::Get(Config::MAIN_UPDATE_CHECK) == true)
    DolphinApp::CheckUpdate();
#endif

  if (m_confirm_stop)
    SConfig::GetInstance().bConfirmStop = m_confirm_setting;

  if (m_play_movie && !m_movie_file.empty())
  {
    std::optional<std::string> savestate_path;
    if (Movie::PlayInput(WxStrToStr(m_movie_file), &savestate_path))
    {
      if (m_boot)
      {
        m_boot->savestate_path = savestate_path;
        main_frame->StartGame(std::move(m_boot));
      }
      else
      {
        main_frame->BootGame("", savestate_path);
      }
    }
  }
  // First check if we have an exec command line.
  else if (m_boot)
  {
    main_frame->StartGame(std::move(m_boot));
  }
  // If we have selected Automatic Start, start the default ISO,
  // or if no default ISO exists, start the last loaded ISO
  else if (m_use_debugger)
  {
    if (main_frame->GetMenuBar()->IsChecked(IDM_AUTOMATIC_START))
    {
      main_frame->BootGame("");
    }
  }
}

void DolphinApp::OnActivate(wxActivateEvent& ev)
{
  m_is_active = ev.GetActive();
}

void DolphinApp::InitLanguageSupport()
{
  std::string language_code;
  {
    IniFile ini;
    ini.Load(File::GetUserPath(F_DOLPHINCONFIG_IDX));
    ini.GetOrCreateSection("Interface")->Get("LanguageCode", &language_code, "");
  }
  int language = wxLANGUAGE_UNKNOWN;
  if (language_code.empty())
  {
    language = wxLANGUAGE_DEFAULT;
  }
  else
  {
    const wxLanguageInfo* language_info = wxLocale::FindLanguageInfo(StrToWxStr(language_code));
    if (language_info)
      language = language_info->Language;
  }

  // Load language if possible, fall back to system default otherwise
  if (wxLocale::IsAvailable(language))
  {
    m_locale.reset(new wxLocale(language));

// Specify where dolphins *.gmo files are located on each operating system
#ifdef __WXMSW__
    m_locale->AddCatalogLookupPathPrefix(StrToWxStr(File::GetExeDirectory() + DIR_SEP "Languages"));
#elif defined(__WXGTK__)
    m_locale->AddCatalogLookupPathPrefix(StrToWxStr(DATA_DIR "../locale"));
#elif defined(__WXOSX__)
    m_locale->AddCatalogLookupPathPrefix(
        StrToWxStr(File::GetBundleDirectory() + "Contents/Resources"));
#endif

    m_locale->AddCatalog("ishiiruka");

    if (!m_locale->IsOk())
    {
      m_locale.reset(new wxLocale(wxLANGUAGE_DEFAULT));
    }
  }
  else
  {
    m_locale.reset(new wxLocale(wxLANGUAGE_DEFAULT));
  }
}

void DolphinApp::OnEndSession(wxCloseEvent& event)
{
  // Close if we've received wxEVT_END_SESSION (ignore wxEVT_QUERY_END_SESSION)
  if (!event.CanVeto())
  {
    main_frame->Close(true);
  }
}

int DolphinApp::OnExit()
{
  Core::Shutdown();
  UICommon::Shutdown();

  return wxApp::OnExit();
}

void DolphinApp::OnFatalException()
{
  WiimoteReal::Shutdown();
}

void DolphinApp::OnIdle(wxIdleEvent& ev)
{
  ev.Skip();
  Core::HostDispatchJobs();
}

#if defined(_WIN32) || defined(__APPLE__)
static void RunSystemCommand(const std::string& command)
{
#ifdef _WIN32
  _wsystem(UTF8ToUTF16(command).c_str());
#else
  system(command.c_str());
#endif
}

void DolphinApp::CheckUpdate()
{
  std::string url = "https://5fed2cdaa92e6d0008a19197--pplus.netlify.app/Update.json";
  Common::HttpRequest req{ std::chrono::seconds{10} };
  auto resp = req.Get(url);
  if (!resp)
  {
    ERROR_LOG(COMMON, "Update request failed");
    return;
  }
  std::string contents(reinterpret_cast<char*>(resp->data()), resp->size());
  INFO_LOG(COMMON, "Update JSON response: %s", contents.c_str());
  picojson::value json;
  std::string err = picojson::parse(json, contents);
  if (!err.empty())
  {
    ERROR_LOG(COMMON, "Invalid JSON received from auto-update service: %s", err.c_str());
    return;
  }
  picojson::object obj = json.get<picojson::object>();

  if (obj["hash"].get<std::string>() == Common::scm_rev_git_str.c_str() && obj["version"].get<std::string>() == Common::scm_desc_str.c_str())
  {
    INFO_LOG(COMMON, "Update status: we are up to date.");
    updateAvailable = false;
  }
  else
  {
    INFO_LOG(COMMON, "Update status: we are not up to date.");
    updateAvailable = true;
    std::string changelog = obj["changelog"].get<std::string>();
    int answer = wxMessageBox(_(
      "An update is available. Would you like to update? Changelog:\n\n" + changelog +
      "\n\nWANRING! IF YOU HAVE ANY CUSTOM CONTENT THIS MAY REPLACE IT!"
      "\nBack up your files if there's something you want to keep!"),
      _("Update"), wxYES_NO, main_frame);

    if (answer == wxYES)
    {
#ifdef _WIN32
      if (File::Exists("./Updater.exe"))
        File::Rename("./Updater.exe", "./Updater-temp.exe");
#endif
      DolphinApp::UpdateApp();
#ifdef _WIN32
      Exit();
#elif defined(__APPLE__)
      main_frame->Close();
#endif
    }
    else if (answer == wxNO && Config::Get(Config::MAIN_UPDATE_CHECK) != false)
    {
      Config::SetBaseOrCurrent(Config::MAIN_UPDATE_CHECK, false);
    }
  }
}

void DolphinApp::UpdateApp()
{
#ifdef _WIN32
  std::string path = File::GetExeDirectory() + "/Updater-temp.exe";
  std::string command = "start " + path + "\"";
  WARN_LOG(COMMON, "Executing app update command: %s", command);
  RunSystemCommand(command);
#elif defined(__APPLE__)
  std::string command = File::GetBundleDirectory() + "/Contents/Resources/Updater";
  RunSystemCommand(command);
#endif
}
#endif

// ------------
// Talk to GUI

bool wxMsgAlert(const char* caption, const char* text, bool yes_no, MsgType /*style*/)
{
  if (wxIsMainThread())
  {
    NetPlayDialog*& npd = NetPlayDialog::GetInstance();
    if (npd != nullptr && npd->IsShown())
    {
      npd->AppendChat("/!\\ " + std::string{ text }, false);
      return true;
    }
    return wxYES == wxMessageBox(StrToWxStr(text), StrToWxStr(caption),
                                 wxSTAY_ON_TOP | ((yes_no) ? wxYES_NO : wxOK),
                                 wxWindow::FindFocus());
  }
  else
  {
    wxCommandEvent event(wxEVT_HOST_COMMAND, IDM_PANIC);
    event.SetString(StrToWxStr(caption) + ":" + StrToWxStr(text));
    event.SetInt(yes_no);
    main_frame->GetEventHandler()->AddPendingEvent(event);
    main_frame->m_panic_event.Wait();
    return main_frame->m_panic_result;
  }
}

std::string wxStringTranslator(const char* text)
{
  return WxStrToStr(wxGetTranslation(wxString::FromUTF8(text)));
}

// Accessor for the main window class
CFrame* DolphinApp::GetCFrame()
{
  return main_frame;
}

void Host_Message(int Id)
{
  if (Id == WM_USER_JOB_DISPATCH)
  {
    // Trigger a wxEVT_IDLE
    wxWakeUpIdle();
    return;
  }
  wxCommandEvent event(wxEVT_HOST_COMMAND, Id);
  main_frame->GetEventHandler()->AddPendingEvent(event);
}

void* Host_GetRenderHandle()
{
  return main_frame->GetRenderHandle();
}

// OK, this thread boundary is DANGEROUS on Linux
// wxPostEvent / wxAddPendingEvent is the solution.
void Host_NotifyMapLoaded()
{
  wxCommandEvent event(wxEVT_HOST_COMMAND, IDM_NOTIFY_MAP_LOADED);
  main_frame->GetEventHandler()->AddPendingEvent(event);

  if (main_frame->m_code_window)
  {
    main_frame->m_code_window->GetEventHandler()->AddPendingEvent(event);
  }
}

void Host_UpdateDisasmDialog()
{
  wxCommandEvent event(wxEVT_HOST_COMMAND, IDM_UPDATE_DISASM_DIALOG);
  main_frame->GetEventHandler()->AddPendingEvent(event);

  if (main_frame->m_code_window)
  {
    main_frame->m_code_window->GetEventHandler()->AddPendingEvent(event);
  }
}

void Host_UpdateMainFrame()
{
  wxCommandEvent event(wxEVT_HOST_COMMAND, IDM_UPDATE_GUI);
  main_frame->GetEventHandler()->AddPendingEvent(event);

  if (main_frame->m_code_window)
  {
    main_frame->m_code_window->GetEventHandler()->AddPendingEvent(event);
  }
}

void Host_UpdateTitle(const std::string& title)
{
  wxCommandEvent event(wxEVT_HOST_COMMAND, IDM_UPDATE_TITLE);
  event.SetString(StrToWxStr(title));
  main_frame->GetEventHandler()->AddPendingEvent(event);
}

void Host_RequestRenderWindowSize(int width, int height)
{
  wxCommandEvent event(wxEVT_HOST_COMMAND, IDM_WINDOW_SIZE_REQUEST);
  event.SetClientData(new std::pair<int, int>(width, height));
  main_frame->GetEventHandler()->AddPendingEvent(event);
}

bool Host_UINeedsControllerState()
{
  return wxGetApp().IsActiveThreadsafe() && GetUINeedsControllerState();
}

bool Host_RendererHasFocus()
{
  return main_frame->RendererHasFocus();
}

bool Host_RendererIsFullscreen()
{
  return main_frame->RendererIsFullscreen();
}

void Host_ShowVideoConfig(void* parent, const std::string& backend_name)
{
  wxWindow* const parent_window = static_cast<wxWindow*>(parent);

  if (backend_name == "Software Renderer")
  {
    SoftwareVideoConfigDialog diag(parent_window, backend_name);
    diag.ShowModal();
  }
  else
  {
    VideoConfigDiag diag(parent_window, backend_name);
    diag.ShowModal();
  }
}

void Host_YieldToUI()
{
  wxGetApp().GetMainLoop()->YieldFor(wxEVT_CATEGORY_UI);
}

void Host_UpdateProgressDialog(const char* caption, int position, int total)
{
  wxCommandEvent event(wxEVT_HOST_COMMAND, IDM_UPDATE_PROGRESS_DIALOG);
  event.SetString(caption);
  event.SetInt(position);
  event.SetExtraLong(total);
  main_frame->GetEventHandler()->AddPendingEvent(event);
}

void Host_TitleChanged()
{
#ifdef USE_DISCORD_PRESENCE
  // TODO: Not sure if the NetPlay check is needed.
  Discord::UpdateDiscordPresence();
#endif
}
