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
/// Declaration of Diligent::ShaderResourceLayoutD3D12 class

#include "unordered_map"

#include "ShaderD3DBase.h"
#include "ShaderBase.h"
#include "HashUtils.h"
#include "ShaderResourceCacheD3D12.h"

#ifdef _DEBUG
#   define VERIFY_SHADER_BINDINGS
#endif

namespace Diligent
{

/// Diligent::ShaderResourceLayoutD3D12 class
class ShaderResourceLayoutD3D12
{
public:
    ShaderResourceLayoutD3D12(IObject *pOwner);


    void CloneLayout(const ShaderResourceLayoutD3D12& SrcLayout, 
                     SHADER_VARIABLE_TYPE *VarTypes, 
                     Uint32 NumVarTypes, 
                     std::shared_ptr<ShaderResourceCacheD3D12> pResourceCache,
                     bool InitializeResourceCache,
                     class RootSignature *pRootSig = nullptr);

    void ParseBytecode(ID3DBlob *pShaderBytecode, const ShaderDesc &ShdrDesc);

    static const Uint32 InvalidRootIndex = static_cast<Uint32>(-1);
    static const Uint32 InvalidOffset = static_cast<Uint32>(-1);
    static const Int32 InvalidSamplerId = -1;

    struct SRV_CBV_UAV : ShaderVariableBase, D3DShaderResourceAttribs
    {
        CachedResourceType ResType;
        Int32 SamplerId;

        Uint32 RootIndex;
        Uint32 OffsetFromTableStart;

        ShaderResourceLayoutD3D12 *m_pParentResLayout;

        SRV_CBV_UAV(IObject *pOwner, 
                    const D3DShaderResourceAttribs& ResourceAttribs,
                    ShaderResourceLayoutD3D12 *pParentResLayout,
                    CachedResourceType _Type,
                    Int32 SamId = InvalidSamplerId,
                    Uint32 RootInd = InvalidRootIndex,
                    Uint32 Offset = InvalidOffset) :
            ShaderVariableBase( pOwner ),
            D3DShaderResourceAttribs(ResourceAttribs),
            m_pParentResLayout(pParentResLayout),
            ResType(_Type),
            SamplerId(SamId),
            RootIndex( RootInd ),
            OffsetFromTableStart( Offset )
        {
            VERIFY(pParentResLayout != nullptr, "Parent resource layout must not be null");
        }

        SRV_CBV_UAV(IObject *pOwner, 
                    const SRV_CBV_UAV& SrvCbvUav,
                    ShaderResourceLayoutD3D12 *pParentResLayout,
                    Uint32 RootInd,
                    Uint32 Offset,
                    Int32 SamId) :
            SRV_CBV_UAV(pOwner, SrvCbvUav, pParentResLayout, SrvCbvUav.ResType, SamId, 
                        RootInd != InvalidRootIndex ? RootInd : SrvCbvUav.RootIndex,
                        Offset != InvalidOffset ? Offset : SrvCbvUav.OffsetFromTableStart)
        {
            VERIFY(RootIndex != InvalidRootIndex, "Root index must be valid");
            VERIFY(OffsetFromTableStart != InvalidOffset, "Offset must be valid");
        }

        D3D12_DESCRIPTOR_RANGE_TYPE GetDescriptorRangeType()const;

        bool IsBound();

        // Non-virtual function
        void BindResource(IDeviceObject *pObject, const ShaderResourceLayoutD3D12 *dbgResLayout);
        virtual void Set(IDeviceObject *pObject)override{ BindResource(pObject, nullptr); }

    private:
        void CacheCB(IDeviceObject *pBuffer, ShaderResourceCacheD3D12::Resource& DstRes);

        template<typename TResourceViewType, 
                 typename TViewTypeEnum,
                 typename TBindSamplerProcType>
        void CacheResourceView(IDeviceObject *pView, ShaderResourceCacheD3D12::Resource& DstRes, TViewTypeEnum dbgExpectedViewType, TBindSamplerProcType BindSamplerProc);
    };

    struct Sampler : D3DShaderResourceAttribs
    {
        Uint32 RootIndex;
        Uint32 OffsetFromTableStart;

        ShaderResourceLayoutD3D12 *m_pParentResLayout;

        Sampler(IObject *pOwner, 
                const D3DShaderResourceAttribs& ResourceAttribs,
                ShaderResourceLayoutD3D12 *pParentResLayout) :
            D3DShaderResourceAttribs(ResourceAttribs),
            m_pParentResLayout(pParentResLayout),
            RootIndex( InvalidRootIndex ),
            OffsetFromTableStart( InvalidOffset )
        {
        }

        Sampler(IObject *pOwner, 
                const Sampler& Sam,
                ShaderResourceLayoutD3D12 *pParentResLayout,
                Uint32 RootInd,
                Uint32 Offset) :
            Sampler(pOwner, Sam, pParentResLayout)
        {
            RootIndex = RootInd != InvalidRootIndex ? RootInd : Sam.RootIndex;
            OffsetFromTableStart = Offset != InvalidOffset ? Offset : Sam.OffsetFromTableStart;

            VERIFY(RootIndex != InvalidRootIndex, "Root index must be valid");
            VERIFY(OffsetFromTableStart != InvalidOffset, "Offset must be valid");
        }

        void CacheSampler(class ITextureViewD3D12 *pTexViewD3D12);
    };


    void CopyStaticResourceDesriptorHandles(const ShaderResourceLayoutD3D12 &SrcLayout);

    // dbgResourceCache is only used for sanity check and as a remainder that the resource cache must be alive
    // while Layout is alive
    void BindResources( IResourceMapping* pResourceMapping, Uint32 Flags, const std::shared_ptr<ShaderResourceCacheD3D12> &dbgResourceCache );

    IShaderVariable* GetShaderVariable( const Char* Name );

#ifdef VERIFY_SHADER_BINDINGS
    void dbgVerifyBindings()const;
#endif

private:
    friend class RootSignature;

    void InitVariablesHashMap();

    Sampler &GetAssignedSampler(const SRV_CBV_UAV &TexSrv);

    std::shared_ptr<ShaderResourceCacheD3D12> m_pResourceCache;

    std::vector<SRV_CBV_UAV> m_CbvSrvUav[SHADER_VARIABLE_TYPE_NUM_TYPES];
    std::vector<Sampler> m_Samplers[SHADER_VARIABLE_TYPE_NUM_TYPES];

    DummyShaderVariable m_DummyShaderVar; ///< Dummy shader variable

    /// Hash map to look up shader variables by name.
    std::unordered_map<HashMapStringKey, IShaderVariable* > m_VariableHash;

    IObject *m_pOwner;
    String m_ShaderName;
    SHADER_TYPE m_ShaderType;
};

}
