#include "resources/Texture.hpp"
#include "graphics/GPUObjects.hpp"
#include "graphics/GPU.hpp"
#include "graphics/GPUInternal.hpp"
#include "graphics/SamplerLibrary.hpp"
#include <imgui/imgui_impl_vulkan.h>

Texture::Texture(const std::string & name) : _name(name) {
}

void Texture::upload(const Layout & layout, bool updateMipmaps) {

	// Compute the last mip level if needed.
	if(updateMipmaps) {
		levels = getMaxMipLevel()+1;
	}

	// Create texture.
	GPU::setupTexture(*this, layout, false);
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

void ImGui::Image(const Texture & texture, const ImVec2& size, const ImVec2& uv0, const ImVec2& uv1, const ImVec4& tint_col, const ImVec4& border_col){
	if(texture.gpu->imgui == VK_NULL_HANDLE){
		GPUContext* context = GPU::getInternal();
		texture.gpu->imgui = ImGui_ImplVulkan_AddTexture(context->samplerLibrary.getDefaultSampler(), texture.gpu->view, texture.gpu->defaultLayout);
	}
	ImGui::Image((ImTextureID)texture.gpu->imgui, size, uv0, uv1, tint_col, border_col);
}

bool ImGui::ImageButton(const Texture & texture, const ImVec2& size, const ImVec2& uv0,  const ImVec2& uv1, int frame_padding, const ImVec4& bg_col, const ImVec4& tint_col){
	if(texture.gpu->imgui == VK_NULL_HANDLE){
		GPUContext* context = GPU::getInternal();
		texture.gpu->imgui = ImGui_ImplVulkan_AddTexture(context->samplerLibrary.getDefaultSampler(), texture.gpu->view, texture.gpu->defaultLayout);
	}
	return ImGui::ImageButton((ImTextureID)texture.gpu->imgui, size, uv0, uv1, frame_padding, bg_col, tint_col);
}
