/*     Copyright 2015-2016 Egor Yusov
 *  
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF ANY PROPRIETARY RIGHTS.
 *
 *  In no event and under no legal theory, whether in tort (including negligence), 
 *  contract, or otherwise, unless required by applicable law (such as deliberate 
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental, 
 *  or consequential damages of any character arising as a result of this License or 
 *  out of the use or inability to use the software (including but not limited to damages 
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and 
 *  all other commercial damages or losses), even if such Contributor has been advised 
 *  of the possibility of such damages.
 */

#include "pch.h"
#include "RenderDeviceFactoryD3D12.h"
#include "RenderDeviceD3D12Impl.h"
#include "DeviceContextD3D12Impl.h"
#include "SwapChainD3D12Impl.h"
#include "D3D12TypeConversions.h"
#include "StringTools.h"
#include "EngineMemory.h"
#include <Windows.h>
#include <dxgi1_4.h>

using namespace Diligent;
using namespace Diligent;

extern "C"
{

void GetHardwareAdapter(IDXGIFactory2* pFactory, IDXGIAdapter1** ppAdapter)
{
	CComPtr<IDXGIAdapter1> adapter;
	*ppAdapter = nullptr;

	for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != pFactory->EnumAdapters1(adapterIndex, &adapter); ++adapterIndex)
	{
		DXGI_ADAPTER_DESC1 desc;
		adapter->GetDesc1(&desc);

		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
		{
			// Don't select the Basic Render Driver adapter.
			// If you want a software adapter, pass in "/warp" on the command line.
			continue;
		}

		// Check to see if the adapter supports Direct3D 12, but don't create the
		// actual device yet.
		if (SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
		{
            LOG_INFO_MESSAGE("D3D12-capabale hardware found: ", NarrowString(desc.Description), " (", desc.DedicatedVideoMemory>>20, " MB)");
			break;
		}
	}

	*ppAdapter = adapter.Detach();
}


void CreateDeviceAndImmediateContextD3D12( const EngineCreationAttribs& CreationAttribs, IRenderDevice **ppDevice, Diligent::IDeviceContext **ppContext )
{
    VERIFY( ppDevice && ppContext, "Null pointer is provided" );
    if( !ppDevice || !ppContext )
        return;

    SetRawAllocator(CreationAttribs.pRawMemAllocator);

    *ppDevice = nullptr;
    *ppContext = nullptr;

    try
    {
#if defined(_DEBUG)
	    // Enable the D3D12 debug layer.
	    {
		    CComPtr<ID3D12Debug> debugController;
		    if (SUCCEEDED(D3D12GetDebugInterface(__uuidof(debugController), reinterpret_cast<void**>(static_cast<ID3D12Debug**>(&debugController)) )))
		    {
			    debugController->EnableDebugLayer();
		    }
	    }
#endif

	    CComPtr<IDXGIFactory4> factory;
        HRESULT hr = CreateDXGIFactory1(__uuidof(factory), reinterpret_cast<void**>(static_cast<IDXGIFactory4**>(&factory)) );
        CHECK_D3D_RESULT_THROW(hr, "Failed to create DXGI factory")

	    CComPtr<IDXGIAdapter1> hardwareAdapter;
	    GetHardwareAdapter(factory, &hardwareAdapter);
    
        CComPtr<ID3D12Device> d3d12Device;
	    hr = D3D12CreateDevice(hardwareAdapter, D3D_FEATURE_LEVEL_11_0, __uuidof(d3d12Device), reinterpret_cast<void**>(static_cast<ID3D12Device**>(&d3d12Device)) );
        if( FAILED(hr))
        {
            LOG_WARNING_MESSAGE("Failed to create hardware device. Attempting to create WARP device")

		    CComPtr<IDXGIAdapter> warpAdapter;
		    hr = factory->EnumWarpAdapter( __uuidof(warpAdapter),  reinterpret_cast<void**>(static_cast<IDXGIAdapter**>(&warpAdapter)) );
            CHECK_D3D_RESULT_THROW(hr, "Failed to enum warp adapter")

		    hr = D3D12CreateDevice( warpAdapter, D3D_FEATURE_LEVEL_11_0, __uuidof(d3d12Device), reinterpret_cast<void**>(static_cast<ID3D12Device**>(&d3d12Device)) );
            CHECK_D3D_RESULT_THROW(hr, "Failed to crate warp device")
        }

#if _DEBUG
        {
	        CComPtr<ID3D12InfoQueue> pInfoQueue;
            hr = d3d12Device->QueryInterface(__uuidof(pInfoQueue), reinterpret_cast<void**>(static_cast<ID3D12InfoQueue**>(&pInfoQueue)));
	        if( SUCCEEDED(hr) )
	        {
		        // Suppress whole categories of messages
		        //D3D12_MESSAGE_CATEGORY Categories[] = {};

		        // Suppress messages based on their severity level
		        D3D12_MESSAGE_SEVERITY Severities[] = 
		        {
			        D3D12_MESSAGE_SEVERITY_INFO
		        };

		        // Suppress individual messages by their ID
		        //D3D12_MESSAGE_ID DenyIds[] = {};

		        D3D12_INFO_QUEUE_FILTER NewFilter = {};
		        //NewFilter.DenyList.NumCategories = _countof(Categories);
		        //NewFilter.DenyList.pCategoryList = Categories;
		        NewFilter.DenyList.NumSeverities = _countof(Severities);
		        NewFilter.DenyList.pSeverityList = Severities;
		        //NewFilter.DenyList.NumIDs = _countof(DenyIds);
		        //NewFilter.DenyList.pIDList = DenyIds;

		        hr = pInfoQueue->PushStorageFilter(&NewFilter);
                VERIFY(SUCCEEDED(hr), "Failed to push storage filter");
            }
        }
#endif

#ifndef RELEASE
	    // Prevent the GPU from overclocking or underclocking to get consistent timings
	    d3d12Device->SetStablePowerState(TRUE);
#endif

	    // Describe and create the command queue.
	    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

        CComPtr<ID3D12CommandQueue> pd3d12CmdQueue;
        hr = d3d12Device->CreateCommandQueue(&queueDesc, __uuidof(pd3d12CmdQueue), reinterpret_cast<void**>(static_cast<ID3D12CommandQueue**>(&pd3d12CmdQueue)));
        CHECK_D3D_RESULT_THROW(hr, "Failed to create command queue");
        hr = pd3d12CmdQueue->SetName(L"Main Command Queue");
        VERIFY_EXPR(SUCCEEDED(hr));

        auto &RawMemAllocator = GetRawAllocator();
        RenderDeviceD3D12Impl *pRenderDeviceD3D12( NEW(RawMemAllocator, "RenderDeviceD3D12Impl instance", RenderDeviceD3D12Impl, d3d12Device, pd3d12CmdQueue ) );
        pRenderDeviceD3D12->QueryInterface(IID_RenderDevice, reinterpret_cast<IObject**>(ppDevice) );

        RefCntAutoPtr<DeviceContextD3D12Impl> pDeviceContextD3D12( NEW(RawMemAllocator, "DeviceContextD3D12Impl instance", DeviceContextD3D12Impl, pRenderDeviceD3D12, false) );
        // We must call AddRef() (implicitly through QueryInterface()) because pRenderDeviceD3D12 will
        // keep a weak reference to the context
        pDeviceContextD3D12->QueryInterface(IID_DeviceContext, reinterpret_cast<IObject**>(ppContext) );
        //pRenderDeviceD3D12->SetImmediateContext(pDeviceContextD3D12);
    }
    catch( const std::runtime_error & )
    {
        if( *ppDevice )
        {
            (*ppDevice)->Release();
            *ppDevice = nullptr;
        }
        if( *ppContext )
        {
            (*ppContext)->Release();
            *ppContext = nullptr;
        }

        LOG_ERROR( "Failed to create device and immediate context" );
    }
}

void CreateSwapChainD3D12( IRenderDevice *pDevice, Diligent::IDeviceContext *pImmediateContext, const SwapChainDesc& SwapChainDesc, void* pNativeWndHandle, ISwapChain **ppSwapChain )
{
    VERIFY( ppSwapChain, "Null pointer is provided" );
    if( !ppSwapChain )
        return;

    *ppSwapChain = nullptr;

    try
    {
        auto *pDeviceD3D12 = ValidatedCast<RenderDeviceD3D12Impl>( pDevice );
        auto *pDeviceContextD3D12 = ValidatedCast<DeviceContextD3D12Impl>(pImmediateContext);
        auto &RawMemAllocator = GetRawAllocator();
        auto *pSwapChainD3D12 = NEW(RawMemAllocator, "SwapChainD3D12Impl instance", SwapChainD3D12Impl, SwapChainDesc, pDeviceD3D12, pDeviceContextD3D12, pNativeWndHandle);
        pSwapChainD3D12->QueryInterface( IID_SwapChain, reinterpret_cast<IObject**>(ppSwapChain) );

        pDeviceContextD3D12->SetSwapChain(pSwapChainD3D12);
        /// Bind default render target
        pDeviceContextD3D12->SetRenderTargets( 0, nullptr, nullptr );
        /// Set default viewport
        pDeviceContextD3D12->SetViewports( 1, nullptr, 0, 0 );
    }
    catch( const std::runtime_error & )
    {
        if( *ppSwapChain )
        {
            (*ppSwapChain)->Release();
            *ppSwapChain = nullptr;
        }

        LOG_ERROR( "Failed to create the swap chain" );
    }
}

}
