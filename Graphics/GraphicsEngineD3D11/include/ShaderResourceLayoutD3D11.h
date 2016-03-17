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
/// Declaration of Diligent::ShaderResourceLayoutD3D11 class

#include "ShaderD3DBase.h"
#include "ShaderBase.h"
#include "ShaderResourceCacheD3D11.h"
#include "EngineD3D11Defines.h"
#include "STDAllocator.h"

namespace Diligent
{

/// Diligent::ShaderResourceLayoutD3D11 class
class ShaderResourceLayoutD3D11
{
public:
    ShaderResourceLayoutD3D11(IObject *pOwner);

    ShaderResourceLayoutD3D11(const ShaderResourceLayoutD3D11&) = delete;
    ShaderResourceLayoutD3D11& operator = (const ShaderResourceLayoutD3D11&) = delete;
    ShaderResourceLayoutD3D11(ShaderResourceLayoutD3D11&&) = default;
    ShaderResourceLayoutD3D11& operator = (ShaderResourceLayoutD3D11&&) = delete;

    void CloneLayout(const ShaderResourceLayoutD3D11& SrcLayout, 
                     SHADER_VARIABLE_TYPE *VarTypes, 
                     Uint32 NumVarTypes, 
                     ShaderResourceCacheD3D11& ResourceCache,
                     class IMemoryAllocator& Allocator);

    void CopyResources(ShaderResourceCacheD3D11 &DstCache);

    void ParseBytecode(ID3DBlob *pShaderBytecode, const ShaderDesc &ShdrDesc);

    struct D3D11ShaderVarBase : ShaderVariableBase, D3DShaderResourceAttribs
    {
        D3D11ShaderVarBase( IObject *pOwner, 
                            const D3DShaderResourceAttribs& ResourceAttribs,
                            ShaderResourceLayoutD3D11 *pParentResLayout) :
            ShaderVariableBase( pOwner ),
            D3DShaderResourceAttribs(ResourceAttribs),
            m_pParentResLayout(pParentResLayout)
        {
            VERIFY(pParentResLayout != nullptr, "Parent resource layout must not be null");
        }

        ShaderResourceLayoutD3D11 *m_pParentResLayout;
    };

    struct ConstBuffBindInfo : D3D11ShaderVarBase
    {
        ConstBuffBindInfo( IObject *pOwner, 
                           const D3DShaderResourceAttribs& ResourceAttribs,
                            ShaderResourceLayoutD3D11 *pParentResLayout ) :
            D3D11ShaderVarBase( pOwner, ResourceAttribs, pParentResLayout )
        {}
        // Non-virtual function
        __forceinline void BindResource(IDeviceObject *pObject, const ShaderResourceLayoutD3D11 *dbgResLayout);
        virtual void Set(IDeviceObject *pObject)override final{ BindResource(pObject, nullptr); }
        
        __forceinline bool IsBound();
    };
    
    struct TexAndSamplerBindInfo : D3D11ShaderVarBase
    {
        TexAndSamplerBindInfo( IObject *pOwner, 
                               const D3DShaderResourceAttribs& TextureAttribs, 
                               const D3DShaderResourceAttribs& _SamplerAttribs,
                               ShaderResourceLayoutD3D11 *pParentResLayout) :
            D3D11ShaderVarBase( pOwner, TextureAttribs, pParentResLayout),
            SamplerAttribs(_SamplerAttribs)
        {}

        // Non-virtual function
        __forceinline void BindResource(IDeviceObject *pObject, const ShaderResourceLayoutD3D11 *dbgResLayout);
        virtual void Set(IDeviceObject *pObject)override final{ BindResource(pObject, nullptr); }

        __forceinline bool IsBound();

        D3DShaderResourceAttribs SamplerAttribs;
    };

    struct TexUAVBindInfo : D3D11ShaderVarBase
    {
        TexUAVBindInfo( IObject *pOwner, 
                        const D3DShaderResourceAttribs& ResourceAttribs,
                        ShaderResourceLayoutD3D11 *pParentResLayout ) :
            D3D11ShaderVarBase( pOwner, ResourceAttribs, pParentResLayout )
        {}

        // Provide non-virtual function
        __forceinline void BindResource(IDeviceObject *pObject, const ShaderResourceLayoutD3D11 *dbgResLayout);
        virtual void Set(IDeviceObject *pObject)override final{ BindResource(pObject, nullptr); }

        __forceinline bool IsBound();
    };

    struct BuffUAVBindInfo : D3D11ShaderVarBase
    {
        BuffUAVBindInfo( IObject *pOwner, 
                         const D3DShaderResourceAttribs& ResourceAttribs,
                         ShaderResourceLayoutD3D11 *pParentResLayout ) :
            D3D11ShaderVarBase( pOwner, ResourceAttribs, pParentResLayout )
        {}

        // Non-virtual function
        __forceinline void BindResource(IDeviceObject *pObject, const ShaderResourceLayoutD3D11 *dbgResLayout);
        virtual void Set(IDeviceObject *pObject)override final{ BindResource(pObject, nullptr); }

        __forceinline bool IsBound();
    };

    struct BuffSRVBindInfo : D3D11ShaderVarBase
    {
        BuffSRVBindInfo( IObject *pOwner, 
                         const D3DShaderResourceAttribs& ResourceAttribs,
                         ShaderResourceLayoutD3D11 *pParentResLayout ) :
            D3D11ShaderVarBase( pOwner, ResourceAttribs, pParentResLayout)
        {}

        // Non-virtual function
        __forceinline void BindResource(IDeviceObject *pObject, const ShaderResourceLayoutD3D11 *dbgResLayout);
        virtual void Set(IDeviceObject *pObject)override final{ BindResource(pObject, nullptr); }

        __forceinline bool IsBound();
    };

    // dbgResourceCache is only used for sanity check and as a remainder that the resource cache must be alive
    // while Layout is alive
    void BindResources( IResourceMapping* pResourceMapping, Uint32 Flags, const ShaderResourceCacheD3D11 &dbgResourceCache );

#ifdef VERIFY_SHADER_BINDINGS
    void dbgVerifyBindings()const;
    void dbgVerifyCommittedResources(ID3D11Buffer*              CommittedD3D11CBs[],
                                     ID3D11ShaderResourceView*  CommittedD3D11SRVs[],
                                     ID3D11Resource*            CommittedD3D11SRVResources[],
                                     ID3D11SamplerState*        CommittedD3D11Samplers[],
                                     ID3D11UnorderedAccessView* CommittedD3D11UAVs[],
                                     ID3D11Resource*            CommittedD3D11UAVResources[],
                                     ShaderResourceCacheD3D11 &ResourceCache)const;
#endif

    IShaderVariable* GetShaderVariable( const Char* Name );
    __forceinline Int32 GetShaderTypeIndex()const{return m_ShaderTypeIndex;}
    __forceinline SHADER_TYPE GetShaderType()const{return m_ShaderType;}

    __forceinline Int32 GetMaxCBBindPoint()const{return m_MaxCBBindPoint;}
    __forceinline Int32 GetMaxSRVBindPoint()const{return m_MaxSRVBindPoint;}
    __forceinline Int32 GetMaxSamplerBindPoint()const{return m_MaxSamplerBindPoint;}
    __forceinline Int32 GetMaxUAVBindPoint()const{return m_MaxUAVBindPoint;}

private:

    void InitVariablesHashMap();

    ShaderResourceCacheD3D11 *m_pResourceCache = nullptr;

    std::vector<ConstBuffBindInfo, STDAllocatorRawMem<ConstBuffBindInfo> > m_ConstantBuffers;
    std::vector<TexAndSamplerBindInfo, STDAllocatorRawMem<TexAndSamplerBindInfo> > m_TexAndSamplers;
    std::vector<TexUAVBindInfo, STDAllocatorRawMem<TexUAVBindInfo> > m_TexUAVs;
    std::vector<BuffUAVBindInfo, STDAllocatorRawMem<BuffUAVBindInfo> > m_BuffUAVs;
    std::vector<BuffSRVBindInfo, STDAllocatorRawMem<BuffSRVBindInfo> > m_BuffSRVs;

    DummyShaderVariable m_DummyShaderVar; ///< Dummy shader variable

    /// Hash map to look up shader variables by name.
    typedef std::pair<HashMapStringKey, IShaderVariable*> VariableHashData;
    std::unordered_map<HashMapStringKey, IShaderVariable*, std::hash<HashMapStringKey>, std::equal_to<HashMapStringKey>, STDAllocatorRawMem<VariableHashData> > m_VariableHash;

    IObject *m_pOwner;
    String m_ShaderName;
    SHADER_TYPE m_ShaderType;
    Int32 m_ShaderTypeIndex;

    Int32 m_MaxCBBindPoint = -1;
    Int32 m_MaxSRVBindPoint = -1;
    Int32 m_MaxSamplerBindPoint = -1;
    Int32 m_MaxUAVBindPoint = -1;
};

}
