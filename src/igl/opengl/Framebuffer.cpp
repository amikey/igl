/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <igl/opengl/Framebuffer.h>

#include <cstdlib>
#include <igl/RenderPass.h>
#include <igl/opengl/CommandBuffer.h>
#include <igl/opengl/Device.h>
#include <igl/opengl/DummyTexture.h>
#include <igl/opengl/Errors.h>
#include <igl/opengl/Texture.h>

#include <algorithm>
#if !IGL_PLATFORM_ANDROID
#include <string>
#else
#include <sstream>

namespace std {

// TODO: Remove once STL in Android NDK supports std::to_string
template<typename T>
string to_string(const T& t) {
  ostringstream os;
  os << t;
  return os.str();
}

} // namespace std

#endif

namespace igl {
namespace opengl {

namespace {

Result checkFramebufferStatus(IContext& context) {
  auto code = Result::Code::Ok;
  std::string message;

  // check that we've created a proper frame buffer
  GLenum status = context.checkFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    code = Result::Code::RuntimeError;

    switch (status) {
    case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
      message = "GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT";
      break;
    case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
      message = "GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT";
      break;
    case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS:
      message = "GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS";
      break;
    case GL_FRAMEBUFFER_UNSUPPORTED:
      message = "GL_FRAMEBUFFER_UNSUPPORTED";
      break;
    default:
      message = "GL_FRAMEBUFFER unknown error: " + std::to_string(status);
      break;
    }
  }

  return Result(code, message);
}

} // namespace

FramebufferBindingGuard::FramebufferBindingGuard(IContext& context) :
  context_(context),
  currentRenderbuffer_(0),
  currentFramebuffer_(0),
  currentReadFramebuffer_(0),
  currentDrawFramebuffer_(0) {
  context_.getIntegerv(GL_RENDERBUFFER_BINDING, reinterpret_cast<GLint*>(&currentRenderbuffer_));

  // Only restore currently bound framebuffer if it's valid
  Result result = checkFramebufferStatus(context_);
  if (result.isOk()) {
    if (context_.deviceFeatures().hasFeature(DeviceFeatures::ReadWriteFramebuffer)) {
      context_.getIntegerv(GL_READ_FRAMEBUFFER_BINDING,
                           reinterpret_cast<GLint*>(&currentReadFramebuffer_));
      context_.getIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,
                           reinterpret_cast<GLint*>(&currentDrawFramebuffer_));
    } else {
      context_.getIntegerv(GL_FRAMEBUFFER_BINDING, reinterpret_cast<GLint*>(&currentFramebuffer_));
    }
  }
}

FramebufferBindingGuard::~FramebufferBindingGuard() {
  if (context_.deviceFeatures().hasFeature(DeviceFeatures::ReadWriteFramebuffer)) {
    context_.bindFramebuffer(GL_READ_FRAMEBUFFER, currentReadFramebuffer_);
    context_.bindFramebuffer(GL_DRAW_FRAMEBUFFER, currentDrawFramebuffer_);
  } else {
    context_.bindFramebuffer(GL_FRAMEBUFFER, currentFramebuffer_);
  }

  context_.bindRenderbuffer(GL_RENDERBUFFER, currentRenderbuffer_);
}

///--------------------------------------
/// MARK: - Framebuffer

Framebuffer::Framebuffer(IContext& context) : WithContext(context) {}

void Framebuffer::bindBuffer() const {
  getContext().bindFramebuffer(GL_FRAMEBUFFER, frameBufferID_);
}

void Framebuffer::bindBufferForRead() const {
  // TODO: enable optimization path
  if (getContext().deviceFeatures().hasFeature(DeviceFeatures::ReadWriteFramebuffer)) {
    getContext().bindFramebuffer(GL_READ_FRAMEBUFFER, frameBufferID_);
  } else {
    bindBuffer();
  }
}

void Framebuffer::copyBytesColorAttachment(ICommandQueue& /* unused */,
                                           size_t index,
                                           void* pixelBytes,
                                           const TextureRangeDesc& range,
                                           size_t bytesPerRow) const {
  // Only support attachment 0 because that's what glReadPixels supports
  if (index != 0) {
    IGL_ASSERT_MSG(0, "Invalid index: %d", index);
    return;
  }

  auto itexture = getColorAttachment(index);
  if (itexture != nullptr) {
    FramebufferBindingGuard guard(getContext());

    auto& texture = static_cast<igl::opengl::Texture&>(*itexture);
    GLuint extraFramebufferId = 0;
    if (texture.getNumLayers() > 1) {
      getContext().genFramebuffers(1, &extraFramebufferId);
      getContext().bindFramebuffer(GL_READ_FRAMEBUFFER, extraFramebufferId);
      attachAsColorLayer(itexture, (uint32_t)range.layer);
      checkFramebufferStatus(getContext());
    } else {
      bindBufferForRead();
    }

    if (bytesPerRow == 0) {
      bytesPerRow = itexture->getProperties().getBytesPerRow(range);
    }
    getContext().pixelStorei(GL_PACK_ALIGNMENT, texture.getAlignment(bytesPerRow, range.mipLevel));

    // Note read out format is based on
    // (https://www.khronos.org/registry/OpenGL-Refpages/es2.0/xhtml/glReadPixels.xml)
    // as using GL_RGBA with GL_UNSIGNED_BYTE is the only always supported combination
    // with glReadPixels.
    getContext().flush();
    auto format = GL_RGBA;
    auto intFormat = GL_RGBA_INTEGER;

    // @fb-only
    if (texture.getFormat() == TextureFormat::RGBA_UInt32) {
      if (getContext().deviceFeatures().hasTextureFeature(TextureFeatures::TextureInteger)) {
        getContext().readPixels(static_cast<GLint>(range.x),
                                static_cast<GLint>(range.y),
                                static_cast<GLsizei>(range.width),
                                static_cast<GLsizei>(range.height),
                                intFormat,
                                GL_UNSIGNED_INT,
                                pixelBytes);
      } else {
        IGL_ASSERT_NOT_IMPLEMENTED();
      }
    } else {
      getContext().readPixels(static_cast<GLint>(range.x),
                              static_cast<GLint>(range.y),
                              static_cast<GLsizei>(range.width),
                              static_cast<GLsizei>(range.height),
                              format,
                              GL_UNSIGNED_BYTE,
                              pixelBytes);
    }

    if (texture.getNumLayers() > 1) {
      attachAsColorLayer(nullptr, 0);
      checkFramebufferStatus(getContext());
      if (extraFramebufferId > 0) {
        getContext().deleteFramebuffers(1, &extraFramebufferId);
        extraFramebufferId = 0;
      }
    }
  } else {
    IGL_ASSERT_NOT_IMPLEMENTED();
  }
}

void Framebuffer::copyBytesDepthAttachment(ICommandQueue& /* unused */,
                                           void* /*pixelBytes*/,
                                           const TextureRangeDesc& /*range*/,
                                           size_t /*bytesPerRow*/) const {
  IGL_ASSERT_NOT_IMPLEMENTED();
}

void Framebuffer::copyBytesStencilAttachment(ICommandQueue& /* unused */,
                                             void* /*pixelBytes*/,
                                             const TextureRangeDesc& /*range*/,
                                             size_t /*bytesPerRow*/) const {
  IGL_ASSERT_NOT_IMPLEMENTED();
}

void Framebuffer::copyTextureColorAttachment(ICommandQueue& /*cmdQueue*/,
                                             size_t index,
                                             std::shared_ptr<ITexture> destTexture,
                                             const TextureRangeDesc& range) const {
  // Only support attachment 0 because that's what glCopyTexImage2D supports
  if (index != 0 || getColorAttachment(index) == nullptr) {
    IGL_ASSERT_MSG(0, "Invalid index: %d", index);
    return;
  }

  FramebufferBindingGuard guard(getContext());

  bindBufferForRead();

  auto& dest = static_cast<Texture&>(*destTexture);
  dest.bind();

  getContext().copyTexSubImage2D(GL_TEXTURE_2D,
                                 0,
                                 0,
                                 0,
                                 static_cast<GLint>(range.x),
                                 static_cast<GLint>(range.y),
                                 static_cast<GLsizei>(range.width),
                                 static_cast<GLsizei>(range.height));
}

void Framebuffer::attachAsColorLayer(const std::shared_ptr<ITexture>& texture,
                                     uint32_t layer) const {
  if (texture) {
    auto& glTex = static_cast<Texture&>(*texture);
    getContext().framebufferTextureLayer(
        GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, glTex.getId(), 0, layer);
  } else {
    getContext().framebufferTextureLayer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 0, 0, 0);
  }
}

///--------------------------------------
/// MARK: - CustomFramebuffer

CustomFramebuffer::~CustomFramebuffer() {
  if (frameBufferID_ != 0) {
    getContext().deleteFramebuffers(1, &frameBufferID_);
    frameBufferID_ = 0;
  }
}

std::vector<size_t> CustomFramebuffer::getColorAttachmentIndices() const {
  std::vector<size_t> indices;

  for (const auto& attachment : renderTarget_.colorAttachments) {
    indices.push_back(attachment.first);
  }

  return indices;
}

std::shared_ptr<ITexture> CustomFramebuffer::getColorAttachment(size_t index) const {
  auto colorAttachment = renderTarget_.colorAttachments.find(index);

  if (colorAttachment != renderTarget_.colorAttachments.end()) {
    return colorAttachment->second.texture;
  }

  return nullptr;
}

std::shared_ptr<ITexture> CustomFramebuffer::getResolveColorAttachment(size_t index) const {
  auto colorAttachment = renderTarget_.colorAttachments.find(index);

  if (colorAttachment != renderTarget_.colorAttachments.end()) {
    return colorAttachment->second.resolveTexture;
  }

  return nullptr;
}

std::shared_ptr<ITexture> CustomFramebuffer::getDepthAttachment() const {
  return renderTarget_.depthAttachment.texture;
}

std::shared_ptr<ITexture> CustomFramebuffer::getResolveDepthAttachment() const {
  return renderTarget_.depthAttachment.resolveTexture;
}

std::shared_ptr<ITexture> CustomFramebuffer::getStencilAttachment() const {
  return renderTarget_.stencilAttachment.texture;
}

std::shared_ptr<ITexture> CustomFramebuffer::updateDrawable(std::shared_ptr<ITexture> texture) {
  auto colorAttachment0 = getColorAttachment(0);

  // Unbind currently bound texture if we are updating to nullptr
  if (texture == nullptr && colorAttachment0 != nullptr) {
    auto& curAttachment = static_cast<Texture&>(*colorAttachment0);

    bindBuffer();
    curAttachment.detachAsColor(0);
    renderTarget_.colorAttachments.erase(0);
  }

  if (texture != nullptr && getColorAttachment(0) != texture) {
    FramebufferBindingGuard guard(getContext());
    bindBuffer();
    attachAsColor(texture, 0);
    renderTarget_.colorAttachments[0].texture = texture;
  }

  return texture;
}

bool CustomFramebuffer::isInitialized() const {
  return initialized_;
}

bool CustomFramebuffer::hasImplicitColorAttachment() const {
  if (frameBufferID_ != 0) {
    return false;
  }

  auto colorAttachment0 = renderTarget_.colorAttachments.find(0);

  return colorAttachment0 != renderTarget_.colorAttachments.end() &&
         colorAttachment0->second.texture != nullptr &&
         static_cast<Texture&>(*colorAttachment0->second.texture).isImplicitStorage();
}

void CustomFramebuffer::initialize(const FramebufferDesc& desc, Result* outResult) {
  if (IGL_UNEXPECTED(isInitialized())) {
    Result::setResult(outResult, Result::Code::RuntimeError, "Framebuffer already initialized.");
    return;
  }
  initialized_ = true;

  renderTarget_ = desc;

  // Restore framebuffer binding
  FramebufferBindingGuard guard(getContext());

  if (hasImplicitColorAttachment()) {
    // Don't generate framebuffer id. Use implicit framebuffer supplied by containing view
    Result::setOk(outResult);
  } else {
    prepareResource(outResult);
  }
}

void CustomFramebuffer::prepareResource(Result* outResult) {
  // create a new frame buffer if we don't already have one
  getContext().genFramebuffers(1, &frameBufferID_);

  bindBuffer();

  std::vector<GLenum> drawBuffers;

  // attach the textures and render buffers to the frame buffer
  for (const auto& colorAttachment : renderTarget_.colorAttachments) {
    auto const colorAttachmentTexture = colorAttachment.second.texture;
    if (colorAttachmentTexture != nullptr) {
      size_t index = colorAttachment.first;
      attachAsColor(colorAttachmentTexture, static_cast<uint32_t>(index));
      drawBuffers.push_back(static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + index));
    }
  }

  std::sort(drawBuffers.begin(), drawBuffers.end());

  if (drawBuffers.size() > 1) {
    getContext().drawBuffers(static_cast<GLsizei>(drawBuffers.size()), drawBuffers.data());
  }

  if (renderTarget_.depthAttachment.texture != nullptr) {
    attachAsDepth(renderTarget_.depthAttachment.texture);
  }

  if (renderTarget_.stencilAttachment.texture != nullptr) {
    attachAsStencil(renderTarget_.stencilAttachment.texture);
  }

  Result result = checkFramebufferStatus(getContext());
  IGL_ASSERT_MSG(result.isOk(), result.message.c_str());
  if (outResult) {
    *outResult = result;
  }
  if (!result.isOk()) {
    return;
  }

  // Check if resolve framebuffer is needed
  FramebufferDesc resolveDesc;
  auto createResolveFramebuffer = false;
  for (const auto& colorAttachment : renderTarget_.colorAttachments) {
    if (colorAttachment.second.resolveTexture) {
      createResolveFramebuffer = true;
      FramebufferDesc::AttachmentDesc attachment;
      attachment.texture = colorAttachment.second.resolveTexture;
      resolveDesc.colorAttachments.emplace(colorAttachment.first, attachment);
    }
  }
  if (createResolveFramebuffer &&
      resolveDesc.colorAttachments.size() != renderTarget_.colorAttachments.size()) {
    IGL_ASSERT_NOT_REACHED();
    if (outResult) {
      *outResult = igl::Result(igl::Result::Code::ArgumentInvalid,
                               "If resolve texture is specified on a color attachment it must be "
                               "specified on all of them");
    }
    return;
  }

  if (renderTarget_.depthAttachment.resolveTexture) {
    createResolveFramebuffer = true;
    resolveDesc.depthAttachment.texture = renderTarget_.depthAttachment.resolveTexture;
  }
  if (renderTarget_.stencilAttachment.resolveTexture) {
    createResolveFramebuffer = true;
    resolveDesc.stencilAttachment.texture = renderTarget_.stencilAttachment.resolveTexture;
  }

  if (createResolveFramebuffer) {
    auto cfb = std::make_shared<CustomFramebuffer>(getContext());
    cfb->initialize(resolveDesc, &result);
    if (outResult) {
      *outResult = result;
    }
    resolveFramebuffer = std::move(cfb);
  }
}

Viewport CustomFramebuffer::getViewport() const {
  auto texture = getColorAttachment(0);

  if (texture == nullptr) {
    IGL_ASSERT_MSG(0, "No color attachment in CustomFrameBuffer at index 0");
    return {0, 0, 0, 0};
  }

  // By default, we set viewport to dimensions of framebuffer
  const auto size = texture->getSize();
  return {0, 0, size.width, size.height};
}

void CustomFramebuffer::bind(const RenderPassDesc& renderPass) const {
  // Cache renderPass for unbind
  renderPass_ = renderPass;

  bindBuffer();

  for (auto colorAttachment : renderTarget_.colorAttachments) {
    auto const colorAttachmentTexture = colorAttachment.second.texture;

    if (colorAttachmentTexture == nullptr) {
      continue;
    }
#if !IGL_OPENGL_ES
    // OpenGL ES doesn't need to call glEnable. All it needs is an sRGB framebuffer.
    if (getContext().deviceFeatures().hasFeature(DeviceFeatures::SRGB)) {
      if (colorAttachmentTexture->getProperties().isSRGB()) {
        getContext().enable(GL_FRAMEBUFFER_SRGB);
      } else {
        getContext().disable(GL_FRAMEBUFFER_SRGB);
      }
    }
#endif
    if (colorAttachmentTexture->getType() == igl::TextureType::Cube) {
      const size_t index = colorAttachment.first;
      IGL_ASSERT(index >= 0 && index < renderPass.colorAttachments.size());
      attachAsColor(colorAttachmentTexture,
                    (uint32_t)index,
                    (uint32_t)renderPass.colorAttachments[index].layer,
                    (uint32_t)renderPass.colorAttachments[index].mipmapLevel);
    }
  }
  // clear the buffers if we're not loading previous contents
  GLbitfield clearMask = 0;
  auto colorAttachment0 = renderTarget_.colorAttachments.find(0);

  if (colorAttachment0 != renderTarget_.colorAttachments.end() &&
      colorAttachment0->second.texture != nullptr &&
      renderPass_.colorAttachments[0].loadAction == LoadAction::Clear) {
    clearMask |= GL_COLOR_BUFFER_BIT;
    auto clearColor = renderPass_.colorAttachments[0].clearColor;
    getContext().colorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    getContext().clearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
  }
  if (renderTarget_.depthAttachment.texture != nullptr) {
    if (renderPass_.depthAttachment.loadAction == LoadAction::Clear) {
      clearMask |= GL_DEPTH_BUFFER_BIT;
      getContext().depthMask(GL_TRUE);
      getContext().clearDepthf(renderPass_.depthAttachment.clearDepth);
    }
  }
  if (renderTarget_.stencilAttachment.texture != nullptr) {
    getContext().enable(GL_STENCIL_TEST);
    if (renderPass_.stencilAttachment.loadAction == LoadAction::Clear) {
      clearMask |= GL_STENCIL_BUFFER_BIT;
      getContext().stencilMask(0xFF);
      getContext().clearStencil(renderPass_.stencilAttachment.clearStencil);
    }
  }

  if (clearMask != 0) {
    getContext().clear(clearMask);
  }
}

void CustomFramebuffer::unbind() const {
  // discard the depthStencil if we don't need to store its contents
  GLenum attachments[3];
  GLsizei numAttachments = 0;
  auto colorAttachment0 = renderTarget_.colorAttachments.find(0);

  if (colorAttachment0 != renderTarget_.colorAttachments.end() &&
      colorAttachment0->second.texture != nullptr &&
      renderPass_.colorAttachments[0].storeAction != StoreAction::Store) {
    attachments[numAttachments++] = GL_COLOR_ATTACHMENT0;
  }
  if (renderTarget_.depthAttachment.texture != nullptr) {
    if (renderPass_.depthAttachment.storeAction != StoreAction::Store) {
      attachments[numAttachments++] = GL_DEPTH_ATTACHMENT;
    }
  }
  if (renderTarget_.stencilAttachment.texture != nullptr) {
    getContext().disable(GL_STENCIL_TEST);
    if (renderPass_.stencilAttachment.storeAction != StoreAction::Store) {
      attachments[numAttachments++] = GL_STENCIL_ATTACHMENT;
    }
  }

  if (numAttachments > 0) {
    auto& features = getContext().deviceFeatures();
    if (features.hasInternalFeature(InternalFeatures::InvalidateFramebuffer)) {
      getContext().invalidateFramebuffer(GL_FRAMEBUFFER, numAttachments, attachments);
    }
  }
}

void CustomFramebuffer::attachAsColor(const std::shared_ptr<ITexture>& texture,
                                      uint32_t index,
                                      uint32_t face,
                                      uint32_t mipmapLevel) const {
  auto& glTex = static_cast<Texture&>(*texture);
  if (renderTarget_.mode == FramebufferMode::Mono) {
    glTex.attachAsColor(index, face, mipmapLevel);
  } else if (renderTarget_.mode == FramebufferMode::Stereo) {
    auto numSamples = texture->getSamples();
    if (numSamples > 1) {
      IGL_ASSERT_MSG(index == 0, "Multisample framebuffer can only use GL_COLOR_ATTACHMENT0");
      getContext().framebufferTextureMultisampleMultiview(
          GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, glTex.getId(), 0, (GLsizei)numSamples, 0, 2);
    } else {
      getContext().framebufferTextureMultiview(
          GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + index, glTex.getId(), 0, 0, 2);
    }
  } else {
    IGL_ASSERT_MSG(0, "MultiviewMode::Multiview not implemented.");
  }
}

void CustomFramebuffer::attachAsDepth(const std::shared_ptr<ITexture>& texture) const {
  auto& glTex = static_cast<Texture&>(*texture);
  if (renderTarget_.mode == FramebufferMode::Mono) {
    glTex.attachAsDepth();
  } else if (renderTarget_.mode == FramebufferMode::Stereo) {
    auto numSamples = texture->getSamples();
    if (numSamples > 1) {
      getContext().framebufferTextureMultisampleMultiview(
          GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, glTex.getId(), 0, (GLsizei)numSamples, 0, 2);
    } else {
      getContext().framebufferTextureMultiview(
          GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, glTex.getId(), 0, 0, 2);
    }
  } else {
    IGL_ASSERT_MSG(0, "MultiviewMode::Multiview not implemented.");
  }
}

void CustomFramebuffer::attachAsStencil(const std::shared_ptr<ITexture>& texture) const {
  auto& glTex = static_cast<Texture&>(*texture);
  if (renderTarget_.mode == FramebufferMode::Mono) {
    glTex.attachAsStencil();
  } else if (renderTarget_.mode == FramebufferMode::Stereo) {
    auto numSamples = texture->getSamples();
    if (numSamples > 1) {
      getContext().framebufferTextureMultisampleMultiview(
          GL_DRAW_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, glTex.getId(), 0, (GLsizei)numSamples, 0, 2);
    } else {
      getContext().framebufferTextureMultiview(
          GL_DRAW_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, glTex.getId(), 0, 0, 2);
    }
  } else {
    IGL_ASSERT_MSG(0, "MultiviewMode::Multiview not implemented.");
  }
}

///--------------------------------------
/// MARK: - CurrentFramebuffer

CurrentFramebuffer::CurrentFramebuffer(IContext& context) : Super(context) {
  getContext().getIntegerv(GL_FRAMEBUFFER_BINDING, reinterpret_cast<GLint*>(&frameBufferID_));

  GLint viewport[4];
  getContext().getIntegerv(GL_VIEWPORT, viewport);
  viewport_.x = static_cast<float>(viewport[0]);
  viewport_.y = static_cast<float>(viewport[1]);
  viewport_.width = static_cast<float>(viewport[2]);
  viewport_.height = static_cast<float>(viewport[3]);

  colorAttachment_ = std::make_shared<DummyTexture>(Size(viewport_.width, viewport_.height));
}

std::vector<size_t> CurrentFramebuffer::getColorAttachmentIndices() const {
  return std::vector<size_t>{0};
}

std::shared_ptr<ITexture> CurrentFramebuffer::getColorAttachment(size_t index) const {
  if (index != 0) {
    IGL_ASSERT_NOT_REACHED();
  }
  return colorAttachment_;
}

std::shared_ptr<ITexture> CurrentFramebuffer::getResolveColorAttachment(size_t index) const {
  if (index != 0) {
    IGL_ASSERT_NOT_REACHED();
  }
  return colorAttachment_;
}

std::shared_ptr<ITexture> CurrentFramebuffer::getDepthAttachment() const {
  return nullptr;
}

std::shared_ptr<ITexture> CurrentFramebuffer::getResolveDepthAttachment() const {
  return nullptr;
}

std::shared_ptr<ITexture> CurrentFramebuffer::getStencilAttachment() const {
  return nullptr;
}

std::shared_ptr<ITexture> CurrentFramebuffer::updateDrawable(
    std::shared_ptr<ITexture> /*texture*/) {
  IGL_ASSERT_NOT_REACHED();
  return nullptr;
}

Viewport CurrentFramebuffer::getViewport() const {
  return viewport_;
}

void CurrentFramebuffer::bind(const RenderPassDesc& renderPass) const {
  bindBuffer();
#if !IGL_OPENGL_ES
  // OpenGL ES doesn't need to call glEnable. All it needs is an sRGB framebuffer.
  auto colorAttach = getResolveColorAttachment(getColorAttachmentIndices()[0]);
  if (getContext().deviceFeatures().hasFeature(DeviceFeatures::SRGB)) {
    if (colorAttach && colorAttach->getProperties().isSRGB()) {
      getContext().enable(GL_FRAMEBUFFER_SRGB);
    } else {
      getContext().disable(GL_FRAMEBUFFER_SRGB);
    }
  }
#endif

  // clear the buffers if we're not loading previous contents
  GLbitfield clearMask = 0;
  if (renderPass.colorAttachments[0].loadAction != LoadAction::Load) {
    clearMask |= GL_COLOR_BUFFER_BIT;
    auto clearColor = renderPass.colorAttachments[0].clearColor;
    getContext().colorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    getContext().clearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
  }
  if (renderPass.depthAttachment.loadAction != LoadAction::Load) {
    clearMask |= GL_DEPTH_BUFFER_BIT;
    getContext().depthMask(GL_TRUE);
    getContext().clearDepthf(renderPass.depthAttachment.clearDepth);
  }
  if (renderPass.stencilAttachment.loadAction != LoadAction::Load) {
    clearMask |= GL_STENCIL_BUFFER_BIT;
    getContext().stencilMask(0xFF);
    getContext().clearStencil(renderPass.stencilAttachment.clearStencil);
  }

  if (clearMask != 0) {
    getContext().clear(clearMask);
  }
}

void CurrentFramebuffer::unbind() const {
  // no-op
}

} // namespace opengl
} // namespace igl
