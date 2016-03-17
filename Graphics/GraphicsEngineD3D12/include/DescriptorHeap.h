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
// Modified by Egor Yusov

#pragma once

#include <mutex>
#include <vector>
#include <queue>
#include <string>
#include "ObjectBase.h"

namespace Diligent
{

class DescriptorHeapAllocation;

class IDescriptorAllocator : public IObject
{
public:
    virtual DescriptorHeapAllocation Allocate( uint32_t Count ) = 0;
    virtual void Free(DescriptorHeapAllocation&& Allocation) = 0;
    virtual Uint32 GetDescriptorSize()const = 0;
};

class DescriptorHeapAllocation
{
public:
	DescriptorHeapAllocation() : 
        NumHandles(1), // One null descriptor handle
        m_pDescriptorHeap(nullptr),
        m_DescriptorSize(0)
	{
		m_FirstCpuHandle.ptr = 0;
		m_FirstGpuHandle.ptr = 0;
	}

	DescriptorHeapAllocation( IDescriptorAllocator *pAllocator, ID3D12DescriptorHeap *pHeap, D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle, Uint32 NHandles ) : 
        m_FirstCpuHandle(CpuHandle),
        m_pAllocator(pAllocator),
        m_pDescriptorHeap(pHeap),
        NumHandles(NHandles)
	{
		m_FirstGpuHandle.ptr = 0;
        VERIFY_EXPR(m_pAllocator != nullptr && m_pDescriptorHeap != nullptr);
        m_DescriptorSize = m_pAllocator->GetDescriptorSize();
	}

	DescriptorHeapAllocation( IDescriptorAllocator *pAllocator, ID3D12DescriptorHeap *pHeap, D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle, Uint32 NHandles ) : 
        m_FirstCpuHandle(CpuHandle), 
        m_FirstGpuHandle(GpuHandle),
        m_pAllocator(pAllocator),
        NumHandles(NHandles),
        m_pDescriptorHeap(pHeap)
	{
        VERIFY_EXPR(m_pAllocator != nullptr && m_pDescriptorHeap != nullptr);
        m_DescriptorSize = m_pAllocator->GetDescriptorSize();
	}

    DescriptorHeapAllocation(DescriptorHeapAllocation &&Allocation) : 
	    m_FirstCpuHandle(Allocation.m_FirstCpuHandle),
	    m_FirstGpuHandle(Allocation.m_FirstGpuHandle),
        NumHandles(Allocation.NumHandles),
        m_pAllocator(std::move(Allocation.m_pAllocator)),
        m_pDescriptorHeap(std::move(Allocation.m_pDescriptorHeap) ),
        m_DescriptorSize(std::move(Allocation.m_DescriptorSize) )
    {
        Allocation.m_pAllocator.Release();
        Allocation.m_FirstCpuHandle.ptr = 0;
        Allocation.m_FirstGpuHandle.ptr = 0;
        Allocation.NumHandles = 0;
        Allocation.m_pDescriptorHeap = nullptr;
        Allocation.m_DescriptorSize = 0;
    }

    DescriptorHeapAllocation& operator = (DescriptorHeapAllocation &&Allocation)
    { 
	    m_FirstCpuHandle = Allocation.m_FirstCpuHandle;
	    m_FirstGpuHandle = Allocation.m_FirstGpuHandle;
        NumHandles = Allocation.NumHandles;
        m_pAllocator = std::move(Allocation.m_pAllocator);
        m_pDescriptorHeap = std::move(Allocation.m_pDescriptorHeap);
        m_DescriptorSize = std::move(Allocation.m_DescriptorSize);

        Allocation.m_FirstCpuHandle.ptr = 0;
        Allocation.m_FirstGpuHandle.ptr = 0;
        Allocation.NumHandles = 0;
        Allocation.m_pAllocator.Release();
        Allocation.m_pDescriptorHeap = nullptr;
        Allocation.m_DescriptorSize = 0;

        return *this;
    }

    ~DescriptorHeapAllocation()
    {
        if(!IsNull() && m_pAllocator)
            m_pAllocator->Free(std::move(*this));
    }

	D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle(Uint32 Offset = 0) const 
    { 
        VERIFY_EXPR(Offset >= 0 && Offset < NumHandles); 

        D3D12_CPU_DESCRIPTOR_HANDLE CPUHandle = m_FirstCpuHandle; 
        if (Offset != 0)
        {
            CPUHandle.ptr += m_DescriptorSize * Offset;
        }
        return CPUHandle;
    }

	D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(Uint32 Offset = 0) const
    { 
        VERIFY_EXPR(Offset >= 0 && Offset < NumHandles); 
        D3D12_GPU_DESCRIPTOR_HANDLE GPUHandle = m_FirstGpuHandle;
        if (Offset != 0)
        {
            GPUHandle.ptr += m_DescriptorSize * Offset;
        }
        return GPUHandle;
    }

    ID3D12DescriptorHeap *GetDescriptorHeap(){return m_pDescriptorHeap;}

    size_t GetNumHandles(){return NumHandles;}

	bool IsNull() const { return m_FirstCpuHandle.ptr == 0; }
	bool IsShaderVisible() const { return m_FirstGpuHandle.ptr != 0; }

private:
    DescriptorHeapAllocation(const DescriptorHeapAllocation&) = delete;
    DescriptorHeapAllocation& operator= (const DescriptorHeapAllocation&) = delete;

	D3D12_CPU_DESCRIPTOR_HANDLE m_FirstCpuHandle;
	D3D12_GPU_DESCRIPTOR_HANDLE m_FirstGpuHandle;
    size_t NumHandles;
    // Keep strong reference to the parent heap to make sure it is alive while allocation is alive
    RefCntAutoPtr<IDescriptorAllocator> m_pAllocator;
    ID3D12DescriptorHeap* m_pDescriptorHeap;
    UINT m_DescriptorSize;
};


// This is an unbounded resource descriptor heap.  It is intended to provide space for CPU-visible resource descriptors
// as resources are created as well as for static and mutable shader descriptor tables.

class StaticDescriptorHeap : public ObjectBase<IDescriptorAllocator>
{
public:
	StaticDescriptorHeap(ID3D12Device *pDevice, Uint32 NumDescriptorsInHeap, D3D12_DESCRIPTOR_HEAP_TYPE Type, D3D12_DESCRIPTOR_HEAP_FLAGS Flags);

	virtual DescriptorHeapAllocation Allocate( uint32_t Count )override;
    virtual void Free(DescriptorHeapAllocation&& Allocation)override;
    virtual Uint32 GetDescriptorSize()const override{return m_DescriptorSize;}

protected:

	std::mutex m_AllocationMutex;
	std::vector<CComPtr<ID3D12DescriptorHeap>> m_DescriptorHeapPool;
	ID3D12DescriptorHeap* RequestNewHeap();

    D3D12_DESCRIPTOR_HEAP_DESC m_HeapDesc;
	ID3D12DescriptorHeap* m_CurrentHeap;
	D3D12_CPU_DESCRIPTOR_HANDLE m_CurrentCPUHandle;
    D3D12_GPU_DESCRIPTOR_HANDLE m_CurrentGPUHandle;
	UINT m_DescriptorSize;
	uint32_t m_RemainingFreeHandles;
    CComPtr<ID3D12Device> m_pDevice;
};

class RenderDeviceD3D12Impl;

class DynamicDescriptorHeapManager: public ObjectBase<IObject>
{
public:
	DynamicDescriptorHeapManager(RenderDeviceD3D12Impl *pDevice, Uint32 NumDescriptorsInHeap, D3D12_DESCRIPTOR_HEAP_TYPE Type, D3D12_DESCRIPTOR_HEAP_FLAGS Flags);

    virtual Uint32 GetDescriptorSize()const{return m_DescriptorSize;}

    void DiscardRetiredHeaps(Uint64 FenceValue, std::vector<ID3D12DescriptorHeap*> &RetiredHeaps);

    ID3D12DescriptorHeap* RequestNewHeap();
    D3D12_DESCRIPTOR_HEAP_DESC &GetHeapDesc(){return m_HeapDesc;}

protected:

	std::mutex m_Mutex;
	std::vector<CComPtr<ID3D12DescriptorHeap>> m_DescriptorHeapPool;
	std::queue<std::pair<uint64_t, ID3D12DescriptorHeap*>> m_DiscardedDescriptorHeaps;
	std::queue<ID3D12DescriptorHeap*> m_AvailableDescriptorHeaps;
    D3D12_DESCRIPTOR_HEAP_DESC m_HeapDesc;
    UINT m_DescriptorSize;
    RefCntWeakPtr<RenderDeviceD3D12Impl> m_wpDevice;
};


class DynamicDescriptorHeapAllocator : public ObjectBase<IDescriptorAllocator>
{
public:
    DynamicDescriptorHeapAllocator(DynamicDescriptorHeapManager& HeapManager);
    void DiscardHeaps(Uint64 FenceValue);

	virtual DescriptorHeapAllocation Allocate( Uint32 Count )override;
    virtual void Free(DescriptorHeapAllocation&& Allocation)override;

    virtual Uint32 GetDescriptorSize()const override{return m_DescriptorSize;}

    void RetireCurrentHeap();
    
private:
    std::vector<ID3D12DescriptorHeap*> m_RetiredHeaps;
    RefCntAutoPtr<DynamicDescriptorHeapManager> m_pHeapManager;

    ID3D12DescriptorHeap* m_CurrentHeapPtr;

	Uint32 m_RemainingFreeHandles;
    D3D12_CPU_DESCRIPTOR_HANDLE m_CurrentCpuHandle;
    D3D12_GPU_DESCRIPTOR_HANDLE m_CurrentGpuHandle;
    UINT m_DescriptorSize;
};

}
