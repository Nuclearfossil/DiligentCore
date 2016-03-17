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
#include "ShaderResourceBindingD3D12Impl.h"
#include "PipelineStateD3D12Impl.h"
#include "ShaderD3D12Impl.h"

namespace Diligent
{

ShaderResourceBindingD3D12Impl::ShaderResourceBindingD3D12Impl( FixedBlockMemoryAllocator &SRBAllocator, PipelineStateD3D12Impl *pPSO, bool IsPSOInternal) :
    TBase( SRBAllocator, pPSO, IsPSOInternal ),
    m_DummyVar(this),
    m_ResourceLayouts
    {
        ShaderResourceLayoutD3D12(this), 
        ShaderResourceLayoutD3D12(this), 
        ShaderResourceLayoutD3D12(this), 
        ShaderResourceLayoutD3D12(this), 
        ShaderResourceLayoutD3D12(this), 
        ShaderResourceLayoutD3D12(this)
    },
    m_InitializedLayouts(0)
{
    auto *pPSOD3D12 = ValidatedCast<PipelineStateD3D12Impl>(pPSO);
    auto *ppShaders = pPSOD3D12->GetShaders();
    auto NumShaders = pPSOD3D12->GetNumShaders();

    m_ShaderResourceCache.reset( new ShaderResourceCacheD3D12() );
    pPSO->GetRootSignature().InitResourceCache(*m_ShaderResourceCache);

    for (Uint32 s = 0; s < NumShaders; ++s)
    {
        auto *pShader = ppShaders[s];
        auto ShaderType = pShader->GetDesc().ShaderType;
        auto ShaderInd = GetShaderTypeIndex(ShaderType);
        SHADER_VARIABLE_TYPE Types[] = {SHADER_VARIABLE_TYPE_STATIC, SHADER_VARIABLE_TYPE_MUTABLE, SHADER_VARIABLE_TYPE_DYNAMIC};
        m_ResourceLayouts[ShaderInd].CloneLayout( pPSO->GetShaderResLayout(ShaderType), Types, _countof(Types), m_ShaderResourceCache, false );
        m_InitializedLayouts |= ShaderType;
    }
}

ShaderResourceBindingD3D12Impl::~ShaderResourceBindingD3D12Impl()
{
}

IMPLEMENT_QUERY_INTERFACE( ShaderResourceBindingD3D12Impl, IID_ShaderResourceBindingD3D12, TBase )

void ShaderResourceBindingD3D12Impl::BindResources(Uint32 ShaderFlags, IResourceMapping *pResMapping, Uint32 Flags)
{
    for (auto ShaderInd = 0; ShaderInd <= CSInd; ++ShaderInd )
    {
        if (ShaderFlags & GetShaderTypeFromIndex(ShaderInd))
        {
            m_ResourceLayouts[ShaderInd].BindResources(pResMapping, Flags, m_ShaderResourceCache);
        }
    }
}

IShaderVariable *ShaderResourceBindingD3D12Impl::GetVariable(SHADER_TYPE ShaderType, const char *Name)
{
    auto Ind = GetShaderTypeIndex(ShaderType);
    VERIFY_EXPR(Ind >= 0 && Ind < _countof(m_ResourceLayouts));
    if (!(m_InitializedLayouts & ShaderType) != 0)
    {
        LOG_ERROR_MESSAGE("Failed to find shader variable \"", Name,"\" in shader resource binding: shader type ", GetShaderTypeLiteralName(ShaderType), " is not initialized");
        return &m_DummyVar;
    }
    return m_ResourceLayouts[Ind].GetShaderVariable(Name);
}

#ifdef VERIFY_SHADER_BINDINGS
void ShaderResourceBindingD3D12Impl::dbgVerifyResourceBindings(PipelineStateD3D12Impl *pPSO)
{
    auto *pRefPSO = GetPipelineState();
    if (pRefPSO != pPSO)
    {
        LOG_ERROR("Shader resource binding does not match the pipeline state \"", pPSO->GetDesc().Name, '\"');
        return;
    }
    for(Int32 l = 0; l < _countof(m_ResourceLayouts); ++l)
        if( (m_InitializedLayouts & GetShaderTypeFromIndex(l)) != 0)
            m_ResourceLayouts[l].dbgVerifyBindings();
}
#endif

}
