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
#include "ShaderResourceBindingD3D11Impl.h"
#include "PipelineStateD3D11Impl.h"
#include "DeviceContextD3D11Impl.h"
#include "RenderDeviceD3D11Impl.h"

namespace Diligent
{


ShaderResourceBindingD3D11Impl::ShaderResourceBindingD3D11Impl( FixedBlockMemoryAllocator &SRBAllocator, PipelineStateD3D11Impl *pPSO, bool IsInternal) :
    TBase( SRBAllocator, pPSO, IsInternal ),
    m_bIsStaticResourcesBound(false),
    m_ResourceLayouts(STD_ALLOCATOR_RAW_MEM(ShaderResourceLayoutD3D11, GetRawAllocator(), "Allocator for vector<ShaderResourceLayoutD3D11>"))
{
    for(size_t s=0; s < _countof(m_ResourceLayoutIndex); ++s)
        m_ResourceLayoutIndex[s] = -1;

    auto ppShaders = pPSO->GetShaders();
    Int8 NumShaders = static_cast<Int8>( pPSO->GetNumShaders() );
    
    // Reserve memory for resource layouts
    m_ResourceLayouts.reserve(NumShaders);
    for (Int8 s = 0; s < NumShaders; ++s)
    {
        auto *pShaderD3D11 = ValidatedCast<ShaderD3D11Impl>(ppShaders[s]);
        auto ShaderInd = pShaderD3D11->GetShaderTypeIndex();
        VERIFY_EXPR(static_cast<Int32>(ShaderInd) == GetShaderTypeIndex(pShaderD3D11->GetDesc().ShaderType));

        SHADER_VARIABLE_TYPE VarTypes[] = {SHADER_VARIABLE_TYPE_MUTABLE, SHADER_VARIABLE_TYPE_DYNAMIC};

        auto &Allocator = pPSO->GetResourceCacheDataAllocator(s);
        m_ResourceLayouts.emplace_back(this);
        // Initialize resource cache to have enough space to contain all shader resources, including static ones
        // Static resources are copied before resources are committed
        m_BoundResourceCaches[s].Initialize(pShaderD3D11->GetResourceLayout(), Allocator);
        m_ResourceLayouts.back().CloneLayout(pShaderD3D11->GetResourceLayout(), VarTypes, _countof(VarTypes), m_BoundResourceCaches[s], Allocator);

        m_ResourceLayoutIndex[ShaderInd] = s;
        m_ShaderTypeIndex[s] = static_cast<Int8>(ShaderInd);
    }
}

ShaderResourceBindingD3D11Impl::~ShaderResourceBindingD3D11Impl()
{
    auto *pPSOD3D11Impl = ValidatedCast<PipelineStateD3D11Impl>(m_pPSO);
    auto NumShaders = m_ResourceLayouts.size();
    for (Uint32 s = 0; s < NumShaders; ++s)
    {
        auto &Allocator = pPSOD3D11Impl->GetResourceCacheDataAllocator(s);
        m_BoundResourceCaches[s].Destroy(Allocator);
    }
}

IMPLEMENT_QUERY_INTERFACE( ShaderResourceBindingD3D11Impl, IID_ShaderResourceBindingD3D11, TBase )

void ShaderResourceBindingD3D11Impl::BindResources(Uint32 ShaderFlags, IResourceMapping *pResMapping, Uint32 Flags)
{
    VERIFY_EXPR( m_ResourceLayouts.size() <= _countof(m_BoundResourceCaches) );
    for(Uint32 ResLayoutInd = 0; ResLayoutInd < m_ResourceLayouts.size(); ++ResLayoutInd)
    {
        auto &ResLayout = m_ResourceLayouts[ResLayoutInd];
        if(ShaderFlags & ResLayout.GetShaderType())
        {
            ResLayout.BindResources(pResMapping, Flags, m_BoundResourceCaches[ResLayoutInd]);
        }
    }
}

void ShaderResourceBindingD3D11Impl::BindStaticShaderResources()
{
    if (!m_bIsStaticResourcesBound)
    {
        auto *pPSOD3D11 = ValidatedCast<PipelineStateD3D11Impl>(GetPipelineState());
        auto ppShaders = pPSOD3D11->GetShaders();
        auto NumShaders = pPSOD3D11->GetNumShaders();
        VERIFY_EXPR(NumShaders == m_ResourceLayouts.size());

        for (Uint32 shader = 0; shader < NumShaders; ++shader)
        {
            auto *pShaderD3D11 = ValidatedCast<ShaderD3D11Impl>( ppShaders[shader] );
#ifdef VERIFY_SHADER_BINDINGS
            pShaderD3D11->GetStaticResourceLayout().dbgVerifyBindings();
#endif

#ifdef _DEBUG
            auto ShaderTypeInd = pShaderD3D11->GetShaderTypeIndex();
            auto ResourceLayoutInd = m_ResourceLayoutIndex[ShaderTypeInd];
            VERIFY_EXPR(ResourceLayoutInd == static_cast<Int8>(shader) );
#endif
            pShaderD3D11->GetStaticResourceLayout().CopyResources( m_BoundResourceCaches[shader] );
        }

        m_bIsStaticResourcesBound = true;
    }
}

IShaderVariable *ShaderResourceBindingD3D11Impl::GetVariable(SHADER_TYPE ShaderType, const char *Name)
{
    auto Ind = GetShaderTypeIndex(ShaderType);
    VERIFY_EXPR(Ind >= 0 && Ind < _countof(m_ResourceLayoutIndex));
    auto ResLayoutIndex = m_ResourceLayoutIndex[Ind];
    if( Ind >= 0 )
        return m_ResourceLayouts[ResLayoutIndex].GetShaderVariable(Name);
    else
    {
        LOG_ERROR_MESSAGE("Shader type ", GetShaderTypeLiteralName(ShaderType)," is not active in the resource binding");
        return nullptr;
    }
}

}
