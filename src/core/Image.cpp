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
#include <unordered_map>

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
	pixels.clear();
	width = height = 0;

	if(path.extension() == ".dds"){
		// Read the DDS file.
		int ddsSize = 0;
		std::vector<char> allCompressedData;

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

			allCompressedData.resize(ddsSize);

			if(fread(allCompressedData.data(), 1, ddsSize, f) != (size_t)ddsSize) {
				fclose(f);
				return false;
			}
			fclose(f);
		}
		// Query DDS header.
		ddsktx_texture_info tc = {};
		if(!ddsktx_parse(&tc, allCompressedData.data(), ddsSize, NULL)) {
			return false;
		}

		static const std::unordered_map<ddsktx_format, Compression> formats = {
			{ DDSKTX_FORMAT_BC1, Compression::BC1 },
			{ DDSKTX_FORMAT_BC2, Compression::BC2 },
			{ DDSKTX_FORMAT_BC3, Compression::BC3 },
			{ DDSKTX_FORMAT_BGRA8, Compression::NONE },
		};

		if(formats.count(tc.format) == 0){
			Log::error("Unsupported DDS format.");
			return false;
		}

		compressedFormat = formats.at(tc.format);

		components = 4;
		// Apparently not needed?
		//if((tc.flags & DDSKTX_TEXTURE_FLAG_ALPHA) == 0){
		//	components = 3;
		//}

		width = tc.width;
		height = tc.height;

		// Query first layer/face/level only.
		ddsktx_sub_data subData;
		ddsktx_get_sub(&tc, &subData, allCompressedData.data(), ddsSize, 0, 0, 0);

		// If not compressed, just flip channels and copu.
		if(compressedFormat == Compression::NONE){

			// Allocate memory.
			const size_t linearSize = width *  height * components;
			pixels.resize(linearSize);
			// Flip BGRA8 data.
			std::memcpy(pixels.data(), subData.buff, subData.size_bytes);
			for(uint32_t pix = 0; pix < (uint32_t)subData.size_bytes; pix += 4){
				// BGRA to RGBA
				std::swap(pixels[pix], pixels[pix+2]);
			}
		} else {
			pixels.resize(subData.size_bytes);
			std::memcpy(pixels.data(), subData.buff, subData.size_bytes);
		}

		return true;
	}

	compressedFormat = Compression::NONE;
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

bool Image::uncompress(){
	// Nothing to do
	if(compressedFormat == Compression::NONE){
		return true;
	}
	std::vector<char> compressedPixels(pixels.size());
	std::memcpy(compressedPixels.data(), pixels.data(), pixels.size());

	pixels.resize(width * height * components);
	static const std::unordered_map<Compression, int> flags = {
		{ Compression::BC1, squishDxt1 },
		{ Compression::BC2, squishDxt3 },
		{ Compression::BC3, squishDxt5 },
		{ Compression::NONE, 0 },
	};
	SquishDecompressImage(pixels.data(), width, height, compressedPixels.data(), flags.at(compressedFormat));
	compressedFormat = Compression::NONE;
	return true;
}

bool Image::save(const fs::path & path) const {
	// Always save the decompressed data.
	if(compressedFormat != Compression::NONE){
		Log::error("Attemmpting to save a compressed texture.");
		return false;
	}
	return stbi_write_png(path.string().c_str(), width, height, components, &pixels[0], components * width) != 0;
}
