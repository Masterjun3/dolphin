// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <array>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>
#include <wx/app.h>
#include <wx/aui/framemanager.h>
#include <wx/bitmap.h>
#include <wx/filedlg.h>
#include <wx/filefn.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/progdlg.h>
#include <wx/statusbr.h>
#include <wx/thread.h>
#include <wx/toolbar.h>
#include <wx/toplevel.h>

#include "Common/CDUtils.h"
#include "Common/CommonTypes.h"
#include "Common/FileSearch.h"
#include "Common/FileUtil.h"
#include "Common/NandPaths.h"
#include "Common/StringUtil.h"

#include "Core/BootManager.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/HW/CPU.h"
#include "Core/HW/DVDInterface.h"
#include "Core/HW/GCKeyboard.h"
#include "Core/HW/GCPad.h"
#include "Core/HW/ProcessorInterface.h"
#include "Core/HW/SI_Device.h"
#include "Core/HW/WiiSaveCrypted.h"
#include "Core/HW/Wiimote.h"
#include "Core/Host.h"
#include "Core/HotkeyManager.h"
#include "Core/IPC_HLE/WII_IPC_HLE.h"
#include "Core/IPC_HLE/WII_IPC_HLE_Device_stm.h"
#include "Core/IPC_HLE/WII_IPC_HLE_Device_usb_bt_emu.h"
#include "Core/IPC_HLE/WII_IPC_HLE_WiiMote.h"
#include "Core/Movie.h"
#include "Core/PowerPC/PPCSymbolDB.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/State.h"

#include "DiscIO/NANDContentLoader.h"

#include "DolphinWX/AboutDolphin.h"
#include "DolphinWX/Cheats/CheatsWindow.h"
#include "DolphinWX/Config/ConfigMain.h"
#include "DolphinWX/ControllerConfigDiag.h"
#include "DolphinWX/Debugger/BreakpointWindow.h"
#include "DolphinWX/Debugger/CodeWindow.h"
#include "DolphinWX/Debugger/WatchWindow.h"
#include "DolphinWX/FifoPlayerDlg.h"
#include "DolphinWX/Frame.h"
#include "DolphinWX/GameListCtrl.h"
#include "DolphinWX/Globals.h"
#include "DolphinWX/ISOFile.h"
#include "DolphinWX/InputConfigDiag.h"
#include "DolphinWX/LogWindow.h"
#include "DolphinWX/MemcardManager.h"
#include "DolphinWX/MovieEditor.h"
#include "DolphinWX/NetPlay/NetPlaySetupFrame.h"
#include "DolphinWX/NetPlay/NetWindow.h"
#include "DolphinWX/TASInputDlg.h"
#include "DolphinWX/WXInputBase.h"
#include "DolphinWX/WxUtils.h"

#include "InputCommon/ControllerInterface/ControllerInterface.h"

#include "VideoCommon/RenderBase.h"
#include "VideoCommon/VideoBackendBase.h"
#include "VideoCommon/VideoConfig.h"

class InputConfig;
class wxFrame;

// This override allows returning a fake menubar object while removing the real one from the screen
wxMenuBar* CFrame::GetMenuBar() const
{
  if (m_frameMenuBar)
  {
    return m_frameMenuBar;
  }
  else
  {
    return m_menubar_shadow;
  }
}

const wxSize& CFrame::GetToolbarBitmapSize() const
{
  return m_toolbar_bitmap_size;
}

// Create menu items
// ---------------------
wxMenuBar* CFrame::CreateMenuBar()
{
  auto* const menu_bar = new wxMenuBar;
  menu_bar->Append(CreateFileMenu(), _("&File"));
  menu_bar->Append(CreateEmulationMenu(), _("&Emulation"));
  menu_bar->Append(CreateMovieMenu(), _("&Movie"));
  menu_bar->Append(CreateOptionsMenu(), _("&Options"));
  menu_bar->Append(CreateToolsMenu(), _("&Tools"));
  menu_bar->Append(CreateViewMenu(), _("&View"));

  if (UseDebugger)
  {
    menu_bar->Append(CreateJITMenu(), _("&JIT"));
    menu_bar->Append(CreateDebugMenu(), _("&Debug"));
    menu_bar->Append(CreateSymbolsMenu(), _("&Symbols"));
    menu_bar->Append(CreateProfilerMenu(), _("&Profiler"));
  }

  menu_bar->Append(CreateHelpMenu(), _("&Help"));

  return menu_bar;
}

wxMenu* CFrame::CreateFileMenu()
{
  auto* const external_drive_menu = new wxMenu;

  drives = cdio_get_devices();
  // Windows Limitation of 24 character drives
  for (unsigned int i = 0; i < drives.size() && i < 24; i++)
  {
    external_drive_menu->Append(IDM_DRIVE1 + i, StrToWxStr(drives[i]));
  }

  auto* const file_menu = new wxMenu;
  file_menu->Append(wxID_OPEN, GetMenuLabel(HK_OPEN));
  file_menu->Append(IDM_CHANGE_DISC, GetMenuLabel(HK_CHANGE_DISC));
  file_menu->Append(IDM_DRIVES, _("&Boot from DVD Backup"), external_drive_menu);
  file_menu->AppendSeparator();
  file_menu->Append(wxID_REFRESH, GetMenuLabel(HK_REFRESH_LIST));
  file_menu->AppendSeparator();
  file_menu->Append(wxID_EXIT, _("E&xit") + "\tAlt+F4");

  return file_menu;
}

wxMenu* CFrame::CreateEmulationMenu()
{
  auto* const load_state_menu = new wxMenu;
  load_state_menu->Append(IDM_LOAD_STATE_FILE, GetMenuLabel(HK_LOAD_STATE_FILE));
  load_state_menu->Append(IDM_LOAD_SELECTED_SLOT, GetMenuLabel(HK_LOAD_STATE_SLOT_SELECTED));
  load_state_menu->Append(IDM_UNDO_LOAD_STATE, GetMenuLabel(HK_UNDO_LOAD_STATE));
  load_state_menu->AppendSeparator();

  auto* const save_state_menu = new wxMenu;
  save_state_menu->Append(IDM_SAVE_STATE_FILE, GetMenuLabel(HK_SAVE_STATE_FILE));
  save_state_menu->Append(IDM_SAVE_SELECTED_SLOT, GetMenuLabel(HK_SAVE_STATE_SLOT_SELECTED));
  save_state_menu->Append(IDM_SAVE_FIRST_STATE, GetMenuLabel(HK_SAVE_FIRST_STATE));
  save_state_menu->Append(IDM_UNDO_SAVE_STATE, GetMenuLabel(HK_UNDO_SAVE_STATE));
  save_state_menu->AppendSeparator();

  auto* const slot_select_menu = new wxMenu;

  for (unsigned int i = 0; i < State::NUM_STATES; i++)
  {
    load_state_menu->Append(IDM_LOAD_SLOT_1 + i, GetMenuLabel(HK_LOAD_STATE_SLOT_1 + i));
    save_state_menu->Append(IDM_SAVE_SLOT_1 + i, GetMenuLabel(HK_SAVE_STATE_SLOT_1 + i));
    slot_select_menu->Append(IDM_SELECT_SLOT_1 + i, GetMenuLabel(HK_SELECT_STATE_SLOT_1 + i));
  }

  load_state_menu->AppendSeparator();
  for (unsigned int i = 0; i < State::NUM_STATES; i++)
    load_state_menu->Append(IDM_LOAD_LAST_1 + i, GetMenuLabel(HK_LOAD_LAST_STATE_1 + i));

  auto* const emulation_menu = new wxMenu;
  emulation_menu->Append(IDM_PLAY, GetMenuLabel(HK_PLAY_PAUSE));
  emulation_menu->Append(IDM_STOP, GetMenuLabel(HK_STOP));
  emulation_menu->Append(IDM_RESET, GetMenuLabel(HK_RESET));
  emulation_menu->AppendSeparator();
  emulation_menu->Append(IDM_TOGGLE_FULLSCREEN, GetMenuLabel(HK_FULLSCREEN));
  emulation_menu->Append(IDM_FRAMESTEP, GetMenuLabel(HK_FRAME_ADVANCE));
  emulation_menu->AppendSeparator();
  emulation_menu->Append(IDM_SCREENSHOT, GetMenuLabel(HK_SCREENSHOT));
  emulation_menu->AppendSeparator();
  emulation_menu->Append(IDM_LOAD_STATE, _("&Load State"), load_state_menu);
  emulation_menu->Append(IDM_SAVE_STATE, _("Sa&ve State"), save_state_menu);
  emulation_menu->Append(IDM_SELECT_SLOT, _("Select State Slot"), slot_select_menu);

  return emulation_menu;
}

wxMenu* CFrame::CreateMovieMenu()
{
  auto* const movie_menu = new wxMenu;
  const auto& config_instance = SConfig::GetInstance();

  movie_menu->Append(IDM_RECORD, GetMenuLabel(HK_START_RECORDING));
  movie_menu->Append(IDM_PLAY_RECORD, GetMenuLabel(HK_PLAY_RECORDING));
  movie_menu->Append(IDM_RECORD_EXPORT, GetMenuLabel(HK_EXPORT_RECORDING));
  movie_menu->AppendCheckItem(IDM_RECORD_READ_ONLY, GetMenuLabel(HK_READ_ONLY_MODE));
  movie_menu->Append(IDM_TAS_INPUT, _("TAS Input"));
  movie_menu->AppendSeparator();
  movie_menu->Append(IDM_MOVIE_EDITOR, _("Movie Editor"));
  movie_menu->AppendSeparator();
  movie_menu->AppendCheckItem(IDM_TOGGLE_PAUSE_MOVIE, _("Pause at End of Movie"));
  movie_menu->Check(IDM_TOGGLE_PAUSE_MOVIE, config_instance.m_PauseMovie);
  movie_menu->AppendCheckItem(IDM_SHOW_LAG, _("Show Lag Counter"));
  movie_menu->Check(IDM_SHOW_LAG, config_instance.m_ShowLag);
  movie_menu->AppendCheckItem(IDM_SHOW_FRAME_COUNT, _("Show Frame Counter"));
  movie_menu->Check(IDM_SHOW_FRAME_COUNT, config_instance.m_ShowFrameCount);
  movie_menu->Check(IDM_RECORD_READ_ONLY, true);
  movie_menu->AppendCheckItem(IDM_SHOW_INPUT_DISPLAY, _("Show Input Display"));
  movie_menu->Check(IDM_SHOW_INPUT_DISPLAY, config_instance.m_ShowInputDisplay);
  movie_menu->AppendCheckItem(IDM_SHOW_RTC_DISPLAY, _("Show System Clock"));
  movie_menu->Check(IDM_SHOW_RTC_DISPLAY, config_instance.m_ShowRTC);
  movie_menu->AppendSeparator();
  movie_menu->AppendCheckItem(IDM_TOGGLE_DUMP_FRAMES, _("Dump Frames"));
  movie_menu->Check(IDM_TOGGLE_DUMP_FRAMES, config_instance.m_DumpFrames);
  movie_menu->AppendCheckItem(IDM_TOGGLE_DUMP_AUDIO, _("Dump Audio"));
  movie_menu->Check(IDM_TOGGLE_DUMP_AUDIO, config_instance.m_DumpAudio);

  return movie_menu;
}

wxMenu* CFrame::CreateOptionsMenu()
{
  auto* const options_menu = new wxMenu;
  options_menu->Append(wxID_PREFERENCES, _("Co&nfigure..."));
  options_menu->AppendSeparator();
  options_menu->Append(IDM_CONFIG_GFX_BACKEND, _("&Graphics Settings"));
  options_menu->Append(IDM_CONFIG_AUDIO, _("&Audio Settings"));
  options_menu->Append(IDM_CONFIG_CONTROLLERS, _("&Controller Settings"));
  options_menu->Append(IDM_CONFIG_HOTKEYS, _("&Hotkey Settings"));

  if (UseDebugger)
  {
    options_menu->AppendSeparator();

    const auto& config_instance = SConfig::GetInstance();

    auto* const boot_to_pause =
        options_menu->AppendCheckItem(IDM_BOOT_TO_PAUSE, _("Boot to Pause"),
                                      _("Start the game directly instead of booting to pause"));
    boot_to_pause->Check(config_instance.bBootToPause);

    auto* const automatic_start = options_menu->AppendCheckItem(
        IDM_AUTOMATIC_START, _("&Automatic Start"),
        _("Automatically load the Default ISO when Dolphin starts, or the last game you loaded,"
          " if you have not given it an elf file with the --elf command line. [This can be"
          " convenient if you are bug-testing with a certain game and want to rebuild"
          " and retry it several times, either with changes to Dolphin or if you are"
          " developing a homebrew game.]"));
    automatic_start->Check(config_instance.bAutomaticStart);

    options_menu->Append(IDM_FONT_PICKER, _("&Font..."));
  }

  return options_menu;
}

wxMenu* CFrame::CreateToolsMenu()
{
  auto* const wiimote_menu = new wxMenu;
  wiimote_menu->AppendCheckItem(IDM_CONNECT_WIIMOTE1, GetMenuLabel(HK_WIIMOTE1_CONNECT));
  wiimote_menu->AppendCheckItem(IDM_CONNECT_WIIMOTE2, GetMenuLabel(HK_WIIMOTE2_CONNECT));
  wiimote_menu->AppendCheckItem(IDM_CONNECT_WIIMOTE3, GetMenuLabel(HK_WIIMOTE3_CONNECT));
  wiimote_menu->AppendCheckItem(IDM_CONNECT_WIIMOTE4, GetMenuLabel(HK_WIIMOTE4_CONNECT));
  wiimote_menu->AppendSeparator();
  wiimote_menu->AppendCheckItem(IDM_CONNECT_BALANCEBOARD, GetMenuLabel(HK_BALANCEBOARD_CONNECT));

  auto* const tools_menu = new wxMenu;
  tools_menu->Append(IDM_MEMCARD, _("&Memcard Manager (GC)"));
  tools_menu->Append(IDM_IMPORT_SAVE, _("Import Wii Save..."));
  tools_menu->Append(IDM_EXPORT_ALL_SAVE, _("Export All Wii Saves"));
  tools_menu->Append(IDM_CHEATS, _("&Cheat Manager"));
  tools_menu->Append(IDM_NETPLAY, _("Start &NetPlay..."));
  tools_menu->Append(IDM_MENU_INSTALL_WAD, _("Install WAD..."));

  UpdateWiiMenuChoice(tools_menu->Append(IDM_LOAD_WII_MENU, "Dummy string to keep wxw happy"));

  tools_menu->Append(IDM_FIFOPLAYER, _("FIFO Player"));
  tools_menu->AppendSeparator();
  tools_menu->AppendSubMenu(wiimote_menu, _("Connect Wiimotes"));

  return tools_menu;
}

wxMenu* CFrame::CreateViewMenu()
{
  const auto& config_instance = SConfig::GetInstance();

  auto* const platform_menu = new wxMenu;
  platform_menu->AppendCheckItem(IDM_LIST_WII, _("Show Wii"));
  platform_menu->Check(IDM_LIST_WII, config_instance.m_ListWii);
  platform_menu->AppendCheckItem(IDM_LIST_GC, _("Show GameCube"));
  platform_menu->Check(IDM_LIST_GC, config_instance.m_ListGC);
  platform_menu->AppendCheckItem(IDM_LIST_WAD, _("Show WAD"));
  platform_menu->Check(IDM_LIST_WAD, config_instance.m_ListWad);
  platform_menu->AppendCheckItem(IDM_LIST_ELFDOL, _("Show ELF/DOL"));
  platform_menu->Check(IDM_LIST_ELFDOL, config_instance.m_ListElfDol);

  auto* const region_menu = new wxMenu;
  region_menu->AppendCheckItem(IDM_LIST_JAP, _("Show JAP"));
  region_menu->Check(IDM_LIST_JAP, config_instance.m_ListJap);
  region_menu->AppendCheckItem(IDM_LIST_PAL, _("Show PAL"));
  region_menu->Check(IDM_LIST_PAL, config_instance.m_ListPal);
  region_menu->AppendCheckItem(IDM_LIST_USA, _("Show USA"));
  region_menu->Check(IDM_LIST_USA, config_instance.m_ListUsa);
  region_menu->AppendSeparator();
  region_menu->AppendCheckItem(IDM_LIST_AUSTRALIA, _("Show Australia"));
  region_menu->Check(IDM_LIST_AUSTRALIA, config_instance.m_ListAustralia);
  region_menu->AppendCheckItem(IDM_LIST_FRANCE, _("Show France"));
  region_menu->Check(IDM_LIST_FRANCE, config_instance.m_ListFrance);
  region_menu->AppendCheckItem(IDM_LIST_GERMANY, _("Show Germany"));
  region_menu->Check(IDM_LIST_GERMANY, config_instance.m_ListGermany);
  region_menu->AppendCheckItem(IDM_LIST_ITALY, _("Show Italy"));
  region_menu->Check(IDM_LIST_ITALY, config_instance.m_ListItaly);
  region_menu->AppendCheckItem(IDM_LIST_KOREA, _("Show Korea"));
  region_menu->Check(IDM_LIST_KOREA, config_instance.m_ListKorea);
  region_menu->AppendCheckItem(IDM_LIST_NETHERLANDS, _("Show Netherlands"));
  region_menu->Check(IDM_LIST_NETHERLANDS, config_instance.m_ListNetherlands);
  region_menu->AppendCheckItem(IDM_LIST_RUSSIA, _("Show Russia"));
  region_menu->Check(IDM_LIST_RUSSIA, config_instance.m_ListRussia);
  region_menu->AppendCheckItem(IDM_LIST_SPAIN, _("Show Spain"));
  region_menu->Check(IDM_LIST_SPAIN, config_instance.m_ListSpain);
  region_menu->AppendCheckItem(IDM_LIST_TAIWAN, _("Show Taiwan"));
  region_menu->Check(IDM_LIST_TAIWAN, config_instance.m_ListTaiwan);
  region_menu->AppendCheckItem(IDM_LIST_WORLD, _("Show World"));
  region_menu->Check(IDM_LIST_WORLD, config_instance.m_ListWorld);
  region_menu->AppendCheckItem(IDM_LIST_UNKNOWN, _("Show Unknown"));
  region_menu->Check(IDM_LIST_UNKNOWN, config_instance.m_ListUnknown);

  auto* const columns_menu = new wxMenu;
  columns_menu->AppendCheckItem(IDM_SHOW_SYSTEM, _("Platform"));
  columns_menu->Check(IDM_SHOW_SYSTEM, config_instance.m_showSystemColumn);
  columns_menu->AppendCheckItem(IDM_SHOW_BANNER, _("Banner"));
  columns_menu->Check(IDM_SHOW_BANNER, config_instance.m_showBannerColumn);
  columns_menu->AppendCheckItem(IDM_SHOW_MAKER, _("Maker"));
  columns_menu->Check(IDM_SHOW_MAKER, config_instance.m_showMakerColumn);
  columns_menu->AppendCheckItem(IDM_SHOW_FILENAME, _("File Name"));
  columns_menu->Check(IDM_SHOW_FILENAME, config_instance.m_showFileNameColumn);
  columns_menu->AppendCheckItem(IDM_SHOW_ID, _("Game ID"));
  columns_menu->Check(IDM_SHOW_ID, config_instance.m_showIDColumn);
  columns_menu->AppendCheckItem(IDM_SHOW_REGION, _("Region"));
  columns_menu->Check(IDM_SHOW_REGION, config_instance.m_showRegionColumn);
  columns_menu->AppendCheckItem(IDM_SHOW_SIZE, _("File Size"));
  columns_menu->Check(IDM_SHOW_SIZE, config_instance.m_showSizeColumn);
  columns_menu->AppendCheckItem(IDM_SHOW_STATE, _("State"));
  columns_menu->Check(IDM_SHOW_STATE, config_instance.m_showStateColumn);

  auto* const view_menu = new wxMenu;
  view_menu->AppendCheckItem(IDM_TOGGLE_TOOLBAR, _("Show &Toolbar"));
  view_menu->Check(IDM_TOGGLE_TOOLBAR, config_instance.m_InterfaceToolbar);
  view_menu->AppendCheckItem(IDM_TOGGLE_STATUSBAR, _("Show &Status Bar"));
  view_menu->Check(IDM_TOGGLE_STATUSBAR, config_instance.m_InterfaceStatusbar);
  view_menu->AppendSeparator();
  view_menu->AppendCheckItem(IDM_LOG_WINDOW, _("Show &Log"));
  view_menu->AppendCheckItem(IDM_LOG_CONFIG_WINDOW, _("Show Log &Configuration"));
  view_menu->AppendSeparator();

  if (g_pCodeWindow)
  {
    view_menu->Check(IDM_LOG_WINDOW, g_pCodeWindow->bShowOnStart[0]);

    static const wxString menu_text[] = {_("&Registers"), _("&Watch"), _("&Breakpoints"),
                                         _("&Memory"),    _("&JIT"),   _("&Sound"),
                                         _("&Video")};

    for (int i = IDM_REGISTER_WINDOW; i <= IDM_VIDEO_WINDOW; i++)
    {
      view_menu->AppendCheckItem(i, menu_text[i - IDM_REGISTER_WINDOW]);
      view_menu->Check(i, g_pCodeWindow->bShowOnStart[i - IDM_LOG_WINDOW]);
    }

    view_menu->AppendSeparator();
  }
  else
  {
    view_menu->Check(IDM_LOG_WINDOW, config_instance.m_InterfaceLogWindow);
    view_menu->Check(IDM_LOG_CONFIG_WINDOW, config_instance.m_InterfaceLogConfigWindow);
  }

  view_menu->AppendSubMenu(platform_menu, _("Show Platforms"));
  view_menu->AppendSubMenu(region_menu, _("Show Regions"));

  view_menu->AppendCheckItem(IDM_LIST_DRIVES, _("Show Drives"));
  view_menu->Check(IDM_LIST_DRIVES, config_instance.m_ListDrives);

  view_menu->Append(IDM_PURGE_GAME_LIST_CACHE, _("Purge Game List Cache"));
  view_menu->AppendSubMenu(columns_menu, _("Select Columns"));

  return view_menu;
}

wxMenu* CFrame::CreateJITMenu()
{
  auto* const jit_menu = new wxMenu;
  const auto& config_instance = SConfig::GetInstance();

  auto* const interpreter = jit_menu->AppendCheckItem(
      IDM_INTERPRETER, _("&Interpreter Core"),
      _("This is necessary to get break points"
        " and stepping to work as explained in the Developer Documentation. But it can be very"
        " slow, perhaps slower than 1 fps."));
  interpreter->Check(config_instance.iCPUCore == PowerPC::CORE_INTERPRETER);

  jit_menu->AppendSeparator();
  jit_menu->AppendCheckItem(IDM_JIT_NO_BLOCK_LINKING, _("&JIT Block Linking Off"),
                            _("Provide safer execution by not linking the JIT blocks."));

  jit_menu->AppendCheckItem(
      IDM_JIT_NO_BLOCK_CACHE, _("&Disable JIT Cache"),
      _("Avoid any involuntary JIT cache clearing, this may prevent Zelda TP from "
        "crashing.\n[This option must be selected before a game is started.]"));

  jit_menu->Append(IDM_CLEAR_CODE_CACHE, _("&Clear JIT Cache"));
  jit_menu->AppendSeparator();
  jit_menu->Append(IDM_LOG_INSTRUCTIONS, _("&Log JIT Instruction Coverage"));
  jit_menu->Append(IDM_SEARCH_INSTRUCTION, _("&Search for an Instruction"));
  jit_menu->AppendSeparator();

  jit_menu->AppendCheckItem(
      IDM_JIT_OFF, _("&JIT Off (JIT Core)"),
      _("Turn off all JIT functions, but still use the JIT core from Jit.cpp"));

  jit_menu->AppendCheckItem(IDM_JIT_LS_OFF, _("&JIT LoadStore Off"));
  jit_menu->AppendCheckItem(IDM_JIT_LSLBZX_OFF, _("&JIT LoadStore lbzx Off"));
  jit_menu->AppendCheckItem(IDM_JIT_LSLXZ_OFF, _("&JIT LoadStore lXz Off"));
  jit_menu->AppendCheckItem(IDM_JIT_LSLWZ_OFF, _("&JIT LoadStore lwz Off"));
  jit_menu->AppendCheckItem(IDM_JIT_LSF_OFF, _("&JIT LoadStore Floating Off"));
  jit_menu->AppendCheckItem(IDM_JIT_LSP_OFF, _("&JIT LoadStore Paired Off"));
  jit_menu->AppendCheckItem(IDM_JIT_FP_OFF, _("&JIT FloatingPoint Off"));
  jit_menu->AppendCheckItem(IDM_JIT_I_OFF, _("&JIT Integer Off"));
  jit_menu->AppendCheckItem(IDM_JIT_P_OFF, _("&JIT Paired Off"));
  jit_menu->AppendCheckItem(IDM_JIT_SR_OFF, _("&JIT SystemRegisters Off"));

  return jit_menu;
}

wxMenu* CFrame::CreateDebugMenu()
{
  m_SavedPerspectives = new wxMenu;
  PopulateSavedPerspectives();

  auto* const add_pane_menu = new wxMenu;
  add_pane_menu->Append(IDM_PERSPECTIVES_ADD_PANE_TOP, _("Top"));
  add_pane_menu->Append(IDM_PERSPECTIVES_ADD_PANE_BOTTOM, _("Bottom"));
  add_pane_menu->Append(IDM_PERSPECTIVES_ADD_PANE_LEFT, _("Left"));
  add_pane_menu->Append(IDM_PERSPECTIVES_ADD_PANE_RIGHT, _("Right"));
  add_pane_menu->Append(IDM_PERSPECTIVES_ADD_PANE_CENTER, _("Center"));

  auto* const perspective_menu = new wxMenu;
  perspective_menu->Append(IDM_SAVE_PERSPECTIVE, _("Save Perspectives"),
                           _("Save currently-toggled perspectives"));
  perspective_menu->AppendCheckItem(IDM_EDIT_PERSPECTIVES, _("Edit Perspectives"),
                                    _("Toggle editing of perspectives"));
  perspective_menu->AppendSeparator();
  perspective_menu->Append(IDM_ADD_PERSPECTIVE, _("Create New Perspective"));
  perspective_menu->AppendSubMenu(m_SavedPerspectives, _("Saved Perspectives"));
  perspective_menu->AppendSeparator();
  perspective_menu->AppendSubMenu(add_pane_menu, _("Add New Pane To"));
  perspective_menu->AppendCheckItem(IDM_TAB_SPLIT, _("Tab Split"));
  perspective_menu->AppendCheckItem(IDM_NO_DOCKING, _("Disable Docking"),
                                    _("Disable docking of perspective panes to main window"));

  auto* const debug_menu = new wxMenu;
  debug_menu->Append(IDM_STEP, _("Step &Into\tF11"));
  debug_menu->Append(IDM_STEPOVER, _("Step &Over\tF10"));
  debug_menu->Append(IDM_STEPOUT, _("Step O&ut\tSHIFT+F11"));
  debug_menu->Append(IDM_TOGGLE_BREAKPOINT, _("Toggle &Breakpoint\tF9"));
  debug_menu->AppendSeparator();
  debug_menu->AppendSubMenu(perspective_menu, _("Perspectives"), _("Edit Perspectives"));

  return debug_menu;
}

wxMenu* CFrame::CreateSymbolsMenu()
{
  auto* const symbols_menu = new wxMenu;
  symbols_menu->Append(IDM_CLEAR_SYMBOLS, _("&Clear Symbols"),
                       _("Remove names from all functions and variables."));
  symbols_menu->Append(IDM_SCAN_FUNCTIONS, _("&Generate Symbol Map"),
                       _("Recognise standard functions from sys\\totaldb.dsy, and use generic zz_ "
                         "names for other functions."));
  symbols_menu->AppendSeparator();
  symbols_menu->Append(IDM_LOAD_MAP_FILE, _("&Load Symbol Map"),
                       _("Try to load this game's function names automatically - but doesn't check "
                         ".map files stored on the disc image yet."));
  symbols_menu->Append(IDM_SAVEMAPFILE, _("&Save Symbol Map"),
                       _("Save the function names for each address to a .map file in your user "
                         "settings map folder, named after the title id."));
  symbols_menu->AppendSeparator();
  symbols_menu->Append(
      IDM_LOAD_MAP_FILE_AS, _("Load &Other Map File..."),
      _("Load any .map file containing the function names and addresses for this game."));
  symbols_menu->Append(
      IDM_LOAD_BAD_MAP_FILE, _("Load &Bad Map File..."),
      _("Try to load a .map file that might be from a slightly different version."));
  symbols_menu->Append(IDM_SAVE_MAP_FILE_AS, _("Save Symbol Map &As..."),
                       _("Save the function names and addresses for this game as a .map file. If "
                         "you want to open it in IDA pro, use the .idc script."));
  symbols_menu->AppendSeparator();
  symbols_menu->Append(
      IDM_SAVE_MAP_FILE_WITH_CODES, _("Save Code"),
      _("Save the entire disassembled code. This may take a several seconds"
        " and may require between 50 and 100 MB of hard drive space. It will only save code"
        " that are in the first 4 MB of memory, if you are debugging a game that load .rel"
        " files with code to memory you may want to increase that to perhaps 8 MB, you can do"
        " that from SymbolDB::SaveMap()."));

  symbols_menu->AppendSeparator();
  symbols_menu->Append(
      IDM_CREATE_SIGNATURE_FILE, _("&Create Signature File..."),
      _("Create a .dsy file that can be used to recognise these same functions in other games."));
  symbols_menu->Append(IDM_APPEND_SIGNATURE_FILE, _("Append to &Existing Signature File..."),
                       _("Add any named functions missing from a .dsy file, so it can also "
                         "recognise these additional functions in other games."));
  symbols_menu->Append(IDM_COMBINE_SIGNATURE_FILES, _("Combine Two Signature Files..."),
                       _("Make a new .dsy file which can recognise more functions, by combining "
                         "two existing files. The first input file has priority."));
  symbols_menu->Append(
      IDM_USE_SIGNATURE_FILE, _("Apply Signat&ure File..."),
      _("Must use Generate symbol map first! Recognise names of any standard library functions "
        "used in multiple games, by loading them from a .dsy file."));
  symbols_menu->AppendSeparator();
  symbols_menu->Append(IDM_PATCH_HLE_FUNCTIONS, _("&Patch HLE Functions"));
  symbols_menu->Append(IDM_RENAME_SYMBOLS, _("&Rename Symbols from File..."));

  return symbols_menu;
}

wxMenu* CFrame::CreateProfilerMenu()
{
  auto* const profiler_menu = new wxMenu;
  profiler_menu->AppendCheckItem(IDM_PROFILE_BLOCKS, _("&Profile Blocks"));
  profiler_menu->AppendSeparator();
  profiler_menu->Append(IDM_WRITE_PROFILE, _("&Write to profile.txt, Show"));

  return profiler_menu;
}

wxMenu* CFrame::CreateHelpMenu()
{
  auto* const help_menu = new wxMenu;
  help_menu->Append(IDM_HELP_WEBSITE, _("&Website"));
  help_menu->Append(IDM_HELP_ONLINE_DOCS, _("Online &Documentation"));
  help_menu->Append(IDM_HELP_GITHUB, _("&GitHub Repository"));
  help_menu->AppendSeparator();
  help_menu->Append(wxID_ABOUT, _("&About"));

  return help_menu;
}

wxString CFrame::GetMenuLabel(int Id)
{
  wxString Label;

  switch (Id)
  {
  case HK_OPEN:
    Label = _("&Open...");
    break;
  case HK_CHANGE_DISC:
    Label = _("Change &Disc...");
    break;
  case HK_REFRESH_LIST:
    Label = _("&Refresh List");
    break;

  case HK_PLAY_PAUSE:
    if (Core::GetState() == Core::CORE_RUN)
      Label = _("&Pause");
    else
      Label = _("&Play");
    break;
  case HK_STOP:
    Label = _("&Stop");
    break;
  case HK_RESET:
    Label = _("&Reset");
    break;
  case HK_FRAME_ADVANCE:
    Label = _("&Frame Advance");
    break;

  case HK_START_RECORDING:
    Label = _("Start Re&cording Input");
    break;
  case HK_PLAY_RECORDING:
    Label = _("P&lay Input Recording...");
    break;
  case HK_EXPORT_RECORDING:
    Label = _("Export Recording...");
    break;
  case HK_READ_ONLY_MODE:
    Label = _("&Read-Only Mode");
    break;

  case HK_FULLSCREEN:
    Label = _("&Fullscreen");
    break;
  case HK_SCREENSHOT:
    Label = _("Take Screenshot");
    break;
  case HK_EXIT:
    Label = _("Exit");
    break;

  case HK_WIIMOTE1_CONNECT:
  case HK_WIIMOTE2_CONNECT:
  case HK_WIIMOTE3_CONNECT:
  case HK_WIIMOTE4_CONNECT:
    Label = wxString::Format(_("Connect Wiimote %i"), Id - HK_WIIMOTE1_CONNECT + 1);
    break;
  case HK_BALANCEBOARD_CONNECT:
    Label = _("Connect Balance Board");
    break;
  case HK_LOAD_STATE_SLOT_1:
  case HK_LOAD_STATE_SLOT_2:
  case HK_LOAD_STATE_SLOT_3:
  case HK_LOAD_STATE_SLOT_4:
  case HK_LOAD_STATE_SLOT_5:
  case HK_LOAD_STATE_SLOT_6:
  case HK_LOAD_STATE_SLOT_7:
  case HK_LOAD_STATE_SLOT_8:
  case HK_LOAD_STATE_SLOT_9:
  case HK_LOAD_STATE_SLOT_10:
    Label = wxString::Format(_("Slot %i - %s"), Id - HK_LOAD_STATE_SLOT_1 + 1,
                             StrToWxStr(State::GetInfoStringOfSlot(Id - HK_LOAD_STATE_SLOT_1 + 1)));
    break;

  case HK_SAVE_STATE_SLOT_1:
  case HK_SAVE_STATE_SLOT_2:
  case HK_SAVE_STATE_SLOT_3:
  case HK_SAVE_STATE_SLOT_4:
  case HK_SAVE_STATE_SLOT_5:
  case HK_SAVE_STATE_SLOT_6:
  case HK_SAVE_STATE_SLOT_7:
  case HK_SAVE_STATE_SLOT_8:
  case HK_SAVE_STATE_SLOT_9:
  case HK_SAVE_STATE_SLOT_10:
    Label = wxString::Format(_("Slot %i - %s"), Id - HK_SAVE_STATE_SLOT_1 + 1,
                             StrToWxStr(State::GetInfoStringOfSlot(Id - HK_SAVE_STATE_SLOT_1 + 1)));
    break;
  case HK_SAVE_STATE_FILE:
    Label = _("Save State...");
    break;

  case HK_LOAD_LAST_STATE_1:
  case HK_LOAD_LAST_STATE_2:
  case HK_LOAD_LAST_STATE_3:
  case HK_LOAD_LAST_STATE_4:
  case HK_LOAD_LAST_STATE_5:
  case HK_LOAD_LAST_STATE_6:
  case HK_LOAD_LAST_STATE_7:
  case HK_LOAD_LAST_STATE_8:
  case HK_LOAD_LAST_STATE_9:
  case HK_LOAD_LAST_STATE_10:
    Label = wxString::Format(_("Last %i"), Id - HK_LOAD_LAST_STATE_1 + 1);
    break;
  case HK_LOAD_STATE_FILE:
    Label = _("Load State...");
    break;

  case HK_SAVE_FIRST_STATE:
    Label = _("Save Oldest State");
    break;
  case HK_UNDO_LOAD_STATE:
    Label = _("Undo Load State");
    break;
  case HK_UNDO_SAVE_STATE:
    Label = _("Undo Save State");
    break;

  case HK_SAVE_STATE_SLOT_SELECTED:
    Label = _("Save State to Selected Slot");
    break;

  case HK_LOAD_STATE_SLOT_SELECTED:
    Label = _("Load State from Selected Slot");
    break;

  case HK_SELECT_STATE_SLOT_1:
  case HK_SELECT_STATE_SLOT_2:
  case HK_SELECT_STATE_SLOT_3:
  case HK_SELECT_STATE_SLOT_4:
  case HK_SELECT_STATE_SLOT_5:
  case HK_SELECT_STATE_SLOT_6:
  case HK_SELECT_STATE_SLOT_7:
  case HK_SELECT_STATE_SLOT_8:
  case HK_SELECT_STATE_SLOT_9:
  case HK_SELECT_STATE_SLOT_10:
    Label =
        wxString::Format(_("Select Slot %i - %s"), Id - HK_SELECT_STATE_SLOT_1 + 1,
                         StrToWxStr(State::GetInfoStringOfSlot(Id - HK_SELECT_STATE_SLOT_1 + 1)));
    break;

  default:
    Label = wxString::Format(_("Undefined %i"), Id);
  }

  return Label;
}

// Create toolbar items
// ---------------------
void CFrame::PopulateToolbar(wxToolBar* ToolBar)
{
  WxUtils::AddToolbarButton(ToolBar, wxID_OPEN, _("Open"), m_Bitmaps[Toolbar_FileOpen],
                            _("Open file..."));
  WxUtils::AddToolbarButton(ToolBar, wxID_REFRESH, _("Refresh"), m_Bitmaps[Toolbar_Refresh],
                            _("Refresh game list"));
  ToolBar->AddSeparator();
  WxUtils::AddToolbarButton(ToolBar, IDM_PLAY, _("Play"), m_Bitmaps[Toolbar_Play], _("Play"));
  WxUtils::AddToolbarButton(ToolBar, IDM_STOP, _("Stop"), m_Bitmaps[Toolbar_Stop], _("Stop"));
  WxUtils::AddToolbarButton(ToolBar, IDM_TOGGLE_FULLSCREEN, _("FullScr"),
                            m_Bitmaps[Toolbar_FullScreen], _("Toggle fullscreen"));
  WxUtils::AddToolbarButton(ToolBar, IDM_SCREENSHOT, _("ScrShot"), m_Bitmaps[Toolbar_Screenshot],
                            _("Take screenshot"));
  ToolBar->AddSeparator();
  WxUtils::AddToolbarButton(ToolBar, wxID_PREFERENCES, _("Config"), m_Bitmaps[Toolbar_ConfigMain],
                            _("Configure..."));
  WxUtils::AddToolbarButton(ToolBar, IDM_CONFIG_GFX_BACKEND, _("Graphics"),
                            m_Bitmaps[Toolbar_ConfigGFX], _("Graphics settings"));
  WxUtils::AddToolbarButton(ToolBar, IDM_CONFIG_CONTROLLERS, _("Controllers"),
                            m_Bitmaps[Toolbar_Controller], _("Controller settings"));
}

// Delete and recreate the toolbar
void CFrame::RecreateToolbar()
{
  static constexpr long TOOLBAR_STYLE = wxTB_DEFAULT_STYLE | wxTB_TEXT | wxTB_FLAT;

  if (m_ToolBar != nullptr)
  {
    m_ToolBar->Destroy();
    m_ToolBar = nullptr;
  }

  m_ToolBar = CreateToolBar(TOOLBAR_STYLE, wxID_ANY);
  m_ToolBar->SetToolBitmapSize(m_toolbar_bitmap_size);

  if (g_pCodeWindow)
  {
    g_pCodeWindow->PopulateToolbar(m_ToolBar);
    m_ToolBar->AddSeparator();
  }

  PopulateToolbar(m_ToolBar);
  // after adding the buttons to the toolbar, must call Realize() to reflect
  // the changes
  m_ToolBar->Realize();

  UpdateGUI();
}

void CFrame::InitBitmaps()
{
  static constexpr std::array<const char* const, EToolbar_Max> s_image_names{
      {"open", "refresh", "play", "stop", "pause", "screenshot", "fullscreen", "config", "graphics",
       "classic"}};
  for (std::size_t i = 0; i < s_image_names.size(); ++i)
    m_Bitmaps[i] = WxUtils::LoadScaledThemeBitmap(s_image_names[i], this, m_toolbar_bitmap_size);

  // Update in case the bitmap has been updated
  if (m_ToolBar != nullptr)
    RecreateToolbar();
}

void CFrame::OpenGeneralConfiguration(int tab)
{
  CConfigMain config_main(this);
  if (tab > -1)
    config_main.SetSelectedTab(tab);

  HotkeyManagerEmu::Enable(false);
  if (config_main.ShowModal() == wxID_OK)
    UpdateGameList();
  HotkeyManagerEmu::Enable(true);

  UpdateGUI();
}

// Menu items

// Start the game or change the disc.
// Boot priority:
// 1. Show the game list and boot the selected game.
// 2. Default ISO
// 3. Boot last selected game
void CFrame::BootGame(const std::string& filename)
{
  std::string bootfile = filename;
  SConfig& StartUp = SConfig::GetInstance();

  if (Core::GetState() != Core::CORE_UNINITIALIZED)
    return;

  // Start filename if non empty.
  // Start the selected ISO, or try one of the saved paths.
  // If all that fails, ask to add a dir and don't boot
  if (bootfile.empty())
  {
    if (m_GameListCtrl->GetSelectedISO() != nullptr)
    {
      if (m_GameListCtrl->GetSelectedISO()->IsValid())
        bootfile = m_GameListCtrl->GetSelectedISO()->GetFileName();
    }
    else if (!StartUp.m_strDefaultISO.empty() && File::Exists(StartUp.m_strDefaultISO))
    {
      bootfile = StartUp.m_strDefaultISO;
    }
    else
    {
      if (!SConfig::GetInstance().m_LastFilename.empty() &&
          File::Exists(SConfig::GetInstance().m_LastFilename))
      {
        bootfile = SConfig::GetInstance().m_LastFilename;
      }
      else
      {
        m_GameListCtrl->BrowseForDirectory();
        return;
      }
    }
  }
  if (!bootfile.empty())
  {
    StartGame(bootfile);
    if (UseDebugger && g_pCodeWindow)
    {
      if (g_pCodeWindow->HasPanel<CWatchWindow>())
        g_pCodeWindow->GetPanel<CWatchWindow>()->LoadAll();
      if (g_pCodeWindow->HasPanel<CBreakPointWindow>())
        g_pCodeWindow->GetPanel<CBreakPointWindow>()->LoadAll();
    }
  }
}

// Open file to boot
void CFrame::OnOpen(wxCommandEvent& WXUNUSED(event))
{
  if (Core::GetState() == Core::CORE_UNINITIALIZED)
    DoOpen(true);
}

void CFrame::DoOpen(bool Boot)
{
  std::string currentDir = File::GetCurrentDir();

  wxString path = wxFileSelector(
      _("Select the file to load"), wxEmptyString, wxEmptyString, wxEmptyString,
      _("All GC/Wii files (elf, dol, gcm, iso, wbfs, ciso, gcz, wad)") +
          wxString::Format("|*.elf;*.dol;*.gcm;*.iso;*.wbfs;*.ciso;*.gcz;*.wad;*.dff;*.tmd|%s",
                           wxGetTranslation(wxALL_FILES)),
      wxFD_OPEN | wxFD_FILE_MUST_EXIST, this);

  if (path.IsEmpty())
    return;

  std::string currentDir2 = File::GetCurrentDir();

  if (currentDir != currentDir2)
  {
    PanicAlertT("Current directory changed from %s to %s after wxFileSelector!", currentDir.c_str(),
                currentDir2.c_str());
    File::SetCurrentDir(currentDir);
  }

  // Should we boot a new game or just change the disc?
  if (Boot && !path.IsEmpty())
  {
    BootGame(WxStrToStr(path));
  }
  else
  {
    DVDInterface::ChangeDiscAsHost(WxStrToStr(path));
  }
}

void CFrame::OnRecordReadOnly(wxCommandEvent& event)
{
  Movie::SetReadOnly(event.IsChecked());
}

void CFrame::OnTASInput(wxCommandEvent& event)
{
  for (int i = 0; i < 4; ++i)
  {
    if (SConfig::GetInstance().m_SIDevice[i] != SIDEVICE_NONE &&
        SConfig::GetInstance().m_SIDevice[i] != SIDEVICE_GC_GBA)
    {
      g_TASInputDlg[i]->CreateGCLayout();
      g_TASInputDlg[i]->Show();
      g_TASInputDlg[i]->SetTitle(wxString::Format(_("TAS Input - Controller %d"), i + 1));
    }

    if (g_wiimote_sources[i] == WIIMOTE_SRC_EMU &&
        !(Core::IsRunning() && !SConfig::GetInstance().bWii))
    {
      g_TASInputDlg[i + 4]->CreateWiiLayout(i);
      g_TASInputDlg[i + 4]->Show();
      g_TASInputDlg[i + 4]->SetTitle(wxString::Format(_("TAS Input - Wiimote %d"), i + 1));
    }
  }
}

void CFrame::OnMovieEditor(wxCommandEvent& event)
{
	if (!g_MovieEditor){
		g_MovieEditor = new MovieEditor(this);
		Movie::SetMovieEditor(MovieEditorFunction);
	} else {
		g_MovieEditor->Raise();
	}
}

void CFrame::OnTogglePauseMovie(wxCommandEvent& WXUNUSED(event))
{
  SConfig::GetInstance().m_PauseMovie = !SConfig::GetInstance().m_PauseMovie;
  SConfig::GetInstance().SaveSettings();
}

void CFrame::OnToggleDumpFrames(wxCommandEvent& WXUNUSED(event))
{
  SConfig::GetInstance().m_DumpFrames = !SConfig::GetInstance().m_DumpFrames;
  SConfig::GetInstance().SaveSettings();
}

void CFrame::OnToggleDumpAudio(wxCommandEvent& WXUNUSED(event))
{
  SConfig::GetInstance().m_DumpAudio = !SConfig::GetInstance().m_DumpAudio;
}

void CFrame::OnShowLag(wxCommandEvent& WXUNUSED(event))
{
  SConfig::GetInstance().m_ShowLag = !SConfig::GetInstance().m_ShowLag;
  SConfig::GetInstance().SaveSettings();
}

void CFrame::OnShowFrameCount(wxCommandEvent& WXUNUSED(event))
{
  SConfig::GetInstance().m_ShowFrameCount = !SConfig::GetInstance().m_ShowFrameCount;
  SConfig::GetInstance().SaveSettings();
}

void CFrame::OnShowInputDisplay(wxCommandEvent& WXUNUSED(event))
{
  SConfig::GetInstance().m_ShowInputDisplay = !SConfig::GetInstance().m_ShowInputDisplay;
  SConfig::GetInstance().SaveSettings();
}

void CFrame::OnShowRTCDisplay(wxCommandEvent& WXUNUSED(event))
{
  SConfig::GetInstance().m_ShowRTC = !SConfig::GetInstance().m_ShowRTC;
  SConfig::GetInstance().SaveSettings();
}

void CFrame::OnFrameStep(wxCommandEvent& event)
{
  bool wasPaused = (Core::GetState() == Core::CORE_PAUSE);

  Movie::DoFrameStep();

  bool isPaused = (Core::GetState() == Core::CORE_PAUSE);
  if (isPaused && !wasPaused)  // don't update on unpause, otherwise the status would be wrong when
                               // pausing next frame
    UpdateGUI();
}

void CFrame::OnChangeDisc(wxCommandEvent& WXUNUSED(event))
{
  DoOpen(false);
}

void CFrame::OnRecord(wxCommandEvent& WXUNUSED(event))
{
  if ((!Core::IsRunningAndStarted() && Core::IsRunning()) || Movie::IsRecordingInput() ||
      Movie::IsPlayingInput())
    return;

  int controllers = 0;

  if (Movie::IsReadOnly())
  {
    // The user just chose to record a movie, so that should take precedence
    Movie::SetReadOnly(false);
    GetMenuBar()->FindItem(IDM_RECORD_READ_ONLY)->Check(false);
  }

  for (int i = 0; i < 4; i++)
  {
    if (SIDevice_IsGCController(SConfig::GetInstance().m_SIDevice[i]))
      controllers |= (1 << i);

    if (g_wiimote_sources[i] != WIIMOTE_SRC_NONE)
      controllers |= (1 << (i + 4));
  }

  if (Movie::BeginRecordingInput(controllers))
    BootGame("");
}

void CFrame::OnPlayRecording(wxCommandEvent& WXUNUSED(event))
{
  wxString path =
      wxFileSelector(_("Select The Recording File"), wxEmptyString, wxEmptyString, wxEmptyString,
                     _("Dolphin TAS Movies (*.dtm)") +
                         wxString::Format("|*.dtm|%s", wxGetTranslation(wxALL_FILES)),
                     wxFD_OPEN | wxFD_PREVIEW | wxFD_FILE_MUST_EXIST, this);

  if (path.IsEmpty())
    return;

  if (!Movie::IsReadOnly())
  {
    // let's make the read-only flag consistent at the start of a movie.
    Movie::SetReadOnly(true);
    GetMenuBar()->FindItem(IDM_RECORD_READ_ONLY)->Check();
  }

  if (Movie::PlayInput(WxStrToStr(path)))
    BootGame("");
}

void CFrame::OnRecordExport(wxCommandEvent& WXUNUSED(event))
{
  DoRecordingSave();
}

void CFrame::OnPlay(wxCommandEvent& WXUNUSED(event))
{
  if (Core::IsRunning())
  {
    // Core is initialized and emulator is running
    if (UseDebugger)
    {
      CPU::EnableStepping(!CPU::IsStepping());

      wxThread::Sleep(20);
      g_pCodeWindow->JumpToAddress(PC);
      g_pCodeWindow->Repopulate();
      // Update toolbar with Play/Pause status
      UpdateGUI();
    }
    else
    {
      DoPause();
    }
  }
  else
  {
    // Core is uninitialized, start the game
    BootGame("");
  }
}

void CFrame::OnRenderParentClose(wxCloseEvent& event)
{
  // Before closing the window we need to shut down the emulation core.
  // We'll try to close this window again once that is done.
  if (Core::GetState() != Core::CORE_UNINITIALIZED)
  {
    DoStop();
    if (event.CanVeto())
    {
      event.Veto();
    }
    return;
  }

  event.Skip();
}

void CFrame::OnRenderParentMove(wxMoveEvent& event)
{
  if (Core::GetState() != Core::CORE_UNINITIALIZED && !RendererIsFullscreen() &&
      !m_RenderFrame->IsMaximized() && !m_RenderFrame->IsIconized())
  {
    SConfig::GetInstance().iRenderWindowXPos = m_RenderFrame->GetPosition().x;
    SConfig::GetInstance().iRenderWindowYPos = m_RenderFrame->GetPosition().y;
  }
  event.Skip();
}

void CFrame::OnRenderParentResize(wxSizeEvent& event)
{
  if (Core::GetState() != Core::CORE_UNINITIALIZED)
  {
    int width, height;
    if (!SConfig::GetInstance().bRenderToMain && !RendererIsFullscreen() &&
        !m_RenderFrame->IsMaximized() && !m_RenderFrame->IsIconized())
    {
      m_RenderFrame->GetClientSize(&width, &height);
      SConfig::GetInstance().iRenderWindowWidth = width;
      SConfig::GetInstance().iRenderWindowHeight = height;
    }
    m_LogWindow->Refresh();
    m_LogWindow->Update();

    // We call Renderer::ChangeSurface here to indicate the size has changed,
    // but pass the same window handle. This is needed for the Vulkan backend,
    // otherwise it cannot tell that the window has been resized on some drivers.
    if (g_renderer)
      g_renderer->ChangeSurface(GetRenderHandle());
  }
  event.Skip();
}

void CFrame::ToggleDisplayMode(bool bFullscreen)
{
#ifdef _WIN32
  if (bFullscreen && SConfig::GetInstance().strFullscreenResolution != "Auto")
  {
    DEVMODE dmScreenSettings;
    memset(&dmScreenSettings, 0, sizeof(dmScreenSettings));
    dmScreenSettings.dmSize = sizeof(dmScreenSettings);
    sscanf(SConfig::GetInstance().strFullscreenResolution.c_str(), "%dx%d",
           &dmScreenSettings.dmPelsWidth, &dmScreenSettings.dmPelsHeight);
    dmScreenSettings.dmBitsPerPel = 32;
    dmScreenSettings.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;

    // Try To Set Selected Mode And Get Results.  NOTE: CDS_FULLSCREEN Gets Rid Of Start Bar.
    ChangeDisplaySettings(&dmScreenSettings, CDS_FULLSCREEN);
  }
  else
  {
    // Change to default resolution
    ChangeDisplaySettings(nullptr, CDS_FULLSCREEN);
  }
#elif defined(HAVE_XRANDR) && HAVE_XRANDR
  if (SConfig::GetInstance().strFullscreenResolution != "Auto")
    m_XRRConfig->ToggleDisplayMode(bFullscreen);
#endif
}

// Prepare the GUI to start the game.
void CFrame::StartGame(const std::string& filename)
{
  if (m_bGameLoading)
    return;
  m_bGameLoading = true;

  if (m_ToolBar)
    m_ToolBar->EnableTool(IDM_PLAY, false);
  GetMenuBar()->FindItem(IDM_PLAY)->Enable(false);

  if (SConfig::GetInstance().bRenderToMain)
  {
    // Game has been started, hide the game list
    m_GameListCtrl->Disable();
    m_GameListCtrl->Hide();

    m_RenderParent = m_Panel;
    m_RenderFrame = this;
    if (SConfig::GetInstance().bKeepWindowOnTop)
      m_RenderFrame->SetWindowStyle(m_RenderFrame->GetWindowStyle() | wxSTAY_ON_TOP);
    else
      m_RenderFrame->SetWindowStyle(m_RenderFrame->GetWindowStyle() & ~wxSTAY_ON_TOP);

    // No, I really don't want TAB_TRAVERSAL being set behind my back,
    // thanks.  (Note that calling DisableSelfFocus would prevent this flag
    // from being set for new children, but wouldn't reset the existing
    // flag.)
    m_RenderParent->SetWindowStyle(m_RenderParent->GetWindowStyle() & ~wxTAB_TRAVERSAL);
  }
  else
  {
    wxRect window_geometry(
        SConfig::GetInstance().iRenderWindowXPos, SConfig::GetInstance().iRenderWindowYPos,
        SConfig::GetInstance().iRenderWindowWidth, SConfig::GetInstance().iRenderWindowHeight);
    // Set window size in framebuffer pixels since the 3D rendering will be operating at
    // that level.
    wxSize default_size{wxSize(640, 480) * (1.0 / GetContentScaleFactor())};
    m_RenderFrame = new CRenderFrame(this, wxID_ANY, _("Dolphin"), wxDefaultPosition, default_size);

    // Convert ClientSize coordinates to frame sizes.
    wxSize decoration_fudge = m_RenderFrame->GetSize() - m_RenderFrame->GetClientSize();
    default_size += decoration_fudge;
    if (!window_geometry.IsEmpty())
      window_geometry.SetSize(window_geometry.GetSize() + decoration_fudge);

    WxUtils::SetWindowSizeAndFitToScreen(m_RenderFrame, window_geometry.GetPosition(),
                                         window_geometry.GetSize(), default_size);

    if (SConfig::GetInstance().bKeepWindowOnTop)
      m_RenderFrame->SetWindowStyle(m_RenderFrame->GetWindowStyle() | wxSTAY_ON_TOP);
    else
      m_RenderFrame->SetWindowStyle(m_RenderFrame->GetWindowStyle() & ~wxSTAY_ON_TOP);

    m_RenderFrame->SetBackgroundColour(*wxBLACK);
    m_RenderFrame->Bind(wxEVT_CLOSE_WINDOW, &CFrame::OnRenderParentClose, this);
    m_RenderFrame->Bind(wxEVT_ACTIVATE, &CFrame::OnActive, this);
    m_RenderFrame->Bind(wxEVT_MOVE, &CFrame::OnRenderParentMove, this);
#ifdef _WIN32
    // The renderer should use a top-level window for exclusive fullscreen support.
    m_RenderParent = m_RenderFrame;
#else
    // To capture key events on Linux and Mac OS X the frame needs at least one child.
    m_RenderParent = new wxPanel(m_RenderFrame, IDM_MPANEL, wxDefaultPosition, wxDefaultSize, 0);
#endif

    m_RenderFrame->Show();
  }

#if defined(__APPLE__)
  m_RenderFrame->EnableFullScreenView(true);
#endif

  wxBusyCursor hourglass;

  DoFullscreen(SConfig::GetInstance().bFullscreen);

  if (!BootManager::BootCore(filename))
  {
    DoFullscreen(false);
    // Destroy the renderer frame when not rendering to main
    if (!SConfig::GetInstance().bRenderToMain)
      m_RenderFrame->Destroy();
    m_RenderFrame = nullptr;
    m_RenderParent = nullptr;
    m_bGameLoading = false;
    UpdateGUI();
  }
  else
  {
#if defined(HAVE_X11) && HAVE_X11
    if (SConfig::GetInstance().bDisableScreenSaver)
      X11Utils::InhibitScreensaver(X11Utils::XDisplayFromHandle(GetHandle()),
                                   X11Utils::XWindowFromHandle(GetHandle()), true);
#endif

#ifdef _WIN32
    // Prevents Windows from sleeping, turning off the display, or idling
    EXECUTION_STATE shouldScreenSave =
        SConfig::GetInstance().bDisableScreenSaver ? ES_DISPLAY_REQUIRED : 0;
    SetThreadExecutionState(ES_CONTINUOUS | shouldScreenSave | ES_SYSTEM_REQUIRED);
#endif

    m_RenderParent->SetFocus();

    wxTheApp->Bind(wxEVT_KEY_DOWN, &CFrame::OnKeyDown, this);
    wxTheApp->Bind(wxEVT_RIGHT_DOWN, &CFrame::OnMouse, this);
    wxTheApp->Bind(wxEVT_RIGHT_UP, &CFrame::OnMouse, this);
    wxTheApp->Bind(wxEVT_MIDDLE_DOWN, &CFrame::OnMouse, this);
    wxTheApp->Bind(wxEVT_MIDDLE_UP, &CFrame::OnMouse, this);
    wxTheApp->Bind(wxEVT_MOTION, &CFrame::OnMouse, this);
    wxTheApp->Bind(wxEVT_SET_FOCUS, &CFrame::OnFocusChange, this);
    wxTheApp->Bind(wxEVT_KILL_FOCUS, &CFrame::OnFocusChange, this);
    m_RenderParent->Bind(wxEVT_SIZE, &CFrame::OnRenderParentResize, this);
  }
}

void CFrame::OnBootDrive(wxCommandEvent& event)
{
  BootGame(drives[event.GetId() - IDM_DRIVE1]);
}

// Refresh the file list and browse for a favorites directory
void CFrame::OnRefresh(wxCommandEvent& WXUNUSED(event))
{
  UpdateGameList();
}

// Create screenshot
void CFrame::OnScreenshot(wxCommandEvent& WXUNUSED(event))
{
  Core::SaveScreenShot();
}

// Pause the emulation
void CFrame::DoPause()
{
  if (Core::GetState() == Core::CORE_RUN)
  {
    Core::SetState(Core::CORE_PAUSE);
    if (SConfig::GetInstance().bHideCursor)
      m_RenderParent->SetCursor(wxNullCursor);
    Core::UpdateTitle();
  }
  else
  {
    Core::SetState(Core::CORE_RUN);
    if (SConfig::GetInstance().bHideCursor && RendererHasFocus())
      m_RenderParent->SetCursor(wxCURSOR_BLANK);
  }
  UpdateGUI();
}

// Stop the emulation
void CFrame::DoStop()
{
  if (!Core::IsRunningAndStarted())
    return;
  if (m_confirmStop)
    return;

  // don't let this function run again until it finishes, or is aborted.
  m_confirmStop = true;

  m_bGameLoading = false;
  if (Core::GetState() != Core::CORE_UNINITIALIZED || m_RenderParent != nullptr)
  {
#if defined __WXGTK__
    wxMutexGuiLeave();
    std::lock_guard<std::recursive_mutex> lk(keystate_lock);
    wxMutexGuiEnter();
#endif
    // Ask for confirmation in case the user accidentally clicked Stop / Escape
    if (SConfig::GetInstance().bConfirmStop)
    {
      // Exit fullscreen to ensure it does not cover the stop dialog.
      DoFullscreen(false);

      // Pause the state during confirmation and restore it afterwards
      Core::EState state = Core::GetState();

      // Do not pause if netplay is running as CPU thread might be blocked
      // waiting on inputs
      bool should_pause = !NetPlayDialog::GetNetPlayClient();

      // If exclusive fullscreen is not enabled then we can pause the emulation
      // before we've exited fullscreen. If not then we need to exit fullscreen first.
      should_pause =
          should_pause && (!RendererIsFullscreen() || !g_Config.ExclusiveFullscreenEnabled() ||
                           SConfig::GetInstance().bRenderToMain);

      if (should_pause)
      {
        Core::SetState(Core::CORE_PAUSE);
      }

      wxMessageDialog m_StopDlg(
          this, !m_tried_graceful_shutdown ? _("Do you want to stop the current emulation?") :
                                             _("A shutdown is already in progress. Unsaved data "
                                               "may be lost if you stop the current emulation "
                                               "before it completes. Force stop?"),
          _("Please confirm..."), wxYES_NO | wxSTAY_ON_TOP | wxICON_EXCLAMATION, wxDefaultPosition);

      HotkeyManagerEmu::Enable(false);
      int Ret = m_StopDlg.ShowModal();
      HotkeyManagerEmu::Enable(true);
      if (Ret != wxID_YES)
      {
        if (should_pause)
          Core::SetState(state);

        m_confirmStop = false;
        return;
      }
    }

    const auto& stm = WII_IPC_HLE_Interface::GetDeviceByName("/dev/stm/eventhook");
    if (!m_tried_graceful_shutdown && stm &&
        std::static_pointer_cast<CWII_IPC_HLE_Device_stm_eventhook>(stm)->HasHookInstalled())
    {
      Core::DisplayMessage("Shutting down", 30000);
      // Unpause because gracefully shutting down needs the game to actually request a shutdown
      if (Core::GetState() == Core::CORE_PAUSE)
        DoPause();
      ProcessorInterface::PowerButton_Tap();
      m_confirmStop = false;
      m_tried_graceful_shutdown = true;
      return;
    }

    if (UseDebugger && g_pCodeWindow)
    {
      if (g_pCodeWindow->HasPanel<CWatchWindow>())
        g_pCodeWindow->GetPanel<CWatchWindow>()->SaveAll();
      PowerPC::watches.Clear();
      if (g_pCodeWindow->HasPanel<CBreakPointWindow>())
        g_pCodeWindow->GetPanel<CBreakPointWindow>()->SaveAll();
      PowerPC::breakpoints.Clear();
      PowerPC::memchecks.Clear();
      if (g_pCodeWindow->HasPanel<CBreakPointWindow>())
        g_pCodeWindow->GetPanel<CBreakPointWindow>()->NotifyUpdate();
      g_symbolDB.Clear();
      Host_NotifyMapLoaded();
    }

    // TODO: Show the author/description dialog here
    if (Movie::IsRecordingInput())
      DoRecordingSave();
    if (Movie::IsMovieActive())
      Movie::EndPlayInput(false);

    if (NetPlayDialog::GetNetPlayClient())
      NetPlayDialog::GetNetPlayClient()->Stop();

    Core::Stop();
    UpdateGUI();
  }
}

void CFrame::OnStopped()
{
  m_confirmStop = false;
  m_tried_graceful_shutdown = false;

#if defined(HAVE_X11) && HAVE_X11
  if (SConfig::GetInstance().bDisableScreenSaver)
    X11Utils::InhibitScreensaver(X11Utils::XDisplayFromHandle(GetHandle()),
                                 X11Utils::XWindowFromHandle(GetHandle()), false);
#endif

#ifdef _WIN32
  // Allow windows to resume normal idling behavior
  SetThreadExecutionState(ES_CONTINUOUS);
#endif

  m_RenderFrame->SetTitle(StrToWxStr(scm_rev_str));

  // Destroy the renderer frame when not rendering to main
  m_RenderParent->Unbind(wxEVT_SIZE, &CFrame::OnRenderParentResize, this);

  // Mouse
  wxTheApp->Unbind(wxEVT_RIGHT_DOWN, &CFrame::OnMouse, this);
  wxTheApp->Unbind(wxEVT_RIGHT_UP, &CFrame::OnMouse, this);
  wxTheApp->Unbind(wxEVT_MIDDLE_DOWN, &CFrame::OnMouse, this);
  wxTheApp->Unbind(wxEVT_MIDDLE_UP, &CFrame::OnMouse, this);
  wxTheApp->Unbind(wxEVT_MOTION, &CFrame::OnMouse, this);
  if (SConfig::GetInstance().bHideCursor)
    m_RenderParent->SetCursor(wxNullCursor);
  DoFullscreen(false);
  if (!SConfig::GetInstance().bRenderToMain)
  {
    m_RenderFrame->Destroy();
  }
  else
  {
#if defined(__APPLE__)
    // Disable the full screen button when not in a game.
    m_RenderFrame->EnableFullScreenView(false);
#endif

    // Make sure the window is not longer set to stay on top
    m_RenderFrame->SetWindowStyle(m_RenderFrame->GetWindowStyle() & ~wxSTAY_ON_TOP);
  }
  m_RenderParent = nullptr;

  // Clean framerate indications from the status bar.
  GetStatusBar()->SetStatusText(" ", 0);

  // Clear wiimote connection status from the status bar.
  GetStatusBar()->SetStatusText(" ", 1);

  // If batch mode was specified on the command-line or we were already closing, exit now.
  if (m_bBatchMode || m_bClosing)
    Close(true);

  // If using auto size with render to main, reset the application size.
  if (SConfig::GetInstance().bRenderToMain && SConfig::GetInstance().bRenderWindowAutoSize)
    SetSize(SConfig::GetInstance().iWidth, SConfig::GetInstance().iHeight);

  m_GameListCtrl->Enable();
  m_GameListCtrl->Show();
  m_GameListCtrl->SetFocus();
  UpdateGUI();
}

void CFrame::DoRecordingSave()
{
  bool paused = (Core::GetState() == Core::CORE_PAUSE);

  if (!paused)
    DoPause();

  wxString path =
      wxFileSelector(_("Select The Recording File"), wxEmptyString, wxEmptyString, wxEmptyString,
                     _("Dolphin TAS Movies (*.dtm)") +
                         wxString::Format("|*.dtm|%s", wxGetTranslation(wxALL_FILES)),
                     wxFD_SAVE | wxFD_PREVIEW | wxFD_OVERWRITE_PROMPT, this);

  if (path.IsEmpty())
    return;

  Movie::SaveRecording(WxStrToStr(path));

  if (!paused)
    DoPause();
}

void CFrame::OnStop(wxCommandEvent& WXUNUSED(event))
{
  DoStop();
}

void CFrame::OnReset(wxCommandEvent& WXUNUSED(event))
{
  if (Movie::IsRecordingInput())
    Movie::SetReset(true);
  ProcessorInterface::ResetButton_Tap();
}

void CFrame::OnConfigMain(wxCommandEvent& WXUNUSED(event))
{
  OpenGeneralConfiguration();
}

void CFrame::OnConfigGFX(wxCommandEvent& WXUNUSED(event))
{
  HotkeyManagerEmu::Enable(false);
  if (g_video_backend)
    g_video_backend->ShowConfig(this);
  HotkeyManagerEmu::Enable(true);
}

void CFrame::OnConfigAudio(wxCommandEvent& WXUNUSED(event))
{
  OpenGeneralConfiguration(CConfigMain::ID_AUDIOPAGE);
}

void CFrame::OnConfigControllers(wxCommandEvent& WXUNUSED(event))
{
  ControllerConfigDiag config_dlg(this);
  HotkeyManagerEmu::Enable(false);
  config_dlg.ShowModal();
  HotkeyManagerEmu::Enable(true);
}

void CFrame::OnConfigHotkey(wxCommandEvent& WXUNUSED(event))
{
  InputConfig* const hotkey_plugin = HotkeyManagerEmu::GetConfig();

  // check if game is running
  bool game_running = false;
  if (Core::GetState() == Core::CORE_RUN)
  {
    Core::SetState(Core::CORE_PAUSE);
    game_running = true;
  }

  HotkeyManagerEmu::Enable(false);

  InputConfigDialog m_ConfigFrame(this, *hotkey_plugin, _("Dolphin Hotkeys"));
  m_ConfigFrame.ShowModal();

  // Update references in case controllers were refreshed
  Wiimote::LoadConfig();
  Keyboard::LoadConfig();
  Pad::LoadConfig();
  HotkeyManagerEmu::LoadConfig();

  HotkeyManagerEmu::Enable(true);

  // if game isn't running
  if (game_running)
  {
    Core::SetState(Core::CORE_RUN);
  }

  // Update the GUI in case menu accelerators were changed
  UpdateGUI();
}

void CFrame::OnHelp(wxCommandEvent& event)
{
  switch (event.GetId())
  {
  case wxID_ABOUT:
  {
    AboutDolphin frame(this);
    HotkeyManagerEmu::Enable(false);
    frame.ShowModal();
    HotkeyManagerEmu::Enable(true);
  }
  break;
  case IDM_HELP_WEBSITE:
    WxUtils::Launch("https://dolphin-emu.org/");
    break;
  case IDM_HELP_ONLINE_DOCS:
    WxUtils::Launch("https://dolphin-emu.org/docs/guides/");
    break;
  case IDM_HELP_GITHUB:
    WxUtils::Launch("https://github.com/dolphin-emu/dolphin");
    break;
  }
}

void CFrame::ClearStatusBar()
{
  if (this->GetStatusBar()->IsEnabled())
  {
    this->GetStatusBar()->SetStatusText("", 0);
  }
}

void CFrame::StatusBarMessage(const char* Text, ...)
{
  const int MAX_BYTES = 1024 * 10;
  char Str[MAX_BYTES];
  va_list ArgPtr;
  va_start(ArgPtr, Text);
  vsnprintf(Str, MAX_BYTES, Text, ArgPtr);
  va_end(ArgPtr);

  if (this->GetStatusBar()->IsEnabled())
  {
    this->GetStatusBar()->SetStatusText(StrToWxStr(Str), 0);
  }
}

// Miscellaneous menus
// ---------------------
// NetPlay stuff
void CFrame::OnNetPlay(wxCommandEvent& WXUNUSED(event))
{
  if (!g_NetPlaySetupDiag)
  {
    if (NetPlayDialog::GetInstance() != nullptr)
      NetPlayDialog::GetInstance()->Raise();
    else
      g_NetPlaySetupDiag = new NetPlaySetupFrame(this, m_GameListCtrl);
  }
  else
  {
    g_NetPlaySetupDiag->Raise();
  }
}

void CFrame::OnMemcard(wxCommandEvent& WXUNUSED(event))
{
  CMemcardManager MemcardManager(this);
  HotkeyManagerEmu::Enable(false);
  MemcardManager.ShowModal();
  HotkeyManagerEmu::Enable(true);
}

void CFrame::OnExportAllSaves(wxCommandEvent& WXUNUSED(event))
{
  CWiiSaveCrypted::ExportAllSaves();
}

void CFrame::OnImportSave(wxCommandEvent& WXUNUSED(event))
{
  wxString path =
      wxFileSelector(_("Select the save file"), wxEmptyString, wxEmptyString, wxEmptyString,
                     _("Wii save files (*.bin)") + "|*.bin|" + wxGetTranslation(wxALL_FILES),
                     wxFD_OPEN | wxFD_PREVIEW | wxFD_FILE_MUST_EXIST, this);

  if (!path.IsEmpty())
    CWiiSaveCrypted::ImportWiiSave(WxStrToStr(path));
}

void CFrame::OnShowCheatsWindow(wxCommandEvent& WXUNUSED(event))
{
  if (!g_CheatsWindow)
    g_CheatsWindow = new wxCheatsWindow(this);
  else
    g_CheatsWindow->Raise();
}

void CFrame::OnLoadWiiMenu(wxCommandEvent& WXUNUSED(event))
{
  BootGame(Common::GetTitleContentPath(TITLEID_SYSMENU, Common::FROM_CONFIGURED_ROOT));
}

void CFrame::OnInstallWAD(wxCommandEvent& event)
{
  std::string fileName;

  switch (event.GetId())
  {
  case IDM_LIST_INSTALL_WAD:
  {
    const GameListItem* iso = m_GameListCtrl->GetSelectedISO();
    if (!iso)
      return;
    fileName = iso->GetFileName();
    break;
  }
  case IDM_MENU_INSTALL_WAD:
  {
    wxString path = wxFileSelector(
        _("Select a Wii WAD file to install"), wxEmptyString, wxEmptyString, wxEmptyString,
        _("Wii WAD files (*.wad)") + "|*.wad|" + wxGetTranslation(wxALL_FILES),
        wxFD_OPEN | wxFD_PREVIEW | wxFD_FILE_MUST_EXIST, this);
    fileName = WxStrToStr(path);
    break;
  }
  default:
    return;
  }

  wxProgressDialog dialog(_("Installing WAD..."), _("Working..."), 1000, this,
                          wxPD_APP_MODAL | wxPD_ELAPSED_TIME | wxPD_ESTIMATED_TIME |
                              wxPD_REMAINING_TIME | wxPD_SMOOTH);

  u64 titleID = DiscIO::CNANDContentManager::Access().Install_WiiWAD(fileName);
  if (titleID == TITLEID_SYSMENU)
  {
    UpdateWiiMenuChoice();
  }
}

void CFrame::UpdateWiiMenuChoice(wxMenuItem* WiiMenuItem)
{
  if (!WiiMenuItem)
  {
    WiiMenuItem = GetMenuBar()->FindItem(IDM_LOAD_WII_MENU);
  }

  const DiscIO::CNANDContentLoader& SysMenu_Loader =
      DiscIO::CNANDContentManager::Access().GetNANDLoader(TITLEID_SYSMENU,
                                                          Common::FROM_CONFIGURED_ROOT);
  if (SysMenu_Loader.IsValid())
  {
    int sysmenuVersion = SysMenu_Loader.GetTitleVersion();
    char sysmenuRegion = SysMenu_Loader.GetCountryChar();
    WiiMenuItem->Enable();
    WiiMenuItem->SetItemLabel(
        wxString::Format(_("Load Wii System Menu %d%c"), sysmenuVersion, sysmenuRegion));
  }
  else
  {
    WiiMenuItem->Enable(false);
    WiiMenuItem->SetItemLabel(_("Load Wii System Menu"));
  }
}

void CFrame::OnFifoPlayer(wxCommandEvent& WXUNUSED(event))
{
  if (m_FifoPlayerDlg)
  {
    m_FifoPlayerDlg->Show();
    m_FifoPlayerDlg->SetFocus();
  }
  else
  {
    m_FifoPlayerDlg = new FifoPlayerDlg(this);
  }
}

void CFrame::ConnectWiimote(int wm_idx, bool connect)
{
  if (Core::IsRunning() && SConfig::GetInstance().bWii &&
      !SConfig::GetInstance().m_bt_passthrough_enabled)
  {
    bool was_unpaused = Core::PauseAndLock(true);
    GetUsbPointer()->AccessWiiMote(wm_idx | 0x100)->Activate(connect);
    const char* message = connect ? "Wiimote %i connected" : "Wiimote %i disconnected";
    Core::DisplayMessage(StringFromFormat(message, wm_idx + 1), 3000);
    Host_UpdateMainFrame();
    Core::PauseAndLock(false, was_unpaused);
  }
}

void CFrame::OnConnectWiimote(wxCommandEvent& event)
{
  if (SConfig::GetInstance().m_bt_passthrough_enabled)
    return;
  bool was_unpaused = Core::PauseAndLock(true);
  ConnectWiimote(event.GetId() - IDM_CONNECT_WIIMOTE1,
                 !GetUsbPointer()
                      ->AccessWiiMote((event.GetId() - IDM_CONNECT_WIIMOTE1) | 0x100)
                      ->IsConnected());
  Core::PauseAndLock(false, was_unpaused);
}

// Toggle fullscreen. In Windows the fullscreen mode is accomplished by expanding the m_Panel to
// cover
// the entire screen (when we render to the main window).
void CFrame::OnToggleFullscreen(wxCommandEvent& WXUNUSED(event))
{
  DoFullscreen(!RendererIsFullscreen());
}

void CFrame::OnToggleDualCore(wxCommandEvent& WXUNUSED(event))
{
  SConfig::GetInstance().bCPUThread = !SConfig::GetInstance().bCPUThread;
  SConfig::GetInstance().SaveSettings();
}

void CFrame::OnLoadStateFromFile(wxCommandEvent& WXUNUSED(event))
{
  wxString path =
      wxFileSelector(_("Select the state to load"), wxEmptyString, wxEmptyString, wxEmptyString,
                     _("All Save States (sav, s##)") +
                         wxString::Format("|*.sav;*.s??|%s", wxGetTranslation(wxALL_FILES)),
                     wxFD_OPEN | wxFD_PREVIEW | wxFD_FILE_MUST_EXIST, this);

  if (!path.IsEmpty())
    State::LoadAs(WxStrToStr(path));
}

void CFrame::OnSaveStateToFile(wxCommandEvent& WXUNUSED(event))
{
  wxString path =
      wxFileSelector(_("Select the state to save"), wxEmptyString, wxEmptyString, wxEmptyString,
                     _("All Save States (sav, s##)") +
                         wxString::Format("|*.sav;*.s??|%s", wxGetTranslation(wxALL_FILES)),
                     wxFD_SAVE, this);

  if (!path.IsEmpty())
    State::SaveAs(WxStrToStr(path));
}

void CFrame::OnLoadLastState(wxCommandEvent& event)
{
  if (Core::IsRunningAndStarted())
  {
    int id = event.GetId();
    int slot = id - IDM_LOAD_LAST_1 + 1;
    State::LoadLastSaved(slot);
  }
}

void CFrame::OnSaveFirstState(wxCommandEvent& WXUNUSED(event))
{
  if (Core::IsRunningAndStarted())
    State::SaveFirstSaved();
}

void CFrame::OnUndoLoadState(wxCommandEvent& WXUNUSED(event))
{
  if (Core::IsRunningAndStarted())
    State::UndoLoadState();
}

void CFrame::OnUndoSaveState(wxCommandEvent& WXUNUSED(event))
{
  if (Core::IsRunningAndStarted())
    State::UndoSaveState();
}

void CFrame::OnLoadState(wxCommandEvent& event)
{
  if (Core::IsRunningAndStarted())
  {
    int id = event.GetId();
    int slot = id - IDM_LOAD_SLOT_1 + 1;
    State::Load(slot);
  }
}

void CFrame::OnSaveState(wxCommandEvent& event)
{
  if (Core::IsRunningAndStarted())
  {
    int id = event.GetId();
    int slot = id - IDM_SAVE_SLOT_1 + 1;
    State::Save(slot);
  }
}

void CFrame::OnSelectSlot(wxCommandEvent& event)
{
  m_saveSlot = event.GetId() - IDM_SELECT_SLOT_1 + 1;
  Core::DisplayMessage(StringFromFormat("Selected slot %d - %s", m_saveSlot,
                                        State::GetInfoStringOfSlot(m_saveSlot, false).c_str()),
                       2500);
}

void CFrame::OnLoadCurrentSlot(wxCommandEvent& event)
{
  if (Core::IsRunningAndStarted())
  {
    State::Load(m_saveSlot);
  }
}

void CFrame::OnSaveCurrentSlot(wxCommandEvent& event)
{
  if (Core::IsRunningAndStarted())
  {
    State::Save(m_saveSlot);
  }
}

// GUI
// ---------------------

// Update the enabled/disabled status
void CFrame::UpdateGUI()
{
  // Save status
  bool Initialized = Core::IsRunning();
  bool Running = Core::GetState() == Core::CORE_RUN;
  bool Paused = Core::GetState() == Core::CORE_PAUSE;
  bool Stopping = Core::GetState() == Core::CORE_STOPPING;

  // Make sure that we have a toolbar
  if (m_ToolBar)
  {
    // Enable/disable the Config and Stop buttons
    m_ToolBar->EnableTool(wxID_OPEN, !Initialized);
    // Don't allow refresh when we don't show the list
    m_ToolBar->EnableTool(wxID_REFRESH, !Initialized);
    m_ToolBar->EnableTool(IDM_STOP, Running || Paused);
    m_ToolBar->EnableTool(IDM_TOGGLE_FULLSCREEN, Running || Paused);
    m_ToolBar->EnableTool(IDM_SCREENSHOT, Running || Paused);
  }

  // File
  GetMenuBar()->FindItem(wxID_OPEN)->Enable(!Initialized);
  GetMenuBar()->FindItem(IDM_DRIVES)->Enable(!Initialized);
  GetMenuBar()->FindItem(wxID_REFRESH)->Enable(!Initialized);

  // Emulation
  GetMenuBar()->FindItem(IDM_STOP)->Enable(Running || Paused);
  GetMenuBar()->FindItem(IDM_RESET)->Enable(Running || Paused);
  GetMenuBar()->FindItem(IDM_RECORD)->Enable(!Movie::IsRecordingInput());
  GetMenuBar()->FindItem(IDM_PLAY_RECORD)->Enable(!Initialized);
  GetMenuBar()->FindItem(IDM_RECORD_EXPORT)->Enable(Movie::IsMovieActive());
  GetMenuBar()->FindItem(IDM_FRAMESTEP)->Enable(Running || Paused);
  GetMenuBar()->FindItem(IDM_SCREENSHOT)->Enable(Running || Paused);
  GetMenuBar()->FindItem(IDM_TOGGLE_FULLSCREEN)->Enable(Running || Paused);

  // Update Key Shortcuts
  for (unsigned int i = 0; i < NUM_HOTKEYS; i++)
  {
    if (GetCmdForHotkey(i) == -1)
      continue;
    if (GetMenuBar()->FindItem(GetCmdForHotkey(i)))
      GetMenuBar()->FindItem(GetCmdForHotkey(i))->SetItemLabel(GetMenuLabel(i));
  }

  GetMenuBar()->FindItem(IDM_LOAD_STATE)->Enable(Initialized);
  GetMenuBar()->FindItem(IDM_SAVE_STATE)->Enable(Initialized);
  // Misc
  GetMenuBar()->FindItem(IDM_CHANGE_DISC)->Enable(Initialized);
  if (DiscIO::CNANDContentManager::Access()
          .GetNANDLoader(TITLEID_SYSMENU, Common::FROM_CONFIGURED_ROOT)
          .IsValid())
    GetMenuBar()->FindItem(IDM_LOAD_WII_MENU)->Enable(!Initialized);

  // Tools
  GetMenuBar()->FindItem(IDM_CHEATS)->Enable(SConfig::GetInstance().bEnableCheats);

  bool ShouldEnableWiimotes = Initialized && SConfig::GetInstance().bWii &&
                              !SConfig::GetInstance().m_bt_passthrough_enabled;
  GetMenuBar()->FindItem(IDM_CONNECT_WIIMOTE1)->Enable(ShouldEnableWiimotes);
  GetMenuBar()->FindItem(IDM_CONNECT_WIIMOTE2)->Enable(ShouldEnableWiimotes);
  GetMenuBar()->FindItem(IDM_CONNECT_WIIMOTE3)->Enable(ShouldEnableWiimotes);
  GetMenuBar()->FindItem(IDM_CONNECT_WIIMOTE4)->Enable(ShouldEnableWiimotes);
  GetMenuBar()->FindItem(IDM_CONNECT_BALANCEBOARD)->Enable(ShouldEnableWiimotes);
  if (ShouldEnableWiimotes)
  {
    bool was_unpaused = Core::PauseAndLock(true);
    GetMenuBar()
        ->FindItem(IDM_CONNECT_WIIMOTE1)
        ->Check(GetUsbPointer()->AccessWiiMote(0x0100)->IsConnected());
    GetMenuBar()
        ->FindItem(IDM_CONNECT_WIIMOTE2)
        ->Check(GetUsbPointer()->AccessWiiMote(0x0101)->IsConnected());
    GetMenuBar()
        ->FindItem(IDM_CONNECT_WIIMOTE3)
        ->Check(GetUsbPointer()->AccessWiiMote(0x0102)->IsConnected());
    GetMenuBar()
        ->FindItem(IDM_CONNECT_WIIMOTE4)
        ->Check(GetUsbPointer()->AccessWiiMote(0x0103)->IsConnected());
    GetMenuBar()
        ->FindItem(IDM_CONNECT_BALANCEBOARD)
        ->Check(GetUsbPointer()->AccessWiiMote(0x0104)->IsConnected());
    Core::PauseAndLock(false, was_unpaused);
  }

  if (m_ToolBar)
  {
    // Get the tool that controls pausing/playing
    wxToolBarToolBase* PlayTool = m_ToolBar->FindById(IDM_PLAY);

    if (PlayTool)
    {
      int position = m_ToolBar->GetToolPos(IDM_PLAY);

      if (Running)
      {
        m_ToolBar->DeleteTool(IDM_PLAY);
        m_ToolBar->InsertTool(position, IDM_PLAY, _("Pause"), m_Bitmaps[Toolbar_Pause],
                              WxUtils::CreateDisabledButtonBitmap(m_Bitmaps[Toolbar_Pause]),
                              wxITEM_NORMAL, _("Pause"));
      }
      else
      {
        m_ToolBar->DeleteTool(IDM_PLAY);
        m_ToolBar->InsertTool(position, IDM_PLAY, _("Play"), m_Bitmaps[Toolbar_Play],
                              WxUtils::CreateDisabledButtonBitmap(m_Bitmaps[Toolbar_Play]),
                              wxITEM_NORMAL, _("Play"));
      }
      m_ToolBar->Realize();
    }
  }

  GetMenuBar()->FindItem(IDM_RECORD_READ_ONLY)->Enable(Running || Paused);

  if (!Initialized && !m_bGameLoading)
  {
    if (m_GameListCtrl->IsEnabled())
    {
      // Prepare to load Default ISO, enable play button
      if (!SConfig::GetInstance().m_strDefaultISO.empty())
      {
        if (m_ToolBar)
          m_ToolBar->EnableTool(IDM_PLAY, true);
        GetMenuBar()->FindItem(IDM_PLAY)->Enable();
        GetMenuBar()->FindItem(IDM_RECORD)->Enable();
        GetMenuBar()->FindItem(IDM_PLAY_RECORD)->Enable();
      }
      // Prepare to load last selected file, enable play button
      else if (!SConfig::GetInstance().m_LastFilename.empty() &&
               File::Exists(SConfig::GetInstance().m_LastFilename))
      {
        if (m_ToolBar)
          m_ToolBar->EnableTool(IDM_PLAY, true);
        GetMenuBar()->FindItem(IDM_PLAY)->Enable();
        GetMenuBar()->FindItem(IDM_RECORD)->Enable();
        GetMenuBar()->FindItem(IDM_PLAY_RECORD)->Enable();
      }
      else
      {
        // No game has been selected yet, disable play button
        if (m_ToolBar)
          m_ToolBar->EnableTool(IDM_PLAY, false);
        GetMenuBar()->FindItem(IDM_PLAY)->Enable(false);
        GetMenuBar()->FindItem(IDM_RECORD)->Enable(false);
        GetMenuBar()->FindItem(IDM_PLAY_RECORD)->Enable(false);
      }
    }

    // Game has not started, show game list
    if (!m_GameListCtrl->IsShown())
    {
      m_GameListCtrl->Enable();
      m_GameListCtrl->Show();
    }
    // Game has been selected but not started, enable play button
    if (m_GameListCtrl->GetSelectedISO() != nullptr && m_GameListCtrl->IsEnabled())
    {
      if (m_ToolBar)
        m_ToolBar->EnableTool(IDM_PLAY, true);
      GetMenuBar()->FindItem(IDM_PLAY)->Enable();
      GetMenuBar()->FindItem(IDM_RECORD)->Enable();
      GetMenuBar()->FindItem(IDM_PLAY_RECORD)->Enable();
    }
  }
  else if (Initialized)
  {
    // Game has been loaded, enable the pause button
    if (m_ToolBar)
      m_ToolBar->EnableTool(IDM_PLAY, !Stopping);
    GetMenuBar()->FindItem(IDM_PLAY)->Enable(!Stopping);

    // Reset game loading flag
    m_bGameLoading = false;
  }

  // Refresh toolbar
  if (m_ToolBar)
  {
    m_ToolBar->Refresh();
  }

  // Commit changes to manager
  m_Mgr->Update();

  // Update non-modal windows
  if (g_CheatsWindow)
  {
    if (SConfig::GetInstance().bEnableCheats)
      g_CheatsWindow->UpdateGUI();
    else
      g_CheatsWindow->Close();
  }
}

void CFrame::UpdateGameList()
{
  if (m_GameListCtrl)
    m_GameListCtrl->ReloadList();
}

void CFrame::GameListChanged(wxCommandEvent& event)
{
  switch (event.GetId())
  {
  case IDM_LIST_WII:
    SConfig::GetInstance().m_ListWii = event.IsChecked();
    break;
  case IDM_LIST_GC:
    SConfig::GetInstance().m_ListGC = event.IsChecked();
    break;
  case IDM_LIST_WAD:
    SConfig::GetInstance().m_ListWad = event.IsChecked();
    break;
  case IDM_LIST_ELFDOL:
    SConfig::GetInstance().m_ListElfDol = event.IsChecked();
    break;
  case IDM_LIST_JAP:
    SConfig::GetInstance().m_ListJap = event.IsChecked();
    break;
  case IDM_LIST_PAL:
    SConfig::GetInstance().m_ListPal = event.IsChecked();
    break;
  case IDM_LIST_USA:
    SConfig::GetInstance().m_ListUsa = event.IsChecked();
    break;
  case IDM_LIST_AUSTRALIA:
    SConfig::GetInstance().m_ListAustralia = event.IsChecked();
    break;
  case IDM_LIST_FRANCE:
    SConfig::GetInstance().m_ListFrance = event.IsChecked();
    break;
  case IDM_LIST_GERMANY:
    SConfig::GetInstance().m_ListGermany = event.IsChecked();
    break;
  case IDM_LIST_ITALY:
    SConfig::GetInstance().m_ListItaly = event.IsChecked();
    break;
  case IDM_LIST_KOREA:
    SConfig::GetInstance().m_ListKorea = event.IsChecked();
    break;
  case IDM_LIST_NETHERLANDS:
    SConfig::GetInstance().m_ListNetherlands = event.IsChecked();
    break;
  case IDM_LIST_RUSSIA:
    SConfig::GetInstance().m_ListRussia = event.IsChecked();
    break;
  case IDM_LIST_SPAIN:
    SConfig::GetInstance().m_ListSpain = event.IsChecked();
    break;
  case IDM_LIST_TAIWAN:
    SConfig::GetInstance().m_ListTaiwan = event.IsChecked();
    break;
  case IDM_LIST_WORLD:
    SConfig::GetInstance().m_ListWorld = event.IsChecked();
    break;
  case IDM_LIST_UNKNOWN:
    SConfig::GetInstance().m_ListUnknown = event.IsChecked();
    break;
  case IDM_LIST_DRIVES:
    SConfig::GetInstance().m_ListDrives = event.IsChecked();
    break;
  case IDM_PURGE_GAME_LIST_CACHE:
    std::vector<std::string> rFilenames =
        DoFileSearch({".cache"}, {File::GetUserPath(D_CACHE_IDX)});

    for (const std::string& rFilename : rFilenames)
    {
      File::Delete(rFilename);
    }
    break;
  }

  UpdateGameList();
}

// Enable and disable the toolbar
void CFrame::OnToggleToolbar(wxCommandEvent& event)
{
  SConfig::GetInstance().m_InterfaceToolbar = event.IsChecked();
  DoToggleToolbar(event.IsChecked());
}
void CFrame::DoToggleToolbar(bool _show)
{
  GetToolBar()->Show(_show);
  m_Mgr->Update();
}

// Enable and disable the status bar
void CFrame::OnToggleStatusbar(wxCommandEvent& event)
{
  SConfig::GetInstance().m_InterfaceStatusbar = event.IsChecked();

  GetStatusBar()->Show(event.IsChecked());

  SendSizeEvent();
}

void CFrame::OnChangeColumnsVisible(wxCommandEvent& event)
{
  switch (event.GetId())
  {
  case IDM_SHOW_SYSTEM:
    SConfig::GetInstance().m_showSystemColumn = !SConfig::GetInstance().m_showSystemColumn;
    break;
  case IDM_SHOW_BANNER:
    SConfig::GetInstance().m_showBannerColumn = !SConfig::GetInstance().m_showBannerColumn;
    break;
  case IDM_SHOW_MAKER:
    SConfig::GetInstance().m_showMakerColumn = !SConfig::GetInstance().m_showMakerColumn;
    break;
  case IDM_SHOW_FILENAME:
    SConfig::GetInstance().m_showFileNameColumn = !SConfig::GetInstance().m_showFileNameColumn;
    break;
  case IDM_SHOW_ID:
    SConfig::GetInstance().m_showIDColumn = !SConfig::GetInstance().m_showIDColumn;
    break;
  case IDM_SHOW_REGION:
    SConfig::GetInstance().m_showRegionColumn = !SConfig::GetInstance().m_showRegionColumn;
    break;
  case IDM_SHOW_SIZE:
    SConfig::GetInstance().m_showSizeColumn = !SConfig::GetInstance().m_showSizeColumn;
    break;
  case IDM_SHOW_STATE:
    SConfig::GetInstance().m_showStateColumn = !SConfig::GetInstance().m_showStateColumn;
    break;
  default:
    return;
  }
  UpdateGameList();
  SConfig::GetInstance().SaveSettings();
}
