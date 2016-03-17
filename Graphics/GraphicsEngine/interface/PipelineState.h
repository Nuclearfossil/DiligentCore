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
/// Definition of the Diligent::IRenderDevice interface and related data structures

#include "GraphicsTypes.h"
#include "Object.h"
#include "BlendState.h"
#include "RasterizerState.h"
#include "DepthStencilState.h"
#include "InputLayout.h"
#include "ShaderResourceBinding.h"

namespace Diligent
{

enum PRIMITIVE_TOPOLOGY_TYPE : Int32
{
    PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED	= 0,
    PRIMITIVE_TOPOLOGY_TYPE_POINT,
    PRIMITIVE_TOPOLOGY_TYPE_LINE,
    PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
    PRIMITIVE_TOPOLOGY_TYPE_PATCH,
    PRIMITIVE_TOPOLOGY_TYPE_NUM_TYPES
};
    
struct SampleDesc
{
    Uint32 Count;
    Uint32 Quality;
    SampleDesc() : 
        Count(1),
        Quality(0)
    {}
};

struct GraphicsPipelineDesc
{
    IShader *pVS;
    IShader *pPS;
    IShader *pDS;
    IShader *pHS;
    IShader *pGS;
    //D3D12_STREAM_OUTPUT_DESC StreamOutput;
    BlendStateDesc BlendDesc;

    /// 32-bit sample mask that determines which samples get updated 
    /// in all the active render targets. A sample mask is always applied; 
    /// it is independent of whether multisampling is enabled, and does not 
    /// depend on whether an application uses multisample render targets.
    Uint32 SampleMask;

    RasterizerStateDesc RasterizerDesc;
    DepthStencilStateDesc DepthStencilDesc;
    InputLayoutDesc InputLayout;
    //D3D12_INDEX_BUFFER_STRIP_CUT_VALUE IBStripCutValue;
    PRIMITIVE_TOPOLOGY_TYPE PrimitiveTopologyType;
    Uint32 NumRenderTargets;
    TEXTURE_FORMAT RTVFormats[8];
    TEXTURE_FORMAT DSVFormat;
    SampleDesc SmplDesc;
    Uint32 NodeMask;
    //D3D12_CACHED_PIPELINE_STATE CachedPSO;
    //D3D12_PIPELINE_STATE_FLAGS Flags;

    GraphicsPipelineDesc() : 
        pVS(nullptr),
        pPS(nullptr),
        pDS(nullptr),
        pHS(nullptr),
        pGS(nullptr),
        PrimitiveTopologyType(PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE),
        SampleMask(0xFFFFFFFF),
        NumRenderTargets(0),
        NodeMask(0)
    {
        for(size_t rt = 0; rt < _countof(RTVFormats); ++rt)
            RTVFormats[rt] = TEX_FORMAT_UNKNOWN;
        DSVFormat = TEX_FORMAT_UNKNOWN;
    };
};

struct ComputePipelineDesc
{
    IShader *pCS;
    ComputePipelineDesc() : 
        pCS(nullptr)
    {}
};

struct PipelineStateDesc : DeviceObjectAttribs
{
    bool IsComputePipeline;
    GraphicsPipelineDesc GraphicsPipeline;
    ComputePipelineDesc ComputePipeline;

    // Shader resource binding allocation granularity
    Uint32 SRBAllocationGranularity;
    PipelineStateDesc() : 
        IsComputePipeline(false),
        SRBAllocationGranularity(1)
    {}
};

// {06084AE5-6A71-4FE8-84B9-395DD489A28C}
static const Diligent::INTERFACE_ID IID_PipelineState =
{ 0x6084ae5, 0x6a71, 0x4fe8, { 0x84, 0xb9, 0x39, 0x5d, 0xd4, 0x89, 0xa2, 0x8c } };

/**
  * Pipeline state interface
  */
class IPipelineState : public IDeviceObject
{
public:
    /// Queries the specific interface, see IObject::QueryInterface() for details
    virtual void QueryInterface( const Diligent::INTERFACE_ID &IID, IObject **ppInterface ) = 0;

    /// Returns the blend state description used to create the object
    virtual const PipelineStateDesc& GetDesc()const = 0;

    /// Binds resources for all shaders in the pipeline state

    /// \param [in] pResourceMapping - Pointer to the resource mapping interface.
    /// \param [in] Flags - Additional flags. See Diligent::BIND_SHADER_RESOURCES_FLAGS.
    /// \remarks For older OpenGL devices that do not support program pipelines 
    ///          (OpenGL4.1-, OpenGLES3.0-). This function is the only way to bind
    ///          shader resources.
    virtual void BindShaderResources( IResourceMapping *pResourceMapping, Uint32 Flags ) = 0;

    virtual void CreateShaderResourceBinding( IShaderResourceBinding **ppShaderResourceBinding ) = 0;
};

}
