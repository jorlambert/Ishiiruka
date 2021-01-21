// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included. 

#include <string>

#include "Common/CommonTypes.h"
#include "Common/FileUtil.h"
#include "Common/StringUtil.h"

#include "Core/ConfigManager.h"
#include "Core/Host.h"

#include "VideoBackends/D3D12/BoundingBox.h"
#include "VideoBackends/D3D12/D3DCommandListManager.h"
#include "VideoBackends/D3D12/D3DBase.h"
#include "VideoBackends/D3D12/D3DUtil.h"
#include "VideoBackends/D3D12/PerfQuery.h"
#include "VideoBackends/D3D12/Render.h"
#include "VideoBackends/D3D12/ShaderCache.h"
#include "VideoBackends/D3D12/ShaderConstantsManager.h"
#include "VideoBackends/D3D12/StaticShaderCache.h"
#include "VideoBackends/D3D12/TextureCache.h"
#include "VideoBackends/D3D12/VertexManager.h"
#include "VideoBackends/D3D12/VideoBackend.h"
#include "VideoBackends/D3D12/XFBEncoder.h"

#include "VideoCommon/BPStructs.h"
#include "VideoCommon/CommandProcessor.h"
#include "VideoCommon/Fifo.h"
#include "VideoCommon/GeometryShaderManager.h"
#include "VideoCommon/IndexGenerator.h"
#include "VideoCommon/OpcodeDecoding.h"
#include "VideoCommon/PixelEngine.h"
#include "VideoCommon/PixelShaderManager.h"
#include "VideoCommon/VertexLoaderManager.h"
#include "VideoCommon/VertexShaderManager.h"
#include "VideoCommon/VideoConfig.h"

namespace DX12
{

unsigned int VideoBackend::PeekMessages()
{
  MSG msg;
  while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
  {
    if (msg.message == WM_QUIT)
      return FALSE;
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  return TRUE;
}

std::string VideoBackend::GetName() const
{
  return "D3D12";
}

std::string VideoBackend::GetDisplayName() const
{
  return "Direct3D 12";
}

void VideoBackend::InitBackendInfo()
{
  HRESULT hr = D3D::LoadDXGI();
  if (FAILED(hr))
    return;

  hr = D3D::LoadD3D();

  if (FAILED(hr))
  {
    D3D::UnloadDXGI();
    return;
  }

  g_Config.backend_info.APIType = API_D3D11;
  g_Config.backend_info.MaxTextureSize = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
  g_Config.backend_info.bSupportedFormats[PC_TEX_FMT_BGRA32] = false;
  g_Config.backend_info.bSupportedFormats[PC_TEX_FMT_RGBA32] = true;
  g_Config.backend_info.bSupportedFormats[PC_TEX_FMT_I4_AS_I8] = false;
  g_Config.backend_info.bSupportedFormats[PC_TEX_FMT_IA4_AS_IA8] = false;
  g_Config.backend_info.bSupportedFormats[PC_TEX_FMT_I8] = false;
  g_Config.backend_info.bSupportedFormats[PC_TEX_FMT_IA8] = false;
  g_Config.backend_info.bSupportedFormats[PC_TEX_FMT_RGB565] = false;
  g_Config.backend_info.bSupportedFormats[PC_TEX_FMT_DXT1] = true;
  g_Config.backend_info.bSupportedFormats[PC_TEX_FMT_DXT3] = true;
  g_Config.backend_info.bSupportedFormats[PC_TEX_FMT_DXT5] = true;

  g_Config.backend_info.bSupportsScaling = false;
  g_Config.backend_info.bSupportsExclusiveFullscreen = false;
  g_Config.backend_info.bSupportsDualSourceBlend = true;
  g_Config.backend_info.bSupportsPixelLighting = true;
  g_Config.backend_info.bNeedBlendIndices = false;
  g_Config.backend_info.bSupportsOversizedViewports = false;
  g_Config.backend_info.bSupportsGeometryShaders = true;
  g_Config.backend_info.bSupports3DVision = true;
  g_Config.backend_info.bSupportsPostProcessing = true;
  g_Config.backend_info.bSupportsClipControl = true;
  g_Config.backend_info.bSupportsNormalMaps = true;
  g_Config.backend_info.bSupportsEarlyZ = true;
  g_Config.backend_info.bSupportsBBox = true;
  g_Config.backend_info.bSupportsBBox = true;
  g_Config.backend_info.bSupportsGSInstancing = true;
  g_Config.backend_info.bSupportsTessellation = true;
  g_Config.backend_info.bSupportsSSAA = true;
  g_Config.backend_info.bSupportsGPUTextureDecoding = false;
  g_Config.backend_info.bSupportsComputeTextureEncoding = false;
  g_Config.backend_info.bSupportsDepthClamp = true;
  g_Config.backend_info.bSupportsMultithreading = true;
  g_Config.backend_info.bSupportsValidationLayer = true;
  g_Config.backend_info.bSupportsReversedDepthRange = true;
  g_Config.backend_info.bSupportsInternalResolutionFrameDumps = true;
  g_Config.backend_info.bSupportsAsyncShaderCompilation = true;
  g_Config.backend_info.bSupportsFragmentStoresAndAtomics = true;
  g_Config.backend_info.bSupportsBitfield = false;
  g_Config.backend_info.bSupportsDynamicSamplerIndexing = false;
  g_Config.backend_info.bSupportsUberShaders = true;
  g_Config.backend_info.bSupportsHighPrecisionFrameBuffer = true;
  IDXGIFactory* factory;
  IDXGIAdapter* ad;
  hr = create_dxgi_factory(__uuidof(IDXGIFactory), (void**)&factory);
  if (FAILED(hr))
  {
    PanicAlert("Failed to create IDXGIFactory object");
    D3D::UnloadD3D();
    D3D::UnloadDXGI();
    return;
  }

  // adapters
  g_Config.backend_info.Adapters.clear();
  g_Config.backend_info.AAModes.clear();
  while (factory->EnumAdapters((UINT)g_Config.backend_info.Adapters.size(), &ad) != DXGI_ERROR_NOT_FOUND)
  {
    const size_t adapter_index = g_Config.backend_info.Adapters.size();

    DXGI_ADAPTER_DESC desc;
    ad->GetDesc(&desc);

    // TODO: These don't get updated on adapter change, yet
    if (adapter_index == g_Config.iAdapter)
    {
      ID3D12Device* temp_device;
      hr = d3d12_create_device(ad, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&temp_device));
      if (FAILED(hr))
        continue;

      std::string samples;
      std::vector<DXGI_SAMPLE_DESC> modes = D3D::EnumAAModes(temp_device);
      // First iteration will be 1. This equals no AA.
      for (unsigned int i = 0; i < modes.size(); ++i)
      {
        g_Config.backend_info.AAModes.push_back(modes[i].Count);
      }
    }
    g_Config.backend_info.Adapters.push_back(UTF16ToUTF8(desc.Description));
    ad->Release();
  }
  factory->Release();

  D3D::UnloadDXGI();
  D3D::UnloadD3D();
}

bool VideoBackend::Initialize(void *window_handle)
{
  if (window_handle == nullptr)
    return false;

  InitBackendInfo();
  InitializeShared();

  if (FAILED(D3D::Create((HWND)window_handle)))
    return false;

  return true;
}

void VideoBackend::Video_Prepare()
{
  // internal interfaces
  g_renderer = std::make_unique<Renderer>(m_window_handle);
  g_texture_cache = std::make_unique<TextureCache>();
  g_vertex_manager = std::make_unique<VertexManager>();
  g_perf_query = std::make_unique<PerfQuery>();
  g_xfb_encoder = std::make_unique<XFBEncoder>();
  g_renderer->Init();
  ShaderCache::Init();
  ShaderConstantsManager::Init();
  StaticShaderCache::Init();
  StateCache::Init(); // PSO cache is populated here, after constituent shaders are loaded.
  D3D::InitUtils();
  BBox::Init();
}

void VideoBackend::Shutdown()
{
  // Immediately stop app from submitting work to GPU, and wait for all submitted work to complete. D3D12TODO: Check this.
  D3D::WaitForOutstandingRenderingToComplete();

  // internal interfaces
  D3D::ShutdownUtils();
  ShaderCache::Shutdown();
  ShaderConstantsManager::Shutdown();
  StaticShaderCache::Shutdown();
  BBox::Shutdown();

  g_xfb_encoder.reset();
  g_perf_query.reset();
  g_vertex_manager.reset();
  g_texture_cache.reset();
  g_renderer.reset();

  D3D::WaitForOutstandingRenderingToComplete(true);

  D3D::Close();
  ShutdownShared();
}

void VideoBackend::Video_Cleanup()
{
  CleanupShared();
}

void VideoBackend::PrepareWindow(void* window_handle)
{

}

}
