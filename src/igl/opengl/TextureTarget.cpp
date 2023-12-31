/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <igl/opengl/TextureTarget.h>

#include <igl/opengl/Errors.h>

namespace igl {
namespace opengl {

TextureTarget::~TextureTarget() {
  if (renderBufferID_ != 0) {
    getContext().deleteRenderbuffers(1, &renderBufferID_);
  }
}

TextureType TextureTarget::getType() const {
  return TextureType::TwoD;
}

ulong_t TextureTarget::getUsage() const {
  return TextureDesc::TextureUsageBits::Attachment;
}

// create a 2D texture given the specified dimensions and format
Result TextureTarget::create(const TextureDesc& desc, bool hasStorageAlready) {
  Result result = Super::create(desc, hasStorageAlready);
  if (result.isOk()) {
    if (desc.usage & TextureDesc::TextureUsageBits::Attachment) {
      result = createRenderBuffer(desc, hasStorageAlready);
    } else {
      result = Result(Result::Code::Unsupported, "invalid usage!");
    }
  }
  return result;
}

// create a render buffer for render target usages
Result TextureTarget::createRenderBuffer(const TextureDesc& desc, bool hasStorageAlready) {
  if (desc.type != TextureType::TwoD) {
    // we currently only support 2D textures with GLES 2.0
    return Result{Result::Code::Unimplemented,
                  "Non-2D textures are currently unsupported on GL backend."};
  }

  if (!toRenderBufferFormatGL(desc.usage, glInternalFormat_)) {
    // can't create a texture with the given format
    return Result{Result::Code::ArgumentInvalid, "Invalid texture format"};
  }

  // create the GL render buffer
  getContext().genRenderbuffers(1, &renderBufferID_);

  if (!hasStorageAlready) {
    getContext().bindRenderbuffer(GL_RENDERBUFFER, renderBufferID_);

    if (desc.numSamples > 1) {
      getContext().renderbufferStorageMultisample(
          GL_RENDERBUFFER, desc.numSamples, glInternalFormat_, getWidth(), getHeight());
    } else {
      getContext().renderbufferStorage(
          GL_RENDERBUFFER, glInternalFormat_, (GLsizei)getWidth(), (GLsizei)getHeight());
    }

    getContext().bindRenderbuffer(GL_RENDERBUFFER, 0);
  }

  return Result();
}

void TextureTarget::bind() {
  getContext().bindRenderbuffer(GL_RENDERBUFFER, renderBufferID_);
}

void TextureTarget::unbind() {
  getContext().bindRenderbuffer(GL_RENDERBUFFER, 0);
}

void TextureTarget::bindImage(size_t /*unit*/) {
  IGL_ASSERT_NOT_IMPLEMENTED();
}

void TextureTarget::attachAsColor(uint32_t index, uint32_t /*face*/, uint32_t /*mipmapLevel*/) {
  if (IGL_VERIFY(renderBufferID_)) {
    getContext().framebufferRenderbuffer(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + index, GL_RENDERBUFFER, renderBufferID_);
  }
}

void TextureTarget::detachAsColor(uint32_t /*index*/, uint32_t /*face*/, uint32_t /*mipmapLevel*/) {
  // Binding to render buffer ID 0 is undefined in iOS, and currently we don't
  // have a need to unbind for this texture type
  IGL_ASSERT_NOT_IMPLEMENTED();
}

void TextureTarget::attachAsDepth() {
  if (IGL_VERIFY(renderBufferID_)) {
    getContext().framebufferRenderbuffer(
        GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, renderBufferID_);
  }
}

void TextureTarget::attachAsStencil() {
  if (IGL_VERIFY(renderBufferID_)) {
    getContext().framebufferRenderbuffer(
        GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, renderBufferID_);
  }
}

bool TextureTarget::toRenderBufferFormatGL(TextureDesc::TextureUsage usage,
                                           GLenum& formatGL) const {
  FormatDescGL formatDescGL;
  if (!toFormatDescGL(getFormat(), usage, formatDescGL)) {
    return false;
  }
  formatGL = formatDescGL.internalFormat;
  return true;
}

} // namespace opengl
} // namespace igl
