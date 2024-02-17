
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include <Shlobj.h>
#include <atlbase.h>
#include <AudioClient.h>
#include <MMDeviceAPI.h>
#include <xinput.h>
#include <Dbt.h>
#pragma warning(push)
#pragma warning( disable: 4005 )
#ifndef NDEBUG
#define D3D_DEBUG_INFO
#endif
#include <d3d9.h>
#include <d3d11.h>
#pragma warning(pop)

#include <wrl/client.h>
template<typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;
#endif
