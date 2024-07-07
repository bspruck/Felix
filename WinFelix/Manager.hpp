#pragma once

#include "ScriptDebugger.hpp"
#include "Utility.hpp"
#include "UI.hpp"
#include "Debugger.hpp"
#include "CPUEditor.hpp"
#include "MemEditor.hpp"
#include "DisasmEditor.h"
#include "Monitor.hpp"
#include "sol/sol.hpp"

class WinAudioOut;
class ComLynxWire;
class Core;
class SymbolSource;
class InputFile;
class WinImgui;
class ScriptDebuggerEscapes;
class UserInput;
class KeyNames;
class ImageProperties;
class ImageROM;
struct ImGuiIO;
class IRenderer;
class ISystemDriver;

class Manager
{
public:
  Manager();
  ~Manager();

  void update();
  void machineReset();
  void updateRotation();
  void initialize( std::shared_ptr<ISystemDriver> systemDriver );
  IUserInput & userInput() const;
  void quit();


private:
  void processLua( std::filesystem::path const& path );
  std::optional<InputFile> computeInputFile();
  void stopThreads();
  void handleFileDrop( std::filesystem::path path );

  void updateDebugWindows();
  
  static std::shared_ptr<ImageROM const> getOptionalBootROM();
private:

  friend struct RamProxy;
  friend struct RomProxy;
  friend struct MikeyProxy;
  friend struct SuzyProxy;
  friend struct CPUProxy;
  friend struct SymbolProxy;
  friend class UI;
  friend class CPUEditor;
  friend class MemEditor;
  friend class WatchEditor;
  friend class DisasmEditor;
  friend class BreakpointEditor;
  friend class Monitor;

  bool mDoReset;

  Debugger mDebugger;
  Monitor mMonitor;

  struct DebugWindows
  {
    std::vector<std::pair<int, std::shared_ptr<ICustomScreenView>>> customScreenViews;
    CPUEditor cpuEditor;
    MemEditor memoryEditor;
    DisasmEditor disasmEditor;
  } mDebugWindows;

  UI mUI;
  sol::state mLua;
  std::atomic_bool mProcessThreads;
  std::atomic_bool mJoinThreads;
  std::atomic_int mThreadsWaiting;
  HMODULE mEncoderMod;
  std::thread mRenderThread;
  std::thread mAudioThread;
  std::shared_ptr<ISystemDriver> mSystemDriver;
  std::shared_ptr<IRenderer> mRenderer;
  std::shared_ptr<WinAudioOut> mAudioOut;
  std::shared_ptr<ComLynxWire> mComLynxWire;
  std::unique_ptr<SymbolSource> mSymbols;
  std::shared_ptr<Core> mInstance;
  std::shared_ptr<ScriptDebuggerEscapes> mScriptDebuggerEscapes;
  std::shared_ptr<ImageProperties> mImageProperties;
  std::filesystem::path mArg;
  std::filesystem::path mLogPath;
  std::mutex mMutex;
  int64_t mRenderingTime;
};
