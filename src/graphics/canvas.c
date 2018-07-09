#include "graphics/canvas.h"
#include "graphics/gpu.h"
#include "graphics/graphics.h"
#include "data/blob.h"
#include "util.h"
#include <stdlib.h>

bool lovrCanvasSupportsFormat(TextureFormat format) {
  switch (format) {
    case FORMAT_RGB:
    case FORMAT_RGBA:
    case FORMAT_RGBA16F:
    case FORMAT_RGBA32F:
    case FORMAT_RG11B10F:
      return true;
    case FORMAT_DXT1:
    case FORMAT_DXT3:
    case FORMAT_DXT5:
      return false;
  }
}

Canvas* lovrCanvasCreate(int width, int height, TextureFormat format, CanvasFlags flags) {
  TextureData* textureData = lovrTextureDataGetEmpty(width, height, format);
  Texture* texture = lovrTextureCreate(TEXTURE_2D, &textureData, 1, true, flags.mipmaps);
  if (!texture) return NULL;

  Canvas* canvas = lovrAlloc(sizeof(Canvas), lovrCanvasDestroy);
  canvas->texture = *texture;
  canvas->flags = flags;

  // Framebuffer
  glGenFramebuffers(1, &canvas->framebuffer);
  gpuBindFramebuffer(canvas->framebuffer);

  // Color attachment
  if (flags.msaa > 0) {
    GLenum internalFormat = lovrTextureFormatGetGLInternalFormat(format, lovrGraphicsIsGammaCorrect());
    glGenRenderbuffers(1, &canvas->msaaTexture);
    glBindRenderbuffer(GL_RENDERBUFFER, canvas->msaaTexture);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, flags.msaa, internalFormat, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, canvas->msaaTexture);
  } else {
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, canvas->texture.id, 0);
  }

  // Depth/Stencil
  if (flags.depth || flags.stencil) {
    GLenum depthStencilFormat = flags.stencil ? GL_DEPTH24_STENCIL8 : GL_DEPTH_COMPONENT24;
    glGenRenderbuffers(1, &canvas->depthStencilBuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, canvas->depthStencilBuffer);
    if (flags.msaa > 0) {
      glRenderbufferStorageMultisample(GL_RENDERBUFFER, flags.msaa, depthStencilFormat, width, height);
    } else {
      glRenderbufferStorage(GL_RENDERBUFFER, depthStencilFormat, width, height);
    }

    if (flags.depth) {
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, canvas->depthStencilBuffer);
    }

    if (flags.stencil) {
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, canvas->depthStencilBuffer);
    }
  }

  // Resolve framebuffer
  if (flags.msaa > 0) {
    glGenFramebuffers(1, &canvas->resolveFramebuffer);
    gpuBindFramebuffer(canvas->resolveFramebuffer);
    glBindTexture(GL_TEXTURE_2D, canvas->texture.id);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, canvas->texture.id, 0);
    gpuBindFramebuffer(canvas->framebuffer);
  }

  lovrAssert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "Error creating Canvas");
  lovrGraphicsClear(true, true, true, (Color) { 0, 0, 0, 0 }, 1., 0);
  gpuBindFramebuffer(0);

  return canvas;
}

void lovrCanvasDestroy(void* ref) {
  Canvas* canvas = ref;
  glDeleteFramebuffers(1, &canvas->framebuffer);
  if (canvas->resolveFramebuffer) {
    glDeleteFramebuffers(1, &canvas->resolveFramebuffer);
  }
  if (canvas->depthStencilBuffer) {
    glDeleteRenderbuffers(1, &canvas->depthStencilBuffer);
  }
  if (canvas->msaaTexture) {
    glDeleteTextures(1, &canvas->msaaTexture);
  }
  lovrTextureDestroy(ref);
}

uint32_t lovrCanvasGetId(Canvas* canvas) {
  return canvas->framebuffer;
}

void lovrCanvasResolve(Canvas* canvas) {
  if (canvas->flags.msaa > 0) {
    int width = canvas->texture.width;
    int height = canvas->texture.height;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, canvas->framebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, canvas->resolveFramebuffer);
    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
  }

  if (canvas->flags.mipmaps) {
    lovrGraphicsBindTexture(&canvas->texture, canvas->texture.type, 0);
    glGenerateMipmap(canvas->texture.type);
  }
}

TextureFormat lovrCanvasGetFormat(Canvas* canvas) {
  return canvas->texture.slices[0]->format;
}

int lovrCanvasGetMSAA(Canvas* canvas) {
  return canvas->flags.msaa;
}

TextureData* lovrCanvasNewTextureData(Canvas* canvas) {
  TextureData* textureData = lovrTextureDataGetBlank(canvas->texture.width, canvas->texture.height, 0, FORMAT_RGBA);
  if (!textureData) return NULL;

  gpuBindFramebuffer(canvas->framebuffer);
  glReadPixels(0, 0, canvas->texture.width, canvas->texture.height, GL_RGBA, GL_UNSIGNED_BYTE, textureData->blob.data);

  return textureData;
}
