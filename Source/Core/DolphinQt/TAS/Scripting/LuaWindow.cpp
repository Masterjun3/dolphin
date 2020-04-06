#include "Common/CommonTypes.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QString>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>

#include "Core/Core.h"
#include "Core/HW/ProcessorInterface.h"
#include "Core/Movie.h"
#include "Core/PowerPC/MMU.cpp"
#include "Core/PowerPC/PowerPC.h"
#include "DolphinQt/TAS/Scripting/LuaWindow.h"
#include "InputCommon/GCPadStatus.h"
#include "Settings.h"
#include "VideoCommon/RenderBase.h"
#include "imgui.h"

#define str(string) QString::fromUtf8(string)

static QPlainTextEdit* output_box;
static std::mutex m_lua_mutex;
static std::queue<std::pair<std::string, bool>> m_lua_ring_buffer;
static std::map<u32, int> mem_execs;
static MathUtil::Rectangle<int> draw_rect;
static ImDrawList* draw_list;
static std::map<std::string, std::pair<std::function<u64()>, std::function<void(u64)>>> reg_funcs;
static GCPadStatus* cur_gcpad;

LuaWindow::LuaWindow(QWidget* parent) : QDialog(parent), m_timer(new QTimer(this))
{
  setWindowTitle(str("Lua Window"));

  L = nullptr;
  running = false;

  main_layout = new QVBoxLayout();
  button_layout = new QHBoxLayout();
  output_box = new QPlainTextEdit();
  output_box->setReadOnly(true);
  output_box->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(output_box, &QPlainTextEdit::customContextMenuRequested, this,
          &LuaWindow::showContextMenu);
  open_file = new QFileDialog();
  browse_button = new QPushButton(str("Browse..."));
  run_button = new QPushButton(str("Run"));
  // run_button->setEnabled(false);
  file_path = new QLineEdit();
  connect(browse_button, &QPushButton::clicked, this, &LuaWindow::browse);
  connect(run_button, &QPushButton::clicked, this, &LuaWindow::run);
  button_layout->addWidget(browse_button);
  button_layout->addWidget(run_button);
  main_layout->addWidget(file_path);
  main_layout->addLayout(button_layout);
  main_layout->addWidget(output_box);

  populatetable();

  connect(m_timer, &QTimer::timeout, this, &LuaWindow::updateText);
  m_timer->start(100);

  setLayout(main_layout);
  browse_button->setFocus();
}
void LuaWindow::showContextMenu(const QPoint& pos)
{
  QMenu* menu = output_box->createStandardContextMenu();
  menu->addSeparator();
  QAction* act = new QAction(str("Clear"), this);
  connect(act, &QAction::triggered, this, &LuaWindow::clearOutput);
  menu->addAction(act);
  menu->exec(output_box->mapToGlobal(pos));
  delete menu;
}

void LuaWindow::updateText()
{
  std::vector<std::pair<std::string, bool>> elements_to_push;
  {
    std::lock_guard lock(m_lua_mutex);
    if (m_lua_ring_buffer.empty())
      return;

    elements_to_push.reserve(m_lua_ring_buffer.size());

    for (size_t i = 0; !m_lua_ring_buffer.empty(); i++)
    {
      elements_to_push.push_back(m_lua_ring_buffer.front());
      m_lua_ring_buffer.pop();
    }
  }

  for (auto& line : elements_to_push)
  {
    if (line.second)
    {
      if (line.first == "clear")
      {
        output_box->clear();
      }
      else if (line.first == "stop")
      {
        stop();
      }
    }
    else
    {
      output_box->moveCursor(QTextCursor::End);
      output_box->insertPlainText(QString::fromStdString(line.first));
      output_box->moveCursor(QTextCursor::End);
    }
  }
}

void LuaWindow::writeString(QString s, bool newline = false)
{
  // output_box->moveCursor(QTextCursor::End);
  std::lock_guard lock(m_lua_mutex);
  m_lua_ring_buffer.emplace((newline ? s + str("\n") : s).toStdString(), false);
  // output_box->insertPlainText(newline ? s + str("\n") : s);
  // output_box->moveCursor(QTextCursor::End);
}

void LuaWindow::clearOutput()
{
  std::lock_guard lock(m_lua_mutex);
  m_lua_ring_buffer.emplace("clear", true);
}

void LuaWindow::scheduleStop()
{
  std::lock_guard lock(m_lua_mutex);
  m_lua_ring_buffer.emplace("stop", true);
}

bool LuaWindow::call(int sendarg = 0, int recarg = 0)
{
  int r = lua_pcall(L, sendarg, recarg, 0);

  if (r == LUA_OK)
  {
    return true;
  }

  if (r == LUA_ERRRUN)
  {
    writeString(str("Error running Lua callback: ") + str(lua_tostring(L, -1)), true);
  }
  else if (r == LUA_ERRMEM)
  {
    writeString(str("Error running Lua callback: Out of memory"), true);
  }
  else if (r == LUA_ERRERR)
  {
    writeString(str("Error running Lua callback: Double Fault???"), true);
  }
  else
  {
    writeString(str("Error running Lua callback: Unknown Error"), true);
  }
  lua_pop(L, 1);
  return false;
}

int LuaWindow::new_print(lua_State* L)
{
  int n = lua_gettop(L);
  int i;
  lua_getglobal(L, "tostring");
  for (i = 1; i <= n; i++)
  {
    const char* s;
    size_t l;
    lua_pushvalue(L, -1); /* function to be called */
    lua_pushvalue(L, i);  /* value to print */
    lua_call(L, 1, 1);
    s = lua_tolstring(L, -1, &l); /* get result */
    if (s == NULL)
      return luaL_error(L, "'tostring' must return a string to 'print'");
    if (i > 1)
      LuaWindow::writeString(str("\t"));
    // lua_writestring("\t", 1);
    LuaWindow::writeString(QString::fromUtf8(s, (int)l));
    // lua_writestring(s, l);
    lua_pop(L, 1); /* pop result */
  }
  LuaWindow::writeString(str("\n"));
  // lua_writeline();
  return 0;
}

// ################################################################
// ################################################################

#define LUAERROR(c, s, ...)                                                                        \
  if (c)                                                                                           \
  {                                                                                                \
    return luaL_error(L, s, __VA_ARGS__);                                                          \
  }

#define LUA_MUST_ARGS(num) LUAERROR(n != num, "%s: must have %I argument(s)", __func__, num)
#define LUA_ATLEAST_ARGS(num)                                                                      \
  LUAERROR(n < num, "%s: must have at least %I argument(s)", __func__, num)

#define LUA_BE_INTEGER(ind)                                                                        \
  LUAERROR(!lua_isinteger(L, ind), "%s: argument #%I must be an integer", __func__, ind)
#define LUA_BE_NUMBER(ind)                                                                         \
  LUAERROR(!lua_isnumber(L, ind), "%s: argument #%I must be a number", __func__, ind)
#define LUA_BE_STRING(ind)                                                                         \
  LUAERROR(!lua_isstring(L, ind), "%s: argument #%I must be a string", __func__, ind)
#define LUA_BE_FUNCTION(ind)                                                                       \
  LUAERROR(!lua_isfunction(L, ind), "%s: argument #%I must be a function", __func__, ind)

#define LUA_COLOR(col) ((col) ^ 0xFF000000)

// ################################################################
// ################################################################

int LuaWindow::memory_readu8(lua_State* L)
{
  int n = lua_gettop(L);
  LUA_MUST_ARGS(1)
  LUA_BE_INTEGER(1)
  u32 addr = lua_tointeger(L, 1);
  u8 value = PowerPC::HostRead_U8(addr);
  lua_pushinteger(L, value);
  return 1;
}
int LuaWindow::memory_reads8(lua_State* L)
{
  int n = lua_gettop(L);
  LUA_MUST_ARGS(1)
  LUA_BE_INTEGER(1)
  u32 addr = lua_tointeger(L, 1);
  s8 value = PowerPC::HostRead_U8(addr);
  lua_pushinteger(L, value);
  return 1;
}
int LuaWindow::memory_readu16(lua_State* L)
{
  int n = lua_gettop(L);
  LUA_MUST_ARGS(1)
  LUA_BE_INTEGER(1)
  u32 addr = lua_tointeger(L, 1);
  u16 value = PowerPC::HostRead_U16(addr);
  lua_pushinteger(L, value);
  return 1;
}
int LuaWindow::memory_reads16(lua_State* L)
{
  int n = lua_gettop(L);
  LUA_MUST_ARGS(1)
  LUA_BE_INTEGER(1)
  u32 addr = lua_tointeger(L, 1);
  s16 value = PowerPC::HostRead_U16(addr);
  lua_pushinteger(L, value);
  return 1;
}
int LuaWindow::memory_readu32(lua_State* L)
{
  int n = lua_gettop(L);
  LUA_MUST_ARGS(1)
  LUA_BE_INTEGER(1)
  u32 addr = lua_tointeger(L, 1);
  u32 value = PowerPC::HostRead_U32(addr);
  lua_pushinteger(L, value);
  return 1;
}
int LuaWindow::memory_reads32(lua_State* L)
{
  int n = lua_gettop(L);
  LUA_MUST_ARGS(1)
  LUA_BE_INTEGER(1)
  u32 addr = lua_tointeger(L, 1);
  s32 value = PowerPC::HostRead_U32(addr);
  lua_pushinteger(L, value);
  return 1;
}
int LuaWindow::memory_readf32(lua_State* L)
{
  int n = lua_gettop(L);
  LUA_MUST_ARGS(1)
  LUA_BE_INTEGER(1)
  u32 addr = lua_tointeger(L, 1);
  float value = PowerPC::HostRead_F32(addr);
  lua_pushnumber(L, value);
  return 1;
}
int LuaWindow::memory_readf64(lua_State* L)
{
  int n = lua_gettop(L);
  LUA_MUST_ARGS(1)
  LUA_BE_INTEGER(1)
  u32 addr = lua_tointeger(L, 1);
  double value = PowerPC::HostRead_F64(addr);
  lua_pushnumber(L, value);
  return 1;
}
int LuaWindow::memory_writeu8(lua_State* L)
{
  int n = lua_gettop(L);
  LUA_MUST_ARGS(2)
  LUA_BE_INTEGER(1)
  LUA_BE_INTEGER(2)
  u32 addr = lua_tointeger(L, 1);
  u8 value = lua_tointeger(L, 2);
  PowerPC::HostWrite_U8(value, addr);
  return 0;
}
int LuaWindow::memory_writeu16(lua_State* L)
{
  int n = lua_gettop(L);
  LUA_MUST_ARGS(2)
  LUA_BE_INTEGER(1)
  LUA_BE_INTEGER(2)
  u32 addr = lua_tointeger(L, 1);
  u16 value = lua_tointeger(L, 2);
  PowerPC::HostWrite_U16(value, addr);
  return 0;
}
int LuaWindow::memory_writeu32(lua_State* L)
{
  int n = lua_gettop(L);
  LUA_MUST_ARGS(2)
  LUA_BE_INTEGER(1)
  LUA_BE_INTEGER(2)
  u32 addr = lua_tointeger(L, 1);
  u32 value = lua_tointeger(L, 2);
  PowerPC::HostWrite_U32(value, addr);
  return 0;
}
int LuaWindow::memory_writef32(lua_State* L)
{
  int n = lua_gettop(L);
  LUA_MUST_ARGS(2)
  LUA_BE_INTEGER(1)
  LUA_BE_NUMBER(2)
  u32 addr = lua_tointeger(L, 1);
  float value = lua_tonumber(L, 2);
  PowerPC::HostWrite_F32(value, addr);
  return 0;
}
int LuaWindow::memory_writef64(lua_State* L)
{
  int n = lua_gettop(L);
  LUA_MUST_ARGS(2)
  LUA_BE_INTEGER(1)
  LUA_BE_NUMBER(2)
  u32 addr = lua_tointeger(L, 1);
  double value = lua_tonumber(L, 2);
  PowerPC::HostWrite_F64(value, addr);
  return 0;
}

int LuaWindow::memory_registerexec(lua_State* L)
{
  int n = lua_gettop(L);
  LUA_MUST_ARGS(2)
  LUA_BE_INTEGER(1)
  LUA_BE_FUNCTION(2)

  u64 addr = lua_tointeger(L, 1);
  if (mem_execs.find(addr) != mem_execs.end())
  {
    return luaL_error(L, "%s: address %I already registered", __func__, addr);
  }
  int r = luaL_ref(L, LUA_REGISTRYINDEX);
  mem_execs.emplace(addr, r);
  PowerPC::breakpoints.AddLua(addr);
  return 0;
}

int LuaWindow::memory_getregistertable(lua_State* L)
{
  lua_createtable(L, 0, int(reg_funcs.size()));
  for (const auto& cur_reg : reg_funcs)
  {
    lua_pushinteger(L, cur_reg.second.first());
    lua_setfield(L, -2, cur_reg.first.c_str());
  }
  return 1;
}

int LuaWindow::memory_setregister(lua_State* L)
{
  int n = lua_gettop(L);
  LUA_MUST_ARGS(2)
  LUA_BE_STRING(1)
  LUA_BE_INTEGER(2)
  std::string reg_str = lua_tostring(L, 1);
  u64 value = lua_tointeger(L, 2);
  if (reg_funcs.find(reg_str) == reg_funcs.end())
  {
    return luaL_error(L, "%s: no register: \"%s\"", __func__, reg_str.c_str());
  }
  reg_funcs[reg_str].second(value);
  return 0;
}

// ################################################################

int LuaWindow::gui_text(lua_State* L)
{
  int n = lua_gettop(L);
  LUA_ATLEAST_ARGS(3)
  LUA_BE_NUMBER(1)
  LUA_BE_NUMBER(2)
  LUA_BE_STRING(3)
  lua_Number pos_x = lua_tonumber(L, 1);
  lua_Number pos_y = lua_tonumber(L, 2);
  const char* text = lua_tostring(L, 3);
  lua_Integer color = 0xFFFFFF;
  if (n > 3)
  {
    LUA_BE_INTEGER(4)
    color = lua_tointeger(L, 4);
  }
  draw_list->AddText(ImVec2(draw_rect.left + pos_x, draw_rect.top + pos_y), LUA_COLOR(color), text);
  return 0;
}

int LuaWindow::gui_getdrawsize(lua_State* L)
{
  lua_pushnumber(L, draw_rect.GetWidth());
  lua_pushnumber(L, draw_rect.GetHeight());
  return 2;
}

// ################################################################

#define LUA_INPUT_BOOL(b, m, s)                                                                    \
  lua_pushboolean(L, ((b)&m) != 0);                                                                \
  lua_setfield(L, -2, s);
#define LUA_INPUT_ANALOG(b, s)                                                                     \
  lua_pushinteger(L, b);                                                                           \
  lua_setfield(L, -2, s);

int LuaWindow::input_getgc(lua_State* L)
{
  if (!cur_gcpad)
  {
    return luaL_error(L, "%s: function only works inside on_gcinput callback", __func__);
  }
  lua_createtable(L, 0, 18);
  LUA_INPUT_BOOL(cur_gcpad->button, PAD_BUTTON_LEFT, "Left")
  LUA_INPUT_BOOL(cur_gcpad->button, PAD_BUTTON_RIGHT, "Right")
  LUA_INPUT_BOOL(cur_gcpad->button, PAD_BUTTON_DOWN, "Down")
  LUA_INPUT_BOOL(cur_gcpad->button, PAD_BUTTON_UP, "Up")
  LUA_INPUT_BOOL(cur_gcpad->button, PAD_TRIGGER_Z, "Z")
  LUA_INPUT_BOOL(cur_gcpad->button, PAD_TRIGGER_R, "R")
  LUA_INPUT_BOOL(cur_gcpad->button, PAD_TRIGGER_L, "L")
  LUA_INPUT_BOOL(cur_gcpad->button, PAD_BUTTON_A, "A")
  LUA_INPUT_BOOL(cur_gcpad->button, PAD_BUTTON_B, "B")
  LUA_INPUT_BOOL(cur_gcpad->button, PAD_BUTTON_X, "X")
  LUA_INPUT_BOOL(cur_gcpad->button, PAD_BUTTON_Y, "Y")
  LUA_INPUT_BOOL(cur_gcpad->button, PAD_BUTTON_START, "Start")
  LUA_INPUT_ANALOG(cur_gcpad->triggerLeft, "TriggerL")
  LUA_INPUT_ANALOG(cur_gcpad->triggerRight, "TriggerR")
  LUA_INPUT_ANALOG(cur_gcpad->stickX, "StickX")
  LUA_INPUT_ANALOG(cur_gcpad->stickY, "StickY")
  LUA_INPUT_ANALOG(cur_gcpad->substickX, "SubstickX")
  LUA_INPUT_ANALOG(cur_gcpad->substickY, "SubstickY")
  return 1;
}

int LuaWindow::input_getwii(lua_State* L)
{
  return 0;
}

// ################################################################

static const luaL_Reg base_funcs[] = {{"print", LuaWindow::new_print}, {NULL, NULL}};
static const luaL_Reg memory_funcs[] = {{"readu8", LuaWindow::memory_readu8},
                                        {"reads8", LuaWindow::memory_reads8},
                                        {"readu16", LuaWindow::memory_readu16},
                                        {"reads16", LuaWindow::memory_reads16},
                                        {"readu32", LuaWindow::memory_readu32},
                                        {"reads32", LuaWindow::memory_reads32},
                                        {"readf32", LuaWindow::memory_readf32},
                                        {"readf64", LuaWindow::memory_readf64},
                                        {"writeu8", LuaWindow::memory_writeu8},
                                        {"writeu16", LuaWindow::memory_writeu16},
                                        {"writeu32", LuaWindow::memory_writeu32},
                                        {"writef32", LuaWindow::memory_writef32},
                                        {"writef64", LuaWindow::memory_writef64},
                                        {"registerexec", LuaWindow::memory_registerexec},
                                        {"getregistertable", LuaWindow::memory_getregistertable},
                                        {"setregister", LuaWindow::memory_setregister},
                                        {NULL, NULL}};
static const luaL_Reg gui_funcs[] = {
    {"text", LuaWindow::gui_text}, {"getdrawsize", LuaWindow::gui_getdrawsize}, {NULL, NULL}};
static const luaL_Reg input_funcs[] = {
    {"getgc", LuaWindow::input_getgc}, {"getwii", LuaWindow::input_getwii}, {NULL, NULL}};

void LuaWindow::editLibs()
{
  lua_pushglobaltable(L);
  luaL_setfuncs(L, base_funcs, 0);
  luaL_newlib(L, memory_funcs);
  lua_setfield(L, -2, "memory");
  luaL_newlib(L, gui_funcs);
  lua_setfield(L, -2, "gui");
  luaL_newlib(L, input_funcs);
  lua_setfield(L, -2, "input");
  lua_setfield(L, -1, "_G");
}

// ################################################################
// ################################################################

void LuaWindow::browse()  // this can be simpler now
{
  QString fileName = QFileDialog::getOpenFileName(this, str("Open Lua File"), str("."),
                                                  str("Lua files (*.lua);;All files (*)"));
  if (fileName.isEmpty())
  {
    return;
  }
  else
  {
    file_path->setText(fileName);
  }
}

void LuaWindow::clearExternal()
{
  // removing all breakpoints
  for (auto iter : mem_execs)
  {
    PowerPC::breakpoints.Remove(iter.first);
  }
  mem_execs.clear();

  // remove all callbacks
  Core::SetLuaCbBoot(nullptr);
  Core::SetLuaCbStop(nullptr);
  Core::SetLuaCbPaint(nullptr);
  PowerPC::breakpoints.SetLuaCbBreakpoint(nullptr);
  Movie::SetGCInputManipLua(nullptr);
  Movie::SetWiiInputManipLua(nullptr);
}

void LuaWindow::stop()
{
  if (!running)
  {
    return;
  }

  clearExternal();

  lua_close(L);
  L = nullptr;
  running = false;

  file_path->setEnabled(true);
  browse_button->setEnabled(true);
  run_button->setText(str("Run"));
}

void LuaWindow::run()
{
  if (running)
  {
    stop();
  }
  else
  {
    running = true;
    L = luaL_newstate();

    Core::SetLuaCbBoot([this]() { cb_boot(); });
    Core::SetLuaCbStop([this]() { cb_stop(); });
    Core::SetLuaCbPaint([this]() { cb_paint(); });
    PowerPC::breakpoints.SetLuaCbBreakpoint([this](u32 address) { cb_breakpoint(address); });
    Movie::SetGCInputManipLua([this](GCPadStatus* pad, int cID) { cb_gcinput(pad, cID); });
    Movie::SetWiiInputManipLua(
        [this](WiimoteCommon::DataReportBuilder& rpt, int cID, int ext,
               const WiimoteEmu::EncryptionKey& key) { cb_wiiinput(rpt, cID, ext, key); });

    file_path->setEnabled(false);
    browse_button->setEnabled(false);
    run_button->setText(str("Stop"));

    if (Core::IsRunningAndStarted())
    {
      startScript();
    }
    else
    {
      clearOutput();
      writeString(str("Waiting for emulation to be started..."));
    }
  }
}

void LuaWindow::startScript()
{
  // g_renderer->SetLuaCallback([this]() { cb_paint(); });

  clearOutput();
  QString fileName = file_path->text();
  Settings::Instance().SetDebugModeEnabled(true);
  // writeString(fileName, true);
  luaL_openlibs(L);
  editLibs();
  int iErr = luaL_loadfile(L, fileName.toUtf8());
  if (iErr == LUA_OK)
  {
    call();
  }
  else if (iErr == LUA_ERRSYNTAX)
  {
    writeString(str("Syntax error: ") + str(lua_tostring(L, -1)));
  }
  else if (iErr == LUA_ERRMEM)
  {
    writeString(str("Out of memory: ") + str(lua_tostring(L, -1)));
  }
  else
  {
    writeString(str("Unknown error: ") + str(lua_tostring(L, -1)));
  }
}

// ################################################################
// ################################################################

void LuaWindow::cb_paint()
{
  int t = lua_getglobal(L, "on_paint");
  if (t != LUA_TFUNCTION)
  {
    lua_pop(L, 1);
  }
  else
  {
    draw_rect = g_renderer->GetTargetRectangle();
    draw_rect.left -= 4;
    draw_rect.right -= 4;
    draw_rect.top -= 1;
    draw_rect.bottom -= 1;
    auto lock = g_renderer->GetImGuiLock();
    ImGui::SetNextWindowPos(ImVec2(draw_rect.left, draw_rect.top));
    ImGui::SetNextWindowSize(ImVec2(draw_rect.GetWidth(), draw_rect.GetHeight()));
    ImGui::Begin("LuaPaint", 0,
                 ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs);
    draw_list = ImGui::GetWindowDrawList();
    call();
    ImGui::End();
  }
}

void LuaWindow::cb_breakpoint(u32 addr)
{
  auto iter = mem_execs.find(addr);
  if (iter != mem_execs.end())
  {
    int r = iter->second;
    lua_rawgeti(L, LUA_REGISTRYINDEX, r);
    call();
  }
  else
  {
    luaL_error(L, "callback: executing address %I but no callback exists?", addr);
    return;
  }
  // writeString(QString::number(addr), true);
}

void LuaWindow::cb_boot()
{
  if (running)
  {
    startScript();
  }
}

void LuaWindow::cb_stop()
{
  clearExternal();  // asap
  scheduleStop();
}

template <typename UX>
void LuaWindow::checksetbutton(lua_State* L, const char* btn, UX& btn_dst, UX mask, bool analog,
                               bool special)
{
  lua_getfield(L, -1, btn);
  if (!analog)
  {
    if (lua_isboolean(L, -1))
    {
      if (special)
      {
        btn_dst = lua_toboolean(L, -1) ? 0xFF : 0x00;
      }
      else
      {
        lua_toboolean(L, -1) ? (btn_dst |= mask) : (btn_dst &= ~mask);
      }
    }
  }
  else
  {
    if (lua_isinteger(L, -1))
    {
      btn_dst = lua_tointeger(L, -1);
    }
  }
  lua_pop(L, 1);
}

void LuaWindow::cb_gcinput(GCPadStatus* pad, int cID)
{
  if (pad->isConnected)
  {
    cur_gcpad = pad;
    int t = lua_getglobal(L, "on_gcinput");
    if (t != LUA_TFUNCTION)
    {
      lua_pop(L, 1);
    }
    else
    {
      lua_pushinteger(L, cID);
      if (call(1, 1))
      {
        if (lua_istable(L, -1))
        {
          checksetbutton<u16>(L, "Left", pad->button, PAD_BUTTON_LEFT, false, false);
          checksetbutton<u16>(L, "Right", pad->button, PAD_BUTTON_RIGHT, false, false);
          checksetbutton<u16>(L, "Down", pad->button, PAD_BUTTON_DOWN, false, false);
          checksetbutton<u16>(L, "Up", pad->button, PAD_BUTTON_UP, false, false);
          checksetbutton<u16>(L, "Z", pad->button, PAD_TRIGGER_Z, false, false);
          checksetbutton<u16>(L, "R", pad->button, PAD_TRIGGER_R, false, false);
          checksetbutton<u16>(L, "L", pad->button, PAD_TRIGGER_L, false, false);
          checksetbutton<u16>(L, "A", pad->button, PAD_BUTTON_A, false, false);
          checksetbutton<u8>(L, "A", pad->analogA, 0, false, true);
          checksetbutton<u16>(L, "B", pad->button, PAD_BUTTON_B, false, false);
          checksetbutton<u8>(L, "B", pad->analogB, 0, false, true);
          checksetbutton<u16>(L, "X", pad->button, PAD_BUTTON_X, false, false);
          checksetbutton<u16>(L, "Y", pad->button, PAD_BUTTON_Y, false, false);
          checksetbutton<u16>(L, "Start", pad->button, PAD_BUTTON_START, false, false);

          checksetbutton<u8>(L, "TriggerL", pad->triggerLeft, 0, true, false);
          checksetbutton<u8>(L, "TriggerR", pad->triggerRight, 0, true, false);
          checksetbutton<u8>(L, "StickX", pad->stickX, 0, true, false);
          checksetbutton<u8>(L, "StickY", pad->stickY, 0, true, false);
          checksetbutton<u8>(L, "SubstickX", pad->substickX, 0, true, false);
          checksetbutton<u8>(L, "SubstickY", pad->substickY, 0, true, false);
        }
        lua_pop(L, 1);
      }
    }
    cur_gcpad = nullptr;
  }
}

void LuaWindow::cb_wiiinput(WiimoteCommon::DataReportBuilder& rpt, int cID, int ext,
                            const WiimoteEmu::EncryptionKey& key)
{
}

// RegisterWidget::AddRegister
void LuaWindow::addregister(std::string register_name, std::function<u64()> get_reg,
                            std::function<void(u64)> set_reg)
{
  reg_funcs.emplace(register_name, std::make_pair(get_reg, set_reg));
}

// RegisterWidget::PopulateTable
void LuaWindow::populatetable()
{
  for (int i = 0; i < 32; i++)
  {
    // General purpose registers (int)
    addregister(
        "r" + std::to_string(i), [i] { return GPR(i); }, [i](u64 value) { GPR(i) = value; });

    // Floating point registers (double)
    addregister(
        "f" + std::to_string(i) + "ps0", [i] { return rPS(i).PS0AsU64(); },
        [i](u64 value) { rPS(i).SetPS0(value); });

    addregister(
        "f" + std::to_string(i) + "ps1", [i] { return rPS(i).PS1AsU64(); },
        [i](u64 value) { rPS(i).SetPS1(value); });
  }

  for (int i = 0; i < 8; i++)
  {
    // IBAT registers
    addregister(
        "IBAT" + std::to_string(i),
        [i] {
          return (static_cast<u64>(PowerPC::ppcState.spr[SPR_IBAT0U + i * 2]) << 32) +
                 PowerPC::ppcState.spr[SPR_IBAT0L + i * 2];
        },
        nullptr);
    // DBAT registers
    addregister(
        "DBAT" + std::to_string(i),
        [i] {
          return (static_cast<u64>(PowerPC::ppcState.spr[SPR_DBAT0U + i * 2]) << 32) +
                 PowerPC::ppcState.spr[SPR_DBAT0L + i * 2];
        },
        nullptr);
    // Graphics quantization registers
    addregister(
        "GQR" + std::to_string(i), [i] { return PowerPC::ppcState.spr[SPR_GQR0 + i]; }, nullptr);
  }

  // HID registers
  addregister(
      "HID0", [] { return PowerPC::ppcState.spr[SPR_HID0]; },
      [](u64 value) { PowerPC::ppcState.spr[SPR_HID0] = static_cast<u32>(value); });
  addregister(
      "HID1", [] { return PowerPC::ppcState.spr[SPR_HID1]; },
      [](u64 value) { PowerPC::ppcState.spr[SPR_HID1] = static_cast<u32>(value); });
  addregister(
      "HID2", [] { return PowerPC::ppcState.spr[SPR_HID2]; },
      [](u64 value) { PowerPC::ppcState.spr[SPR_HID2] = static_cast<u32>(value); });
  addregister(
      "HID4", [] { return PowerPC::ppcState.spr[SPR_HID4]; },
      [](u64 value) { PowerPC::ppcState.spr[SPR_HID4] = static_cast<u32>(value); });

  for (int i = 0; i < 16; i++)
  {
    // SR registers
    addregister(
        "SR" + std::to_string(i), [i] { return PowerPC::ppcState.sr[i]; },
        [i](u64 value) { PowerPC::ppcState.sr[i] = value; });
  }

  // Special registers
  // TB
  addregister("TB", PowerPC::ReadFullTimeBaseValue, nullptr);

  // PC
  addregister(
      "PC", [] { return PowerPC::ppcState.pc; }, [](u64 value) { PowerPC::ppcState.pc = value; });

  // LR
  addregister(
      "LR", [] { return PowerPC::ppcState.spr[SPR_LR]; },
      [](u64 value) { PowerPC::ppcState.spr[SPR_LR] = value; });

  // CTR
  addregister(
      "CTR", [] { return PowerPC::ppcState.spr[SPR_CTR]; },
      [](u64 value) { PowerPC::ppcState.spr[SPR_CTR] = value; });

  // CR
  addregister(
      "CR", [] { return PowerPC::ppcState.cr.Get(); },
      [](u64 value) { PowerPC::ppcState.cr.Set(value); });

  // XER
  addregister(
      "XER", [] { return PowerPC::GetXER().Hex; },
      [](u64 value) { PowerPC::SetXER(UReg_XER(value)); });

  // FPSCR
  addregister(
      "FPSCR", [] { return PowerPC::ppcState.fpscr.Hex; },
      [](u64 value) { PowerPC::ppcState.fpscr = static_cast<u32>(value); });

  // MSR
  addregister(
      "MSR", [] { return PowerPC::ppcState.msr.Hex; },
      [](u64 value) { PowerPC::ppcState.msr.Hex = value; });

  // SRR 0-1
  addregister(
      "SRR0", [] { return PowerPC::ppcState.spr[SPR_SRR0]; },
      [](u64 value) { PowerPC::ppcState.spr[SPR_SRR0] = value; });
  addregister(
      "SRR1", [] { return PowerPC::ppcState.spr[SPR_SRR1]; },
      [](u64 value) { PowerPC::ppcState.spr[SPR_SRR1] = value; });

  // Exceptions
  addregister(
      "Exceptions", [] { return PowerPC::ppcState.Exceptions; },
      [](u64 value) { PowerPC::ppcState.Exceptions = value; });

  // Int Mask
  addregister(
      "Int Mask", [] { return ProcessorInterface::GetMask(); }, nullptr);

  // Int Cause
  addregister(
      "Int Cause", [] { return ProcessorInterface::GetCause(); }, nullptr);

  // DSISR
  addregister(
      "DSISR", [] { return PowerPC::ppcState.spr[SPR_DSISR]; },
      [](u64 value) { PowerPC::ppcState.spr[SPR_DSISR] = value; });
  // DAR
  addregister(
      "DAR", [] { return PowerPC::ppcState.spr[SPR_DAR]; },
      [](u64 value) { PowerPC::ppcState.spr[SPR_DAR] = value; });

  // Hash Mask
  addregister(
      "Hash Mask",
      [] { return (PowerPC::ppcState.pagetable_hashmask << 6) | PowerPC::ppcState.pagetable_base; },
      nullptr);
}
