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

#include "Shader.h"

/// \file
/// Base implementation of a D3D shader

namespace Diligent
{
    struct D3DShaderResourceAttribs 
    {
        String Name;
        UINT BindPoint;
        D3D_SHADER_INPUT_TYPE InputType;
        SHADER_VARIABLE_TYPE VariableType;
        D3DShaderResourceAttribs(const Char* _Name, UINT _BindPoint, D3D_SHADER_INPUT_TYPE _InputType, SHADER_VARIABLE_TYPE _VariableType) :
            Name(_Name),
            BindPoint(_BindPoint),
            InputType(_InputType),
            VariableType(_VariableType)
        {}
    };

    class ShaderD3DBase
    {
    public:
        ShaderD3DBase(const ShaderCreationAttribs &CreationAttribs);

        static const String m_SamplerSuffix;

    protected:

        CComPtr<ID3DBlob> m_pShaderByteCode;
    };
}
