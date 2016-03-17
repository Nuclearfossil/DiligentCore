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

#include "RootSignature.h"
#include "ShaderResourceLayoutD3D12.h"
#include "D3DShaderResourceLoader.h"
#include "ShaderD3D12Impl.h"
#include "CommandContext.h"
#include "RenderDeviceD3D12Impl.h"
#include "TextureD3D12Impl.h"
#include "BufferD3D12Impl.h"

namespace Diligent
{

RootSignature::RootSignature() : 
    m_NumAllocatedRootParameters(0)
{
    for(size_t s=0; s < SHADER_VARIABLE_TYPE_NUM_TYPES; ++s)
    {
        m_TotalSrvCbvUavSlots[s] = 0;
        m_TotalSamplerSlots[s] = 0;
    }
}

D3D12_SHADER_VISIBILITY GetShaderVisibility(SHADER_TYPE ShaderType)
{
    switch (ShaderType)
    {
        case SHADER_TYPE_VERTEX:    return D3D12_SHADER_VISIBILITY_VERTEX;
        case SHADER_TYPE_PIXEL:     return D3D12_SHADER_VISIBILITY_PIXEL;
        case SHADER_TYPE_GEOMETRY:  return D3D12_SHADER_VISIBILITY_GEOMETRY;
        case SHADER_TYPE_HULL:      return D3D12_SHADER_VISIBILITY_HULL;
        case SHADER_TYPE_DOMAIN:    return D3D12_SHADER_VISIBILITY_DOMAIN;
        case SHADER_TYPE_COMPUTE:   return D3D12_SHADER_VISIBILITY_ALL;
        default: LOG_ERROR("Unknown shader type (", ShaderType, ")"); return D3D12_SHADER_VISIBILITY_ALL;
    }
}

SHADER_TYPE ShaderTypeFromShaderVisibility(D3D12_SHADER_VISIBILITY ShaderVisibility)
{
    switch (ShaderVisibility)
    {
        case D3D12_SHADER_VISIBILITY_VERTEX:    return SHADER_TYPE_VERTEX;
        case D3D12_SHADER_VISIBILITY_PIXEL:     return SHADER_TYPE_PIXEL;
        case D3D12_SHADER_VISIBILITY_GEOMETRY:  return SHADER_TYPE_GEOMETRY;
        case D3D12_SHADER_VISIBILITY_HULL:      return SHADER_TYPE_HULL;
        case D3D12_SHADER_VISIBILITY_DOMAIN:    return SHADER_TYPE_DOMAIN;
        case D3D12_SHADER_VISIBILITY_ALL:       return SHADER_TYPE_COMPUTE;
        default: LOG_ERROR("Unknown shader visibility (", ShaderVisibility, ")"); return SHADER_TYPE_UNKNOWN;
    }
}

D3D12_DESCRIPTOR_HEAP_TYPE dbgHeapTypeFromRangeType(D3D12_DESCRIPTOR_RANGE_TYPE RangeType)
{
    switch (RangeType)
    {
        case D3D12_DESCRIPTOR_RANGE_TYPE_CBV: return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        case D3D12_DESCRIPTOR_RANGE_TYPE_SRV: return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        case D3D12_DESCRIPTOR_RANGE_TYPE_UAV: return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER: return D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        default: UNEXPECTED("Unexpected descriptor range type"); return D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
    }
}


void RootSignature::AllocateResourceSlot(SHADER_TYPE ShaderType, const D3DShaderResourceAttribs &ShaderResAttribs, D3D12_DESCRIPTOR_RANGE_TYPE RangeType, Uint32 &RootIndex, Uint32 &OffsetFromTableStart)
{
    auto ShaderInd = GetShaderTypeIndex(ShaderType);
    auto &DescriptorRanges = m_AllocatedSlots[ShaderResAttribs.VariableType][ShaderInd];
    if( DescriptorRanges.ShaderVisibility == D3D12_SHADER_VISIBILITY(-1) )
        DescriptorRanges.ShaderVisibility = GetShaderVisibility(ShaderType);
    else
        VERIFY(DescriptorRanges.ShaderVisibility == GetShaderVisibility(ShaderType), "Inconsistent shader visibility" )
    auto &DstRange = DescriptorRanges.Slots[RangeType];
    if( RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV || 
        RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV || 
        RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV )
    {
        if( DescriptorRanges.TotalSrvCbvUavSlots != 0)
            VERIFY_EXPR(DescriptorRanges.ResourceRootIndex != ShaderResourceLayoutD3D12::InvalidRootIndex)

        if( DescriptorRanges.ResourceRootIndex == ShaderResourceLayoutD3D12::InvalidRootIndex )
        {
            VERIFY_EXPR(DescriptorRanges.TotalSrvCbvUavSlots == 0)
            DescriptorRanges.ResourceRootIndex = m_NumAllocatedRootParameters++;
        }
        
        OffsetFromTableStart = DescriptorRanges.TotalSrvCbvUavSlots++;
        RootIndex = DescriptorRanges.ResourceRootIndex;
        ++m_TotalSrvCbvUavSlots[ShaderResAttribs.VariableType];
    }
    else if( RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER )
    {
        if(!DstRange.empty())
            VERIFY_EXPR(DescriptorRanges.SamplerRootIndex != ShaderResourceLayoutD3D12::InvalidRootIndex)
        
        if( DescriptorRanges.SamplerRootIndex == ShaderResourceLayoutD3D12::InvalidRootIndex )
        {
            VERIFY_EXPR(DstRange.empty())
            DescriptorRanges.SamplerRootIndex = m_NumAllocatedRootParameters++;
        }
        
        OffsetFromTableStart = static_cast<Uint32>(DstRange.size());
        RootIndex = DescriptorRanges.SamplerRootIndex;
        ++m_TotalSamplerSlots[ShaderResAttribs.VariableType];
    }
    else
    {
        UNEXPECTED("Unexpected descriptor range type")
    }

    DstRange.emplace_back( ShaderResAttribs, RootIndex, OffsetFromTableStart );
    
    if(m_Parameters.size() <= RootIndex)
        m_Parameters.resize(m_Parameters.size()+1);

    auto &CurrParam = m_Parameters[RootIndex];
    if( static_cast<const D3D12_ROOT_PARAMETER&>(CurrParam).ParameterType == RootParameter::InvalidRootParameterType )
        m_Parameters.back().InitAsDescriptorTable(DescriptorRanges.ShaderVisibility);

    VERIFY( static_cast<const D3D12_ROOT_PARAMETER&>(CurrParam).ShaderVisibility == DescriptorRanges.ShaderVisibility, "Shader visibility is not correct" );

    CurrParam.AddDescriptorRange(RangeType, ShaderResAttribs.BindPoint, 1, 0, OffsetFromTableStart);
}


#ifdef _DEBUG
void RootSignature::dbgVerifyRootParameters()const
{
    for(SHADER_VARIABLE_TYPE VarType = SHADER_VARIABLE_TYPE_STATIC; VarType < SHADER_VARIABLE_TYPE_NUM_TYPES; VarType = static_cast<SHADER_VARIABLE_TYPE>(VarType+1))
    {
        Uint32 dbgNumSrvCbvUavSlots = 0;
        Uint32 dbgNumSamplerSlots = 0;

        for (Uint32 ShaderInd = 0; ShaderInd < 6; ++ShaderInd)
        {
            const auto &Ranges = m_AllocatedSlots[VarType][ShaderInd];
            VERIFY( Ranges.TotalSrvCbvUavSlots == 
                    Ranges.Slots[D3D12_DESCRIPTOR_RANGE_TYPE_CBV].size() + 
                    Ranges.Slots[D3D12_DESCRIPTOR_RANGE_TYPE_SRV].size() + 
                    Ranges.Slots[D3D12_DESCRIPTOR_RANGE_TYPE_UAV].size(), "Inconsistent number of resource slots");
            dbgNumSrvCbvUavSlots += Ranges.TotalSrvCbvUavSlots;
            const auto &Samplers = Ranges.Slots[D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER];
            auto NumSamplers = Samplers.size();
            dbgNumSamplerSlots += static_cast<Uint32>(NumSamplers);
            if( Ranges.TotalSrvCbvUavSlots == 0 )
            {
                VERIFY( NumSamplers == 0, "Number of samplers cannot be non-zero when no resources are bound")
                VERIFY_EXPR(Ranges.ResourceRootIndex == ShaderResourceLayoutD3D12::InvalidRootIndex)
                VERIFY_EXPR(Ranges.SamplerRootIndex == ShaderResourceLayoutD3D12::InvalidRootIndex)
                continue;
            }


            {
                VERIFY_EXPR(Ranges.ResourceRootIndex != ShaderResourceLayoutD3D12::InvalidRootIndex)

                auto &ResRootParam = static_cast<const D3D12_ROOT_PARAMETER&>( m_Parameters[Ranges.ResourceRootIndex] );
                VERIFY(ResRootParam.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, "Root parameter is expected to be a decriptor table")
                VERIFY(ResRootParam.ShaderVisibility == Ranges.ShaderVisibility, "Incosistent shader visibility");
                auto &Table = ResRootParam.DescriptorTable;
                VERIFY(Table.NumDescriptorRanges == Ranges.TotalSrvCbvUavSlots, "Incosistent number of descriptor ranges");
            }

            if (NumSamplers > 0)
            {
                VERIFY_EXPR(Ranges.SamplerRootIndex != ShaderResourceLayoutD3D12::InvalidRootIndex)

                auto &SamRootParam = static_cast<const D3D12_ROOT_PARAMETER&>( m_Parameters[Ranges.SamplerRootIndex] );
                VERIFY(SamRootParam.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, "Root parameter is expected to be a decriptor table")
                VERIFY(SamRootParam.ShaderVisibility == Ranges.ShaderVisibility, "Incosistent shader visibility");
                auto &Table = SamRootParam.DescriptorTable;
                VERIFY(Table.NumDescriptorRanges == static_cast<Uint32>(Samplers.size()), "Incosistent number of descriptor ranges");
            }

            for(D3D12_DESCRIPTOR_RANGE_TYPE RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; RangeType <= D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER; RangeType = static_cast<D3D12_DESCRIPTOR_RANGE_TYPE>(RangeType+1))
            {
                const auto &RangeTypeSlots = Ranges.Slots[RangeType];
                if( RangeTypeSlots.empty() )
                    continue;

                auto RootIndex = RangeType < D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER ? Ranges.ResourceRootIndex : Ranges.SamplerRootIndex;
                auto &RootParam = static_cast<const D3D12_ROOT_PARAMETER&>( m_Parameters[RootIndex] );
                auto &Table = RootParam.DescriptorTable;
                for (size_t s = 0; s < RangeTypeSlots.size(); ++s)
                {
                    const auto &Slot = RangeTypeSlots[s];
                    const auto &Range = Table.pDescriptorRanges[Slot.OffsetFromTableStart];
                    VERIFY(Slot.RootIndex == RootIndex, "Incorrect root index")
                    VERIFY(Range.RangeType == RangeType, "Unexpected range type")
                    VERIFY(Range.NumDescriptors == 1, "Only one descriptor is expected in the range")
                    VERIFY(Range.BaseShaderRegister == Slot.ResAttribs.BindPoint, "Incorrect shader register")
                    VERIFY(Range.OffsetInDescriptorsFromTableStart == Slot.OffsetFromTableStart, "Incorrect offset")
                    VERIFY(Range.RegisterSpace == 0, "Incorrect register space")
                }
            }
        }

        VERIFY(dbgNumSrvCbvUavSlots == m_TotalSrvCbvUavSlots[VarType], "Unexpected number of SRV CBV UAV resource slots")
        VERIFY(dbgNumSamplerSlots == m_TotalSamplerSlots[VarType], "Unexpected number of sampler slots")
    }

    Uint32 dbgTotalSrvCbvUavSlots = 0;
    Uint32 dbgTotalSamplerSlots = 0;
    for (Uint32 s = 0; s < m_Parameters.size(); ++s)
    {
        auto &Param = static_cast<const D3D12_ROOT_PARAMETER&>( m_Parameters[s] );
        VERIFY(Param.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, "Root parameter is expected to be a descriptor table");
        auto &Table = Param.DescriptorTable;
        VERIFY(Table.NumDescriptorRanges > 0, "Descriptor table is expected to be non-empty");
        VERIFY(Table.pDescriptorRanges[0].OffsetInDescriptorsFromTableStart == 0, "Descriptor table is expected to start at 0 offset");
        bool IsResourceTable = Table.pDescriptorRanges[0].RangeType != D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        for (Uint32 r = 0; r < Table.NumDescriptorRanges; ++r)
        {
            const auto &range = Table.pDescriptorRanges[r];
            VERIFY(range.NumDescriptors == 1, "Only one descriptor is expected in the range")
            if(IsResourceTable)
            {
                VERIFY(range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV ||
                       range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV || 
                       range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV, "Resource type is expected to be SRV, CBV or UAV")
                ++dbgTotalSrvCbvUavSlots;
            }
            else
            {
                VERIFY(range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, "Resource type is expected to be sampler")
                ++dbgTotalSamplerSlots;
            }

            if(r>0)
            {
                VERIFY(Table.pDescriptorRanges[r].OffsetInDescriptorsFromTableStart == Table.pDescriptorRanges[r-1].OffsetInDescriptorsFromTableStart+1, "Ranges in a descriptor table are expected to be consequtive");
            }
        }
    }

    VERIFY(dbgTotalSrvCbvUavSlots == 
                m_TotalSrvCbvUavSlots[SHADER_VARIABLE_TYPE_STATIC] + 
                m_TotalSrvCbvUavSlots[SHADER_VARIABLE_TYPE_MUTABLE] + 
                m_TotalSrvCbvUavSlots[SHADER_VARIABLE_TYPE_DYNAMIC], "Unexpected number of SRV CBV UAV resource slots")
    VERIFY(dbgTotalSamplerSlots == 
                m_TotalSamplerSlots[SHADER_VARIABLE_TYPE_STATIC] +
                m_TotalSamplerSlots[SHADER_VARIABLE_TYPE_MUTABLE] + 
                m_TotalSamplerSlots[SHADER_VARIABLE_TYPE_DYNAMIC], "Unexpected number of sampler slots")
}
#endif

void RootSignature::Finalize(ID3D12Device *pd3d12Device)
{
#ifdef _DEBUG
    dbgVerifyRootParameters();
#endif

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    
    std::vector<D3D12_ROOT_PARAMETER> D3D12Parameters;
    D3D12Parameters.reserve(m_Parameters.size());
    for (size_t p = 0; p < m_Parameters.size(); ++p)
    {
        const D3D12_ROOT_PARAMETER &SrcParam = m_Parameters[p];
        VERIFY( SrcParam.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE && SrcParam.DescriptorTable.NumDescriptorRanges > 0, "Only non-empty descriptor tables are expected" )
        D3D12Parameters.push_back(SrcParam);
    }

    rootSignatureDesc.NumParameters = static_cast<UINT>(D3D12Parameters.size());
    rootSignatureDesc.pParameters = D3D12Parameters.size() ? D3D12Parameters.data() : nullptr;
    rootSignatureDesc.NumStaticSamplers = 0;
    rootSignatureDesc.pStaticSamplers = nullptr;

	CComPtr<ID3DBlob> signature;
	CComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
    hr = pd3d12Device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), __uuidof(m_pd3d12RootSignature), reinterpret_cast<void**>( static_cast<ID3D12RootSignature**>(&m_pd3d12RootSignature)));
    CHECK_D3D_RESULT_THROW(hr, "Failed to create root signature")
}

void RootSignature::InitResourceCache(ShaderResourceCacheD3D12& ResourceCache)
{
    ResourceCache.SetRootParametersCount(static_cast<Uint32>(m_Parameters.size()));
    for (int p = 0; p < static_cast<int>(m_Parameters.size()); ++p)
    {
        auto &RootTableCache = ResourceCache.GetRootTable(p);
        const auto& RootParam = static_cast<const D3D12_ROOT_PARAMETER&>(m_Parameters[p]);
        VERIFY(RootParam.DescriptorTable.NumDescriptorRanges, "Descriptor table is expected to have at least one range");
        auto HeapType = dbgHeapTypeFromRangeType(RootParam.DescriptorTable.pDescriptorRanges[0].RangeType);
#ifdef _DEBUG
        for (Uint32 r = 0; r < RootParam.DescriptorTable.NumDescriptorRanges; ++r)
        {
            auto &RangeType =  RootParam.DescriptorTable.pDescriptorRanges[r].RangeType;
            auto _HeapType = dbgHeapTypeFromRangeType(RangeType);
            VERIFY(_HeapType == HeapType, "Incosistenet range types");
        }
#endif
        RootTableCache.SetSize( RootParam.DescriptorTable.NumDescriptorRanges, HeapType, ShaderTypeFromShaderVisibility(RootParam.ShaderVisibility) );
    }
}

void RootSignature::CommitDescriptorHandles(RenderDeviceD3D12Impl *pRenderDeviceD3D12, 
                                            ShaderResourceCacheD3D12& ResourceCache, 
                                            CommandContext &Ctx, 
                                            bool IsCompute,
                                            std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> &CbvSrvUavDescriptorsToCommit,
                                            std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> &SamplerDescriptorsToCommit,
                                            std::vector<UINT> &UnitRangeSizes)const
{
    auto *pd3d12Device = pRenderDeviceD3D12->GetD3D12Device();

    Uint32 ResourceHeapSize = 
                m_TotalSrvCbvUavSlots[SHADER_VARIABLE_TYPE_STATIC] + 
                m_TotalSrvCbvUavSlots[SHADER_VARIABLE_TYPE_MUTABLE] + 
                m_TotalSrvCbvUavSlots[SHADER_VARIABLE_TYPE_DYNAMIC];
    Uint32 SamplerHeapSize = 
                m_TotalSamplerSlots[SHADER_VARIABLE_TYPE_STATIC] +
                m_TotalSamplerSlots[SHADER_VARIABLE_TYPE_MUTABLE] + 
                m_TotalSamplerSlots[SHADER_VARIABLE_TYPE_DYNAMIC];

    CbvSrvUavDescriptorsToCommit.resize(ResourceHeapSize);
    SamplerDescriptorsToCommit.resize(SamplerHeapSize);
    auto MaxNumRanges = std::max(SamplerHeapSize, ResourceHeapSize);
    if (UnitRangeSizes.size() < MaxNumRanges)
    {
        UnitRangeSizes.reserve(MaxNumRanges);
        while(UnitRangeSizes.size()<MaxNumRanges)
            UnitRangeSizes.push_back(1);
    }

    DescriptorHeapAllocation ResourceHeapAlloc, SamplerHeapAlloc;
    ID3D12DescriptorHeap *ppHeaps[2];
    Uint32 NumHeaps = 0;
    if( ResourceHeapSize != 0 )
    {
        ResourceHeapAlloc = Ctx.AllocateGPUVisibleDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, ResourceHeapSize);
        ppHeaps[NumHeaps++] = ResourceHeapAlloc.GetDescriptorHeap();
    }

    if (SamplerHeapSize != 0)
    {
        SamplerHeapAlloc = Ctx.AllocateGPUVisibleDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, SamplerHeapSize);
        ppHeaps[NumHeaps++] = SamplerHeapAlloc.GetDescriptorHeap();
    }

    Ctx.GetCommandList()->SetDescriptorHeaps(NumHeaps, ppHeaps);

    // Offset to the beginning of the current CBV_SRV_UAV/SAMPLER table from 
    // the start of the allocation
    Uint32 CurrResourceTableStartOffset = 0;
    Uint32 CurrSamplerTableStartOffset = 0;

    for (Uint32 s = 0; s < m_Parameters.size(); ++s)
    {
        auto &Param = static_cast<const D3D12_ROOT_PARAMETER&>( m_Parameters[s] );
        VERIFY(Param.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, "Root parameter is expected to be a descriptor table");
        auto &Table = Param.DescriptorTable;
        VERIFY(Table.NumDescriptorRanges > 0, "Descriptor table is expected to be non-empty");
        bool IsResourceTable = Table.pDescriptorRanges[0].RangeType != D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        D3D12_DESCRIPTOR_HEAP_TYPE dbgHeapType;
        if (IsResourceTable)
        {
            dbgHeapType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            D3D12_GPU_DESCRIPTOR_HANDLE CurrResourceTableGPUDescriptorHandle = ResourceHeapAlloc.GetGpuHandle(CurrResourceTableStartOffset);
            if(IsCompute)
                Ctx.GetCommandList()->SetComputeRootDescriptorTable(s, CurrResourceTableGPUDescriptorHandle);
            else
                Ctx.GetCommandList()->SetGraphicsRootDescriptorTable(s, CurrResourceTableGPUDescriptorHandle);
        }
        else
        {
            dbgHeapType = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
            D3D12_GPU_DESCRIPTOR_HANDLE CurrSamplerTableGPUDescriptorHandle = SamplerHeapAlloc.GetGpuHandle(CurrSamplerTableStartOffset);
            if(IsCompute)
                Ctx.GetCommandList()->SetComputeRootDescriptorTable(s, CurrSamplerTableGPUDescriptorHandle);
            else
                Ctx.GetCommandList()->SetGraphicsRootDescriptorTable(s, CurrSamplerTableGPUDescriptorHandle);
        }

        for (UINT r = 0; r < Param.DescriptorTable.NumDescriptorRanges; ++r)
        {
            const auto &range = Param.DescriptorTable.pDescriptorRanges[r];
            VERIFY(range.NumDescriptors==1, "Unexpected range size");
            //for (UINT p = 0; p < range.NumDescriptors; ++p)
            {
                auto ShaderType = ShaderTypeFromShaderVisibility(Param.ShaderVisibility);
                VERIFY(dbgHeapType == dbgHeapTypeFromRangeType(range.RangeType), "Mistmatch beyween descriptor heap type and descriptor range type");
                auto& Res = ResourceCache.GetRootTable(s).GetResource(r, dbgHeapType, ShaderType);
                
                ITextureD3D12 *pTexToTransition = nullptr;
                IBufferD3D12 *pBuffToTransition = nullptr;
                D3D12_RESOURCE_STATES TargetState = D3D12_RESOURCE_STATE_COMMON;
                static D3D12_RESOURCE_STATES D3D12_RESOURCE_STATE_SHADER_RESOURCE = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                switch (range.RangeType)
                {
                    case D3D12_DESCRIPTOR_RANGE_TYPE_CBV: 
                        TargetState = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
                    break;

                    case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
                        // We don't test if the same resoruce is used as both PS and non-PS resource,
                        // so we must set both flags
                        TargetState = D3D12_RESOURCE_STATE_SHADER_RESOURCE;
                    break;

                    case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
                        TargetState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                    break;
                }

                if(Res.Type == CachedResourceType::CBV)
                {
                    VERIFY(TargetState == D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, "Unexpected target state")
                    // Not using QueryInterface() for the sake of efficiency
                    pBuffToTransition = ValidatedCast<BufferD3D12Impl>(Res.pObject.RawPtr());
                }
                else if (Res.Type == CachedResourceType::BufSRV || Res.Type == CachedResourceType::BufUAV)
                {
                    VERIFY(range.RangeType == (Res.Type == CachedResourceType::BufSRV) ? D3D12_DESCRIPTOR_RANGE_TYPE_SRV : D3D12_DESCRIPTOR_RANGE_TYPE_UAV, "Unexpected descriptor range type");
                    VERIFY(Res.Type == CachedResourceType::BufSRV && TargetState == D3D12_RESOURCE_STATE_SHADER_RESOURCE || 
                           Res.Type == CachedResourceType::BufUAV && TargetState == D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Unexpected target state");
                    auto *pBuffViewD3D12 = ValidatedCast<BufferViewD3D12Impl>(Res.pObject.RawPtr());
                    pBuffToTransition = ValidatedCast<BufferD3D12Impl>(pBuffViewD3D12->GetBuffer());
                }
                else if (Res.Type == CachedResourceType::TexSRV || Res.Type == CachedResourceType::TexUAV)
                {
                    VERIFY(range.RangeType == (Res.Type == CachedResourceType::TexSRV) ? D3D12_DESCRIPTOR_RANGE_TYPE_SRV : D3D12_DESCRIPTOR_RANGE_TYPE_UAV, "Unexpected descriptor range type");
                    VERIFY(Res.Type == CachedResourceType::TexSRV && TargetState == D3D12_RESOURCE_STATE_SHADER_RESOURCE || 
                           Res.Type == CachedResourceType::TexUAV && TargetState == D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Unexpected target state");
                    auto *pTexViewD3D12 = ValidatedCast<TextureViewD3D12Impl>(Res.pObject.RawPtr());
                    pTexToTransition = ValidatedCast<TextureD3D12Impl>(pTexViewD3D12->GetTexture());
                }
                else if(Res.Type == CachedResourceType::Sampler)
                {
                    VERIFY(range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, "Unexpected descriptor range type");
                }
                else
                {
                    // Resource not bound
                    VERIFY(Res.Type == CachedResourceType::Unknown, "Unexpected resource type") 
                    VERIFY(Res.pObject == nullptr && Res.CPUDescriptorHandle.ptr == 0, "Bound resource is unexpected")
                }

                if(pTexToTransition)
                {
                    Ctx.TransitionResource(pTexToTransition, TargetState );
                }
                else if(pBuffToTransition)
                {
                    Ctx.TransitionResource(pBuffToTransition, TargetState );
                }

                VERIFY(range.OffsetInDescriptorsFromTableStart < Param.DescriptorTable.NumDescriptorRanges, "Offset is expected to be within the total number of descriptor ranges")
                if (IsResourceTable)
                {
                    if( Res.CPUDescriptorHandle.ptr == 0 )
                        LOG_ERROR_MESSAGE("No valid CbvSrvUav descriptor handle found for root parameter ", s, ", slot ", r)

                    auto OffsetFromAllocationStart = CurrResourceTableStartOffset + range.OffsetInDescriptorsFromTableStart;
                    VERIFY( OffsetFromAllocationStart < ResourceHeapSize, "Not enough space in the descriptor heap allocation")
                    CbvSrvUavDescriptorsToCommit[OffsetFromAllocationStart] = Res.CPUDescriptorHandle;
                }
                else
                {
                    if( Res.CPUDescriptorHandle.ptr == 0 )
                        LOG_ERROR_MESSAGE("No valid sampler descriptor handle found for root parameter ", s, ", slot ", r)

                    auto OffsetFromAllocationStart = CurrSamplerTableStartOffset + range.OffsetInDescriptorsFromTableStart;
                    VERIFY( OffsetFromAllocationStart < SamplerHeapSize, "Not enough space in the descriptor heap allocation")
                    SamplerDescriptorsToCommit[OffsetFromAllocationStart] = Res.CPUDescriptorHandle;
                }
            }
        }

        if (IsResourceTable)
            CurrResourceTableStartOffset += Param.DescriptorTable.NumDescriptorRanges;
        else
            CurrSamplerTableStartOffset += Param.DescriptorTable.NumDescriptorRanges;
    }

    if(ResourceHeapSize)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE DstRescriptorRange = ResourceHeapAlloc.GetCpuHandle();
        UINT DstRangeSize = ResourceHeapSize;
        pd3d12Device->CopyDescriptors(1, &DstRescriptorRange, &DstRangeSize, 
                                      ResourceHeapSize, CbvSrvUavDescriptorsToCommit.data(), UnitRangeSizes.data(),
                                      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    if (SamplerHeapSize)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE DstRescriptorRange = SamplerHeapAlloc.GetCpuHandle();
        UINT DstRangeSize = SamplerHeapSize;
        pd3d12Device->CopyDescriptors(1, &DstRescriptorRange, &DstRangeSize, 
                                      SamplerHeapSize, SamplerDescriptorsToCommit.data(), UnitRangeSizes.data(),
                                      D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    }
}

}
