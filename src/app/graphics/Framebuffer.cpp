#include "graphics/Framebuffer.hpp"
#include "graphics/GPUObjects.hpp"
#include "graphics/GPU.hpp"
#include "graphics/GPUInternal.hpp"


Framebuffer::Framebuffer(uint width, uint height, const Layout & format, const std::string & name) :
	Framebuffer(TextureShape::D2, width, height, 1, 1, std::vector<Layout>(1, format), name) {
}

Framebuffer::Framebuffer(uint width, uint height, const std::vector<Layout> & formats, const std::string & name) :
	Framebuffer(TextureShape::D2, width, height, 1, 1, formats, name) {
}

Framebuffer::Framebuffer(TextureShape shape, uint width, uint height, uint depth, uint mips, const std::vector<Layout> & formats, const std::string & name) : _depth("Depth ## " + name), _name(name),
	_width(width), _height(height), _mips(mips) {

	// Check that the shape is supported.
	_shape = shape;
	if(_shape != TextureShape::D2 && _shape != TextureShape::Array2D && _shape != TextureShape::Cube && _shape != TextureShape::ArrayCube){
		Log::error("GPU: Unsupported framebuffer shape.");
		return;
	}
	if(shape == TextureShape::D2){
		_layers = 1;
	} else if(shape == TextureShape::Cube){
		_layers = 6;
	} else if(shape == TextureShape::ArrayCube){
		_layers = 6 * depth;
	} else {
		_layers = depth;
	}

	uint cid = 0;
	for(const Layout & format : formats) {
		// Create the color texture to store the result.
		const bool isDepthComp		  = format == Layout::DEPTH_COMPONENT16 || format == Layout::DEPTH_COMPONENT24 || format == Layout::DEPTH_COMPONENT32F;
		const bool hasStencil = format == Layout::DEPTH24_STENCIL8 || format == Layout::DEPTH32F_STENCIL8;

		if(isDepthComp || hasStencil) {
			_hasDepth	  = true;
			_depth.width  = _width;
			_depth.height = _height;
			_depth.depth  = _layers;
			_depth.levels = _mips;
			_depth.shape  = shape;
			GPU::setupTexture(_depth, format, true);

		} else {
			_colors.emplace_back("Color " + std::to_string(cid++) + " ## " + _name);
			Texture & tex = _colors.back();
			tex.width     = _width;
			tex.height	  = _height;
			tex.depth	  = _layers;
			tex.levels	  = _mips;
			tex.shape	  = shape;
			GPU::setupTexture(tex, format, true);

		}
	}
		populateLayoutState();

	// Create the framebuffer.
	finalizeFramebuffer();

}
void Framebuffer::populateLayoutState(){
	for(uint cid = 0; cid < _colors.size(); ++cid){
		_state.colors.push_back(_colors[cid].gpu->typedFormat);
	}
	if(_hasDepth){
		_state.hasDepth = true;
		_state.depth = _depth.gpu->typedFormat;
	}
}

void Framebuffer::finalizeFramebuffer(){

	// Finalize the texture layouts.
	GPUContext* context = GPU::getInternal();
	VkCommandBuffer commandBuffer = context->getUploadCommandBuffer();
	for(size_t cid = 0; cid < _colors.size(); ++cid){
		VkUtils::textureLayoutBarrier(commandBuffer, _colors[cid], _colors[cid].gpu->defaultLayout);
	}
	if(_hasDepth){
		VkUtils::textureLayoutBarrier(commandBuffer, _depth, _depth.gpu->defaultLayout);
	}

	const uint attachCount = uint(_colors.size()) + (_hasDepth ? 1 : 0);

	_framebuffers.resize(_mips);
	// Generate per-mip per-layer framebuffers.
	for(uint mid = 0; mid < _mips; ++mid){
		_framebuffers[mid].resize(_layers);

		for(uint lid = 0; lid < _layers; ++lid){
			Slice& slice = _framebuffers[mid][lid];
			slice.attachments.resize(attachCount);

			//Register which attachments to draw to.
			for(size_t cid = 0; cid < _colors.size(); ++cid){
				
				// Create a custom one-level one-layer view.
				VkImageViewCreateInfo viewInfo = {};
				viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
				viewInfo.image = _colors[cid].gpu->image;
				viewInfo.viewType =  VK_IMAGE_VIEW_TYPE_2D;
				viewInfo.format =  _colors[cid].gpu->format;
				viewInfo.subresourceRange.aspectMask = _colors[cid].gpu->aspect;
				viewInfo.subresourceRange.baseMipLevel = mid;
				viewInfo.subresourceRange.levelCount = 1;
				viewInfo.subresourceRange.baseArrayLayer = lid;
				viewInfo.subresourceRange.layerCount = 1;

				if (vkCreateImageView(context->device, &viewInfo, nullptr, &(slice.attachments[cid])) != VK_SUCCESS) {
					Log::error("GPU: Unable to create image view for framebuffer.");
					return;
				}
			}

			// Depth attachment is last.
			if(_hasDepth){
				VkImageViewCreateInfo viewInfo = {};
				viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
				viewInfo.image = _depth.gpu->image;
				viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
				viewInfo.format =  _depth.gpu->format;
				viewInfo.subresourceRange.aspectMask = _depth.gpu->aspect;
				viewInfo.subresourceRange.baseMipLevel = mid;
				viewInfo.subresourceRange.levelCount = 1;
				viewInfo.subresourceRange.baseArrayLayer = lid;
				viewInfo.subresourceRange.layerCount = 1;
				if (vkCreateImageView(context->device, &viewInfo, nullptr, &(slice.attachments.back())) != VK_SUCCESS) {
					Log::error("GPU: Unable to create image view for framebuffer.");
					return;
				}
			}
		}
	}
}

void Framebuffer::bind(const LoadOperation& colorOp, const LoadOperation& depthOp, const LoadOperation& stencilOp) const {
	bind(0, 0, colorOp, depthOp, stencilOp);
}

void Framebuffer::bind(uint layer, uint mip, const LoadOperation& colorOp, const LoadOperation& depthOp, const LoadOperation& stencilOp) const {
	GPU::unbindFramebufferIfNeeded();

	// Retrieve the framebuffer slice and render pass.
	const Slice& slice = _framebuffers[mip][layer];

	GPU::bindFramebuffer(*this, layer, mip);

	GPUContext* context = GPU::getInternal();
	VkCommandBuffer& commandBuffer = context->getRenderCommandBuffer();

	const uint w = std::max<uint>(1u, _width >> mip);
	const uint h = std::max<uint>(1u, _height >> mip);

	static const std::array<VkAttachmentLoadOp, 3> ops = {VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_LOAD_OP_DONT_CARE};

	const VkAttachmentLoadOp colorLoad = ops[uint(colorOp.mode)];
	const VkAttachmentStoreOp colorStore = VK_ATTACHMENT_STORE_OP_STORE;
	const VkAttachmentLoadOp 	depthLoad = ops[uint(depthOp.mode)];
	const VkAttachmentStoreOp depthStore = VK_ATTACHMENT_STORE_OP_STORE; // could be DONT_CARE when depth is internal ?
	const VkAttachmentLoadOp 	stencilLoad = ops[uint(stencilOp.mode)];
	const VkAttachmentStoreOp stencilStore = VK_ATTACHMENT_STORE_OP_STORE;  // could be DONT_CARE when depth is internal ?

	VkRenderingInfoKHR info = {};
	info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
	info.renderArea.extent = {uint32_t(w), uint32_t(h)};
	info.renderArea.offset = {0u, 0u};
	info.layerCount = 1;
	info.colorAttachmentCount = 0;
	info.pColorAttachments = nullptr;
	info.pDepthAttachment = nullptr;
	info.pStencilAttachment = nullptr;
	info.viewMask = 0;
	info.flags = 0;

	const size_t colorsCount = _colors.size();
	std::vector<VkRenderingAttachmentInfoKHR> colorInfos(colorsCount);
	VkRenderingAttachmentInfoKHR depthInfo;
	VkRenderingAttachmentInfoKHR stencilInfo;

	for(uint cid = 0; cid < colorsCount; ++cid){
		VkRenderingAttachmentInfoKHR& colorInfo = colorInfos[cid];
		colorInfo.imageView = slice.attachments[cid];
		colorInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
		colorInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		colorInfo.clearValue.color.float32[0] = colorOp.value[0];
		colorInfo.clearValue.color.float32[1] = colorOp.value[1];
		colorInfo.clearValue.color.float32[2] = colorOp.value[2];
		colorInfo.clearValue.color.float32[3] = colorOp.value[3];
		colorInfo.loadOp = colorLoad;
		colorInfo.storeOp = colorStore;
		colorInfo.resolveMode = VK_RESOLVE_MODE_NONE;

		VkUtils::imageLayoutBarrier(commandBuffer, *_colors[cid].gpu, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, mip, 1, layer, 1);
	}

	if(colorsCount != 0){
		info.pColorAttachments = colorInfos.data();
		info.colorAttachmentCount = colorsCount;
	}

	if(_hasDepth){
		info.pDepthAttachment = &depthInfo;
		depthInfo.imageView = slice.attachments.back();
		depthInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
		depthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		depthInfo.clearValue.depthStencil.depth = depthOp.value[0];
		depthInfo.loadOp = depthLoad;
		depthInfo.storeOp = depthStore;
		depthInfo.resolveMode = VK_RESOLVE_MODE_NONE;

		if(_depth.gpu->typedFormat == Layout::DEPTH24_STENCIL8 || _depth.gpu->typedFormat == Layout::DEPTH32F_STENCIL8){
			info.pStencilAttachment = &stencilInfo;
			stencilInfo.imageView = slice.attachments.back();
			stencilInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
			stencilInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			stencilInfo.clearValue.depthStencil.stencil = uint32_t(stencilOp.value[0]);
			stencilInfo.loadOp = stencilLoad;
			stencilInfo.storeOp = stencilStore;
			stencilInfo.resolveMode = VK_RESOLVE_MODE_NONE;
		}
		
		VkUtils::imageLayoutBarrier(commandBuffer, *_depth.gpu, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, mip, 1, layer, 1);
	}

	vkCmdBeginRenderingKHR(commandBuffer, &info);
	context->newRenderPass = true;
}

void Framebuffer::setViewport() const {
	GPU::setViewport(0, 0, int(_width), int(_height));
}

void Framebuffer::resize(uint width, uint height) {
	_width  = width;
	_height = height;

	GPU::clean(*this);

	// Resize the renderbuffer.
	if(_hasDepth) {
		_depth.width  = _width;
		_depth.height = _height;
		GPU::setupTexture(_depth, _depth.gpu->typedFormat, true);
	}

	// Resize the textures.
	for(Texture & color : _colors) {
		color.width  = _width;
		color.height = _height;
		GPU::setupTexture(color, color.gpu->typedFormat, true);
	}

	finalizeFramebuffer();
	
}

void Framebuffer::resize(const glm::ivec2 & size) {
	resize(uint(size[0]), uint(size[1]));
}

void Framebuffer::clear(const glm::vec4 & color, float depth){
	// See https://www.khronos.org/registry/vulkan/specs/1.2-extensions/html/vkspec.html#clears
	// for possibilities.
	// End current render pass.
	GPU::unbindFramebufferIfNeeded();

	GPUContext* context = GPU::getInternal();
	VkCommandBuffer& commandBuffer = context->getRenderCommandBuffer();

	VkClearColorValue clearCol = {};
	clearCol.float32[0] = color[0];
	clearCol.float32[1] = color[1];
	clearCol.float32[2] = color[2];
	clearCol.float32[3] = color[3];

	VkImageSubresourceRange rangeCol = {};
	rangeCol.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	rangeCol.baseArrayLayer = 0;
	rangeCol.layerCount = _layers;
	rangeCol.baseMipLevel = 0;
	rangeCol.levelCount = _mips;

	VkClearDepthStencilValue clearZ = {};
	clearZ.depth = depth;
	clearZ.stencil = 0u;

	VkImageSubresourceRange rangeZ = {};
	rangeZ.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	rangeZ.baseArrayLayer = 0;
	rangeZ.layerCount = _layers;
	rangeZ.baseMipLevel = 0;
	rangeZ.levelCount = _mips;

	// Transition all attachments.
	for(Texture& col : _colors){
		VkUtils::textureLayoutBarrier(commandBuffer, col, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	}
	if(_hasDepth){
		VkUtils::textureLayoutBarrier(commandBuffer, _depth, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	}

	// Clear all images, all layers, all mips.
	for(Texture& col : _colors){
		vkCmdClearColorImage(commandBuffer, col.gpu->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearCol, 1, &rangeCol);
	}
	if(_hasDepth){
		vkCmdClearDepthStencilImage(commandBuffer, _depth.gpu->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearZ, 1, &rangeZ);
	}
	
	// Transition all attachments.
	for(Texture& col : _colors){
		VkUtils::textureLayoutBarrier(commandBuffer, col, col.gpu->defaultLayout);
	}
	if(_hasDepth){
		VkUtils::textureLayoutBarrier(commandBuffer, _depth, _depth.gpu->defaultLayout);
	}
}

bool Framebuffer::isEquivalent(const Framebuffer& other) const {
	return _state.isEquivalent(other.getState());
}

glm::vec4 Framebuffer::read(const glm::uvec2 & pos) {
	if(_colors.empty()){
		return _readColor;
	}

	_readTask = GPU::downloadTextureAsync( _colors[0], pos, glm::uvec2(2), 1, [this](const Texture& result){
		const auto& pixels = result.images[0].pixels;
		for(unsigned int i = 0; i < 4; ++i){
			_readColor[i] = ((float)pixels[i]) / 255.0f;
		}
	});

	//GPU::_metrics.downloads += 1;
	// Return the value from the previous frame.
	return _readColor;
}

const Layout & Framebuffer::format(unsigned int i) const {
   return _colors[i].gpu->typedFormat;
}

uint Framebuffer::attachments() const {
	return uint(_colors.size());
}

const Framebuffer::State& Framebuffer::getState() const {
	return _state;
}

bool Framebuffer::State::isEquivalent(const Framebuffer::State& other) const {
	if(other.colors.size() != colors.size()){
		return false;
	}

	if(hasDepth != other.hasDepth){
		return false;
	}

	// Two attachment references are compatible if they have matching format and sample count.
	// We can ignore: resolve, image layouts, load/store operations.

	if(hasDepth){
		// We can safely compare depths.
		if(depth != other.depth){
			return false;
		}
	}

	// We can safely compare color attachments.
	for(uint cid = 0; cid < colors.size(); ++cid){
		if(colors[cid] != other.colors[cid]){
			return false;
		}
	}
	return true;
}

Framebuffer::~Framebuffer() {
	GPU::cancelAsyncOperation(_readTask);
	GPU::clean(*this);
}

Framebuffer * Framebuffer::_backbuffer = nullptr;

Framebuffer * Framebuffer::backbuffer() {
	return _backbuffer;
}
