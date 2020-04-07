#pragma once

#include <QDialog>
#include "DolphinQT/TAS/Scripting/Lua/lua.hpp"

#include <mutex>
#include <queue>
#include "Common/CommonTypes.h"

class QFileDialog;
class QVBoxLayout;
class QHBoxLayout;
class QPushButton;
class QPlainTextEdit;
class QTimer;
class QLineEdit;

class LuaWindow : public QDialog
{
public:
  explicit LuaWindow(QWidget* parent);
  void showContextMenu(const QPoint& pt);
  static int new_print(lua_State* L);
  static int memory_registerexec(lua_State* L);

  static int memory_readu8(lua_State* L);
  static int memory_reads8(lua_State* L);
  static int memory_readu16(lua_State* L);
  static int memory_reads16(lua_State* L);
  static int memory_readu32(lua_State* L);
  static int memory_reads32(lua_State* L);
  static int memory_readf32(lua_State* L);
  static int memory_readf64(lua_State* L);

  static int memory_writeu8(lua_State* L);
  static int memory_writeu16(lua_State* L);
  static int memory_writeu32(lua_State* L);
  static int memory_writef32(lua_State* L);
  static int memory_writef64(lua_State* L);

  static int memory_getregistertable(lua_State* L);
  static int memory_setregister(lua_State* L);
  static int gui_text(lua_State* L);
  static int gui_texthalo(lua_State* L);
  static int gui_getdrawsize(lua_State* L);
  static int input_getgc(lua_State* L);
  static int input_getwii(lua_State* L);

private:
  QFileDialog* open_file;
  QVBoxLayout* main_layout;
  QHBoxLayout* button_layout;
  QPushButton* browse_button;
  QPushButton* run_button;
  QLineEdit* file_path;
  QTimer* m_timer;

  lua_State* L;

  bool running;

  void browse();
  void clearExternal();
  void stop();
  void run();

  void startScript();

  int imgui_lastframecount;

  void cb_paint();
  void cb_breakpoint(u32 addr);
  void cb_boot();
  void cb_stop();
  void cb_gcinput(GCPadStatus* pad, int cID);
  void cb_wiiinput(WiimoteCommon::DataReportBuilder& rpt, int cID, int ext,
                   const WiimoteEmu::EncryptionKey& key);

  void addregister(std::string register_name, std::function<u64()> get_reg,
                   std::function<void(u64)> set_reg);

  void populatetable();

  bool call(int sendarg, int recarg);

  void editLibs();

  void updateText();
  static void writeString(QString s, bool newline);
  void clearOutput();
  void scheduleStop();
  template <typename UX>
  void checksetbutton(lua_State* L, const char* btn, UX& btn_dst, UX mask, bool analog,
                      bool special);
};
