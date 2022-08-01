#include "core/Image.hpp"
#include "core/Log.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define DDSKTX_IMPLEMENT

#include <stb/stb_image.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#include <stb/stb_image_write.h>
#pragma clang diagnostic pop
#include <dds-ktx/dds-ktx.h>

#include <squish/squish.h>
//#include <crnlib/inc/crnlib.h>


void Image::generateDefaultImage(Image& image){

	image = Image(4, 4, 4, 0);
	image.pixels = {
		255,   0, 255, 255, 255,   0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
		255,   0, 255, 255, 255,   0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
		255, 255, 255, 255, 255, 255, 255, 255, 255,   0, 255, 255, 255,   0, 255, 255,
		255, 255, 255, 255, 255, 255, 255, 255, 255,   0, 255, 255, 255,   0, 255, 255
	};

}

Image::Image(unsigned int awidth, unsigned int aheight, unsigned int acomponents, char value) :
	width(awidth), height(aheight), components(acomponents) {
	pixels.resize(width * height * components, value);
}


bool Image::load(const fs::path & path) {
	if(path.extension() == ".dds"){
		// Read the DDS file.
		int ddsSize = 0;

		{
			FILE* f = fopen( path.string().c_str(), "rb");
			if(f == nullptr){
				return false;
			}
			fseek(f, 0, SEEK_END);
			ddsSize = ftell(f);
			if(ddsSize == 0){
				fclose(f);
				return false;
			}
			fseek(f, 0, SEEK_SET);

			compressedPixels.resize(ddsSize);

			if(fread(compressedPixels.data(), 1, ddsSize, f) != (size_t)ddsSize) {
				fclose(f);
				return false;
			}
			fclose(f);
		}
		// Query DDS header.
		ddsktx_texture_info tc = {};
		if(!ddsktx_parse(&tc, compressedPixels.data(), ddsSize, NULL)) {
			return false;
		}

		compressedFormat = 0;
		components = 4;
		switch(tc.format){
			case DDSKTX_FORMAT_BC1:
				compressedFormat = squishDxt1;
				break;
			case DDSKTX_FORMAT_BC2:
				compressedFormat = squishDxt3;
				break;
			case DDSKTX_FORMAT_BC3:
				compressedFormat = squishDxt5;
				break;
			case DDSKTX_FORMAT_BGRA8:
				compressedFormat = 0;
				break;
			default:
				Log::error("Unsupported DDS format.");
				return false;
		}


		// Query first layer/face/level only.
		ddsktx_sub_data subData;
		ddsktx_get_sub(&tc, &subData, compressedPixels.data(), ddsSize, 0, 0, 0);

//		if((tc.flags & DDSKTX_TEXTURE_FLAG_ALPHA) == 0){
//			components = 3;
//		}
		width = subData.width;
		height = subData.height;

		// Allocate memory.
		const size_t linearSize = width *  height * components;
		pixels.resize(linearSize);

		if(compressedFormat > 0){
			// Decompress.
			SquishDecompressImage(pixels.data(), subData.width, subData.height, subData.buff, compressedFormat);

		} else {
			// Flip BGRA8 data.
			std::memcpy(pixels.data(), subData.buff, subData.size_bytes);
			for(uint32_t pix = 0; pix < (uint32_t)subData.size_bytes; pix += 4){
				// BGRA to RGBA
				std::swap(pixels[pix], pixels[pix+2]);
			}
			// Purge compressed data.
			compressedPixels.clear();
		}

		return true;
	}

	compressedFormat = 0;
	int x, y, comp;
	unsigned char* data = stbi_load(path.string().c_str(), &x, &y, &comp, 4);
	if(data == nullptr){
		return false;
	}
	width = x;
	height = y;
	components = 4;

	const size_t byteSize = width * height * components;
	pixels.resize(byteSize);
	if(byteSize != 0)
		std::memcpy(pixels.data(), data, byteSize);

	stbi_image_free(data);
	return true;
}

bool Image::save(const fs::path & path) const {
	// Always save the decompressed data.
	return stbi_write_png(path.string().c_str(), width, height, components, &pixels[0], components * width) != 0;
}
