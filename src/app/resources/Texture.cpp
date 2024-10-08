#include "resources/Texture.hpp"
#include "graphics/GPUObjects.hpp"
#include "graphics/GPU.hpp"
#include "graphics/GPUInternal.hpp"
#include "graphics/SamplerLibrary.hpp"
#include <imgui/imgui_impl_vulkan.h>
#include <dds-ktx/dds-ktx.h>

Texture::Texture(const std::string & name) : _name(name) {
}

void Texture::uncompress(){
	if(images.empty() || images[0].compressedFormat == Image::Compression::NONE){
		// Nothing to do.
		return;
	}

	if(Log::check(shape == TextureShape::D2, "Texture shape is not 2D.")){
		return;
	}

	Image& img = images[0];

	const uint components = img.components;
	const Image::Compression compression = img.compressedFormat;
	// We need to cheat and uncompress all mip levels that are stored in the raw BC data blob stored in images[0].
	const int ddsSize = (int)img.pixels.size();

	// Copy raw data.
	std::vector<char> compressedPixels(ddsSize);
	std::memcpy(compressedPixels.data(), img.pixels.data(), ddsSize);
	img.pixels.clear();

	// Parse the header again.
	// Query DDS header.
	ddsktx_texture_info tc = {};
	if(!ddsktx_parse(&tc, compressedPixels.data(), ddsSize, NULL)) {
		Log::error("Unable to parse DDS header again.");
		return;
	}

	width = tc.width;
	height = tc.height;
	levels = tc.num_mips;
	depth = tc.num_layers * tc.depth;

	bool isArray = tc.num_layers > 1;
	bool isCube = tc.flags & DDSKTX_TEXTURE_FLAG_CUBEMAP;
	bool is3D = tc.flags & DDSKTX_TEXTURE_FLAG_VOLUME;
	shape = isCube ? TextureShape::Cube : (is3D ? TextureShape::D3 : TextureShape::D2);
	if( isArray ){
		shape = shape | TextureShape::Array;
	}

	images.resize(levels * depth);

	// Outer loop is on mip levels
	for(uint mid = 0; mid < levels; ++mid){
		const uint w = std::max(width >> mid, 1u);
		const uint h = std::max(height >> mid, 1u);

		for(uint lid = 0; lid < (uint)tc.num_layers; ++lid){

			for( uint did = 0; did < (uint)tc.depth; ++did ){
				Image& slice = images[ mid * depth + lid * tc.depth + did ];
				slice = Image( w, h, components );
				slice.compressedFormat = compression;
				ddsktx_sub_data subData;
				ddsktx_get_sub( &tc, &subData, compressedPixels.data(), ddsSize, lid, did, mid );
				assert( ( ( int )w == subData.width ) && ( ( int )h == subData.height ) );
				slice.pixels.resize( subData.size_bytes );
				std::memcpy( slice.pixels.data(), subData.buff, subData.size_bytes );
			}
		}
	}
}

void Texture::upload(const Layout & layout, bool updateMipmaps) {

	// Compute the last mip level if needed.
	if(updateMipmaps) {
		levels = getMaxMipLevel()+1;
	}

	Layout finalLayout = layout;

	// Auto-cleverness for compressed images.
	if(images[0].compressedFormat != Image::Compression::NONE){

		static const std::unordered_map<Image::Compression, Layout> bcLayouts = {
			   { Image::Compression::BC1, Layout::BC1 },
			   { Image::Compression::BC2, Layout::BC2 },
			   { Image::Compression::BC3, Layout::BC3 },
		};
		finalLayout = bcLayouts.at(images[0].compressedFormat);
	}
	
	// Create texture.
	GPU::setupTexture(*this, finalLayout, false);
	GPU::uploadTexture(*this);

	// Generate mipmaps pyramid automatically.
	if(updateMipmaps) {
		GPU::generateMipMaps(*this);
	}
}

uint Texture::getMaxMipLevel() const {
	uint minDimension = width;
	if(shape & TextureShape::D2){
		minDimension = std::min(minDimension, height);
	}
	if(shape & TextureShape::D3){
		minDimension = std::min(minDimension, height);
		minDimension = std::min(minDimension, depth);
	}
	return uint(std::floor(std::log2(minDimension)));
}

void Texture::clearImages() {
	images.clear();
}

void Texture::allocateImages(uint channels, uint firstMip, uint mipCount){

	const uint effectiveFirstMip = std::min(levels - 1u, firstMip);
	const uint effectiveMipCount = std::min(mipCount, levels - effectiveFirstMip);
	const uint effectiveLastMip = effectiveFirstMip + effectiveMipCount - 1u;

	const bool is3D = shape & TextureShape::D3;

	uint totalCount = 0;
	uint currentCount = 0;
	for(uint mid = 0; mid < levels; ++mid){
		if(mid == effectiveFirstMip){
			currentCount = totalCount;
		}
		totalCount += is3D ? std::max<uint>(depth >> mid, 1u) : depth;
	}

	images.resize(totalCount);

	for(uint mid = effectiveFirstMip; mid <= effectiveLastMip; ++mid){
		const uint w = std::max<uint>(width >> mid, 1u);
		const uint h = std::max<uint>(height >> mid, 1u);
		// Compute the size and count of images.
		const uint imageCount = is3D ? std::max<uint>(depth >> mid, 1u) : depth;
		for(uint iid = 0; iid < imageCount; ++iid){
			const uint imageIndex = currentCount + iid;
			// Avoid reallocating existing images.
			if(images[imageIndex].components != channels){
				images[imageIndex] = Image(w, h, channels);
			}
		}
		currentCount += imageCount;
	}
}

void Texture::resize(const glm::vec2& res){
	resize(uint(res[0]), uint(res[1]));
}

void Texture::resize(uint w, uint h, uint d){
	if(width == w && height == h && depth == d){
		return;
	}
	if(d != 1 && (shape == TextureShape::D2 || shape == TextureShape::D1)){
		Log::warning("GPU: attempting to change texture shape.");
		d = 1;
	}
	if(gpu == nullptr){
		Log::warning("GPU: texture is not allocated on the GPU.");
		return;
	}
	width = w;
	height = h;
	depth = d;
	GPU::setupTexture(*this, gpu->typedFormat, true);
	// \todo Resize CPU.
}

void Texture::setupRendertarget(Texture& texture, Layout format,uint width, uint height, uint mips, TextureShape shape, uint depth){

	// Check that the shape is supported.
	if(shape != TextureShape::D2 && shape != TextureShape::Array2D && shape != TextureShape::Cube && shape != TextureShape::ArrayCube){
		Log::error("GPU: Unsupported render texture shape.");
		return;
	}
	
	// Number of layers based on shape.
	uint layers = 1;
	if(shape == TextureShape::Array2D){
		layers = depth;
	} else if(shape == TextureShape::Cube){
		layers = 6;
	} else if(shape == TextureShape::ArrayCube){
		layers = 6 * depth;
	}

	texture.width  = width;
	texture.height = height;
	texture.depth  = layers;
	texture.levels = mips;
	texture.shape  = shape;

	GPU::setupTexture(texture, format, true);
}

void Texture::clean() {
	clearImages();
	if(gpu) {
		gpu->clean();
	}
	gpu = nullptr;
}

Texture::Texture(Texture &&) = default;

Texture::~Texture(){
	clean();
}

const std::string & Texture::name() const {
	return _name;
}

void ImGui::Image(const Texture & texture, uint mip, uint layer, const ImVec2& size, const ImVec2& uv0, const ImVec2& uv1, const ImVec4& tint_col, const ImVec4& border_col){
	if(mip >= texture.levels || layer >= texture.depth){
		return;
	}
	uint linearIndex = mip * texture.depth + layer;
	if(linearIndex >= texture.gpu->imguiViews.size()){
		texture.gpu->imguiViews.resize(linearIndex + 1, VK_NULL_HANDLE);
	}
	if(texture.gpu->imguiViews[linearIndex] == VK_NULL_HANDLE){
		GPUContext* context = GPU::getInternal();
		texture.gpu->imguiViews[linearIndex] = ImGui_ImplVulkan_AddTexture(context->samplerLibrary.getDefaultSampler(), texture.gpu->views[mip].views[layer], texture.gpu->defaultLayout);
	}
	ImGui::Image((ImTextureID)texture.gpu->imguiViews[linearIndex], size, uv0, uv1, tint_col, border_col);
}

bool ImGui::ImageButton(const Texture & texture, uint mip, uint layer, const ImVec2& size, const ImVec2& uv0,  const ImVec2& uv1, int frame_padding, const ImVec4& bg_col, const ImVec4& tint_col){
	if(mip >= texture.levels || layer >= texture.depth){
		return false;
	}
	uint linearIndex = mip * texture.depth + layer;
	if(linearIndex >= texture.gpu->imguiViews.size()){
		texture.gpu->imguiViews.resize(linearIndex + 1, VK_NULL_HANDLE);
	}
	if(texture.gpu->imguiViews[linearIndex] == VK_NULL_HANDLE){
		GPUContext* context = GPU::getInternal();
		// Always take a 2D view.
		texture.gpu->imguiViews[linearIndex] = ImGui_ImplVulkan_AddTexture(context->samplerLibrary.getDefaultSampler(), texture.gpu->views[mip].views[layer], texture.gpu->defaultLayout);
	}
	return ImGui::ImageButton((ImTextureID)texture.gpu->imguiViews[linearIndex], size, uv0, uv1, frame_padding, bg_col, tint_col);
}
