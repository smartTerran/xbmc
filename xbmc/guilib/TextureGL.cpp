/*
 *      Copyright (C) 2005-2015 Team XBMC
 *      http://kodi.tv
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "ServiceBroker.h"
#include "Texture.h"
#include "rendering/RenderSystem.h"
#include "utils/log.h"
#include "utils/GLUtils.h"
#include "guilib/TextureManager.h"
#include "settings/AdvancedSettings.h"
#ifdef TARGET_POSIX
#include "platform/linux/XMemUtils.h"
#endif


/************************************************************************/
/*    CGLTexture                                                       */
/************************************************************************/
CGLTexture::CGLTexture(unsigned int width, unsigned int height, unsigned int format)
: CBaseTexture(width, height, format)
{
  unsigned int major, minor;
  CServiceBroker::GetRenderSystem().GetRenderVersion(major, minor);
  if (major >= 3)
    m_isOglVersion3orNewer = true;
}

CGLTexture::~CGLTexture()
{
  DestroyTextureObject();
}

void CGLTexture::CreateTextureObject()
{
  glGenTextures(1, (GLuint*) &m_texture);
}

void CGLTexture::DestroyTextureObject()
{
  if (m_texture)
    g_TextureManager.ReleaseHwTexture(m_texture);
}

void CGLTexture::LoadToGPU()
{
  if (!m_pixels)
  {
    // nothing to load - probably same image (no change)
    return;
  }
  if (m_texture == 0)
  {
    // Have OpenGL generate a texture object handle for us
    // this happens only one time - the first time the texture is loaded
    CreateTextureObject();
  }

  // Bind the texture object
  glBindTexture(GL_TEXTURE_2D, m_texture);

  GLenum filter = (m_scalingMethod == TEXTURE_SCALING::NEAREST ? GL_NEAREST : GL_LINEAR);

  // Set the texture's stretching properties
  if (IsMipmapped())
  {
    GLenum mipmapFilter = (m_scalingMethod == TEXTURE_SCALING::NEAREST ? GL_LINEAR_MIPMAP_NEAREST : GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipmapFilter);

#ifndef HAS_GLES
    // Lower LOD bias equals more sharpness, but less smooth animation
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, -0.5f);
    if (!m_isOglVersion3orNewer)
      glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
#endif
  }
  else
  {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
  }

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  unsigned int maxSize = CServiceBroker::GetRenderSystem().GetMaxTextureSize();
  if (m_textureHeight > maxSize)
  {
    CLog::Log(LOGERROR, "GL: Image height %d too big to fit into single texture unit, truncating to %u", m_textureHeight, maxSize);
    m_textureHeight = maxSize;
  }
  if (m_textureWidth > maxSize)
  {
    CLog::Log(LOGERROR, "GL: Image width %d too big to fit into single texture unit, truncating to %u", m_textureWidth, maxSize);
#ifndef HAS_GLES
    glPixelStorei(GL_UNPACK_ROW_LENGTH, m_textureWidth);
#endif
    m_textureWidth = maxSize;
  }

#ifndef HAS_GLES
  GLenum format = GL_BGRA;
  GLint numcomponents = GL_RGBA;

  switch (m_format)
  {
  case XB_FMT_DXT1:
    format = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
    break;
  case XB_FMT_DXT3:
    format = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
    break;
  case XB_FMT_DXT5:
  case XB_FMT_DXT5_YCoCg:
    format = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    break;
  case XB_FMT_RGB8:
    format = GL_RGB;
    numcomponents = GL_RGB;
    break;
  case XB_FMT_A8R8G8B8:
  default:
    break;
  }

  if ((m_format & XB_FMT_DXT_MASK) == 0)
  {
    glTexImage2D(GL_TEXTURE_2D, 0, numcomponents,
                 m_textureWidth, m_textureHeight, 0,
                 format, GL_UNSIGNED_BYTE, m_pixels);
  }
  else
  {
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, format,
                           m_textureWidth, m_textureHeight, 0,
                           GetPitch() * GetRows(), m_pixels);
  }

  if (IsMipmapped() && m_isOglVersion3orNewer)
  {
    glGenerateMipmap(GL_TEXTURE_2D);
  }

  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

#else	// GLES version

  // All incoming textures are BGRA, which GLES does not necessarily support.
  // Some (most?) hardware supports BGRA textures via an extension.
  // If not, we convert to RGBA first to avoid having to swizzle in shaders.
  // Explicitly define GL_BGRA_EXT here in the case that it's not defined by
  // system headers, and trust the extension list instead.
#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT 0x80E1
#endif

  GLint internalformat;
  GLenum pixelformat;

  switch (m_format)
  {
    default:
    case XB_FMT_RGBA8:
      internalformat = pixelformat = GL_RGBA;
      break;
    case XB_FMT_RGB8:
      internalformat = pixelformat = GL_RGB;
      break;
    case XB_FMT_A8R8G8B8:
      if (CServiceBroker::GetRenderSystem().SupportsBGRA())
      {
        internalformat = pixelformat = GL_BGRA_EXT;
      }
      else if (CServiceBroker::GetRenderSystem().SupportsBGRAApple())
      {
        // Apple's implementation does not conform to spec. Instead, they require
        // differing format/internalformat, more like GL.
        internalformat = GL_RGBA;
        pixelformat = GL_BGRA_EXT;
      }
      else
      {
        SwapBlueRed(m_pixels, m_textureHeight, GetPitch());
        internalformat = pixelformat = GL_RGBA;
      }
      break;
  }
  glTexImage2D(GL_TEXTURE_2D, 0, internalformat, m_textureWidth, m_textureHeight, 0,
    pixelformat, GL_UNSIGNED_BYTE, m_pixels);

  if (IsMipmapped())
  {
    glGenerateMipmap(GL_TEXTURE_2D);
  }

#endif
  VerifyGLState();

  if (!m_bCacheMemory)
  {
    _aligned_free(m_pixels);
    m_pixels = NULL;
  }

  m_loadedToGPU = true;
}

void CGLTexture::BindToUnit(unsigned int unit)
{
  glActiveTexture(GL_TEXTURE0 + unit);
  glBindTexture(GL_TEXTURE_2D, m_texture);
}

