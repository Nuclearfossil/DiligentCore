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

#pragma once

/// \file
/// Declaration of Diligent::RenderDeviceD3D12Impl class

#include "RenderDeviceD3D12.h"
#include "RenderDeviceD3DBase.h"
#include "DescriptorHeap.h"
#include "CommandListManager.h"
#include "CommandContext.h"
#include "LinearAllocator.h"

/// Namespace for the Direct3D11 implementation of the graphics engine
namespace Diligent
{

/// Implementation of the Diligent::IRenderDeviceD3D12 interface
class RenderDeviceD3D12Impl : public RenderDeviceD3DBase<IRenderDeviceD3D12>
{
public:
    typedef RenderDeviceD3DBase<IRenderDeviceD3D12> TRenderDeviceBase;

    RenderDeviceD3D12Impl( IMemoryAllocator &RawMemAllocator, ID3D12Device *pD3D12Device, ID3D12CommandQueue *pd3d12CmdQueue );
    ~RenderDeviceD3D12Impl();

    virtual void QueryInterface( const Diligent::INTERFACE_ID &IID, IObject **ppInterface )override;

    virtual void CreatePipelineState( const PipelineStateDesc &PipelineDesc, IPipelineState **ppPipelineState )override;

    virtual void CreateDeferredContext(IDeviceContext **ppCtx)override final;

    virtual void CreateBuffer(const BufferDesc& BuffDesc, const BufferData &BuffData, IBuffer **ppBuffer);

    virtual void CreateShader(const ShaderCreationAttribs &ShaderCreationAttribs, IShader **ppShader);

    virtual void CreateTexture(const TextureDesc& TexDesc, const TextureData &Data, ITexture **ppTexture);
    
    void CreateTexture(TextureDesc& TexDesc, ID3D12Resource *pd3d12Texture, class TextureD3D12Impl **ppTexture);
    
    virtual void CreateSampler(const SamplerDesc& SamplerDesc, ISampler **ppSampler);

    virtual ID3D12Device* GetD3D12Device()override{return m_pd3d12Device;}
    
    DescriptorHeapAllocation AllocateDescriptor( D3D12_DESCRIPTOR_HEAP_TYPE Type, UINT Count = 1 );

    bool IsFenceComplete(Uint64 FenceValue);

    ID3D12CommandQueue *GetCmdQueue(){return m_pd3d12CmdQueue;}

	Uint64 IncrementFence();
	void WaitForFence(Uint64 FenceValue);
	void IdleGPU();
    CommandContext* AllocateCommandContext(const wchar_t *ID = L"");
    Uint64 CloseAndExecuteCommandContext(CommandContext *pCtx, bool ReleasePendingObjects = false);

    void SafeReleaseD3D12Object(ID3D12Object* pObj);

private:
    virtual void TestTextureFormat( TEXTURE_FORMAT TexFormat );
    void ProcessReleaseQueue();

    RefCntAutoPtr<StaticDescriptorHeap> m_CPUDescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES];
    RefCntAutoPtr<DynamicDescriptorHeapManager> m_DynamicGPUDescriptorHeapManager[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES];

    /// D3D12 device
    CComPtr<ID3D12Device> m_pd3d12Device;
    CComPtr<ID3D12CommandQueue> m_pd3d12CmdQueue;

	std::mutex m_FenceMutex;
	std::mutex m_EventMutex;

	CComPtr<ID3D12Fence> m_pFence;
	Uint64 m_NextFenceValue;
	Uint64 m_LastCompletedFenceValue;
	HANDLE m_FenceEventHandle;

    CommandListManager m_CmdListManager;

	std::vector<std::unique_ptr<CommandContext> > m_ContextPool;
	std::queue<CommandContext*> m_AvailableContexts;
	std::mutex m_ContextAllocationMutex;

    LinearAllocatorPageManager m_GPUExclusivePageManager;
    LinearAllocatorPageManager m_CPUWritablePageManager;

    // Object that must be kept alive
    std::mutex m_ReleasedObjectsMutex;
    std::vector< CComPtr<ID3D12Object> > m_PendingReleasedObjects;
    // Release queue
    std::queue< std::pair<Uint64, CComPtr<ID3D12Object> > > m_D3D12ObjReleaseQueue;
};

}
