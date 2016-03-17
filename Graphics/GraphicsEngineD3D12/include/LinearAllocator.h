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
// Adapted to Diligent Engine: Egor Yusov
//
// Description:  This is a dynamic graphics memory allocator for DX12.  It's designed to work in concert
// with the CommandContext class and to do so in a thread-safe manner.  There may be many command contexts,
// each with its own linear allocators.  They act as windows into a global memory pool by reserving a
// context-local memory page.  Requesting a new page is done in a thread-safe manner by guarding accesses
// with a mutex lock.
//
// When a command context is finished, it will receive a fence ID that indicates when it's safe to reclaim
// used resources.  The DiscardUsedPages() method must be invoked at this time so that the used pages can be
// scheduled for reuse after the fence has cleared.

#pragma once

#include <vector>
#include <queue>
#include <mutex>

namespace Diligent
{
// Constant blocks must be multiples of 16 constants @ 16 bytes each
#define DEFAULT_ALIGN 256

// Various types of allocations may contain NULL pointers.  Check before dereferencing if you are unsure.
struct DynamicAllocation
{
	DynamicAllocation(ID3D12Resource *pBuff, size_t ThisOffset, size_t ThisSize)
		: pBuffer(pBuff), Offset(ThisOffset), Size(ThisSize) {}

	CComPtr<ID3D12Resource> pBuffer;	    // The D3D buffer associated with this memory.
	size_t Offset;			                // Offset from start of buffer resource
	size_t Size;			                // Reserved size of this allocation
	void* CPUAddress;			            // The CPU-writeable address
	D3D12_GPU_VIRTUAL_ADDRESS GPUAddress;	// The GPU-visible address
};


class LinearAllocationPage
{
public:
	LinearAllocationPage(class LinearAllocatorPageManager &OwingPageManager, ID3D12Resource* pResource, D3D12_RESOURCE_STATES State) ;
	~LinearAllocationPage();

    D3D12_RESOURCE_STATES m_State;
	void* m_CpuVirtualAddress;
	D3D12_GPU_VIRTUAL_ADDRESS m_GpuVirtualAddress;
    CComPtr<ID3D12Resource> m_pBuffer;
};


class LinearAllocatorPageManager
{
public:
    enum AllocatorType
    {
	    kGpuExclusive = 0,		// DEFAULT   GPU-writeable (via UAV)
	    kCpuWritable = 1,		// UPLOAD CPU-writeable (but write combined)

	    kNumAllocatorTypes
    };


	LinearAllocatorPageManager(AllocatorType AllocationType, size_t PageSize, class RenderDeviceD3D12Impl* pDevice);
	LinearAllocationPage* RequestPage();
	void DiscardPages( uint64_t FenceID, const std::vector<LinearAllocationPage*>& Pages );

	void Destroy( void ) { m_PagePool.clear(); }
    size_t GetPageSize(){return m_PageSize;}
    AllocatorType GetAllocationType(){return m_AllocationType;}

private:

	LinearAllocationPage* CreateNewPage( void );

	AllocatorType m_AllocationType;
    size_t m_PageSize;
    RenderDeviceD3D12Impl* m_pDevice;

	std::vector<std::unique_ptr<LinearAllocationPage> > m_PagePool;
	std::queue<std::pair<uint64_t, LinearAllocationPage*> > m_RetiredPages;
	std::queue<LinearAllocationPage*> m_AvailablePages;
	std::mutex m_Mutex;
};


class LinearAllocator
{
public:

	LinearAllocator(LinearAllocatorPageManager &PageManager) : 
        m_PageManager(PageManager), 
        m_CurOffset(0), 
        m_CurPage(nullptr)
	{
	}

	DynamicAllocation Allocate( size_t SizeInBytes, size_t Alignment = DEFAULT_ALIGN );

	void DiscardUsedPages( uint64_t FenceID );

private:
    LinearAllocatorPageManager &m_PageManager;
	size_t m_CurOffset;
	LinearAllocationPage* m_CurPage;
	std::vector<LinearAllocationPage*> m_RetiredPages;
};

}
