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

#include <d3dcompiler.h>

#include "ShaderResourceLayoutD3D11.h"
#include "ShaderResourceCacheD3D11.h"
#include "BufferD3D11Impl.h"
#include "BufferViewD3D11Impl.h"
#include "TextureBaseD3D11.h"
#include "TextureViewD3D11.h"
#include "SamplerD3D11Impl.h"
#include "D3DShaderResourceLoader.h"
#include "ShaderD3D11Impl.h"

namespace Diligent
{

ShaderResourceLayoutD3D11::ShaderResourceLayoutD3D11(IObject *pOwner) : 
    m_pOwner(pOwner),
    m_DummyShaderVar(pOwner),
    m_ShaderType(SHADER_TYPE_UNKNOWN),
    m_ShaderTypeIndex(-1),
    m_ConstantBuffers(STD_ALLOCATOR_RAW_MEM(ConstBuffBindInfo, GetRawAllocator(), "Allocator for vector<ConstBuffBindInfo>")),
    m_TexAndSamplers(STD_ALLOCATOR_RAW_MEM(TexAndSamplerBindInfo, GetRawAllocator(), "Allocator for vector<TexAndSamplerBindInfo>")),
    m_TexUAVs(STD_ALLOCATOR_RAW_MEM(TexUAVBindInfo, GetRawAllocator(), "Allocator for vector<TexUAVBindInfo>")),
    m_BuffUAVs(STD_ALLOCATOR_RAW_MEM(BuffUAVBindInfo, GetRawAllocator(), "Allocator for vector<BuffUAVBindInfo>")),
    m_BuffSRVs(STD_ALLOCATOR_RAW_MEM(BuffSRVBindInfo, GetRawAllocator(), "Allocator for vector<BuffSRVBindInfo>")),
    m_VariableHash(STD_ALLOCATOR_RAW_MEM(VariableHashData, GetRawAllocator(), "Allocator for vector<BuffSRVBindInfo>"))
{
    VERIFY(m_pOwner != nullptr, "Owner must not be null");
}

void ShaderResourceLayoutD3D11::ParseBytecode(ID3DBlob *pShaderBytecode, const ShaderDesc &ShdrDesc)
{
    m_ShaderName = ShdrDesc.Name;
    m_ShaderType = ShdrDesc.ShaderType;
    m_ShaderTypeIndex = Diligent::GetShaderTypeIndex(ShdrDesc.ShaderType);

    LoadD3DShaderResources<D3D11_SHADER_DESC, D3D11_SHADER_INPUT_BIND_DESC, ID3D11ShaderReflection>(
        pShaderBytecode,
        
        [&](const D3DShaderResourceAttribs& CBAttribs)
        {
#ifdef _DEBUG
            std::for_each( m_ConstantBuffers.begin(), m_ConstantBuffers.end(), 
                            [&]( const ConstBuffBindInfo &CBInfo )
                            {
                                VERIFY( CBInfo.Name != CBAttribs.Name, "Constant buffer with the same name already exists" );
                            }
            );
#endif
            m_ConstantBuffers.emplace_back( ConstBuffBindInfo(m_pOwner, CBAttribs, this) );
            m_MaxCBBindPoint = std::max(m_MaxCBBindPoint, static_cast<Int32>(CBAttribs.BindPoint));
        },

        [&](const D3DShaderResourceAttribs& TexAttribs, const D3DShaderResourceAttribs& SamplerAttribs)
        {
            m_TexAndSamplers.emplace_back( TexAndSamplerBindInfo(m_pOwner, TexAttribs, SamplerAttribs, this) );
            m_MaxSRVBindPoint = std::max(m_MaxSRVBindPoint, static_cast<Int32>(TexAttribs.BindPoint));
            if(SamplerAttribs.BindPoint != InvalidBindPoint)
                m_MaxSamplerBindPoint = std::max(m_MaxSamplerBindPoint, static_cast<Int32>(SamplerAttribs.BindPoint));
        },

        [&](const D3DShaderResourceAttribs& TexUAV)
        {
            m_TexUAVs.push_back( TexUAVBindInfo(m_pOwner, TexUAV, this) );
            m_MaxUAVBindPoint = std::max(m_MaxUAVBindPoint, static_cast<Int32>(TexUAV.BindPoint));
        },

        [&](const D3DShaderResourceAttribs& BuffUAV)
        {
            m_BuffUAVs.push_back( BuffUAVBindInfo(m_pOwner, BuffUAV, this) );
            m_MaxUAVBindPoint = std::max(m_MaxUAVBindPoint, static_cast<Int32>(BuffUAV.BindPoint));
        },

        [&](const D3DShaderResourceAttribs& BuffSRV)
        {
            m_BuffSRVs.emplace_back( BuffSRVBindInfo(m_pOwner, BuffSRV, this) );
            m_MaxSRVBindPoint = std::max(m_MaxSRVBindPoint, static_cast<Int32>(BuffSRV.BindPoint));
        },

        ShdrDesc,
        ShaderD3DBase::m_SamplerSuffix.c_str());
}


static bool CheckType(SHADER_VARIABLE_TYPE Type, SHADER_VARIABLE_TYPE* AllowedTypes, Uint32 NumAllowedTypes)
{
    for(Uint32 i=0; i < NumAllowedTypes; ++i)
        if(Type == AllowedTypes[i])
            return true;
    
    return false;
}

void ShaderResourceLayoutD3D11::CloneLayout(const ShaderResourceLayoutD3D11& SrcLayout, SHADER_VARIABLE_TYPE *VarTypes, Uint32 NumVarTypes, ShaderResourceCacheD3D11 &ResourceCache, IMemoryAllocator& Allocator)
{
    m_pResourceCache = &ResourceCache;

    m_ShaderName = SrcLayout.m_ShaderName;
    m_ShaderType = SrcLayout.m_ShaderType;
    m_ShaderTypeIndex = SrcLayout.m_ShaderTypeIndex;

    m_MaxCBBindPoint = -1;
    m_MaxSRVBindPoint = -1;
    m_MaxSamplerBindPoint = -1;
    m_MaxUAVBindPoint = -1;
    for( auto cb = SrcLayout.m_ConstantBuffers.begin(); cb != SrcLayout.m_ConstantBuffers.end(); ++cb )
        if(CheckType(cb->VariableType, VarTypes, NumVarTypes))
        {
            m_ConstantBuffers.emplace_back( ConstBuffBindInfo(m_pOwner, *cb, this) );
            m_MaxCBBindPoint = std::max(m_MaxCBBindPoint, static_cast<int>(cb->BindPoint));
        }

    for( auto ts = SrcLayout.m_TexAndSamplers.begin(); ts != SrcLayout.m_TexAndSamplers.end(); ++ts )
        if(CheckType(ts->VariableType, VarTypes, NumVarTypes))
        {
            m_TexAndSamplers.emplace_back( TexAndSamplerBindInfo(m_pOwner, *ts, ts->SamplerAttribs, this) );
            m_MaxSRVBindPoint = std::max(m_MaxSRVBindPoint, static_cast<int>(ts->BindPoint));
            if( ts->SamplerAttribs.BindPoint != InvalidBindPoint )
                m_MaxSamplerBindPoint = std::max(m_MaxSamplerBindPoint, static_cast<int>(ts->SamplerAttribs.BindPoint));
        }

    for( auto uav = SrcLayout.m_TexUAVs.begin(); uav != SrcLayout.m_TexUAVs.end(); ++uav )
        if(CheckType(uav->VariableType, VarTypes, NumVarTypes))
        {
            m_TexUAVs.emplace_back( TexUAVBindInfo(m_pOwner, *uav, this) );
            m_MaxUAVBindPoint = std::max(m_MaxUAVBindPoint, static_cast<int>(uav->BindPoint));
        }

    for( auto uav = SrcLayout.m_BuffUAVs.begin(); uav != SrcLayout.m_BuffUAVs.end(); ++uav )
        if(CheckType(uav->VariableType, VarTypes, NumVarTypes))
        {
            m_BuffUAVs.emplace_back( BuffUAVBindInfo(m_pOwner, *uav, this) );
            m_MaxUAVBindPoint = std::max(m_MaxUAVBindPoint, static_cast<int>(uav->BindPoint));
        }

    for( auto srv = SrcLayout.m_BuffSRVs.begin(); srv != SrcLayout.m_BuffSRVs.end(); ++srv )
        if(CheckType(srv->VariableType, VarTypes, NumVarTypes))
        {
            m_BuffSRVs.emplace_back( BuffSRVBindInfo(m_pOwner, *srv, this) );
            m_MaxSRVBindPoint = std::max(m_MaxSRVBindPoint, static_cast<int>(srv->BindPoint));
        }

    if (!m_pResourceCache->IsInitialized())
    {
        m_pResourceCache->Initialize(*this, Allocator);
    }

    InitVariablesHashMap();
}

void ShaderResourceLayoutD3D11::CopyResources(ShaderResourceCacheD3D11 &DstCache)
{
    VERIFY(m_pResourceCache, "Resource cache must not be null");

    VERIFY( DstCache.GetCBCount() >= m_pResourceCache->GetCBCount(), "Dst cache is not large enough to contain all CBs" )
    VERIFY( DstCache.GetSRVCount() >= m_pResourceCache->GetSRVCount(), "Dst cache is not large enough to contain all SRVs" )
    VERIFY( DstCache.GetSamplerCount() >= m_pResourceCache->GetSamplerCount(), "Dst cache is not large enough to contain all samplers" )
    VERIFY( DstCache.GetUAVCount() >= m_pResourceCache->GetUAVCount(), "Dst cache is not large enough to contain all UAVs" )

    ShaderResourceCacheD3D11::CachedCB* CachedCBs = nullptr;
    ID3D11Buffer** d3d11CBs = nullptr;
    ShaderResourceCacheD3D11::CachedResource* CachedSRVResources = nullptr;
    ID3D11ShaderResourceView** d3d11SRVs = nullptr;
    ShaderResourceCacheD3D11::CachedSampler* CachedSamplers = nullptr;
    ID3D11SamplerState** d3d11Samplers = nullptr;
    ShaderResourceCacheD3D11::CachedResource* CachedUAVResources = nullptr;
    ID3D11UnorderedAccessView** d3d11UAVs = nullptr;
    m_pResourceCache->GetResourceArrays(CachedCBs, d3d11CBs, CachedSRVResources, d3d11SRVs, CachedSamplers, d3d11Samplers, CachedUAVResources, d3d11UAVs);


    ShaderResourceCacheD3D11::CachedCB* DstCBs = nullptr;
    ID3D11Buffer** DstD3D11CBs = nullptr;
    ShaderResourceCacheD3D11::CachedResource* DstSRVResources = nullptr;
    ID3D11ShaderResourceView** DstD3D11SRVs = nullptr;
    ShaderResourceCacheD3D11::CachedSampler* DstSamplers = nullptr;
    ID3D11SamplerState** DstD3D11Samplers = nullptr;
    ShaderResourceCacheD3D11::CachedResource* DstUAVResources = nullptr;
    ID3D11UnorderedAccessView** DstD3D11UAVs = nullptr;
    DstCache.GetResourceArrays(DstCBs, DstD3D11CBs, DstSRVResources, DstD3D11SRVs, DstSamplers, DstD3D11Samplers, DstUAVResources, DstD3D11UAVs);

    for( auto cb = m_ConstantBuffers.begin(); cb != m_ConstantBuffers.end(); ++cb )
    {
        auto CBSlot = cb->BindPoint;
        VERIFY_EXPR(CBSlot < m_pResourceCache->GetCBCount() && CBSlot < DstCache.GetCBCount());
        DstCBs[CBSlot] = CachedCBs[CBSlot];
        DstD3D11CBs[CBSlot] = d3d11CBs[CBSlot];
    }

    for( auto ts = m_TexAndSamplers.begin(); ts != m_TexAndSamplers.end(); ++ts )
    {
        auto SRVSlot = ts->BindPoint;
        VERIFY_EXPR(SRVSlot < m_pResourceCache->GetSRVCount() && SRVSlot < DstCache.GetSRVCount());
        DstSRVResources[SRVSlot] = CachedSRVResources[SRVSlot];
        DstD3D11SRVs[SRVSlot] = d3d11SRVs[SRVSlot];
        if( ts->SamplerAttribs.BindPoint != InvalidBindPoint )
        {
            auto SamplerSlot = ts->SamplerAttribs.BindPoint;
            VERIFY_EXPR( SamplerSlot < m_pResourceCache->GetSamplerCount() && SamplerSlot < DstCache.GetSamplerCount() );
            DstSamplers[SamplerSlot] = CachedSamplers[SamplerSlot];
            DstD3D11Samplers[SamplerSlot] = d3d11Samplers[SamplerSlot];
        }
    }

    for( auto uav = m_TexUAVs.begin(); uav != m_TexUAVs.end(); ++uav )
    {
        auto UAVSlot = uav->BindPoint;
        VERIFY_EXPR(UAVSlot < m_pResourceCache->GetUAVCount() && UAVSlot < DstCache.GetUAVCount());
        DstUAVResources[UAVSlot] = CachedUAVResources[UAVSlot];
        DstD3D11UAVs[UAVSlot] = d3d11UAVs[UAVSlot];
    }

    for( auto uav = m_BuffUAVs.begin(); uav != m_BuffUAVs.end(); ++uav )
    {
        auto UAVSlot = uav->BindPoint;
        VERIFY_EXPR(UAVSlot < m_pResourceCache->GetUAVCount() && UAVSlot < DstCache.GetUAVCount());
        DstUAVResources[UAVSlot] = CachedUAVResources[UAVSlot];
        DstD3D11UAVs[UAVSlot] = d3d11UAVs[UAVSlot];
    }

    for( auto srv = m_BuffSRVs.begin(); srv != m_BuffSRVs.end(); ++srv )
    {
        auto SRVSlot = srv->BindPoint;
        VERIFY_EXPR(SRVSlot < m_pResourceCache->GetSRVCount() && SRVSlot < DstCache.GetSRVCount());
        DstSRVResources[SRVSlot] = CachedSRVResources[SRVSlot];
        DstD3D11SRVs[SRVSlot] = d3d11SRVs[SRVSlot];
    }
}

void ShaderResourceLayoutD3D11::InitVariablesHashMap()
{
    // After all resources are loaded, we can populate shader variable hash map.
    // The map contains raw pointers, but none of the arrays will ever change.
#define STORE_SHADER_VARIABLES(ResArr)\
    {                                                                       \
        for( auto it = ResArr.begin(); it != ResArr.end(); ++it )           \
            /* HashMapStringKey will make a copy of the string*/            \
            m_VariableHash.insert( std::make_pair( Diligent::HashMapStringKey(it->Name), &*it ) ); \
    }

    STORE_SHADER_VARIABLES(m_ConstantBuffers)
    STORE_SHADER_VARIABLES(m_TexAndSamplers)
    STORE_SHADER_VARIABLES(m_TexUAVs)
    STORE_SHADER_VARIABLES(m_BuffUAVs)
    STORE_SHADER_VARIABLES(m_BuffSRVs)
#undef STORE_SHADER_VARIABLES
}

#define LOG_RESOURCE_BINDING_ERROR(ResType, pResource, VarName, ShaderName, ...)\
{                                                                                                   \
    const auto &ResName = pResource->GetDesc().Name;                                                \
    LOG_ERROR_MESSAGE( "Failed to bind ", ResType, " \"", ResName, "\" to variable \"", VarName,    \
                        "\" in shader \"", ShaderName, "\". ", __VA_ARGS__ );                \
}

void ShaderResourceLayoutD3D11::ConstBuffBindInfo::BindResource(IDeviceObject *pBuffer, const ShaderResourceLayoutD3D11 *dbgResLayout)
{
    VERIFY(m_pParentResLayout, "Parent resource layout is null. This should not be possible as the pointer is initialized in the constructor and must not be null");
    auto &pResourceCache = m_pParentResLayout->m_pResourceCache;
    VERIFY(pResourceCache, "Resource cache is null");
    VERIFY(dbgResLayout == nullptr || pResourceCache == dbgResLayout->m_pResourceCache, "Invalid resource cache");

    RefCntAutoPtr<BufferD3D11Impl> pBuffD3D11Impl;
    if( pBuffer )
    {
        // We cannot use ValidatedCast<> here as the resource retrieved from the
        // resource mapping can be of wrong type
        IBufferD3D11 *pBuffD3D11 = nullptr;
        pBuffer->QueryInterface(IID_BufferD3D11, reinterpret_cast<IObject**>(&pBuffD3D11));
        if( pBuffD3D11 )
        {
            pBuffD3D11Impl.Attach(ValidatedCast<BufferD3D11Impl>(pBuffD3D11));
            if( !(pBuffD3D11Impl->GetDesc().BindFlags & BIND_UNIFORM_BUFFER) )
            {
                pBuffD3D11Impl.Release();
                LOG_RESOURCE_BINDING_ERROR("buffer", pBuffer, Name, m_pParentResLayout->m_ShaderName, "Buffer was not created with BIND_UNIFORM_BUFFER flag.")
            }
        }
        else
        {
            LOG_RESOURCE_BINDING_ERROR("buffer", pBuffer, Name, m_pParentResLayout->m_ShaderName, "Incorrect resource type: buffer is expected.")
        }
    }

    pResourceCache->SetCB(BindPoint, std::move(pBuffD3D11Impl) );
}

bool ShaderResourceLayoutD3D11::ConstBuffBindInfo::IsBound()
{
    VERIFY(m_pParentResLayout, "Parent resource layout is null. This should not be possible as the pointer is initialized in the constructor and must not be null");
    auto &pResourceCache = m_pParentResLayout->m_pResourceCache;
    VERIFY(pResourceCache, "Resource cache is null");

    return pResourceCache->IsCBBound(BindPoint);
}



#ifdef VERIFY_SHADER_BINDINGS
template<typename TResourceViewType,        ///< Type of the view (ITextureViewD3D11 or IBufferViewD3D11)
         typename TViewTypeEnum>            ///< Type of the expected view enum
bool dbgVerifyViewType( const char *ViewTypeName,
                        TResourceViewType pViewD3D11,
                        const String& VarName, 
                        TViewTypeEnum dbgExpectedViewType,
                        const String &ShaderName )
{
    const auto& ViewDesc = pViewD3D11->GetDesc();
    auto ViewType = ViewDesc.ViewType;
    if (ViewType == dbgExpectedViewType)
    {
        return true;
    }
    else
    {
        const auto *ExpectedViewTypeName = GetViewTypeLiteralName( dbgExpectedViewType );
        const auto *ActualViewTypeName = GetViewTypeLiteralName( ViewType );
        LOG_RESOURCE_BINDING_ERROR(ViewTypeName, pViewD3D11, VarName, ShaderName, 
                                   "Incorrect view type: ", ExpectedViewTypeName, " is expected, ", ActualViewTypeName, " provided." );
        return false;
    }
}
#endif

void ShaderResourceLayoutD3D11::TexAndSamplerBindInfo::BindResource( IDeviceObject *pView, const ShaderResourceLayoutD3D11 *dbgResLayout )
{
    VERIFY(m_pParentResLayout, "Parent resource layout is null. This should not be possible as the pointer is initialized in the constructor and must not be null");
    auto &pResourceCache = m_pParentResLayout->m_pResourceCache;
    VERIFY(pResourceCache, "Resource cache is null");
    VERIFY(dbgResLayout == nullptr || pResourceCache == dbgResLayout->m_pResourceCache, "Invalid resource cache");

    // We cannot use ValidatedCast<> here as the resource retrieved from the
    // resource mapping can be of wrong type
    RefCntAutoPtr<TextureViewD3D11Impl> pViewD3D11(pView, IID_TextureViewD3D11);
#ifdef VERIFY_SHADER_BINDINGS
    if(pView && !pViewD3D11)
        LOG_RESOURCE_BINDING_ERROR("resource", pView, Name, "", "Incorect resource type: texture view is expected.")
    if(pViewD3D11 && !dbgVerifyViewType("texture view", pViewD3D11.RawPtr(), Name, TEXTURE_VIEW_SHADER_RESOURCE, m_pParentResLayout->m_ShaderName))
        pViewD3D11.Release();
#endif
    
    if( SamplerAttribs.BindPoint != InvalidBindPoint )
    {
        SamplerD3D11Impl *pSamplerD3D11Impl = nullptr;
        if( pViewD3D11 )
        {
            pSamplerD3D11Impl = ValidatedCast<SamplerD3D11Impl>(pViewD3D11->GetSampler());
#ifdef VERIFY_SHADER_BINDINGS
            if(pSamplerD3D11Impl==nullptr)
                LOG_ERROR_MESSAGE( "Failed to bind sampler to variable \"", SamplerAttribs.Name, ". Sampler is not set in the texture view \"", pViewD3D11->GetDesc().Name, "\"" );
#endif
        }
        pResourceCache->SetSampler(SamplerAttribs.BindPoint, pSamplerD3D11Impl);
    }          
        
    pResourceCache->SetTexSRV(BindPoint, std::move(pViewD3D11));
}


void ShaderResourceLayoutD3D11::BuffSRVBindInfo::BindResource( IDeviceObject *pView, const ShaderResourceLayoutD3D11 *dbgResLayout )
{
    VERIFY(m_pParentResLayout, "Parent resource layout is null. This should not be possible as the pointer is initialized in the constructor and must not be null");
    auto &pResourceCache = m_pParentResLayout->m_pResourceCache;
    VERIFY(pResourceCache, "Resource cache is null");
    VERIFY(dbgResLayout == nullptr || pResourceCache == dbgResLayout->m_pResourceCache, "Invalid resource cache");

    // We cannot use ValidatedCast<> here as the resource retrieved from the
    // resource mapping can be of wrong type
    RefCntAutoPtr<BufferViewD3D11Impl> pViewD3D11(pView, IID_BufferViewD3D11);
#ifdef VERIFY_SHADER_BINDINGS
    if(pView && !pViewD3D11)
        LOG_RESOURCE_BINDING_ERROR("resource", pView, Name, "", "Incorect resource type: buffer view is expected.")
    if(pViewD3D11 && !dbgVerifyViewType("buffer view", pViewD3D11.RawPtr(), Name, BUFFER_VIEW_SHADER_RESOURCE, m_pParentResLayout->m_ShaderName))
        pViewD3D11.Release();
#endif

    pResourceCache->SetBufSRV(BindPoint, std::move(pViewD3D11));
}



void ShaderResourceLayoutD3D11::TexUAVBindInfo::BindResource( IDeviceObject *pView, const ShaderResourceLayoutD3D11 *dbgResLayout )
{
    VERIFY(m_pParentResLayout, "Parent resource layout is null. This should not be possible as the pointer is initialized in the constructor and must not be null");
    auto &pResourceCache = m_pParentResLayout->m_pResourceCache;
    VERIFY(pResourceCache, "Resource cache is null");
    VERIFY(dbgResLayout == nullptr || pResourceCache == dbgResLayout->m_pResourceCache, "Invalid resource cache");

    // We cannot use ValidatedCast<> here as the resource retrieved from the
    // resource mapping can be of wrong type
    RefCntAutoPtr<TextureViewD3D11Impl> pViewD3D11(pView, IID_TextureViewD3D11);
#ifdef VERIFY_SHADER_BINDINGS
    if(pView && !pViewD3D11)
        LOG_RESOURCE_BINDING_ERROR("resource", pView, Name, "", "Incorect resource type: texture view is expected.")
    if(pViewD3D11 && !dbgVerifyViewType("texture view", pViewD3D11.RawPtr(), Name, TEXTURE_VIEW_UNORDERED_ACCESS, m_pParentResLayout->m_ShaderName))
        pViewD3D11.Release();
#endif

    pResourceCache->SetTexUAV(BindPoint, std::move(pViewD3D11));
}

void ShaderResourceLayoutD3D11::BuffUAVBindInfo::BindResource( IDeviceObject *pView, const ShaderResourceLayoutD3D11 *dbgResLayout )
{
    VERIFY(m_pParentResLayout, "Parent resource layout is null. This should not be possible as the pointer is initialized in the constructor and must not be null");
    auto &pResourceCache = m_pParentResLayout->m_pResourceCache;
    VERIFY(pResourceCache, "Resource cache is null");
    VERIFY(dbgResLayout == nullptr || pResourceCache == dbgResLayout->m_pResourceCache, "Invalid resource cache");

    // We cannot use ValidatedCast<> here as the resource retrieved from the
    // resource mapping can be of wrong type
    RefCntAutoPtr<BufferViewD3D11Impl> pViewD3D11(pView, IID_BufferViewD3D11);
#ifdef VERIFY_SHADER_BINDINGS
    if(pView && !pViewD3D11)
        LOG_RESOURCE_BINDING_ERROR("resource", pView, Name, "", "Incorect resource type: buffer view is expected.")
    if(pViewD3D11 && !dbgVerifyViewType("buffer view", pViewD3D11.RawPtr(), Name, BUFFER_VIEW_UNORDERED_ACCESS, m_pParentResLayout->m_ShaderName) )
        pViewD3D11.Release();
#endif

    pResourceCache->SetBufUAV(BindPoint, std::move(pViewD3D11));
}

bool ShaderResourceLayoutD3D11::TexAndSamplerBindInfo::IsBound()
{
    VERIFY(m_pParentResLayout, "Parent resource layout is null. This should not be possible as the pointer is initialized in the constructor and must not be null");
    auto &pResourceCache = m_pParentResLayout->m_pResourceCache;
    VERIFY(pResourceCache, "Resource cache is null");

    return pResourceCache->IsSRVBound(BindPoint, true);
}


bool ShaderResourceLayoutD3D11::BuffSRVBindInfo::IsBound()
{
    VERIFY(m_pParentResLayout, "Parent resource layout is null. This should not be possible as the pointer is initialized in the constructor and must not be null");
    auto &pResourceCache = m_pParentResLayout->m_pResourceCache;
    VERIFY(pResourceCache, "Resource cache is null");

    return pResourceCache->IsSRVBound(BindPoint, false);
}

bool ShaderResourceLayoutD3D11::TexUAVBindInfo::IsBound()
{
    VERIFY(m_pParentResLayout, "Parent resource layout is null. This should not be possible as the pointer is initialized in the constructor and must not be null");
    auto &pResourceCache = m_pParentResLayout->m_pResourceCache;
    VERIFY(pResourceCache, "Resource cache is null");

    return pResourceCache->IsUAVBound(BindPoint, true);
}

bool ShaderResourceLayoutD3D11::BuffUAVBindInfo::IsBound()
{
    VERIFY(m_pParentResLayout, "Parent resource layout is null. This should not be possible as the pointer is initialized in the constructor and must not be null");
    auto &pResourceCache = m_pParentResLayout->m_pResourceCache;
    VERIFY(pResourceCache, "Resource cache is null");

    return pResourceCache->IsUAVBound(BindPoint, false);
}



// Helper template class that facilitates binding CBs, SRVs, and UAVs
class BindResourceHelper
{
public:
    BindResourceHelper(IResourceMapping *pRM, Uint32 Fl, const ShaderResourceLayoutD3D11 *pSRL) :
        pResourceMapping(pRM),
        Flags(Fl),
        pShaderResLayout(pSRL)
    {
        VERIFY(pResourceMapping != nullptr, "Resource mapping is null");
        VERIFY(pSRL != nullptr, "Shader resource layout is null");
    }

    template<typename ResourceArrayType>
    void Bind( ResourceArrayType &ResourceArray)
    {
        for(auto ResIt = ResourceArray.begin(); ResIt != ResourceArray.end(); ++ResIt)
        {
            if( Flags & BIND_SHADER_RESOURCES_RESET_BINDINGS )
                ResIt->BindResource(nullptr, pShaderResLayout);

            if( (Flags & BIND_SHADER_RESOURCES_UPDATE_UNRESOLVED) && ResIt->IsBound() )
                continue;

            const auto& VarName = ResIt->Name;
            RefCntAutoPtr<IDeviceObject> pRes;
            VERIFY_EXPR(pResourceMapping != nullptr);
            pResourceMapping->GetResource( VarName.c_str(), &pRes );
            if( pRes )
            {
                //  Call non-virtual function
                ResIt->BindResource(pRes, pShaderResLayout);
            }
            else
            {
                if( (Flags & BIND_SHADER_RESOURCES_ALL_RESOLVED) && !ResIt->IsBound() )
                    LOG_ERROR_MESSAGE( "Cannot bind resource to shader variable \"", VarName, "\": resource view not found in the resource mapping" )
            }
        }
    }

private:
    IResourceMapping* const pResourceMapping;
    const Uint32 Flags;
    const ShaderResourceLayoutD3D11 *pShaderResLayout;
};

void ShaderResourceLayoutD3D11::BindResources( IResourceMapping* pResourceMapping, Uint32 Flags, const ShaderResourceCacheD3D11 &dbgResourceCache )
{
    VERIFY(&dbgResourceCache == m_pResourceCache, "Resource cache does not match the cache provided at initialization");

    if( !pResourceMapping )
    {
        LOG_ERROR_MESSAGE( "Failed to bind resources in shader \"", m_ShaderName, "\": resource mapping is null" );
        return;
    }

    BindResourceHelper BindResHelper(pResourceMapping, Flags, this);

    // Bind constant buffers
    BindResHelper.Bind(m_ConstantBuffers);
    
    // Bind textures and samplers
    BindResHelper.Bind(m_TexAndSamplers);
    
    // Bind buffer SRVs
    BindResHelper.Bind(m_BuffSRVs);
    
    // Bind texture UAVs
    BindResHelper.Bind(m_TexUAVs);
    
    // Bind buffer UAVs
    BindResHelper.Bind(m_BuffUAVs);
}

IShaderVariable* ShaderResourceLayoutD3D11::GetShaderVariable(const Char* Name)
{
    // Name will be implicitly converted to HashMapStringKey without making a copy
    auto it = m_VariableHash.find( Name );
    if( it == m_VariableHash.end() )
    {
        LOG_ERROR_MESSAGE( "Shader variable \"", Name, "\" is not found in shader \"", m_ShaderName, "\". Attempts to set the variable will be silently ignored." );
        return &m_DummyShaderVar;
    }
    return it->second;
}


#ifdef VERIFY_SHADER_BINDINGS
void ShaderResourceLayoutD3D11::dbgVerifyBindings()const
{

#define LOG_MISSING_BINDING(VarType, Name)\
    LOG_ERROR_MESSAGE( "No resource is bound to ", VarType, " variable \"", Name, "\" in shader \"", m_ShaderName, "\"" );

    m_pResourceCache->dbgVerifyCacheConsistency();

    for( auto cb = m_ConstantBuffers.begin(); cb != m_ConstantBuffers.end(); ++cb )
    {
        if( !m_pResourceCache->IsCBBound(cb->BindPoint) )
            LOG_MISSING_BINDING("constant buffer", cb->Name)
    }

    for( auto tex = m_TexAndSamplers.begin(); tex != m_TexAndSamplers.end(); ++tex )
    {
        if( !m_pResourceCache->IsSRVBound(tex->BindPoint, true) )
            LOG_MISSING_BINDING("texture", tex->Name)

        if( tex->SamplerAttribs.BindPoint != InvalidBindPoint && !m_pResourceCache->IsSamplerBound(tex->SamplerAttribs.BindPoint) )
            LOG_MISSING_BINDING("sampler", tex->SamplerAttribs.Name)
    }

    for( auto buf = m_BuffSRVs.begin(); buf != m_BuffSRVs.end(); ++buf )
    {
        if( !m_pResourceCache->IsSRVBound(buf->BindPoint, false) )
            LOG_MISSING_BINDING("buffer", buf->Name)
    }

    for( auto uav = m_TexUAVs.begin(); uav != m_TexUAVs.end(); ++uav )
    {
        if( !m_pResourceCache->IsUAVBound(uav->BindPoint, true) )
            LOG_MISSING_BINDING("texture UAV", uav->Name)
    }

    for( auto uav = m_BuffUAVs.begin(); uav != m_BuffUAVs.end(); ++uav )
    {
        if( !m_pResourceCache->IsUAVBound(uav->BindPoint, false) )
            LOG_MISSING_BINDING("buffer UAV", uav->Name)
    }
#undef LOG_MISSING_BINDING
}


void ShaderResourceLayoutD3D11::dbgVerifyCommittedResources(ID3D11Buffer*              CommittedD3D11CBs[],
                                                            ID3D11ShaderResourceView*  CommittedD3D11SRVs[],
                                                            ID3D11Resource*            CommittedD3D11SRVResources[],
                                                            ID3D11SamplerState*        CommittedD3D11Samplers[],
                                                            ID3D11UnorderedAccessView* CommittedD3D11UAVs[],
                                                            ID3D11Resource*            CommittedD3D11UAVResources[],
                                                            ShaderResourceCacheD3D11 &ResourceCache)const
{
    ShaderResourceCacheD3D11::CachedCB* CachedCBs = nullptr;
    ID3D11Buffer** d3d11CBs = nullptr;
    ShaderResourceCacheD3D11::CachedResource* CachedSRVResources = nullptr;
    ID3D11ShaderResourceView** d3d11SRVs = nullptr;
    ShaderResourceCacheD3D11::CachedSampler* CachedSamplers = nullptr;
    ID3D11SamplerState** d3d11Samplers = nullptr;
    ShaderResourceCacheD3D11::CachedResource* CachedUAVResources = nullptr;
    ID3D11UnorderedAccessView** d3d11UAVs = nullptr;
    ResourceCache.GetResourceArrays(CachedCBs, d3d11CBs, CachedSRVResources, d3d11SRVs, CachedSamplers, d3d11Samplers, CachedUAVResources, d3d11UAVs);

    // Verify constant buffers
    for( auto cb = m_ConstantBuffers.begin(); cb != m_ConstantBuffers.end(); ++cb )
    {
        if (cb->BindPoint >= ResourceCache.GetCBCount())
        {
            LOG_ERROR_MESSAGE( "Unable to find constant buffer \"", cb->Name, "\" (slot ", cb->BindPoint, ") in the resource cache: the cache is initialized to hold ", ResourceCache.GetCBCount()," CB slots only. This should never happen and may be the result of using wrong resource cache." );
            continue;
        }
        auto &CB = CachedCBs[cb->BindPoint];
        if(CB.pBuff == nullptr)
        {
            LOG_ERROR_MESSAGE( "Constant buffer \"", cb->Name, "\" is not initialized in the resource cache." );
            continue;
        }

        if (!(CB.pBuff->GetDesc().BindFlags & BIND_UNIFORM_BUFFER))
        {
            LOG_ERROR_MESSAGE( "Buffer \"", CB.pBuff->GetDesc().Name, "\" committed in the device context as constant buffer to variable \"", cb->Name, "\" in shader \"", m_ShaderName, "\" does not have BIND_UNIFORM_BUFFER flag" );
            continue;
        }

        VERIFY_EXPR(d3d11CBs[cb->BindPoint] == CB.pBuff->GetD3D11Buffer());

        if(CommittedD3D11CBs[cb->BindPoint] == nullptr )
        {
            LOG_ERROR_MESSAGE( "No D3D11 resource committed to constant buffer \"", cb->Name, "\" (slot ", cb->BindPoint ,") in shader \"", m_ShaderName, "\"" );
            continue;
        }

        if(CommittedD3D11CBs[cb->BindPoint] != d3d11CBs[cb->BindPoint] )
        {
            LOG_ERROR_MESSAGE( "D3D11 resource committed to constant buffer \"", cb->Name, "\" (slot ", cb->BindPoint ,") in shader \"", m_ShaderName, "\" does not match the resource in the resource cache" );
            continue;
        }
    }


    // Verify texture SRVs and samplers
    for( auto tex = m_TexAndSamplers.begin(); tex != m_TexAndSamplers.end(); ++tex )
    {
        if (tex->BindPoint >= ResourceCache.GetSRVCount())
        {
            LOG_ERROR_MESSAGE( "Unable to find texture SRV \"", tex->Name, "\" (slot ", tex->BindPoint, ") in the resource cache: the cache is initialized to hold ", ResourceCache.GetSRVCount()," SRV slots only. This should never happen and may be the result of using wrong resource cache." );
            continue;
        }
        auto &SRVRes = CachedSRVResources[tex->BindPoint];
        if(SRVRes.pBuffer != nullptr)
        {
            LOG_ERROR_MESSAGE( "Unexpected buffer bound to variable \"", tex->Name, "\". Texture is expected." );
            continue;
        }
        if(SRVRes.pTexture == nullptr)
        {
            LOG_ERROR_MESSAGE( "Texture \"", tex->Name, "\" is not initialized in the resource cache." );
            continue;
        }

        if (!(SRVRes.pTexture->GetDesc().BindFlags & BIND_SHADER_RESOURCE))
        {
            LOG_ERROR_MESSAGE( "Texture \"", SRVRes.pTexture->GetDesc().Name, "\" committed in the device context as SRV to variable \"", tex->Name, "\" in shader \"", m_ShaderName, "\" does not have BIND_SHADER_RESOURCE flag" );
        }

        if(CommittedD3D11SRVs[tex->BindPoint] == nullptr )
        {
            LOG_ERROR_MESSAGE( "No D3D11 resource committed to texture SRV \"", tex->Name, "\" (slot ", tex->BindPoint ,") in shader \"", m_ShaderName, "\"" );
            continue;
        }

        if(CommittedD3D11SRVs[tex->BindPoint] != d3d11SRVs[tex->BindPoint] )
        {
            LOG_ERROR_MESSAGE( "D3D11 resource committed to texture SRV \"", tex->Name, "\" (slot ", tex->BindPoint ,") in shader \"", m_ShaderName, "\" does not match the resource in the resource cache" );
            continue;
        }
        
        const auto& SamAttribs = tex->SamplerAttribs;
        if( SamAttribs.BindPoint != InvalidBindPoint )
        {
            if (SamAttribs.BindPoint >= ResourceCache.GetSamplerCount())
            {
                LOG_ERROR_MESSAGE( "Unable to find sampler \"", SamAttribs.Name, "\" (slot ", SamAttribs.BindPoint, ") in the resource cache: the cache is initialized to hold ", ResourceCache.GetSamplerCount()," Sampler slots only. This should never happen and may be the result of using wrong resource cache." );
                continue;
            }
            auto &Sam = CachedSamplers[SamAttribs.BindPoint];
            if(Sam.pSampler == nullptr)
            {
                LOG_ERROR_MESSAGE( "Sampler \"", SamAttribs.Name, "\" is not initialized in the resource cache." );
                continue;
            }
            VERIFY_EXPR(d3d11Samplers[SamAttribs.BindPoint] == Sam.pSampler->GetD3D11SamplerState());

            if(CommittedD3D11Samplers[SamAttribs.BindPoint] == nullptr )
            {
                LOG_ERROR_MESSAGE( "No D3D11 sampler committed to variable \"", SamAttribs.Name, "\" (slot ", SamAttribs.BindPoint ,") in shader \"", m_ShaderName, "\"" );
                continue;
            }

            if(CommittedD3D11Samplers[SamAttribs.BindPoint] != d3d11Samplers[SamAttribs.BindPoint])
            {
                LOG_ERROR_MESSAGE( "D3D11 sampler committed to variable \"", SamAttribs.Name, "\" (slot ", SamAttribs.BindPoint ,") in shader \"", m_ShaderName, "\" does not match the resource in the resource cache" );
                continue;
            }
        }
    }


    // Verify buffer SRVs
    for( auto buf = m_BuffSRVs.begin(); buf != m_BuffSRVs.end(); ++buf )
    {
        if (buf->BindPoint >= ResourceCache.GetSRVCount())
        {
            LOG_ERROR_MESSAGE( "Unable to find buffer SRV \"", buf->Name, "\" (slot ", buf->BindPoint, ") in the resource cache: the cache is initialized to hold ", ResourceCache.GetSRVCount()," SRV slots only. This should never happen and may be the result of using wrong resource cache." );
            continue;
        }
        auto &SRVRes = CachedSRVResources[buf->BindPoint];
        if(SRVRes.pTexture != nullptr)
        {
            LOG_ERROR_MESSAGE( "Unexpected texture bound to variable \"", buf->Name, "\". Buffer is expected." );
            continue;
        }
        if(SRVRes.pBuffer == nullptr)
        {
            LOG_ERROR_MESSAGE( "Buffer \"", buf->Name, "\" is not initialized in the resource cache." );
            continue;
        }

        if (!(SRVRes.pBuffer->GetDesc().BindFlags & BIND_SHADER_RESOURCE))
        {
            LOG_ERROR_MESSAGE( "Buffer \"", SRVRes.pBuffer->GetDesc().Name, "\" committed in the device context as SRV to variable \"", buf->Name, "\" in shader \"", m_ShaderName, "\" does not have BIND_SHADER_RESOURCE flag" );
        }

        if(CommittedD3D11SRVs[buf->BindPoint] == nullptr )
        {
            LOG_ERROR_MESSAGE( "No D3D11 resource committed to buffer SRV \"", buf->Name, "\" (slot ", buf->BindPoint ,") in shader \"", m_ShaderName, "\"" );
            continue;
        }

        if(CommittedD3D11SRVs[buf->BindPoint] != d3d11SRVs[buf->BindPoint] )
        {
            LOG_ERROR_MESSAGE( "D3D11 resource committed to buffer SRV \"", buf->Name, "\" (slot ", buf->BindPoint ,") in shader \"", m_ShaderName, "\" does not match the resource in the resource cache" );
            continue;
        }
    }


    // Verify texture UAVs
    for( auto uav = m_TexUAVs.begin(); uav != m_TexUAVs.end(); ++uav )
    {
        if (uav->BindPoint >= ResourceCache.GetUAVCount())
        {
            LOG_ERROR_MESSAGE( "Unable to find texture UAV \"", uav->Name, "\" (slot ", uav->BindPoint, ") in the resource cache: the cache is initialized to hold ", ResourceCache.GetUAVCount()," UAV slots only. This should never happen and may be the result of using wrong resource cache." );
            continue;
        }
        auto &UAVRes = CachedUAVResources[uav->BindPoint];
        if(UAVRes.pBuffer != nullptr)
        {
            LOG_ERROR_MESSAGE( "Unexpected buffer bound to variable \"", uav->Name, "\". Texture is expected." );
            continue;
        }
        if(UAVRes.pTexture == nullptr)
        {
            LOG_ERROR_MESSAGE( "Texture \"", uav->Name, "\" is not initialized in the resource cache." );
            continue;
        }

        if (!(UAVRes.pTexture->GetDesc().BindFlags & BIND_UNORDERED_ACCESS))
        {
            LOG_ERROR_MESSAGE( "Texture \"", UAVRes.pTexture->GetDesc().Name, "\" committed in the device context as UAV to variable \"", uav->Name, "\" in shader \"", m_ShaderName, "\" does not have BIND_UNORDERED_ACCESS flag" );
        }

        if(CommittedD3D11UAVs[uav->BindPoint] == nullptr )
        {
            LOG_ERROR_MESSAGE( "No D3D11 resource committed to texture UAV \"", uav->Name, "\" (slot ", uav->BindPoint ,") in shader \"", m_ShaderName, "\"" );
            continue;
        }

        if(CommittedD3D11UAVs[uav->BindPoint] != d3d11UAVs[uav->BindPoint] )
        {
            LOG_ERROR_MESSAGE( "D3D11 resource committed to texture UAV \"", uav->Name, "\" (slot ", uav->BindPoint ,") in shader \"", m_ShaderName, "\" does not match the resource in the resource cache" );
            continue;
        }
    }


    // Verify buffer UAVs
    for( auto uav = m_BuffUAVs.begin(); uav != m_BuffUAVs.end(); ++uav )
    {
        if (uav->BindPoint >= ResourceCache.GetUAVCount())
        {
            LOG_ERROR_MESSAGE( "Unable to find buffer UAV \"", uav->Name, "\" (slot ", uav->BindPoint, ") in the resource cache: the cache is initialized to hold ", ResourceCache.GetUAVCount()," UAV slots only. This should never happen and may be the result of using wrong resource cache." );
            continue;
        }
        auto &UAVRes = CachedUAVResources[uav->BindPoint];
        if(UAVRes.pTexture != nullptr)
        {
            LOG_ERROR_MESSAGE( "Unexpected texture bound to variable \"", uav->Name, "\". Buffer is expected." );
            continue;
        }
        if(UAVRes.pBuffer == nullptr)
        {
            LOG_ERROR_MESSAGE( "Buffer UAV \"", uav->Name, "\" is not initialized in the resource cache." );
            continue;
        }

        if (!(UAVRes.pBuffer->GetDesc().BindFlags & BIND_UNORDERED_ACCESS))
        {
            LOG_ERROR_MESSAGE( "Buffer \"", UAVRes.pBuffer->GetDesc().Name, "\" committed in the device context as UAV to variable \"", uav->Name, "\" in shader \"", m_ShaderName, "\" does not have BIND_UNORDERED_ACCESS flag" );
        }

        if(CommittedD3D11UAVs[uav->BindPoint] == nullptr )
        {
            LOG_ERROR_MESSAGE( "No D3D11 resource committed to buffer UAV \"", uav->Name, "\" (slot ", uav->BindPoint ,") in shader \"", m_ShaderName, "\"" );
            continue;
        }

        if(CommittedD3D11UAVs[uav->BindPoint] != d3d11UAVs[uav->BindPoint] )
        {
            LOG_ERROR_MESSAGE( "D3D11 resource committed to buffer UAV \"", uav->Name, "\" (slot ", uav->BindPoint ,") in shader \"", m_ShaderName, "\" does not match the resource in the resource cache" );
            continue;
        }
    }
}

#endif
}
