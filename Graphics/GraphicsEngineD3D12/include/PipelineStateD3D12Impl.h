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
/// Declaration of Diligent::PipelineStateD3D12Impl class

#include "RenderDeviceD3D12.h"
#include "PipelineStateD3D12.h"
#include "PipelineStateBase.h"
#include "RootSignature.h"
#include "ShaderResourceLayoutD3D12.h"

/// Namespace for the Direct3D11 implementation of the graphics engine
namespace Diligent
{

class FixedBlockMemoryAllocator;
/// Implementation of the Diligent::IRenderDeviceD3D12 interface
class PipelineStateD3D12Impl : public PipelineStateBase<IPipelineStateD3D12, IRenderDeviceD3D12, FixedBlockMemoryAllocator>
{
public:
    typedef PipelineStateBase<IPipelineStateD3D12, IRenderDeviceD3D12, FixedBlockMemoryAllocator> TPipelineStateBase;

    PipelineStateD3D12Impl( FixedBlockMemoryAllocator &PSOAllocator, class RenderDeviceD3D12Impl *pDeviceD3D12, const PipelineStateDesc &PipelineDesc );
    ~PipelineStateD3D12Impl();

    virtual void QueryInterface( const Diligent::INTERFACE_ID &IID, IObject **ppInterface );
   
    virtual ID3D12PipelineState *GetD3D12PipelineState()override;

    virtual void BindShaderResources( IResourceMapping *pResourceMapping, Uint32 Flags )override;

    virtual void CreateShaderResourceBinding( IShaderResourceBinding **ppShaderResourceBinding )override;

    virtual ID3D12RootSignature *GetD3D12RootSignature()override{return m_RootSig.GetD3D12RootSignature(); }

    void CommitShaderResources(IShaderResourceBinding *pShaderResourceBinding, 
                               class CommandContext &Ctx,
                               std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> &CbvSrvUavDescriptorsToCommit,
                               std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> &SamplerDescriptorsToCommit,
                               std::vector<UINT> &UnitRangeSizes);
    
    RootSignature& GetRootSignature(){return m_RootSig;}
    
    ShaderResourceLayoutD3D12& GetShaderResLayout(SHADER_TYPE ShaderType);
    
    bool dbgContainsShaderResources();

private:

    void ParseShaderResourceLayout(IShader *pShader);

    /// D3D12 device
    CComPtr<ID3D12PipelineState> m_pd3d12PSO;
    RootSignature m_RootSig;
       
    ShaderResourceLayoutD3D12 m_ShaderResourceLayouts[6];
    // Do not use strong reference to avoid cyclic references
    std::unique_ptr<class ShaderResourceBindingD3D12Impl, STDDeleter<ShaderResourceBindingD3D12Impl, FixedBlockMemoryAllocator> > m_pDefaultShaderResBinding;
};

}
