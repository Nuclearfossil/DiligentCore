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

// This file is partially based on RootSignature.h file from MineEngine project
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Developed by Minigraph
//
// Author:  James Stanard 
//

#pragma once

/// \file
/// Declaration of Diligent::RootSignature class
#include "ShaderD3DBase.h"
#include "ShaderResourceLayoutD3D12.h"
namespace Diligent
{

SHADER_TYPE ShaderTypeFromShaderVisibility(D3D12_SHADER_VISIBILITY ShaderVisibility);
D3D12_SHADER_VISIBILITY GetShaderVisibility(SHADER_TYPE ShaderType);
D3D12_DESCRIPTOR_HEAP_TYPE dbgHeapTypeFromRangeType(D3D12_DESCRIPTOR_RANGE_TYPE RangeType);

class RootParameter
{
public:

    static const D3D12_ROOT_PARAMETER_TYPE InvalidRootParameterType = (D3D12_ROOT_PARAMETER_TYPE)0xFFFFFFFF;

	RootParameter() 
	{
        memset(&m_RootParam, 0 ,sizeof(m_RootParam));
		m_RootParam.ParameterType = (D3D12_ROOT_PARAMETER_TYPE)0xFFFFFFFF;
	}

	RootParameter(RootParameter &&RP) : 
        m_RootParam(RP.m_RootParam),
        m_DescriptorRanges(std::move(RP.m_DescriptorRanges))
	{
        VERIFY(m_RootParam.DescriptorTable.pDescriptorRanges == m_DescriptorRanges.data(), "Error in move constructor");
    }

	~RootParameter()
	{
		Clear();
	}

	void Clear()
	{
		if (m_RootParam.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
			m_DescriptorRanges.clear();

        memset(&m_RootParam, 0 ,sizeof(m_RootParam));
		m_RootParam.ParameterType = (D3D12_ROOT_PARAMETER_TYPE)0xFFFFFFFF;
	}

	void InitAsConstants( UINT Register, UINT NumDwords, D3D12_SHADER_VISIBILITY Visibility = D3D12_SHADER_VISIBILITY_ALL )
	{
		m_RootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		m_RootParam.ShaderVisibility = Visibility;
		m_RootParam.Constants.Num32BitValues = NumDwords;
		m_RootParam.Constants.ShaderRegister = Register;
		m_RootParam.Constants.RegisterSpace = 0;
	}

	void InitAsConstantBuffer( UINT Register, D3D12_SHADER_VISIBILITY Visibility = D3D12_SHADER_VISIBILITY_ALL )
	{
		m_RootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		m_RootParam.ShaderVisibility = Visibility;
		m_RootParam.Descriptor.ShaderRegister = Register;
		m_RootParam.Descriptor.RegisterSpace = 0;
	}

	void InitAsBufferSRV( UINT Register, D3D12_SHADER_VISIBILITY Visibility = D3D12_SHADER_VISIBILITY_ALL )
	{
		m_RootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
		m_RootParam.ShaderVisibility = Visibility;
		m_RootParam.Descriptor.ShaderRegister = Register;
		m_RootParam.Descriptor.RegisterSpace = 0;
	}

	void InitAsBufferUAV( UINT Register, D3D12_SHADER_VISIBILITY Visibility = D3D12_SHADER_VISIBILITY_ALL )
	{
		m_RootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
		m_RootParam.ShaderVisibility = Visibility;
		m_RootParam.Descriptor.ShaderRegister = Register;
		m_RootParam.Descriptor.RegisterSpace = 0;
	}

	void InitAsDescriptorTable( D3D12_SHADER_VISIBILITY Visibility = D3D12_SHADER_VISIBILITY_ALL )
	{
		m_RootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		m_RootParam.ShaderVisibility = Visibility;
		m_RootParam.DescriptorTable.NumDescriptorRanges = 0;
		m_RootParam.DescriptorTable.pDescriptorRanges = 0;
	}

	void AddDescriptorRange( D3D12_DESCRIPTOR_RANGE_TYPE Type, UINT Register, UINT Count, UINT Space = 0, UINT OffsetFromTableStart =  D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND)
	{
        SetDescriptorRange( static_cast<UINT>(m_DescriptorRanges.size()), Type, Register, Count, Space, OffsetFromTableStart);
    }

	void SetDescriptorRange( UINT RangeIndex, D3D12_DESCRIPTOR_RANGE_TYPE Type, UINT Register, UINT Count, UINT Space = 0, UINT OffsetFromTableStart =  D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND)
	{
        VERIFY(m_RootParam.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, "Incorrect parameter table: descriptor table is expected");
        if(RangeIndex >= m_DescriptorRanges.size())
        {
            m_DescriptorRanges.resize(RangeIndex+1);
		    m_RootParam.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(m_DescriptorRanges.size());
		    m_RootParam.DescriptorTable.pDescriptorRanges = m_DescriptorRanges.data();
        }
		D3D12_DESCRIPTOR_RANGE &range = m_DescriptorRanges[RangeIndex];
		range.RangeType = Type;
		range.NumDescriptors = Count;
		range.BaseShaderRegister = Register;
		range.RegisterSpace = Space;
		range.OffsetInDescriptorsFromTableStart = OffsetFromTableStart;
	}

	operator const D3D12_ROOT_PARAMETER&()const{return m_RootParam;}

private:
    RootParameter(const RootParameter &RP){}

	D3D12_ROOT_PARAMETER m_RootParam;
    std::vector<D3D12_DESCRIPTOR_RANGE> m_DescriptorRanges;
};



/// Implementation of the Diligent::RootSignature class
class RootSignature
{
public:
    RootSignature();

    void Finalize(ID3D12Device *pd3d12Device);

    ID3D12RootSignature* GetD3D12RootSignature(){return m_pd3d12RootSignature;}
    void InitResourceCache(class ShaderResourceCacheD3D12& ResourceCache);
    
    void AllocateResourceSlot(SHADER_TYPE ShaderType, const D3DShaderResourceAttribs &ShaderResAttribs, D3D12_DESCRIPTOR_RANGE_TYPE RangeType, Uint32 &RootIndex, Uint32 &OffsetFromTableStart);
    const std::vector<RootParameter>& GetParameters(){return m_Parameters;}

    // This method should be thread-safe as it does not modify any object state
    void CommitDescriptorHandles(class RenderDeviceD3D12Impl *pRenderDeviceD3D12, 
                                 ShaderResourceCacheD3D12& ResourceCache, 
                                 class CommandContext &Ctx, 
                                 bool IsCompute,
                                 std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> &CbvSrvUavDescriptorsToCommit,
                                 std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> &SamplerDescriptorsToCommit,
                                 std::vector<UINT> &UnitRangeSizes)const;
    Uint32 GetTotalSrvCbvUavSlots(SHADER_VARIABLE_TYPE VarType)const
    {
        VERIFY_EXPR(VarType >= 0 && VarType < SHADER_VARIABLE_TYPE_NUM_TYPES);
        return m_TotalSrvCbvUavSlots[VarType];
    }
    Uint32 GetTotalSamplerSlots(SHADER_VARIABLE_TYPE VarType)const
    {
        VERIFY_EXPR(VarType >= 0 && VarType < SHADER_VARIABLE_TYPE_NUM_TYPES);
        return m_TotalSamplerSlots[VarType];
    }

private:
#ifdef _DEBUG
    void dbgVerifyRootParameters()const;
#endif

    struct AllocatedSlot
    {
        D3DShaderResourceAttribs ResAttribs;
        Uint32 RootIndex;
        Uint32 OffsetFromTableStart;
        AllocatedSlot(const D3DShaderResourceAttribs &Attribs, Uint32 RootInd, Uint32 Offset) : 
            ResAttribs(Attribs),
            RootIndex(RootInd),
            OffsetFromTableStart(Offset)
        {}
    };

    struct AllocatedDescriptorRanges
    {
        // One for each D3D12_DESCRIPTOR_RANGE_TYPE
        std::vector<AllocatedSlot> Slots[4];
        Uint32 TotalSrvCbvUavSlots;
        Uint32 ResourceRootIndex;
        Uint32 SamplerRootIndex;
        D3D12_SHADER_VISIBILITY ShaderVisibility;
        AllocatedDescriptorRanges() : 
            TotalSrvCbvUavSlots(0),
            ResourceRootIndex(ShaderResourceLayoutD3D12::InvalidRootIndex),
            SamplerRootIndex(ShaderResourceLayoutD3D12::InvalidRootIndex),
            ShaderVisibility(D3D12_SHADER_VISIBILITY(-1))
        {}
    };

    Uint32 m_TotalSrvCbvUavSlots[SHADER_VARIABLE_TYPE_NUM_TYPES];
    Uint32 m_TotalSamplerSlots[SHADER_VARIABLE_TYPE_NUM_TYPES];
    // One group of allocated ranges for every variable type and shader type
    AllocatedDescriptorRanges m_AllocatedSlots[SHADER_VARIABLE_TYPE_NUM_TYPES][6];
    Uint32 m_NumAllocatedRootParameters;

    std::vector<RootParameter> m_Parameters;
    CComPtr<ID3D12RootSignature> m_pd3d12RootSignature;
};

}
