#include "pch.hpp"
#include "Manager.hpp"
#include "InputFile.hpp"
#include "WinImgui.hpp"
#include "WinAudioOut.hpp"
#include "ComLynxWire.hpp"
#include "Core.hpp"
#include "SymbolSource.hpp"
#include "imgui.h"
#include "Log.hpp"
#include "Ex.hpp"
#include "CPUState.hpp"
#include "IEncoder.hpp"
#include "ConfigProvider.hpp"
#include "SysConfig.hpp"
#include "ScriptDebuggerEscapes.hpp"
#include "UserInput.hpp"
#include "ImageROM.hpp"
#include "ImageProperties.hpp"
#include "LuaProxies.hpp"
#include "CPU.hpp"
#include "DebugRAM.hpp"
#include "BaseRenderer.hpp"



Manager::Manager() : mUI{ *this },
                     mLua{},
                     mDoReset{ false },
                     mDebugger{},
                     mProcessThreads{},
                     mJoinThreads{},
                     mRenderThread{},
                     mAudioThread{},
                     mRenderingTime{},
                     mScriptDebuggerEscapes{ std::make_shared<ScriptDebuggerEscapes>() },
                     mIntputSource{},
                     mImageProperties{},
                     mRenderer{}
{
  auto sysConfig = gConfigProvider.sysConfig();

  mDebugger( RunMode::RUN );
  mAudioOut = std::make_shared<WinAudioOut>( mDebugger.mRunMode );
  mComLynxWire = std::make_shared<ComLynxWire>();
  mIntputSource = std::make_shared<UserInput>( *sysConfig );

  mRenderThread = std::thread{ [this]
  {
    while ( !mJoinThreads.load() )
    {
      if ( mProcessThreads.load() )
      {
        auto renderingTime = mRenderer->render( mUI );
        std::scoped_lock<std::mutex> l{ mMutex };
        mRenderingTime = renderingTime;
      }
      else
      {
        std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
      }
    }
  } };

  mAudioThread = std::thread{ [this]
  {
    try
    {
      while ( !mJoinThreads.load() )
      {
        if ( mProcessThreads.load() )
        {
          int64_t renderingTime;
          {
            std::scoped_lock<std::mutex> l{ mMutex };
            renderingTime = mRenderingTime;
          }
          mAudioOut->fillBuffer( mInstance, renderingTime );
          updateDebugWindows();
        }
        else
        {
          std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
        }
      }
    }
  catch ( sol::error const& err )
  {
    L_ERROR << err.what();
    MessageBoxA( nullptr, err.what(), "Error", 0 );
    std::terminate();
  }
  catch ( std::exception const& ex )
  {
    L_ERROR << ex.what();
    MessageBoxA( nullptr, ex.what(), "Error", 0 );
    std::terminate();
  }
  } };

  mAudioOut->mute( sysConfig->audio.mute );
}

void Manager::update()
{
  mIntputSource->updateGamepad();

  if ( mDoReset )
    reset();
  mDoReset = false;
}

void Manager::doArg( std::wstring arg )
{
  mArg = std::filesystem::path{ std::move( arg ) };
  reset();
}

void Manager::initialize( std::shared_ptr<ISystemDriver> systemDriver )
{
  assert( !mSystemDriver );
  mSystemDriver = std::move( systemDriver );
  mRenderer = mSystemDriver->baseRenderer();
  mExtendedRenderer = mSystemDriver->extendedRenderer();

  mSystemDriver->registerDropFiles( std::bind( &Manager::handleFileDrop, this, std::placeholders::_1 ) );
}

int Manager::win32_WndProcHandler( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam )
{
  RECT r;
  switch ( msg )
  {
  case WM_CLOSE:
    if ( GetWindowRect( hWnd, &r ) )
    {
      auto sysConfig = gConfigProvider.sysConfig();
      sysConfig->mainWindow.x = r.left;
      sysConfig->mainWindow.y = r.top;
      sysConfig->mainWindow.width = r.right - r.left;
      sysConfig->mainWindow.height = r.bottom - r.top;
    }
    DestroyWindow( hWnd );
    return 0;
  case WM_DESTROY:
    PostQuitMessage( 0 );
    return 0;
  case WM_KEYDOWN:
  case WM_SYSKEYDOWN:
    if ( wParam < 256 )
    {
      mIntputSource->keyDown( (int)wParam );
    }
    return mSystemDriver->wndProcHandler( hWnd, msg, wParam, lParam );
  case WM_KEYUP:
  case WM_SYSKEYUP:
    if ( wParam < 256 )
    {
      mIntputSource->keyUp( (int)wParam );
    }
    return mSystemDriver->wndProcHandler( hWnd, msg, wParam, lParam );
  case WM_KILLFOCUS:
    mIntputSource->lostFocus();
    return mSystemDriver->wndProcHandler( hWnd, msg, wParam, lParam );
  case WM_DEVICECHANGE:
    if ( (UINT)wParam == DBT_DEVNODES_CHANGED )
      mIntputSource->recheckGamepad();
    return 0;
  default:
    assert( mSystemDriver );
    return mSystemDriver->wndProcHandler( hWnd, msg, wParam, lParam );
  }
}

Manager::~Manager()
{
  auto sysConfig = gConfigProvider.sysConfig();

  mIntputSource->serialize( *gConfigProvider.sysConfig() );
  stopThreads();

  sysConfig->audio.mute = mAudioOut->mute();
}

void Manager::quit()
{
  mSystemDriver->quit();
}

void Manager::updateDebugWindows()
{

  if ( !mInstance || !mExtendedRenderer )
    return;

  if ( !mDebugger.isDebugMode() )
  {
    mDebugWindows.mainScreenView.reset();
    return;
  }

  std::unique_lock<std::mutex> l{ mDebugger.mutex };

  if ( !mDebugWindows.mainScreenView )
  {
    mDebugWindows.mainScreenView = mExtendedRenderer->makeMainScreenView();
  }

  auto svs = mDebugger.screenViews();
  auto& csvs = mDebugWindows.customScreenViews;
  //removing elements in csvs that are not in svs 
  auto ret = std::ranges::remove_if( csvs, [&]( int id ) { return std::ranges::find( svs, id, &ScreenView::id ) == svs.end(); }, []( auto const& p ) { return p.first; } );
  csvs.erase( ret.begin(), ret.end() );
  //add missing elements to csvs that are in svs
  for ( auto const& sv : svs )
  {
    if ( std::ranges::find( csvs, sv.id, []( auto const& p ) { return p.first; } ) == csvs.end() )
    {
      csvs.emplace_back( sv.id, mExtendedRenderer->makeCustomScreenView() );
    }
  }

  auto& cpu = mInstance->debugCPU();

  if ( mDebugger.isCPUVisualized() )
  {
    auto & cpuVis = mDebugger.cpuVisualizer();
    cpu.printStatus( std::span<uint8_t, 3 * 14>( cpuVis.data.data(), cpuVis.data.size() ) );

    if ( !mDebugWindows.cpuBoard )
    {
      mDebugWindows.cpuBoard = mExtendedRenderer->makeBoard( cpuVis.columns, cpuVis.rows );
    }
  }
  else if ( mDebugWindows.cpuBoard )
  {
    mDebugWindows.cpuBoard.reset();
  }


  if ( mDebugger.isDisasmVisualized() )
  {
    auto & disVis = mDebugger.disasmVisualizer();
    cpu.disassemblyFromPC( mInstance->debugRAM(), (char*)disVis.data.data(), disVis.columns, disVis.rows );

    if ( !mDebugWindows.disasmBoard )
    {
      mDebugWindows.disasmBoard = mExtendedRenderer->makeBoard( disVis.columns, disVis.rows );
    }
  }
  else if ( !mDebugWindows.disasmBoard )
  {
    mDebugWindows.disasmBoard.reset();
  }

  if ( mDebugger.isHistoryVisualized() )
  {
    auto & hisVis = mDebugger.historyVisualizer();
    cpu.copyHistory( std::span<char>( (char*)hisVis.data.data(), hisVis.data.size() ) );

    if ( !mDebugWindows.historyBoard )
    {
      mDebugWindows.historyBoard = mExtendedRenderer->makeBoard( hisVis.columns, hisVis.rows );
    }
  }
  else if ( !mDebugWindows.historyBoard )
  {
    mDebugWindows.historyBoard.reset();
  }
}

BoardRendering Manager::renderCPUWindow()
{
  if ( mDebugger.isCPUVisualized() && mDebugWindows.cpuBoard )
  {
    auto win = mDebugger.cpuVisualizer();
    auto tex = mDebugWindows.cpuBoard->render( std::span<uint8_t const>{ win.data.data(), win.data.size() } );
    return { true, tex, 8.0f * win.columns , 16.0f * win.rows };
  }
  else
  {
    return { mDebugger.isCPUVisualized() };
  }
}

BoardRendering Manager::renderDisasmWindow()
{
  if ( mDebugger.isDisasmVisualized() && mDebugWindows.disasmBoard )
  {
    auto win = mDebugger.disasmVisualizer();
    auto tex = mDebugWindows.disasmBoard->render( std::span<uint8_t const>{ win.data.data(), win.data.size() } );
    return { true, tex, 8.0f * win.columns , 16.0f * win.rows };
  }
  else
  {
    return { mDebugger.isDisasmVisualized() };
  }
}

BoardRendering Manager::renderHistoryWindow()
{
  if ( mDebugger.isHistoryVisualized() && mDebugWindows.historyBoard )
  {
    auto win = mDebugger.historyVisualizer();
    auto tex = mDebugWindows.historyBoard->render( std::span<uint8_t const>{ win.data.data(), win.data.size() } );
    return { true, tex, 8.0f * win.columns , 16.0f * win.rows };
  }
  else
  {
    return { mDebugger.isHistoryVisualized() };
  }
}

void Manager::processLua( std::filesystem::path const& path )
{
  auto luaPath = path;
  auto cfgPath = path;

  luaPath.replace_extension( path.extension().string() + ".lua" );
  cfgPath.replace_extension( path.extension().string() + ".cfg" );

  if ( !std::filesystem::exists( luaPath ) && !std::filesystem::exists( cfgPath ) )
    return;

  mLua = sol::state{};
  mLua.open_libraries( sol::lib::base, sol::lib::io );

  if ( std::filesystem::exists( cfgPath ) )
  {
    mLua.safe_script_file( cfgPath.string(), sol::script_pass_on_error );
    //ignoring errors
  }

  if ( !std::filesystem::exists( luaPath ) )
    return;

  mLua.new_usertype<TrapProxy>( "TRAP", sol::meta_function::new_index, &TrapProxy::set );
  mLua.new_usertype<RamProxy>( "RAM", sol::meta_function::index, &RamProxy::get, sol::meta_function::new_index, &RamProxy::set );
  mLua.new_usertype<RomProxy>( "ROM", sol::meta_function::index, &RomProxy::get, sol::meta_function::new_index, &RomProxy::set );
  mLua.new_usertype<MikeyProxy>( "MIKEY", sol::meta_function::index, &MikeyProxy::get, sol::meta_function::new_index, &MikeyProxy::set );
  mLua.new_usertype<SuzyProxy>( "SUZY", sol::meta_function::index, &SuzyProxy::get, sol::meta_function::new_index, &SuzyProxy::set );
  mLua.new_usertype<CPUProxy>( "CPU", sol::meta_function::index, &CPUProxy::get, sol::meta_function::new_index, &CPUProxy::set );

  mLua["ram"] = std::make_unique<RamProxy>( *this );
  mLua["rom"] = std::make_unique<RomProxy>( *this );
  mLua["mikey"] = std::make_unique<MikeyProxy>( *this );
  mLua["suzy"] = std::make_unique<SuzyProxy>( *this );
  mLua["cpu"] = std::make_unique<CPUProxy>( *this );

  mLua["Encoder"] = [this]( sol::table const& tab )
  {
    if ( !mExtendedRenderer )
      throw Ex{} << "Encoder not available";

    std::filesystem::path path;
    int vbitrate{}, abitrate{}, vscale{};
    if ( sol::optional<std::string> opt = tab["path"] )
      path = *opt;
    else throw Ex{} << "path = \"path/to/file.mp4\" required";

    if ( sol::optional<int> opt = tab["video_bitrate"] )
      vbitrate = *opt;
    else throw Ex{} << "video_bitrate required";

    if ( sol::optional<int> opt = tab["audio_bitrate"] )
      abitrate = *opt;
    else throw Ex{} << "audio_bitrate required";

    if ( sol::optional<int> opt = tab["video_scale"] )
      vscale = *opt;
    else throw Ex{} << "video_scale required";

    if ( vscale % 2 == 1 )
      throw Ex{} << "video_scale must be even number";

    static PCREATE_ENCODER s_createEncoder = nullptr;
    static PDISPOSE_ENCODER s_disposeEncoder = nullptr;

    mEncoderMod = ::LoadLibrary( L"Encoder.dll" );
    if ( mEncoderMod == nullptr )
      throw Ex{} << "Encoder.dll not found";

    s_createEncoder = (PCREATE_ENCODER)GetProcAddress( mEncoderMod, "createEncoder" );
    s_disposeEncoder = (PDISPOSE_ENCODER)GetProcAddress( mEncoderMod, "disposeEncoder" );

    mEncoder = std::shared_ptr<IEncoder>( s_createEncoder( path.string().c_str(), vbitrate, abitrate, SCREEN_WIDTH * vscale, SCREEN_HEIGHT * vscale ), s_disposeEncoder );

    mExtendedRenderer->setEncoder( mEncoder );
    mAudioOut->setEncoder( mEncoder );
  };

  mLua["WavOut"] = [this]( sol::table const& tab )
  {
    std::filesystem::path path;
    if ( sol::optional<std::string> opt = tab["path"] )
      path = *opt;
    else throw Ex{} << "path = \"path/to/file.wav\" required";

    mAudioOut->setWavOut( std::move( path ) );
  };

  mLua["traceCurrent"] = [this]()
  {
    if ( mInstance )
    {
      mInstance->debugCPU().toggleTrace( true );
    }
  };

  mLua["traceOn"] = [this]()
  {
    if ( mInstance )
    {
      mInstance->debugCPU().enableTrace();
    }
  };
  mLua["traceOf"] = [this]()
  {
    if ( mInstance )
    {
      mInstance->debugCPU().disableTrace();
    }
  };

  auto trap = [this]()
  {
    if ( mInstance )
    {
      mInstance->debugCPU().breakFromLua();
    }
  };

  mLua["trap"] = trap;
  mLua["brk"] = trap;

  mLua.script_file( luaPath.string() );

  if ( sol::optional<std::string> opt = mLua["log"] )
  {
    mLogPath = *opt;
  }
  if ( sol::optional<std::string> opt = mLua["lab"] )
  {
    mSymbols = std::make_unique<SymbolSource>( *opt );
  }

}

std::optional<InputFile> Manager::computeInputFile()
{
  std::optional<InputFile> input;

  std::filesystem::path path = std::filesystem::absolute( mArg );

  InputFile file{ path, mImageProperties };
  if ( !file.valid() )
    return {};

  processLua( path );

  return file;
}

std::shared_ptr<ImageROM const> Manager::getOptionalBootROM()
{
  auto sysConfig = gConfigProvider.sysConfig();
  if ( sysConfig->bootROM.useExternal && !sysConfig->bootROM.path.empty() )
  {
    return ImageROM::create( sysConfig->bootROM.path );
  }

  return {};
}

void Manager::reset()
{
  std::unique_lock<std::mutex> l{ mDebugger.mutex };
  mProcessThreads.store( false );
  //TODO wait for threads to stop.
  mInstance.reset();

  if ( auto input = computeInputFile() )
  {
    mInstance = std::make_shared<Core>( *mImageProperties, mComLynxWire, mRenderer->getVideoSink(), mIntputSource,
      *input, getOptionalBootROM(), mScriptDebuggerEscapes );

    updateRotation();

    if ( !mLogPath.empty() )
      mInstance->setLog( mLogPath );
  }
  else
  {
    mImageProperties.reset();
  }

  if ( mInstance )
  {
    mInstance->debugCPU().breakOnBrk( mDebugger.isBreakOnBrk() );
    if ( mDebugger.isHistoryVisualized() )
    {
      mInstance->debugCPU().enableHistory( mDebugger.historyVisualizer().columns, mDebugger.historyVisualizer().rows );
    }
    else
    {
      mInstance->debugCPU().disableHistory();
    }
  }

  mProcessThreads.store( true );

  mDebugger( mDebugger.isDebugMode() ? RunMode::PAUSE : RunMode::RUN );
}

void Manager::updateRotation()
{
  mIntputSource->setRotation( mImageProperties->getRotation() );
  mRenderer->setRotation( mImageProperties->getRotation() );
}

void Manager::stopThreads()
{
  mJoinThreads.store( true );
  if ( mAudioThread.joinable() )
    mAudioThread.join();
  mAudioThread = {};
  if ( mRenderThread.joinable() )
    mRenderThread.join();
  mRenderThread = {};
}

void Manager::handleFileDrop( std::filesystem::path path )
{
  if ( !path.empty() )
    mArg = path;
  mDoReset = true;
}
