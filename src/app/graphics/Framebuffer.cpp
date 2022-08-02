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
}

void Framebuffer::bind(const LoadOperation& colorOp, const LoadOperation& depthOp, const LoadOperation& stencilOp) const {
	bind(0, 0, colorOp, depthOp, stencilOp);
}

void Framebuffer::bind(uint layer, uint mip, const LoadOperation& colorOp, const LoadOperation& depthOp, const LoadOperation& stencilOp) const {

	const size_t colorCount = _colors.size();
	GPU::bindFramebuffer(layer, mip, depthOp, stencilOp, colorOp, _hasDepth ? &_depth : nullptr,
						 colorCount > 0 ? &_colors[0] : nullptr,
						 colorCount > 1 ? &_colors[1] : nullptr,
						 colorCount > 2 ? &_colors[2] : nullptr,
						 colorCount > 3 ? &_colors[3] : nullptr,
						 colorCount > 4 ? &_colors[4] : nullptr,
						 colorCount > 5 ? &_colors[5] : nullptr,
						 colorCount > 6 ? &_colors[6] : nullptr,
						 colorCount > 7 ? &_colors[7] : nullptr);

}

void Framebuffer::setViewport() const {
	GPU::setViewport(0, 0, int(_width), int(_height));
}

void Framebuffer::resize(uint width, uint height) {
	_width  = width;
	_height = height;

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
}

void Framebuffer::resize(const glm::ivec2 & size) {
	resize(uint(size[0]), uint(size[1]));
}

void Framebuffer::clear(const glm::vec4 & color, float depth){
	for(Texture& col : _colors){
		GPU::clearTexture(col, color);
	}
	if(_hasDepth){
		GPU::clearDepth(_depth, depth);
	}
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

	// Return the value from the previous frame.
	return _readColor;
}

uint Framebuffer::attachments() const {
	return uint(_colors.size());
}

Framebuffer::~Framebuffer() {
	GPU::cancelAsyncOperation(_readTask);
}

Framebuffer * Framebuffer::_backbuffer = nullptr;

Framebuffer * Framebuffer::backbuffer() {
	return _backbuffer;
}
