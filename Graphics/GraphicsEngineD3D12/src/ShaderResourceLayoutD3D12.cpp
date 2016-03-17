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

#include "ShaderResourceLayoutD3D12.h"
#include "ShaderResourceCacheD3D12.h"
#include "BufferD3D12Impl.h"
#include "BufferViewD3D12.h"
#include "TextureD3D12Impl.h"
#include "TextureViewD3D12Impl.h"
#include "SamplerD3D12Impl.h"
#include "D3DShaderResourceLoader.h"
#include "ShaderD3D12Impl.h"
#include "RootSignature.h"

namespace Diligent
{
 
ShaderResourceLayoutD3D12::ShaderResourceLayoutD3D12(IObject *pOwner) : 
    m_pOwner(pOwner),
    m_DummyShaderVar(pOwner),
    m_ShaderType(SHADER_TYPE_UNKNOWN)
{
    VERIFY(m_pOwner != nullptr, "Owner must not be null");
}


void ShaderResourceLayoutD3D12::ParseBytecode(ID3DBlob *pShaderBytecode, const ShaderDesc &ShdrDesc)
{
    m_ShaderName = ShdrDesc.Name;
    m_ShaderType = ShdrDesc.ShaderType;

    LoadD3DShaderResources<D3D12_SHADER_DESC, D3D12_SHADER_INPUT_BIND_DESC, ID3D12ShaderReflection>(
        pShaderBytecode,
        
        [&](const D3DShaderResourceAttribs& CBAttribs)
        {
            m_CbvSrvUav[CBAttribs.VariableType].emplace_back( SRV_CBV_UAV(m_pOwner, CBAttribs, this, CachedResourceType::CBV) );
        },

        [&](const D3DShaderResourceAttribs& TexAttribs, const D3DShaderResourceAttribs& SamplerAttribs)
        {
            Int32 SamplerId = InvalidSamplerId;
            if( SamplerAttribs.BindPoint != InvalidBindPoint )
            {
                VERIFY(SamplerAttribs.VariableType == TexAttribs.VariableType, "Inconsistent texture and sampler variable types" );
                SamplerId = static_cast<Int32>(m_Samplers[SamplerAttribs.VariableType].size());
                m_Samplers[SamplerAttribs.VariableType].emplace_back(Sampler(m_pOwner, SamplerAttribs, this));
            }
            m_CbvSrvUav[TexAttribs.VariableType].emplace_back( SRV_CBV_UAV(m_pOwner, TexAttribs, this, CachedResourceType::TexSRV, SamplerId) );
        },

        [&](const D3DShaderResourceAttribs& TexUAV)
        {
            m_CbvSrvUav[TexUAV.VariableType].emplace_back( SRV_CBV_UAV(m_pOwner, TexUAV, this, CachedResourceType::TexUAV) );
        },

        [&](const D3DShaderResourceAttribs& BuffUAV)
        {
            m_CbvSrvUav[BuffUAV.VariableType].emplace_back( SRV_CBV_UAV(m_pOwner, BuffUAV, this, CachedResourceType::BufUAV) );
        },

        [&](const D3DShaderResourceAttribs& BuffSRV)
        {
            m_CbvSrvUav[BuffSRV.VariableType].emplace_back( SRV_CBV_UAV(m_pOwner, BuffSRV, this, CachedResourceType::BufSRV) );
        },

        ShdrDesc,
        ShaderD3DBase::m_SamplerSuffix.c_str());
}


static bool CheckType(SHADER_VARIABLE_TYPE ResType, SHADER_VARIABLE_TYPE* AllowedTypes, Uint32 NumAllowedTypes)
{
    if( AllowedTypes == nullptr )
        return true;

    for(Uint32 i=0; i < NumAllowedTypes; ++i)
        if(ResType == AllowedTypes[i])
            return true;
    
    return false;
}

D3D12_DESCRIPTOR_RANGE_TYPE ShaderResourceLayoutD3D12::SRV_CBV_UAV::GetDescriptorRangeType()const
{
    static D3D12_DESCRIPTOR_RANGE_TYPE RangeTypes[(size_t)CachedResourceType::NumTypes] = {};
    static bool Initialized = false;
    if (!Initialized)
    {
        RangeTypes[(size_t)CachedResourceType::CBV]    = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
        RangeTypes[(size_t)CachedResourceType::TexSRV] = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        RangeTypes[(size_t)CachedResourceType::BufSRV] = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        RangeTypes[(size_t)CachedResourceType::TexUAV] = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        RangeTypes[(size_t)CachedResourceType::BufUAV] = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        RangeTypes[(size_t)CachedResourceType::Sampler] = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        Initialized = true;
    }
    auto Ind = static_cast<size_t>(ResType);
    VERIFY(Ind >= 0 && Ind < (size_t)CachedResourceType::NumTypes, "Unexpected resource type")
    return RangeTypes[Ind];
}

void ShaderResourceLayoutD3D12::CloneLayout(const ShaderResourceLayoutD3D12& SrcLayout, 
                                            SHADER_VARIABLE_TYPE *AllowedVarTypes, 
                                            Uint32 NumVarTypes, 
                                            std::shared_ptr<ShaderResourceCacheD3D12> pResourceCache,
                                            bool InitializeResourceCache,
                                            RootSignature *pRootSig)
{
    m_pResourceCache = pResourceCache;

    m_ShaderName = SrcLayout.m_ShaderName;
    m_ShaderType = SrcLayout.m_ShaderType;

    int MaxBindPoint[4] = {-1, -1, -1, -1};

    for(SHADER_VARIABLE_TYPE VarType = SHADER_VARIABLE_TYPE_STATIC; VarType < SHADER_VARIABLE_TYPE_NUM_TYPES; VarType = static_cast<SHADER_VARIABLE_TYPE>(VarType+1))
    {
        if( !CheckType(VarType, AllowedVarTypes, NumVarTypes))
            continue;

        auto &SrcResources = SrcLayout.m_CbvSrvUav[VarType];
        auto &CbvSrvUav = m_CbvSrvUav[VarType];
        auto &Samplers = m_Samplers[VarType];
        for( auto res = SrcResources.begin(); res != SrcResources.end(); ++res )
        {
            VERIFY(res->VariableType == VarType, "Unexpected variable type");

            Int32 SamplerId = InvalidSamplerId;
            if (res->SamplerId != InvalidSamplerId)
            {
                const auto &SrcSamplerAttribs = const_cast<ShaderResourceLayoutD3D12&>(SrcLayout).GetAssignedSampler(*res);
                VERIFY(SrcSamplerAttribs.VariableType == res->VariableType, "Inconsistent texture and sampler variable types" );

                Uint32 SamplerRootIndex = SrcSamplerAttribs.RootIndex;
                Uint32 SamplerOffset = SrcSamplerAttribs.OffsetFromTableStart;
                if(InitializeResourceCache)
                {
                    if (pRootSig)
                    {
                        pRootSig->AllocateResourceSlot(m_ShaderType, SrcSamplerAttribs, D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, SamplerRootIndex, SamplerOffset );
                    }
                    else
                    {
                        // If root signature is not provided, we are initializing resource cache to store 
                        // static shader resources. We use the following artifial root signature:
                        // CBVs at root index D3D12_DESCRIPTOR_RANGE_TYPE_CBV,
                        // SRVs at root index D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
                        // UAVs at root index D3D12_DESCRIPTOR_RANGE_TYPE_UAV, and
                        // Samplers at root index D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER
                        // Every resource is stored at offset that equals its bind point
                        SamplerRootIndex = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER; 
                        SamplerOffset = SrcSamplerAttribs.BindPoint;
                        MaxBindPoint[SamplerRootIndex] = std::max(MaxBindPoint[SamplerRootIndex], static_cast<int>(SamplerOffset));
                    }
                }
                SamplerId = static_cast<Int32>(Samplers.size());
                Samplers.emplace_back(Sampler(m_pOwner, SrcSamplerAttribs, this, SamplerRootIndex, SamplerOffset));
            }

            Uint32 RootIndex = res->RootIndex;
            Uint32 Offset = res->OffsetFromTableStart;
            if(InitializeResourceCache)
            {
                auto DescriptorRangeType = res->GetDescriptorRangeType();
                if (pRootSig)
                {
                    pRootSig->AllocateResourceSlot(m_ShaderType, *res, DescriptorRangeType, RootIndex, Offset );
                }
                else
                {
                    // If root signature is not provided - use artifial root signature to store
                    // static shader resources
                    RootIndex = DescriptorRangeType;
                    Offset = res->BindPoint;
                    MaxBindPoint[RootIndex] = std::max(MaxBindPoint[RootIndex], static_cast<int>(Offset));
                }
            }
            CbvSrvUav.emplace_back( SRV_CBV_UAV(m_pOwner, *res, this, RootIndex, Offset, SamplerId) );
        }
    }

    if(m_pResourceCache && InitializeResourceCache && !pRootSig)
    {
        m_pResourceCache->SetRootParametersCount(4);
        m_pResourceCache->GetRootTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV).SetSize(MaxBindPoint[D3D12_DESCRIPTOR_RANGE_TYPE_SRV] + 1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_ShaderType);
        m_pResourceCache->GetRootTable(D3D12_DESCRIPTOR_RANGE_TYPE_UAV).SetSize(MaxBindPoint[D3D12_DESCRIPTOR_RANGE_TYPE_UAV] + 1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_ShaderType);
        m_pResourceCache->GetRootTable(D3D12_DESCRIPTOR_RANGE_TYPE_CBV).SetSize(MaxBindPoint[D3D12_DESCRIPTOR_RANGE_TYPE_CBV] + 1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_ShaderType);
        m_pResourceCache->GetRootTable(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER).SetSize(MaxBindPoint[D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER] + 1, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, m_ShaderType);
    }

    InitVariablesHashMap();
}


void ShaderResourceLayoutD3D12::InitVariablesHashMap()
{
    // After all resources are loaded, we can populate shader variable hash map.
    // The map contains raw pointers, but none of the arrays will ever change.
    for(SHADER_VARIABLE_TYPE VarType = SHADER_VARIABLE_TYPE_STATIC; VarType < SHADER_VARIABLE_TYPE_NUM_TYPES; VarType = static_cast<SHADER_VARIABLE_TYPE>(VarType+1))
    {
        auto &ResArr = m_CbvSrvUav[VarType];
        for( auto it = ResArr.begin(); it != ResArr.end(); ++it )
            /* HashMapStringKey will make a copy of the string*/
            m_VariableHash.insert( std::make_pair( Diligent::HashMapStringKey(it->Name), &*it ) );
    }
}


#define LOG_RESOURCE_BINDING_ERROR(ResType, pResource, VarName, ShaderName, ...)\
{                                                                                                   \
    const auto &ResName = pResource->GetDesc().Name;                                                \
    LOG_ERROR_MESSAGE( "Failed to bind ", ResType, " \"", ResName, "\" to variable \"", VarName,    \
                        "\" in shader \"", ShaderName, "\". ", __VA_ARGS__ );                \
}


void ShaderResourceLayoutD3D12::SRV_CBV_UAV::CacheCB(IDeviceObject *pBuffer, ShaderResourceCacheD3D12::Resource& DstRes)
{
    // We cannot use ValidatedCast<> here as the resource retrieved from the
    // resource mapping can be of wrong type
    RefCntAutoPtr<BufferD3D12Impl> pBuffD3D12(pBuffer, IID_BufferD3D12);
    if( pBuffD3D12 )
    {
        if( pBuffD3D12->GetDesc().BindFlags & BIND_UNIFORM_BUFFER )
        {
            if( VariableType != SHADER_VARIABLE_TYPE_DYNAMIC && DstRes.pObject != nullptr && DstRes.pObject != pBuffD3D12)
            {
                auto VarTypeStr = GetShaderVariableTypeLiteralName(VariableType);
                LOG_ERROR_MESSAGE( "Non-null constant buffer is already bound to ", VarTypeStr, " shader variable \"", Name, "\" in shader \"", m_pParentResLayout->m_ShaderName, "\". Attempring to bind another constant buffer is an error and may cause unpredicted behavior. Use another shader resource binding instance or mark shader variable as dynamic." );
            }

            DstRes.Type = ResType;
            DstRes.CPUDescriptorHandle = pBuffD3D12->GetCBVHandle();
            VERIFY(DstRes.CPUDescriptorHandle.ptr != 0, "No relevant CBV CPU descriptor handle")

            DstRes.pObject = pBuffD3D12;
        }
        else
        {
            LOG_RESOURCE_BINDING_ERROR("buffer", pBuffer, Name, m_pParentResLayout->m_ShaderName, "Buffer was not created with BIND_UNIFORM_BUFFER flag.")
        }
    }
    else
    {
        LOG_RESOURCE_BINDING_ERROR("buffer", pBuffer, Name, m_pParentResLayout->m_ShaderName, "Incorrect resource type: buffer is expected.")
    }
}


template<typename TResourceViewType>
struct ResourceViewTraits{};

template<>
struct ResourceViewTraits<ITextureViewD3D12>
{
    static const Char *Name;
};
const Char *ResourceViewTraits<ITextureViewD3D12>::Name = "texture view";

template<>
struct ResourceViewTraits<IBufferViewD3D12>
{
    static const Char *Name;
};
const Char *ResourceViewTraits<IBufferViewD3D12>::Name = "buffer view";

void QueryViewD3D12(IDeviceObject *pView, ITextureViewD3D12 **ppViewD3D12)
{
    pView->QueryInterface(IID_TextureViewD3D12, reinterpret_cast<IObject**>(ppViewD3D12));
}

void QueryViewD3D12(IDeviceObject *pView, IBufferViewD3D12 **ppViewD3D12)
{
    pView->QueryInterface(IID_BufferViewD3D12, reinterpret_cast<IObject**>(ppViewD3D12));
}

   
template<typename TResourceViewType,        ///< ResType of the view (ITextureViewD3D12 or IBufferViewD3D12)
         typename TViewTypeEnum,            ///< ResType of the expected view type enum (TEXTURE_VIEW_TYPE or BUFFER_VIEW_TYPE)
         typename TBindSamplerProcType>     ///< ResType of the procedure to set sampler
void ShaderResourceLayoutD3D12::SRV_CBV_UAV::CacheResourceView(IDeviceObject *pView, 
                                                               ShaderResourceCacheD3D12::Resource& DstRes, 
                                                               TViewTypeEnum dbgExpectedViewType,
                                                               TBindSamplerProcType BindSamplerProc)
{
    // We cannot use ValidatedCast<> here as the resource retrieved from the
    // resource mapping can be of wrong type
    RefCntAutoPtr<TResourceViewType> pViewD3D12;
    QueryViewD3D12(pView, &pViewD3D12);
    if( pViewD3D12 )
    {
#ifdef VERIFY_SHADER_BINDINGS
        const auto& ViewDesc = pViewD3D12->GetDesc();
        auto ViewType = ViewDesc.ViewType;
        if( ViewType != dbgExpectedViewType )
        {
            const auto *ExpectedViewTypeName = GetViewTypeLiteralName( dbgExpectedViewType );
            const auto *ActualViewTypeName = GetViewTypeLiteralName( ViewType );
            LOG_RESOURCE_BINDING_ERROR(ResourceViewTraits<TResourceViewType>::Name, pViewD3D12, Name, m_pParentResLayout->m_ShaderName, 
                                        "Incorrect view type: ", ExpectedViewTypeName, " is expected, ", ActualViewTypeName, " provided." );
            return;
        }
#endif
        if( VariableType != SHADER_VARIABLE_TYPE_DYNAMIC && DstRes.pObject != nullptr && DstRes.pObject != pViewD3D12)
        {
            auto VarTypeStr = GetShaderVariableTypeLiteralName(VariableType);
            LOG_ERROR_MESSAGE( "Non-null resource is already bound to ", VarTypeStr, " shader variable \"", Name, "\" in shader \"", m_pParentResLayout->m_ShaderName, "\". Attempting to bind another resource is an error and may cause unpredicted behavior. Use another shader resource binding instance or mark shader variable as dynamic." );
        }

        DstRes.Type = ResType;
        DstRes.pObject = pViewD3D12;
        DstRes.CPUDescriptorHandle = pViewD3D12->GetCPUDescriptorHandle();
        VERIFY(DstRes.CPUDescriptorHandle.ptr != 0, "No relevant D3D12 view")

        BindSamplerProc(pViewD3D12);
    }
    else
    {
        LOG_RESOURCE_BINDING_ERROR("resource", pView, Name, m_pParentResLayout->m_ShaderName, "Incorect resource type: ", ResourceViewTraits<TResourceViewType>::Name, " is expected.")
    }   
}

void ShaderResourceLayoutD3D12::Sampler::CacheSampler(ITextureViewD3D12 *pTexViewD3D12)
{
    VERIFY(m_pParentResLayout, "Parent resource layout is null. This should not be possible as the pointer is initialized in the constructor and must not be null");
    auto &pResourceCache = m_pParentResLayout->m_pResourceCache;
    VERIFY(pResourceCache, "Resource cache is null");
    VERIFY(BindPoint != InvalidBindPoint, "Invalid bind point");

    auto &DstSam = pResourceCache->GetRootTable(RootIndex).GetResource(OffsetFromTableStart, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, m_pParentResLayout->m_ShaderType);

    if( pTexViewD3D12 )
    {
        auto pSampler = pTexViewD3D12->GetSampler();
        if( pSampler )
        {
            if( VariableType != SHADER_VARIABLE_TYPE_DYNAMIC && DstSam.pObject != nullptr && DstSam.pObject != pSampler)
            {
                auto VarTypeStr = GetShaderVariableTypeLiteralName(VariableType);
                LOG_ERROR_MESSAGE( "Non-null sampler is already bound to ", VarTypeStr, " shader variable \"", Name, "\" in shader \"", m_pParentResLayout->m_ShaderName, "\". Attempting to bind another sampler is an error and may cause unpredicted behavior. Use another shader resource binding instance or mark shader variable as dynamic." );
            }

            DstSam.Type = CachedResourceType::Sampler;

            auto *pSamplerD3D12 = ValidatedCast<SamplerD3D12Impl>(pSampler);
            DstSam.CPUDescriptorHandle = pSamplerD3D12->GetCPUDescriptorHandle();
            VERIFY(DstSam.CPUDescriptorHandle.ptr != 0, "No relevant D3D12 sampler descriptor handle")

            DstSam.pObject = pSampler;
        }
        else
        {
            LOG_ERROR_MESSAGE( "Failed to bind sampler to variable \"", Name, ". Sampler is not set in the texture view \"", pTexViewD3D12->GetDesc().Name, "\"" );
        }
    }
    else
    {
        DstSam = ShaderResourceCacheD3D12::Resource();
    }
} 


ShaderResourceLayoutD3D12::Sampler &ShaderResourceLayoutD3D12::GetAssignedSampler(const SRV_CBV_UAV &TexSrv)
{
    VERIFY(TexSrv.ResType == CachedResourceType::TexSRV, "Unexpected resource type: texture SRV is expected")
    VERIFY(TexSrv.SamplerId != InvalidSamplerId, "Texture SRV has no associated sampler")
    auto &SamInfo = m_Samplers[TexSrv.VariableType][TexSrv.SamplerId];
    VERIFY(SamInfo.VariableType == TexSrv.VariableType, "Inconsistent texture and sampler variable types")
    VERIFY(SamInfo.Name == TexSrv.Name + ShaderD3DBase::m_SamplerSuffix, "Sampler name \"", SamInfo.Name, "\" does not match texture name \"", TexSrv.Name, '\"');
    return SamInfo;
}


void ShaderResourceLayoutD3D12::SRV_CBV_UAV::BindResource(IDeviceObject *pObj, const ShaderResourceLayoutD3D12 *dbgResLayout)
{
    VERIFY(m_pParentResLayout, "Parent resource layout is null. This should not be possible as the pointer is initialized in the constructor and must not be null");
    auto &pResourceCache = m_pParentResLayout->m_pResourceCache;
    VERIFY(pResourceCache, "Resource cache is null");
    VERIFY(dbgResLayout == nullptr || pResourceCache == dbgResLayout->m_pResourceCache, "Invalid resource cache");
    
    auto &DstRes = pResourceCache->GetRootTable(RootIndex).GetResource(OffsetFromTableStart, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_pParentResLayout->m_ShaderType);

    if( pObj )
    {
        switch (ResType)
        {
            case CachedResourceType::CBV:
                CacheCB(pObj, DstRes); 
            break;
            
            case CachedResourceType::TexSRV: 
                CacheResourceView<ITextureViewD3D12>(pObj, DstRes, TEXTURE_VIEW_SHADER_RESOURCE, [&](ITextureViewD3D12* pTexView)
                {
                    if(SamplerId != InvalidSamplerId)
                    {
                        auto &Sam = m_pParentResLayout->GetAssignedSampler(*this);
                        Sam.CacheSampler(pTexView);
                    }
                });
            break;

            case CachedResourceType::TexUAV: 
                CacheResourceView<ITextureViewD3D12>(pObj, DstRes, TEXTURE_VIEW_UNORDERED_ACCESS, [](ITextureViewD3D12*){});
            break;

            case CachedResourceType::BufSRV: 
                CacheResourceView<IBufferViewD3D12>(pObj, DstRes, BUFFER_VIEW_SHADER_RESOURCE, [](IBufferViewD3D12*){});
            break;

            case CachedResourceType::BufUAV: 
                CacheResourceView<IBufferViewD3D12>(pObj, DstRes, BUFFER_VIEW_UNORDERED_ACCESS, [](IBufferViewD3D12*){});
            break;

            default: UNEXPECTED("Unknown resource type ", static_cast<Int32>(ResType))
        }
    }
    else
    {
        if (DstRes.pObject && VariableType != SHADER_VARIABLE_TYPE_DYNAMIC)
        {
            LOG_ERROR_MESSAGE( "Shader variable \"", Name, "\" in shader \"", m_pParentResLayout->m_ShaderName, "\" is not dynamic but being unbound. This is an error and may cause unpredicted behavior. Use another shader resource binding instance or mark shader variable as dynamic if you need to bind another resource." );
        }

        DstRes = ShaderResourceCacheD3D12::Resource();
        if(SamplerId != InvalidSamplerId)
        {
            auto &Sam = m_pParentResLayout->GetAssignedSampler(*this);
            Sam.CacheSampler(nullptr);
        }
    }
}


bool ShaderResourceLayoutD3D12::SRV_CBV_UAV::IsBound()
{
    VERIFY(m_pParentResLayout, "Parent resource layout is null. This should not be possible as the pointer is initialized in the constructor and must not be null");
    auto &pResourceCache = m_pParentResLayout->m_pResourceCache;
    VERIFY(pResourceCache, "Resource cache is null");

    if( RootIndex < pResourceCache->GetNumParametersCount() )
    {
        auto &RootTable = pResourceCache->GetRootTable(RootIndex);
        if(OffsetFromTableStart < RootTable.GetSize())
        {
            auto &CachedRes = RootTable.GetResource(OffsetFromTableStart, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_pParentResLayout->m_ShaderType);
            if( CachedRes.pObject != nullptr )
            {
                VERIFY(CachedRes.CPUDescriptorHandle.ptr != 0, "No relevant descriptor handle")
                return true;
            }
        }
    }

    return false;
}




// Helper template class that facilitates binding CBs, SRVs, and UAVs
class BindResourceHelper
{
public:
    BindResourceHelper(IResourceMapping *pRM, Uint32 Fl, const ShaderResourceLayoutD3D12 *pSRL) :
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
    const ShaderResourceLayoutD3D12 *pShaderResLayout;
};


void ShaderResourceLayoutD3D12::BindResources( IResourceMapping* pResourceMapping, Uint32 Flags, const std::shared_ptr<ShaderResourceCacheD3D12> &dbgResourceCache )
{
    VERIFY(dbgResourceCache == m_pResourceCache, "Resource cache does not match the cache provided at initialization");

    if( !pResourceMapping )
    {
        LOG_ERROR_MESSAGE( "Failed to bind resources in shader \"", m_ShaderName, "\": resource mapping is null" );
        return;
    }

    BindResourceHelper BindResHelper(pResourceMapping, Flags, this);

    // Bind constant buffers
    for(SHADER_VARIABLE_TYPE VarType = SHADER_VARIABLE_TYPE_STATIC; VarType < SHADER_VARIABLE_TYPE_NUM_TYPES; VarType = static_cast<SHADER_VARIABLE_TYPE>(VarType+1))
    {
        auto &ResArr = m_CbvSrvUav[VarType];
        BindResHelper.Bind(ResArr);
    }
}


IShaderVariable* ShaderResourceLayoutD3D12::GetShaderVariable(const Char* Name)
{
    // Name will be implicitly converted to HashMapStringKey without making a copy
    auto it = m_VariableHash.find( Name );
    if( it == m_VariableHash.end() )
    {
        LOG_ERROR_MESSAGE( "Shader variable \"", Name, "\" is not found in shader \"", m_ShaderName, "\" (", GetShaderTypeLiteralName(m_ShaderType), "). Attempts to set the variable will be silently ignored." );
        return &m_DummyShaderVar;
    }
    return it->second;
}



void ShaderResourceLayoutD3D12::CopyStaticResourceDesriptorHandles(const ShaderResourceLayoutD3D12 &SrcLayout)
{
    if (!m_pResourceCache)
    {
        LOG_ERROR("Resource layout has no resource cache");
        return;
    }

    if (!SrcLayout.m_pResourceCache)
    {
        LOG_ERROR("Dst layout has no resource cache");
        return;
    }

    // Static shader resources are stored as follows:
    // CBVs at root index D3D12_DESCRIPTOR_RANGE_TYPE_CBV,
    // SRVs at root index D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
    // UAVs at root index D3D12_DESCRIPTOR_RANGE_TYPE_UAV, and
    // Samplers at root index D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER
    // Every resource is stored at offset that equals resource bind point

    //auto &SrcSrvCbvUav = SrcLayout.m_CbvSrvUav[SHADER_VARIABLE_TYPE_STATIC];
    auto &DstSrvCbvUav = m_CbvSrvUav[SHADER_VARIABLE_TYPE_STATIC];
    for(auto res = DstSrvCbvUav.begin(); res != DstSrvCbvUav.end(); ++res)
    {
        VERIFY(SrcLayout.m_ShaderType == m_ShaderType, "Incosistent shader types")
        auto RangeType = res->GetDescriptorRangeType();
        const auto &SrcRes = SrcLayout.m_pResourceCache->GetRootTable(RangeType).GetResource(res->BindPoint, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, SrcLayout.m_ShaderType);
        if( !SrcRes.pObject )
            LOG_ERROR_MESSAGE( "No resource assigned to static shader variable \"", res->Name, "\" in shader \"", m_ShaderName, "\"." );
        if(res->SamplerId != InvalidSamplerId)
        {
            auto &SamInfo = GetAssignedSampler(*res);
            VERIFY(SamInfo.BindPoint != InvalidBindPoint, "Sampler bind point must be valid")
            auto& SrcSampler = SrcLayout.m_pResourceCache->GetRootTable(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER).GetResource(SamInfo.BindPoint, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, SrcLayout.m_ShaderType);
            if( !SrcSampler.pObject )
                LOG_ERROR_MESSAGE( "No sampler assigned to static shader variable \"", res->Name, "\" in shader \"", m_ShaderName, "\"." );
            auto &DstSampler = m_pResourceCache->GetRootTable(SamInfo.RootIndex).GetResource(SamInfo.OffsetFromTableStart, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, m_ShaderType);
            DstSampler = SrcSampler;
        }
        auto &DstRes = m_pResourceCache->GetRootTable(res->RootIndex).GetResource(res->OffsetFromTableStart, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_ShaderType);
        DstRes = SrcRes;
    }
}


#ifdef VERIFY_SHADER_BINDINGS
void ShaderResourceLayoutD3D12::dbgVerifyBindings()const
{
    VERIFY(m_pResourceCache, "Resource cache is null")

    for(SHADER_VARIABLE_TYPE VarType = SHADER_VARIABLE_TYPE_STATIC; VarType < SHADER_VARIABLE_TYPE_NUM_TYPES; VarType = static_cast<SHADER_VARIABLE_TYPE>(VarType+1))
    {
        const auto &SrvCbvUav = m_CbvSrvUav[VarType];
        for(auto res = SrvCbvUav.begin(); res != SrvCbvUav.end(); ++res)
        {
            VERIFY(res->VariableType == VarType, "Unexpected variable type")

            const auto &CachedRes = m_pResourceCache->GetRootTable(res->RootIndex).GetResource(res->OffsetFromTableStart, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_ShaderType);
            if(CachedRes.pObject)
                VERIFY(CachedRes.Type == res->ResType, "Inconsistent cached resource types")
            else
                VERIFY(CachedRes.Type == CachedResourceType::Unknown, "Unexpected cached resource types")

            if( !CachedRes.pObject || CachedRes.CPUDescriptorHandle.ptr == 0 )
                LOG_ERROR_MESSAGE( "No resource is bound to ", GetShaderVariableTypeLiteralName(res->VariableType), " variable \"", res->Name, "\" in shader \"", m_ShaderName, "\"" )

            if (res->SamplerId != InvalidSamplerId)
            {
                VERIFY(res->ResType == CachedResourceType::TexSRV, "Sampler can only be assigned to texture SRV" );
                const auto &SamInfo = const_cast<ShaderResourceLayoutD3D12*>(this)->GetAssignedSampler(*res);
                VERIFY(SamInfo.BindPoint != InvalidBindPoint, "Sampler bind point must be valid")
                const auto &CachedSampler = m_pResourceCache->GetRootTable(SamInfo.RootIndex).GetResource(SamInfo.OffsetFromTableStart, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, m_ShaderType);
                if( CachedSampler.pObject )
                    VERIFY(CachedSampler.Type == CachedResourceType::Sampler, "Incorrect cached sampler type")
                else
                    VERIFY(CachedSampler.Type == CachedResourceType::Unknown, "Unexpected cached sampler type")
                if( !CachedSampler.pObject || CachedSampler.CPUDescriptorHandle.ptr == 0 )
                    LOG_ERROR_MESSAGE( "No sampler is assigned to texture variable \"", res->Name, "\" in shader \"", m_ShaderName, "\"" )
            }
        }

        const auto &Samplers = m_Samplers[VarType];
        for(auto sam = Samplers.begin(); sam != Samplers.end(); ++sam)
        {
            VERIFY(sam->VariableType == VarType, "Unexpected sampler variable type")
            
            const auto &CachedSampler = m_pResourceCache->GetRootTable(sam->RootIndex).GetResource(sam->OffsetFromTableStart, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, m_ShaderType);
            if( CachedSampler.pObject )
                VERIFY(CachedSampler.Type == CachedResourceType::Sampler, "Incorrect cached sampler type")
            else
                VERIFY(CachedSampler.Type == CachedResourceType::Unknown, "Unexpected cached sampler type")
            if( !CachedSampler.pObject || CachedSampler.CPUDescriptorHandle.ptr == 0 )
                LOG_ERROR_MESSAGE( "No sampler is bound to sampler variable \"", sam->Name, "\" in shader \"", m_ShaderName, "\"" )
        }
    }
}
#endif

}
