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
/// D3D shader resource loading

namespace Diligent
{
    const UINT InvalidBindPoint = std::numeric_limits<UINT>::max();

    template<typename D3D_SHADER_DESC, 
             typename D3D_SHADER_INPUT_BIND_DESC,
             typename TShaderReflection, 

             typename TOnNewCB, 
             typename TOnNewTexAndSampler, 
             typename TOnNewTexUAV, 
             typename TOnNewBuffUAV, 
             typename TOnNewBuffSRV>
    void LoadD3DShaderResources(ID3DBlob *pShaderByteCode, 

                                TOnNewCB OnNewCB, 
                                TOnNewTexAndSampler OnNewTexAndSampler, 
                                TOnNewTexUAV OnNewTexUAV, 
                                TOnNewBuffUAV OnNewBuffUAV, 
                                TOnNewBuffSRV OnNewBuffSRV,

                                const ShaderDesc &ShdrDesc,
                                const Char *SamplerSuffix)
    {
        CComPtr<TShaderReflection> pShaderReflection;
        CHECK_D3D_RESULT_THROW( D3DReflect( pShaderByteCode->GetBufferPointer(), pShaderByteCode->GetBufferSize(), __uuidof(pShaderReflection), reinterpret_cast<void**>(static_cast<TShaderReflection**>(&pShaderReflection)) ),
                                "Failed to get the shader reflection" );

        struct ResourceBindInfo
        {
            String Name;
            UINT BindPoint;
        };

        std::vector<D3DShaderResourceAttribs> Samplers, TextureSRVs;

        D3D_SHADER_DESC shaderDesc;
        memset( &shaderDesc, 0, sizeof(shaderDesc) );
        pShaderReflection->GetDesc( &shaderDesc );
        for( UINT Res = 0; Res < shaderDesc.BoundResources; ++Res )
        {
            D3D_SHADER_INPUT_BIND_DESC BindingDesc;
            memset( &BindingDesc, 0, sizeof( BindingDesc ) );
            pShaderReflection->GetResourceBindingDesc( Res, &BindingDesc );

            SHADER_VARIABLE_TYPE VarType = GetShaderVariableType(BindingDesc.Name, ShdrDesc.DefaultVariableType, ShdrDesc.VariableDesc, ShdrDesc.NumVariables);
            D3DShaderResourceAttribs NewShaderResource(BindingDesc.Name, BindingDesc.BindPoint, BindingDesc.Type, VarType);

            switch( BindingDesc.Type )
            {
                case D3D_SIT_CBUFFER:
                {
                    OnNewCB( NewShaderResource );
                    break;
                }
            
                case D3D_SIT_TBUFFER:
                {
                    UNSUPPORTED( "TBuffers are not supported" );
                    break;
                }

                case D3D_SIT_TEXTURE:
                {
                    if( BindingDesc.Dimension == D3D_SRV_DIMENSION_BUFFER )
                    {
                        OnNewBuffSRV( NewShaderResource );
                    }
                    else
                    {
                        TextureSRVs.emplace_back( NewShaderResource );
                    }
                    break;
                }

                case D3D_SIT_SAMPLER:
                {
                    Samplers.emplace_back( NewShaderResource );
                    break;
                }

                case D3D_SIT_UAV_RWTYPED:
                {
                    if( BindingDesc.Dimension == D3D_SRV_DIMENSION_BUFFER )
                    {
                        OnNewBuffUAV(NewShaderResource);
                    }
                    else
                    {
                        OnNewTexUAV(NewShaderResource);
                    }
                    break;
                }

                case D3D_SIT_STRUCTURED:
                {
                    UNSUPPORTED( "Structured buffers are not supported" );
                    break;
                }

                case D3D_SIT_UAV_RWSTRUCTURED:
                {
                    OnNewBuffUAV(NewShaderResource);
                    break;
                }

                case D3D_SIT_BYTEADDRESS:
                {
                    UNSUPPORTED( "Byte address buffers are not supported" );
                    break;
                }

                case D3D_SIT_UAV_RWBYTEADDRESS:
                {
                    OnNewBuffUAV(NewShaderResource);
                    break;
                }

                case D3D_SIT_UAV_APPEND_STRUCTURED:
                {
                    UNSUPPORTED( "Append structured buffers are not supported" );
                    break;
                }

                case D3D_SIT_UAV_CONSUME_STRUCTURED:
                {
                    UNSUPPORTED( "Consume structured buffers are not supported" );
                    break;
                }

                case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
                {
                    UNSUPPORTED( "RW structured buffers with counter are not supported" );
                    break;
                }
            }
        }

        // Merge texture and sampler list
        for( auto it = TextureSRVs.begin(); it != TextureSRVs.end(); ++it )
        {
            auto SamplerName = it->Name + SamplerSuffix;
            auto SamplerIt = std::find_if(Samplers.begin(), Samplers.end(), [&](const D3DShaderResourceAttribs& Sampler)
                                          {
                                            return Sampler.Name == SamplerName;
                                          });
            UINT SamplerBindPoint = InvalidBindPoint;
            if( SamplerIt != Samplers.end() )
            {
                VERIFY( SamplerIt->Name == SamplerName, "Unexpected sampler name" );
                SamplerBindPoint = SamplerIt->BindPoint;
            }
            else
            {
                SamplerName = "";
            }
            OnNewTexAndSampler(*it, D3DShaderResourceAttribs(SamplerName.c_str(), SamplerBindPoint, D3D_SIT_SAMPLER, it->VariableType));
        }
    }
}
