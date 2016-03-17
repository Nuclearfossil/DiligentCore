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
/// Declaration of Diligent::ShaderResourceCacheD3D12 class


namespace Diligent
{

enum class CachedResourceType : Int32
{
    Unknown = -1,
    CBV = 0,
    TexSRV,
    BufSRV,
    TexUAV,
    BufUAV,
    Sampler,
    NumTypes
};

class ShaderResourceCacheD3D12
{
public:
    ShaderResourceCacheD3D12()
    {}

    struct Resource
    {
        CachedResourceType Type;
        D3D12_CPU_DESCRIPTOR_HANDLE CPUDescriptorHandle;
        RefCntAutoPtr<IDeviceObject> pObject;

        Resource()
        {
            Type = CachedResourceType::Unknown;
            CPUDescriptorHandle.ptr = 0;
        }
    };

    class RootTable
    {
    public:
        RootTable() :
            dbgDescriptorType(D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES),
            dbgShaderType(SHADER_TYPE_UNKNOWN)
        {}

        inline void SetSize(Uint32 MaxOffset, 
                            const D3D12_DESCRIPTOR_HEAP_TYPE dbgRefDescriptorType, 
                            const SHADER_TYPE dbgRefShaderType)
        { 
            dbgDescriptorType = dbgRefDescriptorType;
            dbgShaderType = dbgRefShaderType;
            m_Resources.resize(MaxOffset); 
        }

        inline Resource& GetResource(int OffsetFromTableStart, 
                                     const D3D12_DESCRIPTOR_HEAP_TYPE dbgRefDescriptorType, 
                                     const SHADER_TYPE dbgRefShaderType)
        {
            if (m_Resources.empty())
            {
                dbgDescriptorType = dbgRefDescriptorType;
                dbgShaderType = dbgRefShaderType;
            }
            if( static_cast<int>(m_Resources.size()) <= OffsetFromTableStart )
            {
                UNEXPECTED("Root table at index is not large enough to store descriptor at offset ", OffsetFromTableStart );
                m_Resources.resize( OffsetFromTableStart + 1 );
            }
            VERIFY(dbgDescriptorType == dbgRefDescriptorType, "Incosistent descriptor heap type" )
            VERIFY(dbgShaderType == dbgRefShaderType, "Incosistent shader type" )

            return m_Resources[OffsetFromTableStart];
        }

        inline Uint32 GetSize()const{return static_cast<Uint32>(m_Resources.size()); }

        D3D12_DESCRIPTOR_HEAP_TYPE dbgDescriptorType;
        SHADER_TYPE dbgShaderType;

    private:
        std::vector<Resource> m_Resources;
    };

    inline RootTable& GetRootTable(int RootIndex)
    {
        if( static_cast<int>(m_RootTables.size()) <= RootIndex )
        {
            UNEXPECTED("No space is allocated for root table at index ", RootIndex );
            m_RootTables.resize( RootIndex + 1 );
        }
        return m_RootTables[RootIndex];
    }

    inline void SetRootParametersCount(Uint32 NumRootParameters)
    {
        m_RootTables.resize(NumRootParameters);
    }

    inline Uint32 GetNumParametersCount()const{return static_cast<Uint32>(m_RootTables.size()); }

private:
    ShaderResourceCacheD3D12(const ShaderResourceCacheD3D12&) = delete;
    ShaderResourceCacheD3D12(ShaderResourceCacheD3D12&&) = delete;
    ShaderResourceCacheD3D12& operator = (const ShaderResourceCacheD3D12&) = delete;
    ShaderResourceCacheD3D12& operator = (ShaderResourceCacheD3D12&&) = delete;

    std::vector< RootTable > m_RootTables;
};

}
