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
/// Declaration of Diligent::ShaderResourceBindingD3D12Impl class

#include "ShaderResourceBindingD3D12.h"
#include "RenderDeviceD3D12.h"
#include "ShaderResourceBindingBase.h"
#include "ShaderBase.h"
#include "ShaderResourceCacheD3D12.h"
#include "ShaderResourceLayoutD3D12.h"

namespace Diligent
{

class FixedBlockMemoryAllocator;
/// Implementation of the Diligent::IShaderResourceBindingD3D12 interface
class ShaderResourceBindingD3D12Impl : public ShaderResourceBindingBase<IShaderResourceBindingD3D12, FixedBlockMemoryAllocator>
{
public:
    typedef ShaderResourceBindingBase<IShaderResourceBindingD3D12, FixedBlockMemoryAllocator> TBase;
    ShaderResourceBindingD3D12Impl(FixedBlockMemoryAllocator &SRBAllocator, class PipelineStateD3D12Impl *pPSO, bool IsPSOInternal);
    ~ShaderResourceBindingD3D12Impl();

    virtual void QueryInterface( const Diligent::INTERFACE_ID &IID, IObject **ppInterface )override;

    virtual void BindResources(Uint32 ShaderFlags, IResourceMapping *pResMapping, Uint32 Flags)override;

    virtual IShaderVariable *GetVariable(SHADER_TYPE ShaderType, const char *Name)override;

    ShaderResourceLayoutD3D12& GetResourceLayout(SHADER_TYPE ResType){return m_ResourceLayouts[GetShaderTypeIndex(ResType)];}
    ShaderResourceCacheD3D12& GetResourceCache(){return *m_ShaderResourceCache;}

#ifdef VERIFY_SHADER_BINDINGS
    void dbgVerifyResourceBindings(PipelineStateD3D12Impl *pPSO);
#endif

private:

    DummyShaderVariable m_DummyVar;
    std::shared_ptr<ShaderResourceCacheD3D12> m_ShaderResourceCache;
    ShaderResourceLayoutD3D12 m_ResourceLayouts[6];
    Uint32 m_InitializedLayouts;
};

}
