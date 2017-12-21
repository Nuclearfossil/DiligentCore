/*     Copyright 2015-2017 Egor Yusov
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

#include "PlatformDefinitions.h"

#if defined( PLATFORM_WIN32 )

    #include "..\Win32\include\Win32FileSystem.h"
    typedef WindowsFileSystem FileSystem;
    typedef WindowsFile       CFile;

#elif defined( PLATFORM_UNIVERSAL_WINDOWS )

    #include "..\UWP\include\UWPFileSystem.h"
    typedef WindowsStoreFileSystem FileSystem;
    typedef WindowsStoreFile       CFile;

#elif defined ( PLATFORM_ANDROID )

    #include "../Android/include/AndroidFileSystem.h"
    typedef AndroidFileSystem FileSystem;
    typedef AndroidFile       CFile;

#elif defined ( PLATFORM_LINUX )

    #include "../Linux/include/LinuxFileSystem.h"
    typedef LinuxFileSystem FileSystem;
    typedef LinuxFile       CFile;

#elif
    #error Unsupported platform
#endif