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
/// Declaration of Diligent::ShaderResourceBindingD3D11Impl class

#include "ShaderResourceBindingD3D11.h"
#include "RenderDeviceD3D11.h"
#include "ShaderResourceBindingBase.h"
#include "ShaderResourceCacheD3D11.h"
#include "ShaderResourceLayoutD3D11.h"
#include "STDAllocator.h"

namespace Diligent
{

class FixedBlockMemoryAllocator;
/// Implementation of the Diligent::IShaderResourceBindingD3D11 interface
class ShaderResourceBindingD3D11Impl : public ShaderResourceBindingBase<IShaderResourceBindingD3D11, FixedBlockMemoryAllocator>
{
public:
    typedef ShaderResourceBindingBase<IShaderResourceBindingD3D11, FixedBlockMemoryAllocator> TBase;
    ShaderResourceBindingD3D11Impl(FixedBlockMemoryAllocator &SRBAllocator, class PipelineStateD3D11Impl *pPSO, bool IsInternal);
    ~ShaderResourceBindingD3D11Impl();

    virtual void QueryInterface( const Diligent::INTERFACE_ID &IID, IObject **ppInterface )override final;

    virtual void BindResources(Uint32 ShaderFlags, IResourceMapping *pResMapping, Uint32 Flags)override final;

    virtual IShaderVariable *GetVariable(SHADER_TYPE ShaderType, const char *Name)override final;

    ShaderResourceCacheD3D11 &GetResourceCache(Uint32 Ind){VERIFY_EXPR(Ind < _countof(m_BoundResourceCaches)); return m_BoundResourceCaches[Ind];}
    ShaderResourceLayoutD3D11 &GetResourceLayout(Uint32 Ind){return m_ResourceLayouts[Ind];}

    void BindStaticShaderResources();
    Uint32 GetNumActiveShaders()
    {
        VERIFY_EXPR(m_ResourceLayouts.size() <= _countof(m_BoundResourceCaches));
        return static_cast<Uint32>(m_ResourceLayouts.size());
    }

    Int32 GetActiveShaderTypeIndex(Uint32 s){return m_ShaderTypeIndex[s];}

private:
    // No more than 5 shader stages can be bound at the same time
    // The caches are indexed by the shader order in the PSO, not shader index
    ShaderResourceCacheD3D11 m_BoundResourceCaches[5];
    std::vector<ShaderResourceLayoutD3D11, STDAllocatorRawMem<ShaderResourceLayoutD3D11> > m_ResourceLayouts;
    
    Int8 m_ShaderTypeIndex[6] = {};

    // Resource layout index in m_ResourceLayouts[] array for every shader stage
    Int8 m_ResourceLayoutIndex[6];
    
    bool m_bIsStaticResourcesBound;
};

}
