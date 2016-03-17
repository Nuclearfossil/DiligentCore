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
#include "RenderDeviceD3D12Impl.h"
#include "DeviceContextD3D12Impl.h"
#include "SwapChainD3D12Impl.h"
#include "PipelineStateD3D12Impl.h"
#include "CommandContext.h"
#include "TextureD3D12Impl.h"
#include "BufferD3D12Impl.h"
#include "D3D12TypeConversions.h"
#include "d3dx12_win.h"

using namespace Diligent;

namespace Diligent
{

    DeviceContextD3D12Impl::DeviceContextD3D12Impl( IMemoryAllocator &RawMemAllocator, IRenderDevice *pDevice, bool bIsDeferred) :
        TDeviceContextBase(RawMemAllocator, pDevice, bIsDeferred),
        m_NumCommandsInCurCtx(0),
        m_pCurrCmdCtx(nullptr),
        m_CommittedD3D12IndexFmt(DXGI_FORMAT_UNKNOWN),
        m_CommittedD3D12IndexDataStartOffset(0),
        m_bShaderResourcesCommitted(false),
        m_MipsGenerator(ValidatedCast<RenderDeviceD3D12Impl>(pDevice)->GetD3D12Device())
    {
        m_NumBoundRenderTargets = 0;

        auto *pd3d12Device = ValidatedCast<RenderDeviceD3D12Impl>(pDevice)->GetD3D12Device();

        D3D12_COMMAND_SIGNATURE_DESC CmdSignatureDesc = {};
        D3D12_INDIRECT_ARGUMENT_DESC IndirectArg = {};
        CmdSignatureDesc.NodeMask = 0;
        CmdSignatureDesc.NumArgumentDescs = 1;
        CmdSignatureDesc.pArgumentDescs = &IndirectArg;

        CmdSignatureDesc.ByteStride = sizeof(UINT)*4;
        IndirectArg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
        auto hr = pd3d12Device->CreateCommandSignature(&CmdSignatureDesc, nullptr, __uuidof(m_pDrawIndirectSignature), reinterpret_cast<void**>(static_cast<ID3D12CommandSignature**>(&m_pDrawIndirectSignature)) );
        CHECK_D3D_RESULT_THROW(hr, "Failed to create indirect draw command signature")

        CmdSignatureDesc.ByteStride = sizeof(UINT)*5;
        IndirectArg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
        hr = pd3d12Device->CreateCommandSignature(&CmdSignatureDesc, nullptr, __uuidof(m_pDrawIndexedIndirectSignature), reinterpret_cast<void**>(static_cast<ID3D12CommandSignature**>(&m_pDrawIndexedIndirectSignature)) );
        CHECK_D3D_RESULT_THROW(hr, "Failed to create draw indexed indirect command signature")

        CmdSignatureDesc.ByteStride = sizeof(UINT)*3;
        IndirectArg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
        hr = pd3d12Device->CreateCommandSignature(&CmdSignatureDesc, nullptr, __uuidof(m_pDispatchIndirectSignature), reinterpret_cast<void**>(static_cast<ID3D12CommandSignature**>(&m_pDispatchIndirectSignature)) );
        CHECK_D3D_RESULT_THROW(hr, "Failed to create dispatch indirect command signature")
    }

    DeviceContextD3D12Impl::~DeviceContextD3D12Impl()
    {
        Flush();
    }

    //IMPLEMENT_QUERY_INTERFACE( DeviceContextD3D12Impl, IID_DeviceContextD3D12, TDeviceContextBase )

    void DeviceContextD3D12Impl::SetPipelineState(IPipelineState *pPipelineState)
    {
        TDeviceContextBase::SetPipelineState( pPipelineState );
        auto *pPipelineStateD3D12 = ValidatedCast<PipelineStateD3D12Impl>(pPipelineState);

        if (m_NumCommandsInCurCtx >= NumCommandsToFlush)
        {
            Flush();
        }

        auto *pCmdCtx = RequestCmdContext();
        auto &Desc = pPipelineStateD3D12->GetDesc();
        auto *pd3d12PSO = pPipelineStateD3D12->GetD3D12PipelineState();
        if (Desc.IsComputePipeline)
        {
            pCmdCtx->AsComputeContext().SetPipelineState(pd3d12PSO);
            ++m_NumCommandsInCurCtx;
        }
        else
        {
            auto &GraphicsCtx = pCmdCtx->AsGraphicsContext();
            GraphicsCtx.SetPipelineState(pd3d12PSO);
            ++m_NumCommandsInCurCtx;

            {
                const Uint32 MaxScissorRects = D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
                D3D12_RECT d3d12ScissorRects[MaxScissorRects];
                Uint32 NumRects = 0;
                if (Desc.GraphicsPipeline.RasterizerDesc.ScissorEnable)
                {
                    // Commit currently set scissor rectangles
                    NumRects = m_NumScissorRects;
                    for( Uint32 sr = 0; sr < NumRects; ++sr )
                    {
                        d3d12ScissorRects[sr].left   = m_ScissorRects[sr].left;
                        d3d12ScissorRects[sr].top    = m_ScissorRects[sr].top;
                        d3d12ScissorRects[sr].right  = m_ScissorRects[sr].right;
                        d3d12ScissorRects[sr].bottom = m_ScissorRects[sr].bottom;
                    }
                }
                else
                {
                    // Disable scissor rectangles
                    NumRects = MaxScissorRects;
                    for( Uint32 sr = 0; sr < NumRects; ++sr )
                    {
                        d3d12ScissorRects[sr].left   = 0;
                        d3d12ScissorRects[sr].top    = 0;
                        d3d12ScissorRects[sr].right  = 16384;
                        d3d12ScissorRects[sr].bottom = 16384;
                    }
                }
                GraphicsCtx.SetScissorRects(NumRects, d3d12ScissorRects);
                ++m_NumCommandsInCurCtx;
            }
            GraphicsCtx.SetStencilRef( m_StencilRef );
            GraphicsCtx.SetBlendFactor( m_BlendFactors );
            m_NumCommandsInCurCtx += 2;
            m_CommittedD3D12IndexBuffer = nullptr;
            m_CommittedD3D12IndexDataStartOffset = 0;
            m_CommittedD3D12IndexFmt = DXGI_FORMAT_UNKNOWN;
            RebindRenderTargets();
            CommitViewports();
        }
        m_bShaderResourcesCommitted = false;
    }

    void DeviceContextD3D12Impl::CommitShaderResources(IShaderResourceBinding *pShaderResourceBinding)
    {
        if (!m_pPipelineState)
        {
            LOG_ERROR("No pipeline state is bound");
            return;
        }

        auto *pCtx = RequestCmdContext();
        auto *pPipelineStateD3D12 = ValidatedCast<PipelineStateD3D12Impl>(m_pPipelineState.RawPtr());

        pPipelineStateD3D12->CommitShaderResources(pShaderResourceBinding, *pCtx, m_CbvSrvUavDescriptorsToCommit, m_SamplerDescriptorsToCommit, m_UnitRangeSizes);
        m_bShaderResourcesCommitted = true;
    }

    void DeviceContextD3D12Impl::SetStencilRef(Uint32 StencilRef)
    {
        if (TDeviceContextBase::SetStencilRef(StencilRef, 0))
        {
            RequestCmdContext()->AsGraphicsContext().SetStencilRef( m_StencilRef );
            ++m_NumCommandsInCurCtx;
        }
    }

    void DeviceContextD3D12Impl::SetBlendFactors(const float* pBlendFactors)
    {
        if (TDeviceContextBase::SetBlendFactors(m_BlendFactors, 0))
        {
            RequestCmdContext()->AsGraphicsContext().SetBlendFactor( m_BlendFactors );
            ++m_NumCommandsInCurCtx;
        }
    }

    void DeviceContextD3D12Impl::CommitD3D12IndexBuffer(VALUE_TYPE IndexType)
    {
        if( !m_pIndexBuffer )
        {
            LOG_ERROR_MESSAGE( "Index buffer is not set up for indexed draw command" );
            return;
        }

        D3D12_INDEX_BUFFER_VIEW IBView;
        BufferD3D12Impl *pBuffD3D12 = static_cast<BufferD3D12Impl *>(m_pIndexBuffer.RawPtr());
        IBView.BufferLocation = pBuffD3D12->GetD3D12Buffer()->GetGPUVirtualAddress() + m_IndexDataStartOffset;
        if( IndexType == VT_UINT32 )
            IBView.Format = DXGI_FORMAT_R32_UINT;
        else if( IndexType == VT_UINT16 )
            IBView.Format = DXGI_FORMAT_R16_UINT;
        else
        {
            LOG_ERROR_MESSAGE( "Unsupported index format. Only R16_UINT and R32_UINT are allowed." );
            return;
        }
        IBView.SizeInBytes = m_pIndexBuffer->GetDesc().uiSizeInBytes;

        // Device context keeps strong reference to bound index buffer.
        // When the buffer is unbound, the reference to the D3D12 resource
        // is added to the context. There is no need to add reference here
        //auto &GraphicsCtx = RequestCmdContext()->AsGraphicsContext();
        //auto *pd3d12Resource = pBuffD3D12->GetD3D12Buffer();
        //GraphicsCtx.AddReferencedObject(pd3d12Resource);

        auto &GraphicsCtx = RequestCmdContext()->AsGraphicsContext();
        // Resource transitioning must always be performed!
        GraphicsCtx.TransitionResource(pBuffD3D12, D3D12_RESOURCE_STATE_INDEX_BUFFER, true);

        if( m_CommittedD3D12IndexBuffer != pBuffD3D12->GetD3D12Buffer() ||
            m_CommittedD3D12IndexFmt != IBView.Format ||
            m_CommittedD3D12IndexDataStartOffset != m_IndexDataStartOffset )
        {
            m_CommittedD3D12IndexBuffer = pBuffD3D12->GetD3D12Buffer();
            m_CommittedD3D12IndexFmt = IBView.Format;
            m_CommittedD3D12IndexDataStartOffset = m_IndexDataStartOffset;
            GraphicsCtx.SetIndexBuffer( IBView );
            ++m_NumCommandsInCurCtx;
        }
    }

    void DeviceContextD3D12Impl::CommitD3D12VertexBuffers()
    {
        auto *pPipelineStateD3D12 = ValidatedCast<PipelineStateD3D12Impl>(m_pPipelineState.RawPtr());

        auto &GraphicsCtx = RequestCmdContext()->AsGraphicsContext();
        D3D12_VERTEX_BUFFER_VIEW VBViews[MaxBufferSlots] = {};
        UINT NumBoundBuffers = m_NumVertexStreams;
        VERIFY( NumBoundBuffers <= MaxBufferSlots, "Too many buffers are being set" );
        NumBoundBuffers = std::min( NumBoundBuffers, static_cast<UINT>(MaxBufferSlots) );
        const auto *TightStrides = pPipelineStateD3D12->GetTightStrides();
        for( UINT Buff = 0; Buff < NumBoundBuffers; ++Buff )
        {
            auto &CurrStream = m_VertexStreams[Buff];
            auto &VBView = VBViews[Buff];
            VERIFY( CurrStream.pBuffer, "Attempting to bind a null buffer for rendering" );
            
            auto *pBufferD3D12 = static_cast<BufferD3D12Impl*>(CurrStream.pBuffer.RawPtr());
            GraphicsCtx.TransitionResource(pBufferD3D12, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
            auto *pd3d12Resource = pBufferD3D12->GetD3D12Buffer();
            
            // Device context keeps strong references to all vertex buffers.
            // When a buffer is unbound, a reference to D3D12 resource is added to the context,
            // so there is no need to reference the resource here
            //GraphicsCtx.AddReferencedObject(pd3d12Resource);

            VBView.BufferLocation = pd3d12Resource->GetGPUVirtualAddress() + CurrStream.Offset;
            VBView.StrideInBytes = CurrStream.Stride ? CurrStream.Stride : TightStrides[Buff];
            VBView.SizeInBytes = pBufferD3D12->GetDesc().uiSizeInBytes;
        }

#if 0
        UINT NumBuffersToSet = NumBoundBuffers;
        bool BindVBs = false;
        if( NumBoundBuffers != m_CommittedD3D11VertexBuffers.size() )
        {
            // If number of currently bound d3d11 buffers is larger than NumBuffersToSet, the
            // unused buffers will be unbound
            NumBuffersToSet = std::max(NumBuffersToSet, static_cast<UINT>(m_CommittedD3D11VertexBuffers.size()) );
            m_CommittedD3D11VertexBuffers.resize(NumBoundBuffers);
            m_CommittedD3D11VBStrides.resize(NumBoundBuffers);
            m_CommittedD3D11VBOffsets.resize( NumBoundBuffers );
            BindVBs = true;
        }

        for( UINT Slot = 0; Slot < NumBoundBuffers; ++Slot )
        {
            if( m_CommittedD3D11VertexBuffers[Slot] != ppD3D11Buffers[Slot] ||
                m_CommittedD3D11VBStrides[Slot] != Strides[Slot] ||
                m_CommittedD3D11VBOffsets[Slot] != Offsets[Slot] )
            {
                BindVBs = true;

                m_CommittedD3D11VertexBuffers[Slot] = ppD3D11Buffers[Slot];
                m_CommittedD3D11VBStrides[Slot] = Strides[Slot];
                m_CommittedD3D11VBOffsets[Slot] = Offsets[Slot];
            }
        }

        if( BindVBs )
#endif
        {
            GraphicsCtx.FlushResourceBarriers();
            GraphicsCtx.SetVertexBuffers( 0, NumBoundBuffers, VBViews );
            ++m_NumCommandsInCurCtx;
        }
    }

    void DeviceContextD3D12Impl::Draw( DrawAttribs &DrawAttribs )
    {
        if (!m_pPipelineState)
        {
            LOG_ERROR("No pipeline state is bound");
            return;
        }
        if (m_pPipelineState->GetDesc().IsComputePipeline)
        {
            LOG_ERROR("No graphics pipeline state is bound");
            return;
        }

        if( DrawAttribs.IsIndexed )
        {
            CommitD3D12IndexBuffer(DrawAttribs.IndexType);
        }

        auto *pPipelineStateD3D12 = ValidatedCast<PipelineStateD3D12Impl>(m_pPipelineState.RawPtr());
        if(!m_bShaderResourcesCommitted)
        {
            if( pPipelineStateD3D12->dbgContainsShaderResources() )
                LOG_ERROR_MESSAGE("Pipeline state \"", pPipelineStateD3D12->GetDesc().Name, "\" contains shader resources, but IDeviceContext::CommitShaderResources() was not called" )
        }

        CommitD3D12VertexBuffers();

        auto &GraphCtx = RequestCmdContext()->AsGraphicsContext();
        auto D3D12Topology = TopologyToD3D12Topology( DrawAttribs.Topology );
        GraphCtx.SetPrimitiveTopology(D3D12Topology);

        GraphCtx.SetRootSignature( pPipelineStateD3D12->GetD3D12RootSignature() );

        if( DrawAttribs.IsIndirect )
        {
            auto *pBufferD3D12 = ValidatedCast<BufferD3D12Impl>(DrawAttribs.pIndirectDrawAttribs);
            GraphCtx.TransitionResource(pBufferD3D12, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
            ID3D12Resource *pd3d12ArgsBuff = pBufferD3D12 ? pBufferD3D12->GetD3D12Buffer() : nullptr;
            GraphCtx.ExecuteIndirect(DrawAttribs.IsIndexed ? m_pDrawIndexedIndirectSignature : m_pDrawIndirectSignature, pd3d12ArgsBuff, DrawAttribs.IndirectDrawArgsOffset);
        }
        else
        {
            if( DrawAttribs.IsIndexed )
                GraphCtx.DrawIndexed(DrawAttribs.NumIndices, DrawAttribs.NumInstances, DrawAttribs.FirstIndexLocation, DrawAttribs.BaseVertex, DrawAttribs.FirstInstanceLocation);
            else
                GraphCtx.Draw(DrawAttribs.NumVertices, DrawAttribs.NumInstances, DrawAttribs.StartVertexLocation, DrawAttribs.FirstInstanceLocation );
        }
        ++m_NumCommandsInCurCtx;
    }

    void DeviceContextD3D12Impl::DispatchCompute( const DispatchComputeAttribs &DispatchAttrs )
    {
        if (!m_pPipelineState)
        {
            LOG_ERROR("No pipeline state is bound");
            return;
        }
        if (!m_pPipelineState->GetDesc().IsComputePipeline)
        {
            LOG_ERROR("No compute pipeline state is bound");
            return;
        }

        auto *pPipelineStateD3D12 = ValidatedCast<PipelineStateD3D12Impl>(m_pPipelineState.RawPtr());
        if(!m_bShaderResourcesCommitted)
        {
            if( pPipelineStateD3D12->dbgContainsShaderResources() )
                LOG_ERROR_MESSAGE("Pipeline state \"", pPipelineStateD3D12->GetDesc().Name, "\" contains shader resources, but IDeviceContext::CommitShaderResources() was not called" )
        }

        auto &ComputeCtx = RequestCmdContext()->AsComputeContext();
        ComputeCtx.SetRootSignature( pPipelineStateD3D12->GetD3D12RootSignature() );
      
        if( DispatchAttrs.pIndirectDispatchAttribs )
        {
            auto *pBufferD3D12 = ValidatedCast<BufferD3D12Impl>(DispatchAttrs.pIndirectDispatchAttribs);
            ComputeCtx.TransitionResource(pBufferD3D12, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
            ID3D12Resource *pd3d12ArgsBuff = pBufferD3D12 ? pBufferD3D12->GetD3D12Buffer() : nullptr;
            ComputeCtx.ExecuteIndirect(m_pDispatchIndirectSignature, pd3d12ArgsBuff, DispatchAttrs.DispatchArgsByteOffset);
        }
        else
            ComputeCtx.Dispatch(DispatchAttrs.ThreadGroupCountX, DispatchAttrs.ThreadGroupCountY, DispatchAttrs.ThreadGroupCountZ);
        ++m_NumCommandsInCurCtx;
    }

    void DeviceContextD3D12Impl::ClearDepthStencil( ITextureView *pView, Uint32 ClearFlags, float fDepth, Uint8 Stencil )
    {
        ITextureViewD3D12 *pDSVD3D12 = nullptr;
        if( pView != nullptr )
        {
            pDSVD3D12 = ValidatedCast<ITextureViewD3D12>(pView);
#ifdef _DEBUG
            const auto& ViewDesc = pDSVD3D12->GetDesc();
            VERIFY( ViewDesc.ViewType == TEXTURE_VIEW_DEPTH_STENCIL, "Incorrect view type: depth stencil is expected" );
#endif
        }
        else
        {
            auto *pDSV = ValidatedCast<SwapChainD3D12Impl>(m_pSwapChain.RawPtr())->GetDepthBufferDSV();
            pDSVD3D12 = ValidatedCast<ITextureViewD3D12>(pDSV);
        }
        D3D12_CLEAR_FLAGS d3d12ClearFlags = (D3D12_CLEAR_FLAGS)0;
        if( ClearFlags & CLEAR_DEPTH_FLAG )   d3d12ClearFlags |= D3D12_CLEAR_FLAG_DEPTH;
        if( ClearFlags & CLEAR_STENCIL_FLAG ) d3d12ClearFlags |= D3D12_CLEAR_FLAG_STENCIL;
        // The full extent of the resource view is always cleared. 
        // Viewport and scissor settings are not applied??
        RequestCmdContext()->AsGraphicsContext().ClearDepthStencil( pDSVD3D12, d3d12ClearFlags, fDepth, Stencil );
        ++m_NumCommandsInCurCtx;
    }

    void DeviceContextD3D12Impl::ClearRenderTarget( ITextureView *pView, const float *RGBA )
    {
        ITextureViewD3D12 *pd3d12RTV = nullptr;
        if( pView != nullptr )
        {
#ifdef _DEBUG
            const auto& ViewDesc = pView->GetDesc();
            VERIFY( ViewDesc.ViewType == TEXTURE_VIEW_RENDER_TARGET, "Incorrect view type: render target is expected" );
#endif
            pd3d12RTV = ValidatedCast<ITextureViewD3D12>(pView);
        }
        else
        {
            auto *pBackBufferRTV = ValidatedCast<SwapChainD3D12Impl>(m_pSwapChain.RawPtr())->GetCurrentBackBufferRTV();
            pd3d12RTV = ValidatedCast<ITextureViewD3D12>(pBackBufferRTV);
        }

        static const float Zero[4] = { 0.f, 0.f, 0.f, 0.f };
        if( RGBA == nullptr )
            RGBA = Zero;

        // The full extent of the resource view is always cleared. 
        // Viewport and scissor settings are not applied??
        RequestCmdContext()->AsGraphicsContext().ClearRenderTarget( pd3d12RTV, RGBA );
        ++m_NumCommandsInCurCtx;
    }

    void DeviceContextD3D12Impl::Flush()
    {
        if( m_pCurrCmdCtx )
        {
            // All commands from the context will be scheduled for execution now, so we can release pending objects
            ValidatedCast<RenderDeviceD3D12Impl>(m_pDevice.RawPtr())->CloseAndExecuteCommandContext(m_pCurrCmdCtx, true);
            m_pCurrCmdCtx = nullptr;
            m_NumCommandsInCurCtx = 0;
        }
    }

    void DeviceContextD3D12Impl::SetVertexBuffers( Uint32 StartSlot, Uint32 NumBuffersSet, IBuffer **ppBuffers, Uint32 *pStrides, Uint32 *pOffsets, Uint32 Flags )
    {
        TDeviceContextBase::SetVertexBuffers( StartSlot, NumBuffersSet, ppBuffers, pStrides, pOffsets, Flags );
    }

    void DeviceContextD3D12Impl::ClearState()
    {
        TDeviceContextBase::ClearState();
        LOG_ERROR_ONCE("DeviceContextD3D12Impl::ClearState() is not implemented");
    }

    void DeviceContextD3D12Impl::SetIndexBuffer( IBuffer *pIndexBuffer, Uint32 ByteOffset )
    {
        TDeviceContextBase::SetIndexBuffer( pIndexBuffer, ByteOffset );
    }

    void DeviceContextD3D12Impl::CommitViewports()
    {
        const Uint32 MaxViewports = D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        D3D12_VIEWPORT d3d12Viewports[MaxViewports];
        
        for( Uint32 vp = 0; vp < m_NumViewports; ++vp )
        {
            d3d12Viewports[vp].TopLeftX = m_Viewports[vp].TopLeftX;
            d3d12Viewports[vp].TopLeftY = m_Viewports[vp].TopLeftY;
            d3d12Viewports[vp].Width    = m_Viewports[vp].Width;
            d3d12Viewports[vp].Height   = m_Viewports[vp].Height;
            d3d12Viewports[vp].MinDepth = m_Viewports[vp].MinDepth;
            d3d12Viewports[vp].MaxDepth = m_Viewports[vp].MaxDepth;
        }
        // All viewports must be set atomically as one operation. 
        // Any viewports not defined by the call are disabled.
        RequestCmdContext()->AsGraphicsContext().SetViewports( m_NumViewports, d3d12Viewports );
        ++m_NumCommandsInCurCtx;
    }

    void DeviceContextD3D12Impl::SetViewports( Uint32 NumViewports, const Viewport *pViewports, Uint32 RTWidth, Uint32 RTHeight  )
    {
        const Uint32 MaxViewports = D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        VERIFY( NumViewports < MaxViewports, "Too many viewports are being set" );
        NumViewports = std::min( NumViewports, MaxViewports );

        TDeviceContextBase::SetViewports( NumViewports, pViewports, RTWidth, RTHeight );
        VERIFY( NumViewports == m_NumViewports, "Unexpected number of viewports" );

        CommitViewports();
    }

    void DeviceContextD3D12Impl::SetScissorRects( Uint32 NumRects, const Rect *pRects, Uint32 RTWidth, Uint32 RTHeight  )
    {
        const Uint32 MaxScissorRects = D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        VERIFY( NumRects < MaxScissorRects, "Too many scissor rects are being set" );
        NumRects = std::min( NumRects, MaxScissorRects );

        TDeviceContextBase::SetScissorRects(NumRects, pRects, RTWidth, RTHeight);

        // Only commit scissor rects if scissor test is enabled in the rasterizer state. 
        if( !m_pPipelineState || m_pPipelineState->GetDesc().GraphicsPipeline.RasterizerDesc.ScissorEnable )
        {
            D3D12_RECT d3d12ScissorRects[MaxScissorRects];
            VERIFY( NumRects == m_NumScissorRects, "Unexpected number of scissor rects" );
            for( Uint32 sr = 0; sr < NumRects; ++sr )
            {
                d3d12ScissorRects[sr].left   = m_ScissorRects[sr].left;
                d3d12ScissorRects[sr].top    = m_ScissorRects[sr].top;
                d3d12ScissorRects[sr].right  = m_ScissorRects[sr].right;
                d3d12ScissorRects[sr].bottom = m_ScissorRects[sr].bottom;
            }
            RequestCmdContext()->AsGraphicsContext().SetScissorRects(NumRects, d3d12ScissorRects);
            ++m_NumCommandsInCurCtx;
        }
    }


    void DeviceContextD3D12Impl::RebindRenderTargets()
    {
        const Uint32 MaxD3D12RTs = D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT;
        Uint32 NumRenderTargets = m_NumBoundRenderTargets;
        VERIFY( NumRenderTargets <= MaxD3D12RTs, "D3D12 only allows 8 simultaneous render targets" );
        NumRenderTargets = std::min( MaxD3D12RTs, NumRenderTargets );

        ITextureViewD3D12 *ppRTVs[MaxD3D12RTs] = {};
        ITextureViewD3D12 *pDSV = nullptr;
        if( NumRenderTargets == 0 && m_pBoundDepthStencil == nullptr )
        {
            NumRenderTargets = 1;
            auto *pSwapChainD3D12 = ValidatedCast<SwapChainD3D12Impl>( m_pSwapChain.RawPtr() );
            ppRTVs[0] = ValidatedCast<ITextureViewD3D12>(pSwapChainD3D12->GetCurrentBackBufferRTV());
            pDSV = ValidatedCast<ITextureViewD3D12>(pSwapChainD3D12->GetDepthBufferDSV());
        }
        else
        {
            for( Uint32 rt = 0; rt < NumRenderTargets; ++rt )
                ppRTVs[rt] = ValidatedCast<ITextureViewD3D12>(m_pBoundRenderTargets[rt].RawPtr());
            pDSV = ValidatedCast<ITextureViewD3D12>(m_pBoundDepthStencil.RawPtr());
        }
        RequestCmdContext()->AsGraphicsContext().SetRenderTargets(NumRenderTargets, ppRTVs, pDSV);
        ++m_NumCommandsInCurCtx;
    }

    void DeviceContextD3D12Impl::SetRenderTargets( Uint32 NumRenderTargets, ITextureView *ppRenderTargets[], ITextureView *pDepthStencil )
    {
        if( TDeviceContextBase::SetRenderTargets( NumRenderTargets, ppRenderTargets, pDepthStencil ) )
        {
            RebindRenderTargets();

            // Set the viewport to match the render target size
            SetViewports(1, nullptr, 0, 0);
        }
    }
   
    CommandContext* DeviceContextD3D12Impl::RequestCmdContext()
    {
        if (m_pCurrCmdCtx == nullptr)
        {
            m_pCurrCmdCtx = ValidatedCast<RenderDeviceD3D12Impl>(m_pDevice.RawPtr())->AllocateCommandContext();
        }
        return m_pCurrCmdCtx;
    }

    void DeviceContextD3D12Impl::UpdateBufferRegion(BufferD3D12Impl *pBuffD3D12, const void *pData, Uint64 DstOffset, Uint64 NumBytes)
    {
        auto pCmdCtx = RequestCmdContext();
        auto TmpSpace = pCmdCtx->AllocateUploadBuffer(NumBytes);
	    memcpy(TmpSpace.CPUAddress, pData, NumBytes);
        pCmdCtx->TransitionResource(pBuffD3D12, D3D12_RESOURCE_STATE_COPY_DEST, true);
        // Source buffer is already in right state, which cannot be changed
        pCmdCtx->GetCommandList()->CopyBufferRegion( pBuffD3D12->GetD3D12Buffer(), DstOffset, TmpSpace.pBuffer, TmpSpace.Offset, NumBytes);
        ++m_NumCommandsInCurCtx;
    }

    void DeviceContextD3D12Impl::CopyBufferRegion(BufferD3D12Impl *pSrcBuffD3D12, BufferD3D12Impl *pDstBuffD3D12, Uint64 SrcOffset, Uint64 DstOffset, Uint64 NumBytes)
    {
        auto pCmdCtx = RequestCmdContext();
        pCmdCtx->TransitionResource(pSrcBuffD3D12, D3D12_RESOURCE_STATE_COPY_SOURCE);
        pCmdCtx->TransitionResource(pDstBuffD3D12, D3D12_RESOURCE_STATE_COPY_DEST, true);
        // Source buffer is already in right state, which cannot be changed
        pCmdCtx->GetCommandList()->CopyBufferRegion( pDstBuffD3D12->GetD3D12Buffer(), DstOffset, pSrcBuffD3D12->GetD3D12Buffer(), SrcOffset, NumBytes);
        ++m_NumCommandsInCurCtx;
    }

    void DeviceContextD3D12Impl::CopyTextureRegion(TextureD3D12Impl *pSrcTexture, Uint32 SrcSubResIndex, const D3D12_BOX *pD3D12SrcBox,
                                                   TextureD3D12Impl *pDstTexture, Uint32 DstSubResIndex, Uint32 DstX, Uint32 DstY, Uint32 DstZ)
    {
        auto pCmdCtx = RequestCmdContext();
        pCmdCtx->TransitionResource(pSrcTexture, D3D12_RESOURCE_STATE_COPY_SOURCE);
        pCmdCtx->TransitionResource(pDstTexture, D3D12_RESOURCE_STATE_COPY_DEST, true);

        D3D12_TEXTURE_COPY_LOCATION DstLocation = {}, SrcLocation = {};

        DstLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        DstLocation.pResource = pDstTexture->GetD3D12Resource();
        DstLocation.SubresourceIndex = SrcSubResIndex;

        SrcLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        SrcLocation.pResource = pSrcTexture->GetD3D12Resource();
        SrcLocation.SubresourceIndex = DstSubResIndex;

        pCmdCtx->GetCommandList()->CopyTextureRegion( &DstLocation, DstX, DstY, DstZ, &SrcLocation, pD3D12SrcBox);
        ++m_NumCommandsInCurCtx;
    }

    void DeviceContextD3D12Impl::GenerateMips(TextureViewD3D12Impl *pTexView)
    {
        auto *pCtx = RequestCmdContext();
        m_MipsGenerator.GenerateMips(ValidatedCast<RenderDeviceD3D12Impl>(m_pDevice.RawPtr()), pTexView, *pCtx);
    }

    void DeviceContextD3D12Impl::FinishCommandList(class ICommandList **ppCommandList)
    {

    }

    void DeviceContextD3D12Impl::ExecuteCommandList(class ICommandList *pCommandList)
    {

    }
}
