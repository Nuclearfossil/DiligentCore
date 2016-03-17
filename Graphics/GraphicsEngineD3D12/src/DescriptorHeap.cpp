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

#include "pch.h"
#include "DescriptorHeap.h"
#include "RenderDeviceD3D12Impl.h"

namespace Diligent
{

//
// StaticDescriptorHeap implementation
//
StaticDescriptorHeap::StaticDescriptorHeap(ID3D12Device *pDevice, Uint32 NumDescriptorsInHeap, D3D12_DESCRIPTOR_HEAP_TYPE Type, D3D12_DESCRIPTOR_HEAP_FLAGS Flags) : 
    m_pDevice(pDevice), 
    m_CurrentHeap(nullptr) 
{
    m_HeapDesc.Type = Type;
    m_HeapDesc.NodeMask = 1;
    m_HeapDesc.NumDescriptors = NumDescriptorsInHeap;
    m_HeapDesc.Flags = Flags;

    m_CurrentCPUHandle.ptr = 0;
    m_CurrentGPUHandle.ptr = 0;
    m_DescriptorSize  = pDevice->GetDescriptorHandleIncrementSize(Type);
}

ID3D12DescriptorHeap* StaticDescriptorHeap::RequestNewHeap()
{
	std::lock_guard<std::mutex> LockGuard(m_AllocationMutex);

	CComPtr<ID3D12DescriptorHeap> pHeap;
	auto hr = m_pDevice->CreateDescriptorHeap(&m_HeapDesc, __uuidof(pHeap), reinterpret_cast<void**>(static_cast<ID3D12DescriptorHeap**>(&pHeap)));
    if(FAILED(hr))
    {
        LOG_ERROR("Failed to create a descriptior heap");
        return nullptr;
    }

	m_DescriptorHeapPool.emplace_back(pHeap);
	return pHeap;
}

DescriptorHeapAllocation StaticDescriptorHeap::Allocate( uint32_t Count )
{
	if (m_CurrentHeap == nullptr || m_RemainingFreeHandles < Count)
	{
		m_CurrentHeap = RequestNewHeap();
		m_CurrentCPUHandle = m_CurrentHeap->GetCPUDescriptorHandleForHeapStart();
        if(m_HeapDesc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
            m_CurrentGPUHandle = m_CurrentHeap->GetGPUDescriptorHandleForHeapStart();
		m_RemainingFreeHandles = m_HeapDesc.NumDescriptors;
	}

	DescriptorHeapAllocation Allocation( this, m_CurrentHeap, m_CurrentCPUHandle, m_CurrentGPUHandle, Count);
	m_CurrentCPUHandle.ptr += Count * m_DescriptorSize;
    if(m_CurrentGPUHandle.ptr != 0)
        m_CurrentGPUHandle.ptr += Count * m_DescriptorSize;
	m_RemainingFreeHandles -= Count;
	return Allocation;
}

void StaticDescriptorHeap::Free(DescriptorHeapAllocation&& Allocation)
{
    LOG_ERROR_ONCE("StaticDescriptorHeap::Free() is not implemented. Descriptor heap allocations will not be released");
}



DynamicDescriptorHeapManager::DynamicDescriptorHeapManager(RenderDeviceD3D12Impl *pDevice, Uint32 NumDescriptorsInHeap, D3D12_DESCRIPTOR_HEAP_TYPE Type, D3D12_DESCRIPTOR_HEAP_FLAGS Flags) :
    m_wpDevice(pDevice)

{
    m_HeapDesc.Type = Type;
    m_HeapDesc.NodeMask = 1;
    m_HeapDesc.NumDescriptors = NumDescriptorsInHeap;
    m_HeapDesc.Flags = Flags;

    m_DescriptorSize  = pDevice->GetD3D12Device()->GetDescriptorHandleIncrementSize(Type);
}

ID3D12DescriptorHeap* DynamicDescriptorHeapManager::RequestNewHeap()
{
	std::lock_guard<std::mutex> LockGuard(m_Mutex);

    auto pRenderDevice = m_wpDevice.Lock();
    if (!pRenderDevice)
    {
        LOG_ERROR("Render device has been destroyed");
        return nullptr;
    }

    auto *pd3d12Device = pRenderDevice->GetD3D12Device();
	while (!m_DiscardedDescriptorHeaps.empty() && pRenderDevice->IsFenceComplete(m_DiscardedDescriptorHeaps.front().first))
	{
		m_AvailableDescriptorHeaps.push(m_DiscardedDescriptorHeaps.front().second);
		m_DiscardedDescriptorHeaps.pop();
	}

	if (!m_AvailableDescriptorHeaps.empty())
	{
		ID3D12DescriptorHeap* HeapPtr = m_AvailableDescriptorHeaps.front();
		m_AvailableDescriptorHeaps.pop();
		return HeapPtr;
	}
	else
	{
		CComPtr<ID3D12DescriptorHeap> pHeap;
		pd3d12Device->CreateDescriptorHeap(&m_HeapDesc, __uuidof(pHeap), reinterpret_cast<void**>(static_cast<ID3D12DescriptorHeap**>(&pHeap)));
		m_DescriptorHeapPool.emplace_back(pHeap);
		return pHeap;
	}
}



void DynamicDescriptorHeapManager::DiscardRetiredHeaps(Uint64 FenceValue, std::vector<ID3D12DescriptorHeap*> &RetiredHeaps)
{
	std::lock_guard<std::mutex> LockGuard(m_Mutex);
	for (auto iter = RetiredHeaps.begin(); iter != RetiredHeaps.end(); ++iter)
		m_DiscardedDescriptorHeaps.push(std::make_pair(FenceValue, *iter));
    RetiredHeaps.clear();
}




DynamicDescriptorHeapAllocator::DynamicDescriptorHeapAllocator(DynamicDescriptorHeapManager& HeapManager) :
    m_pHeapManager(&HeapManager),
    m_RemainingFreeHandles(0),
    m_CurrentHeapPtr(nullptr),
    m_DescriptorSize(HeapManager.GetDescriptorSize())
{
    m_CurrentCpuHandle.ptr = 0;
    m_CurrentGpuHandle.ptr = 0;
}

DescriptorHeapAllocation DynamicDescriptorHeapAllocator::Allocate(Uint32 Count)
{
    if(m_RemainingFreeHandles < Count)
    {
        RetireCurrentHeap();
    }

	if (m_CurrentHeapPtr == nullptr)
	{
		m_CurrentHeapPtr = m_pHeapManager->RequestNewHeap();
        m_CurrentCpuHandle = m_CurrentHeapPtr->GetCPUDescriptorHandleForHeapStart();
        auto &HeapDesc = m_pHeapManager->GetHeapDesc();
        if( HeapDesc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
            m_CurrentGpuHandle = m_CurrentHeapPtr->GetGPUDescriptorHandleForHeapStart();
        m_RemainingFreeHandles = HeapDesc.NumDescriptors;
	}

	DescriptorHeapAllocation Allocation( this, m_CurrentHeapPtr, m_CurrentCpuHandle, m_CurrentGpuHandle, Count );
	m_CurrentCpuHandle.ptr += Count * m_DescriptorSize;
    if(m_CurrentGpuHandle.ptr != 0)
        m_CurrentGpuHandle.ptr += Count * m_DescriptorSize;
	m_RemainingFreeHandles -= Count;

    return Allocation;
}

void DynamicDescriptorHeapAllocator::Free(DescriptorHeapAllocation&& Allocation)
{
    //LOG_ERROR_ONCE("DynamicDescriptorHeapManager::Free() is not implemented. Descriptor heap allocations will not be released");
}

void DynamicDescriptorHeapAllocator::DiscardHeaps(Uint64 FenceValue)
{
    RetireCurrentHeap();
    m_pHeapManager->DiscardRetiredHeaps(FenceValue, m_RetiredHeaps);
    VERIFY_EXPR(m_RetiredHeaps.empty());
}

void DynamicDescriptorHeapAllocator::RetireCurrentHeap()
{
	// Don't retire unused heaps.
	if (m_CurrentHeapPtr == nullptr)
	{
		VERIFY_EXPR(m_RemainingFreeHandles == 0);
		return;
	}

	m_RetiredHeaps.push_back(m_CurrentHeapPtr);
	m_CurrentHeapPtr = nullptr;
	m_RemainingFreeHandles = 0;
}

}
