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
/// Implementation of the Diligent::ShaderBase template class

#include "Shader.h"
#include "DeviceObjectBase.h"
#include "STDAllocator.h"

namespace Diligent
{

inline SHADER_TYPE GetShaderTypeFromIndex( Int32 Index )
{
    return static_cast<SHADER_TYPE>(1 << Index);
}

inline Int32 GetShaderTypeIndex( SHADER_TYPE Type )
{
    auto ShaderIndex = 0;
    switch( Type )
    {
        case SHADER_TYPE_UNKNOWN: ShaderIndex = -1; break;
        case SHADER_TYPE_VERTEX:  ShaderIndex =  0; break;
        case SHADER_TYPE_PIXEL:   ShaderIndex =  1; break;
        case SHADER_TYPE_GEOMETRY:ShaderIndex =  2; break;
        case SHADER_TYPE_HULL:    ShaderIndex =  3; break;
        case SHADER_TYPE_DOMAIN:  ShaderIndex =  4; break;
        case SHADER_TYPE_COMPUTE: ShaderIndex =  5; break;
        default: UNEXPECTED( "Unexpected shader type (", Type, ")" ); ShaderIndex = -1;
    }
    VERIFY( Type == GetShaderTypeFromIndex(ShaderIndex), "Incorrect shader type index" );
    return ShaderIndex;
}

static const int VSInd = GetShaderTypeIndex(SHADER_TYPE_VERTEX);
static const int PSInd = GetShaderTypeIndex(SHADER_TYPE_PIXEL);
static const int GSInd = GetShaderTypeIndex(SHADER_TYPE_GEOMETRY);
static const int HSInd = GetShaderTypeIndex(SHADER_TYPE_HULL);
static const int DSInd = GetShaderTypeIndex(SHADER_TYPE_DOMAIN);
static const int CSInd = GetShaderTypeIndex(SHADER_TYPE_COMPUTE);

inline SHADER_VARIABLE_TYPE GetShaderVariableType(const Char* Name, SHADER_VARIABLE_TYPE DefaultVariableType, const ShaderVariableDesc *VariableDesc, Uint32 NumVars)
{
    for (Uint32 v = 0; v < NumVars; ++v)
    {
        const auto &CurrVarDesc = VariableDesc[v];
        if (strcmp(CurrVarDesc.Name, Name) == 0)
        {
            return CurrVarDesc.Type;
        }
    }
    return DefaultVariableType;
}


/// Base implementation of a shader variable
struct ShaderVariableBase : public ObjectBase<IShaderVariable>
{
    ShaderVariableBase(IObject *pOwner) : 
        // Shader variables are always created as part of the shader, or 
        // shader resource binding, so we must provide owner pointer to 
        // the base class constructor
        ObjectBase<IShaderVariable>(pOwner),
        m_pOwner(pOwner)
    {
        VERIFY(pOwner, "Owner must not be null");
    }

    IObject* GetOwner()
    {
        return m_pOwner;
    }

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE( IID_ShaderVariable, ObjectBase<IShaderVariable> )

protected:
    IObject *m_pOwner;
};

/// Implementation of a dummy shader variable that silently ignores all operations
struct DummyShaderVariable : ShaderVariableBase
{
    DummyShaderVariable(IObject *pOwner) :
        ShaderVariableBase(pOwner)
    {}

    virtual void Set( IDeviceObject *pObject )override
    {
        // Ignore operation
        // Probably output warning
    }
};

/// Template class implementing base functionality for a shader object
template<class BaseInterface, class RenderDeviceBaseInterface, class ShaderObjAllocator>
class ShaderBase : public DeviceObjectBase<BaseInterface, ShaderDesc, ShaderObjAllocator>
{
public:
    typedef DeviceObjectBase<BaseInterface, ShaderDesc, ShaderObjAllocator> TDeviceObjectBase;

	/// \param pDevice - pointer to the device.
	/// \param ShdrDesc - shader description.
	/// \param bIsDeviceInternal - flag indicating if the shader is an internal device object and 
	///							   must not keep a strong reference to the device.
    ShaderBase( ShaderObjAllocator &ObjAllocator, IRenderDevice *pDevice, const ShaderDesc& ShdrDesc, bool bIsDeviceInternal = false ) :
        TDeviceObjectBase( ObjAllocator, pDevice, ShdrDesc, nullptr, bIsDeviceInternal ),
        m_DummyShaderVar(this),
        m_VariablesDesc(ShdrDesc.NumVariables, ShaderVariableDesc(), STD_ALLOCATOR_RAW_MEM(ShaderVariableDesc, GetRawAllocator(), "Allocator for vector<ShaderVariableDesc>") ),
        m_ShaderVarNames(ShdrDesc.NumVariables, String(), STD_ALLOCATOR_RAW_MEM(String, GetRawAllocator(), "Allocator for vector<String>"))
    {
        if(m_Desc.VariableDesc)
        {
            for (Uint32 v = 0; v < m_Desc.NumVariables; ++v)
            {
                m_VariablesDesc[v] = m_Desc.VariableDesc[v];
                m_ShaderVarNames[v] = m_VariablesDesc[v].Name;
                m_VariablesDesc[v].Name = m_ShaderVarNames[v].c_str();
            }
            m_Desc.VariableDesc = m_VariablesDesc.data();
        }
    }

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE( IID_Shader, TDeviceObjectBase )
    
protected:
    DummyShaderVariable m_DummyShaderVar; ///< Dummy shader variable
    std::vector<ShaderVariableDesc, STDAllocatorRawMem<ShaderVariableDesc> > m_VariablesDesc;
    std::vector<String, STDAllocatorRawMem<String> > m_ShaderVarNames;
};

}
