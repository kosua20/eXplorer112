#pragma once
#include "core/Image.hpp"
#include "graphics/GPUTypes.hpp"
#include "core/Common.hpp"

class GPUTexture;

/**
 \brief Represents a texture containing one or more images, stored on the CPU and/or GPU.
 \ingroup Resources
 */
class Texture {
public:

	/** Constructor.
	 \param name the texture identifier
	 */
	Texture(const std::string & name);

	void uncompress();
	
	/** Send to the GPU.
	 \param layout the data layout and type to use for the texture
	 \param updateMipmaps generate the mipmaps automatically
	 */
	void upload(const Layout & layout, bool updateMipmaps);

	/** Clear CPU images data. */
	void clearImages();

	/** Allocate the CPU images in the mip range defined as input. Images outside the given range will be left untouched if present, or created empty if not.
	 \param channels the number of channels for each image
	 \param firstMip the first mip to allocate images for
	 \param mipCount the number of mip to allocate, up to the texture level count
	 */
	void allocateImages(uint channels, uint firstMip = 0, uint mipCount = 0xFFFFFFFF);

	void resize(const glm::vec2& res);

	void resize(uint width, uint height, uint depth = 1u);

	/** Cleanup all data.
	 \note The dimensions and shape of the texture are preserved.
	 */
	void clean();
	
	/** Get the resource name.
		\return the name.
	 */
	const std::string & name() const;

	/** Compute the maximum possible mipmap level based on the texture type and dimensions.
	 \return the maximum level
	 */
	uint getMaxMipLevel() const;

	/** Copy assignment operator (disabled).
	 \return a reference to the object assigned to
	 */
	Texture & operator=(const Texture &) = delete;
	
	/** Copy constructor (disabled). */
	Texture(const Texture &) = delete;
	
	/** Move assignment operator .
	 \return a reference to the object assigned to
	 */
	Texture & operator=(Texture &&) = default;
	
	/** Move constructor. */
	Texture(Texture &&);

	~Texture();
	
	std::vector<Image> images;		 ///< The images CPU data (optional).
	std::unique_ptr<GPUTexture> gpu; ///< The GPU data (optional).
	
	uint width  = 0; ///< The texture width.
	uint height = 0; ///< The texture height.
	uint depth  = 1; ///< The texture depth.
	uint levels = 1; ///< The mipmap count.
	
	TextureShape shape = TextureShape::D2; ///< Texure type.

	static void setupRendertarget(Texture& texture, Layout format, uint width, uint height, uint mips = 1, TextureShape shape = TextureShape::D2, uint depth = 1);

private:
		
	std::string _name; ///< Resource name.
	
};

namespace ImGui {

	/** Display a texture as an ImGui image.
	 \param texture the texture to display
	 \param size the image display size
	 \param uv0 coordinates of the top left corner
	 \param uv1 coordinates of the bottom right corner
	 \param tint_col the optional image tint color
	 \param border_col the border color
	 */
	void Image(const Texture & texture, const ImVec2& size, const ImVec2& uv0 = ImVec2(0,0), const ImVec2& uv1 = ImVec2(1,1), const ImVec4& tint_col = ImVec4(1,1,1,1), const ImVec4& border_col = ImVec4(0,0,0,0));

	/** Display a texture as an ImGui button.
	\param texture the texture to display
	\param size the image display size
	\param uv0 coordinates of the top left corner
	\param uv1 coordinates of the bottom right corner
	\param frame_padding padding between the image and the button edges
	\param bg_col the button background color
	\param tint_col the optional image tint color
	\return true if pressed
	*/
	bool ImageButton(const Texture & texture, const ImVec2& size, const ImVec2& uv0 = ImVec2(0,0), const ImVec2& uv1 = ImVec2(1,1), int frame_padding = -1, const ImVec4& bg_col = ImVec4(0,0,0,0), const ImVec4& tint_col = ImVec4(1,1,1,1));

}
