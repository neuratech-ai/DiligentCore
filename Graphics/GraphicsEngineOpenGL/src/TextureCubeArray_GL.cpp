/*
 *  Copyright 2019-2025 Diligent Graphics LLC
 *  Copyright 2015-2019 Egor Yusov
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
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

#include "pch.h"

#include "TextureCubeArray_GL.hpp"

#include "RenderDeviceGLImpl.hpp"
#include "DeviceContextGLImpl.hpp"
#include "BufferGLImpl.hpp"

#include "GLTypeConversions.hpp"
#include "GraphicsAccessories.hpp"


namespace Diligent
{

TextureCubeArray_GL::TextureCubeArray_GL(IReferenceCounters*        pRefCounters,
                                         FixedBlockMemoryAllocator& TexViewObjAllocator,
                                         RenderDeviceGLImpl*        pDeviceGL,
                                         GLContextState&            GLState,
                                         const TextureDesc&         TexDesc,
                                         const TextureData*         pInitData /*= nullptr*/,
                                         bool                       bIsDeviceInternal /*= false*/) :
    // clang-format off
    TextureBaseGL
    {
        pRefCounters,
        TexViewObjAllocator,
        pDeviceGL,
        TexDesc,
        GL_TEXTURE_CUBE_MAP_ARRAY,
        pInitData,
        bIsDeviceInternal
    }
// clang-format on
{
    if (TexDesc.Usage == USAGE_STAGING)
    {
        // We will use PBO initialized by TextureBaseGL
        return;
    }

    VERIFY(m_Desc.SampleCount == 1, "Multisampled texture cube arrays are not supported");

    GLState.BindTexture(-1, m_BindTarget, m_GlTexture);

    // Every OpenGL API call that operates on cubemap array textures takes layer-faces, not array layers.
    // For example, when you allocate storage for the texture, you would use glTexStorage3D? or glTexImage3D? or similar.
    // The depth? parameter will be the number of layer-faces, not layers. So it must be divisible by 6.
    VERIFY((m_Desc.ArraySize % 6) == 0, "Array size must be multiple of 6");
    //                             levels             format          width         height          depth
    glTexStorage3D(m_BindTarget, m_Desc.MipLevels, m_GLTexFormat, m_Desc.Width, m_Desc.Height, m_Desc.ArraySize);
    DEV_CHECK_GL_ERROR_AND_THROW("Failed to allocate storage for the Cubemap texture array");
    //When target is GL_TEXTURE_CUBE_MAP_ARRAY glTexStorage3D is equivalent to:
    //
    //for (i = 0; i < levels; i++) {
    //    glTexImage3D(target, i, internalformat, width, height, depth, 0, format, type, NULL);
    //    width = max(1, (width / 2));
    //    height = max(1, (height / 2));
    //}

    SetDefaultGLParameters();

    if (pInitData != nullptr && pInitData->pSubResources != nullptr)
    {
        VERIFY((m_Desc.ArraySize % 6) == 0, "Array size must be multiple of 6");
        if (m_Desc.MipLevels * m_Desc.ArraySize == pInitData->NumSubresources)
        {
            for (Uint32 Slice = 0; Slice < m_Desc.ArraySize; ++Slice)
            {
                for (Uint32 Mip = 0; Mip < m_Desc.MipLevels; ++Mip)
                {
                    Box DstBox{0, std::max(m_Desc.Width >> Mip, 1U),
                               0, std::max(m_Desc.Height >> Mip, 1U)};
                    // UpdateData() is a virtual function. If we try to call it through vtbl from here,
                    // we will get into TextureBaseGL::UpdateData(), because instance of TextureCubeArray_GL
                    // is not fully constructed yet.
                    // To call the required function, we need to explicitly specify the class:
                    TextureCubeArray_GL::UpdateData(GLState, Mip, Slice, DstBox, pInitData->pSubResources[Slice * m_Desc.MipLevels + Mip]);
                }
            }
        }
        else
        {
            UNEXPECTED("Incorrect number of subresources");
        }
    }

    m_GlTexture.SetName(m_Desc.Name);

    GLState.BindTexture(-1, m_BindTarget, GLObjectWrappers::GLTextureObj::Null());
}

TextureCubeArray_GL::TextureCubeArray_GL(IReferenceCounters*        pRefCounters,
                                         FixedBlockMemoryAllocator& TexViewObjAllocator,
                                         RenderDeviceGLImpl*        pDeviceGL,
                                         GLContextState&            GLState,
                                         const TextureDesc&         TexDesc,
                                         GLuint                     GLTextureHandle,
                                         GLuint                     GLBindTarget,
                                         bool                       bIsDeviceInternal) :
    // clang-format off
    TextureBaseGL
    {
        pRefCounters,
        TexViewObjAllocator,
        pDeviceGL,
        GLState,
        TexDesc,
        GLTextureHandle,
        static_cast<GLenum>(GLBindTarget != 0 ? GLBindTarget : GL_TEXTURE_CUBE_MAP_ARRAY),
        bIsDeviceInternal
    }
// clang-format on
{
}

TextureCubeArray_GL::~TextureCubeArray_GL()
{
}

void TextureCubeArray_GL::UpdateData(GLContextState&          ContextState,
                                     Uint32                   MipLevel,
                                     Uint32                   Slice,
                                     const Box&               DstBox,
                                     const TextureSubResData& SubresData)
{
    TextureBaseGL::UpdateData(ContextState, MipLevel, Slice, DstBox, SubresData);

    ContextState.BindTexture(-1, m_BindTarget, m_GlTexture);

    // Bind buffer if it is provided; copy from CPU memory otherwise
    GLuint UnpackBuffer = 0;
    if (SubresData.pSrcBuffer != nullptr)
    {
        BufferGLImpl* pBufferGL = ClassPtrCast<BufferGLImpl>(SubresData.pSrcBuffer);
        UnpackBuffer            = pBufferGL->GetGLHandle();
    }

    // Transfers to OpenGL memory are called unpack operations
    // If there is a buffer bound to GL_PIXEL_UNPACK_BUFFER target, then all the pixel transfer
    // operations will be performed from this buffer.
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, UnpackBuffer);

    const NativePixelAttribs& TransferAttribs = GetNativePixelTransferAttribs(m_Desc.Format);

    glPixelStorei(GL_UNPACK_ALIGNMENT, PBOOffsetAlignment);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);

    if (TransferAttribs.IsCompressed)
    {
        Uint32 MipWidth  = std::max(m_Desc.Width >> MipLevel, 1U);
        Uint32 MipHeight = std::max(m_Desc.Height >> MipLevel, 1U);
        // clang-format off
        VERIFY((DstBox.MinX % 4) == 0 && (DstBox.MinY % 4) == 0 &&
               ((DstBox.MaxX % 4) == 0 || DstBox.MaxX == MipWidth) &&
               ((DstBox.MaxY % 4) == 0 || DstBox.MaxY == MipHeight),
               "Compressed texture update region must be 4 pixel-aligned");
        // clang-format on
#ifdef DILIGENT_DEBUG
        {
            const TextureFormatAttribs& FmtAttribs      = GetTextureFormatAttribs(m_Desc.Format);
            Uint32                      BlockBytesInRow = ((DstBox.Width() + 3) / 4) * Uint32{FmtAttribs.ComponentSize};
            VERIFY(SubresData.Stride == BlockBytesInRow,
                   "Compressed data stride (", SubresData.Stride, " must match the size of a row of compressed blocks (", BlockBytesInRow, ")");
        }
#endif

        // Every OpenGL API call that operates on cubemap array textures takes layer-faces, not array layers.

        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0); // Must be 0 on WebGL
        //glPixelStorei(GL_UNPACK_COMPRESSED_BLOCK_WIDTH, 0);
        Uint32 UpdateRegionWidth  = DstBox.Width();
        Uint32 UpdateRegionHeight = DstBox.Height();
        UpdateRegionWidth         = std::min(UpdateRegionWidth, MipWidth - DstBox.MinX);
        UpdateRegionHeight        = std::min(UpdateRegionHeight, MipHeight - DstBox.MinY);
        glCompressedTexSubImage3D(m_BindTarget, MipLevel,
                                  DstBox.MinX,
                                  DstBox.MinY,
                                  Slice,
                                  UpdateRegionWidth,
                                  UpdateRegionHeight,
                                  1,
                                  // The format must be the same compressed-texture format previously
                                  // specified by glTexStorage2D() (thank you OpenGL for another useless
                                  // parameter that is nothing but the source of confusion), otherwise
                                  // INVALID_OPERATION error is generated.
                                  m_GLTexFormat,
                                  // An INVALID_VALUE error is generated if imageSize is not consistent with
                                  // the format, dimensions, and contents of the compressed image( too little or
                                  // too much data ),
                                  StaticCast<GLsizei>(((DstBox.Height() + 3) / 4) * SubresData.Stride),
                                  // If a non-zero named buffer object is bound to the GL_PIXEL_UNPACK_BUFFER target, 'data' is treated
                                  // as a byte offset into the buffer object's data store.
                                  // https://www.khronos.org/registry/OpenGL-Refpages/gl4/html/glCompressedTexSubImage3D.xhtml
                                  SubresData.pSrcBuffer != nullptr ? reinterpret_cast<void*>(StaticCast<size_t>(SubresData.SrcOffset)) : SubresData.pData);
    }
    else
    {
        const TextureFormatAttribs TexFmtInfo = GetTextureFormatAttribs(m_Desc.Format);
        const Uint32               PixelSize  = Uint32{TexFmtInfo.NumComponents} * Uint32{TexFmtInfo.ComponentSize};
        VERIFY((SubresData.Stride % PixelSize) == 0, "Data stride is not multiple of pixel size");
        glPixelStorei(GL_UNPACK_ROW_LENGTH, StaticCast<GLint>(SubresData.Stride / PixelSize));

        // Every OpenGL API call that operates on cubemap array textures takes layer-faces, not array layers.
        // When uploading texel data to the cubemap array, the parameters that represent the Z component
        // are layer-faces. So if you want to upload to just the positive Z face of the second layer in the array,
        // you would use call glTexSubImage3D?, with the zoffset?? parameter set to 10 (layer 1 * 6 faces per layer + face index 4),
        // and the depth? set to 1 (because you're only uploading one layer-face).

        // Target must be GL_TEXTURE_3D, GL_TEXTURE_2D_ARRAY, or GL_TEXTURE_CUBE_MAP_ARRAY.
        // (NO individual cubemap faces GL_TEXTURE_CUBE_MAP_POSITIVE_X .. GL_TEXTURE_CUBE_MAP_NEGATIVE_Z!!!)
        glTexSubImage3D(m_BindTarget, MipLevel,
                        DstBox.MinX,
                        DstBox.MinY,
                        Slice,
                        DstBox.Width(),
                        DstBox.Height(),
                        1,
                        TransferAttribs.PixelFormat, TransferAttribs.DataType,
                        // If a non-zero named buffer object is bound to the GL_PIXEL_UNPACK_BUFFER target, 'data' is treated
                        // as a byte offset into the buffer object's data store.
                        // https://www.khronos.org/registry/OpenGL-Refpages/gl4/html/glTexSubImage3D.xhtml
                        SubresData.pSrcBuffer != nullptr ? reinterpret_cast<void*>(StaticCast<size_t>(SubresData.SrcOffset)) : SubresData.pData);
    }
    DEV_CHECK_GL_ERROR("Failed to update subimage data");

    if (UnpackBuffer != 0)
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    ContextState.BindTexture(-1, m_BindTarget, GLObjectWrappers::GLTextureObj::Null());
}

void TextureCubeArray_GL::AttachToFramebuffer(const TextureViewDesc& ViewDesc, GLenum AttachmentPoint, FRAMEBUFFER_TARGET_FLAGS Targets)
{
    // Same as for 2D array textures

    // Every OpenGL API call that operates on cubemap array textures takes layer-faces, not array layers.
    // So the parameters that represent the Z component are layer-faces.
    if (ViewDesc.NumArraySlices == 1)
    {
        // Texture name must either be zero or the name of an existing 3D texture, 1D or 2D array texture,
        // cube map array texture, or multisample array texture.
        if (Targets & FRAMEBUFFER_TARGET_FLAG_DRAW)
        {
            VERIFY_EXPR(ViewDesc.ViewType == TEXTURE_VIEW_RENDER_TARGET || ViewDesc.ViewType == TEXTURE_VIEW_DEPTH_STENCIL);
            glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, AttachmentPoint, m_GlTexture, ViewDesc.MostDetailedMip, ViewDesc.FirstArraySlice);
            DEV_CHECK_GL_ERROR("Failed to attach texture cubemap array to draw framebuffer");
        }
        if (Targets & FRAMEBUFFER_TARGET_FLAG_READ)
        {
            glFramebufferTextureLayer(GL_READ_FRAMEBUFFER, AttachmentPoint, m_GlTexture, ViewDesc.MostDetailedMip, ViewDesc.FirstArraySlice);
            DEV_CHECK_GL_ERROR("Failed to attach texture cubemap array to read framebuffer");
        }
    }
    else if (ViewDesc.NumArraySlices == m_Desc.ArraySize)
    {
        // glFramebufferTexture() attaches the given mipmap level as a layered image with the number of layers that the given texture has.
        if (Targets & FRAMEBUFFER_TARGET_FLAG_DRAW)
        {
            VERIFY_EXPR(ViewDesc.ViewType == TEXTURE_VIEW_RENDER_TARGET || ViewDesc.ViewType == TEXTURE_VIEW_DEPTH_STENCIL);
            glFramebufferTexture(GL_DRAW_FRAMEBUFFER, AttachmentPoint, m_GlTexture, ViewDesc.MostDetailedMip);
            DEV_CHECK_GL_ERROR("Failed to attach texture cubemap array to draw framebuffer");
        }
        if (Targets & FRAMEBUFFER_TARGET_FLAG_READ)
        {
            glFramebufferTexture(GL_READ_FRAMEBUFFER, AttachmentPoint, m_GlTexture, ViewDesc.MostDetailedMip);
            DEV_CHECK_GL_ERROR("Failed to attach texture cubemap array to read framebuffer");
        }
    }
    else
    {
        UNEXPECTED("Only one slice or the entire cubemap array can be attached to a framebuffer");
    }
}

void TextureCubeArray_GL::CopyTexSubimage(GLContextState& GLState, const CopyTexSubimageAttribs& Attribs)
{
    GLState.BindTexture(-1, GetBindTarget(), GetGLHandle());

    glCopyTexSubImage3D(GetBindTarget(),
                        Attribs.DstMip,
                        Attribs.DstX,
                        Attribs.DstY,
                        Attribs.DstLayer,
                        Attribs.SrcBox.MinX,
                        Attribs.SrcBox.MinY,
                        Attribs.SrcBox.Width(),
                        Attribs.SrcBox.Height());
    DEV_CHECK_GL_ERROR("Failed to copy subimage data to texture cube array");
}

} // namespace Diligent
