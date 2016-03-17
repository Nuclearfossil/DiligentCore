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
// Author(s):  James Stanard
//             Alex Nankervis
//
// Adapted to Diligent Engine: Egor Yusov
//

#include "pch.h"
#include <thread>
#include "LinearAllocator.h"
#include "RenderDeviceD3D12Impl.h"

namespace Diligent
{

LinearAllocationPage::LinearAllocationPage(LinearAllocatorPageManager &OwingPageManager, ID3D12Resource* pResource, D3D12_RESOURCE_STATES State) : 
    m_pBuffer(pResource),
    m_State(State),
    m_CpuVirtualAddress(nullptr)
{
    m_GpuVirtualAddress = m_pBuffer->GetGPUVirtualAddress();
    if(OwingPageManager.GetAllocationType() == LinearAllocatorPageManager::kCpuWritable)
	    m_pBuffer->Map(0, nullptr, &m_CpuVirtualAddress);
}

LinearAllocationPage::~LinearAllocationPage()
{
    if( m_CpuVirtualAddress )
	    m_pBuffer->Unmap(0, nullptr);
}


LinearAllocatorPageManager::LinearAllocatorPageManager(AllocatorType AllocationType, size_t PageSize, RenderDeviceD3D12Impl* pDevice) : 
    m_AllocationType(AllocationType),
    m_PageSize(PageSize),
    m_pDevice(pDevice)
{
}

LinearAllocationPage* LinearAllocatorPageManager::RequestPage()
{
	std::lock_guard<std::mutex> LockGuard(m_Mutex);

	while (!m_RetiredPages.empty() && m_pDevice->IsFenceComplete(m_RetiredPages.front().first))
	{
		m_AvailablePages.push(m_RetiredPages.front().second);
		m_RetiredPages.pop();
	}

	LinearAllocationPage* PagePtr = nullptr;

	if (!m_AvailablePages.empty())
	{
		PagePtr = m_AvailablePages.front();
		m_AvailablePages.pop();
	}
	else
	{
		PagePtr = CreateNewPage();
		m_PagePool.emplace_back(PagePtr);
	}

	return PagePtr;
}


void LinearAllocatorPageManager::DiscardPages( uint64_t FenceValue, const std::vector<LinearAllocationPage*>& UsedPages )
{
	std::lock_guard<std::mutex> LockGuard(m_Mutex);
	for (auto iter = UsedPages.begin(); iter != UsedPages.end(); ++iter)
		m_RetiredPages.push(std::make_pair(FenceValue, *iter));
}


LinearAllocationPage* LinearAllocatorPageManager::CreateNewPage( void )
{
	D3D12_HEAP_PROPERTIES HeapProps;
	HeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	HeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	HeapProps.CreationNodeMask = 1;
	HeapProps.VisibleNodeMask = 1;

	D3D12_RESOURCE_DESC ResourceDesc;
	ResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	ResourceDesc.Alignment = 0;
	ResourceDesc.Height = 1;
	ResourceDesc.DepthOrArraySize = 1;
	ResourceDesc.MipLevels = 1;
	ResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
	ResourceDesc.SampleDesc.Count = 1;
	ResourceDesc.SampleDesc.Quality = 0;
	ResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	D3D12_RESOURCE_STATES DefaultUsage;
	if (m_AllocationType == kGpuExclusive)
	{
		HeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
		ResourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		DefaultUsage = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	}
	else
	{
		HeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
		ResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		DefaultUsage = D3D12_RESOURCE_STATE_GENERIC_READ;
	}
    ResourceDesc.Width = m_PageSize;

	CComPtr<ID3D12Resource> pBuffer;
	auto hr = m_pDevice->GetD3D12Device()->CreateCommittedResource(&HeapProps, D3D12_HEAP_FLAG_NONE, &ResourceDesc,
		DefaultUsage, nullptr, __uuidof(pBuffer), reinterpret_cast<void**>(static_cast<ID3D12Resource**>(&pBuffer)) );
    if(FAILED(hr))
        LOG_ERROR("Failed to create new page")

	pBuffer->SetName(L"LinearAllocator Page");

	return new LinearAllocationPage(*this, pBuffer, DefaultUsage);
}


void LinearAllocator::DiscardUsedPages( uint64_t FenceID )
{
	if (m_CurPage == nullptr)
		return;

	m_RetiredPages.push_back(m_CurPage);
	m_CurPage = nullptr;
	m_CurOffset = 0;

	m_PageManager.DiscardPages(FenceID, m_RetiredPages);
	m_RetiredPages.clear();
}


DynamicAllocation LinearAllocator::Allocate(size_t SizeInBytes, size_t Alignment)
{
    auto PageSize = m_PageManager.GetPageSize();
	VERIFY(SizeInBytes <= PageSize, "Exceeded max linear allocator page size with single allocation");

	const size_t AlignmentMask = Alignment - 1;

	// Assert that it's a power of two.
	VERIFY_EXPR((AlignmentMask & Alignment) == 0);

	// Align the allocation
	const size_t AlignedSize = (SizeInBytes + AlignmentMask) & ~AlignmentMask;

	m_CurOffset = (m_CurOffset + AlignmentMask) & ~AlignmentMask;

	if (m_CurOffset + AlignedSize > PageSize)
	{
		VERIFY_EXPR(m_CurPage != nullptr);
		m_RetiredPages.push_back(m_CurPage);
		m_CurPage = nullptr;
	}

	if (m_CurPage == nullptr)
	{
		m_CurPage = m_PageManager.RequestPage();
		m_CurOffset = 0;
	}

	DynamicAllocation ret(m_CurPage->m_pBuffer, m_CurOffset, AlignedSize);
	ret.CPUAddress = (uint8_t*)m_CurPage->m_CpuVirtualAddress + m_CurOffset;
	ret.GPUAddress = m_CurPage->m_GpuVirtualAddress + m_CurOffset;

	m_CurOffset += AlignedSize;

	return ret;
}

}
