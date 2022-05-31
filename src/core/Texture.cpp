#include "core/Texture.hpp"
#include "core/Log.hpp"
#include "core/Geometry.hpp"


#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define DDSKTX_IMPLEMENT

#include <stb/stb_image.h>
#include <stb/stb_image_write.h>
#include <dds-ktx/dds-ktx.h>

#include <squish/squish.h>
//#include <crnlib/inc/crnlib.h>

bool convertDDStoPNG(const fs::path& ddsPath, const fs::path& pngPath){
	void* ddsData = nullptr;
	int ddsSize = 0;

	{
		FILE* f = fopen( ddsPath.string().c_str(), "rb");
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

		ddsData = malloc(ddsSize);
		if(ddsData == nullptr){
			fclose(f);
			return false;
		}

		if(fread(ddsData, 1, ddsSize, f) != ddsSize) {
			free(ddsData);
			fclose(f);
			return false;
		}
		fclose(f);
	}
	// Query DDS header.
	ddsktx_texture_info tc = {};
	if(!ddsktx_parse(&tc, ddsData, ddsSize, NULL)) {
		free(ddsData);
		return false;
	}

	int dxtFormat = 0;
	switch(tc.format){
		case DDSKTX_FORMAT_BC1:
			dxtFormat = squishDxt1;
			break;
		case DDSKTX_FORMAT_BC2:
			dxtFormat = squishDxt3;
			break;
		case DDSKTX_FORMAT_BC3:
			dxtFormat = squishDxt5;
			break;
		case DDSKTX_FORMAT_BGRA8:
			dxtFormat = 0;
			break;
		default:
			Log::error("Unsupported DDS format.");
			free(ddsData);
			return false;
	}

	// Query first layer/face/level only.
	ddsktx_sub_data subData;
	ddsktx_get_sub(&tc, &subData, ddsData, ddsSize, 0, 0, 0);

	const size_t linearSize = subData.width * subData.height * tc.bpp;

	int res = 0;
	if(dxtFormat > 0){
		// Allocate memory.
		unsigned char* linearData = (unsigned char*)malloc(linearSize);
		SquishDecompressImage(linearData, subData.width, subData.height, subData.buff, dxtFormat);
		// Write result to png.
		res = stbi_write_png(pngPath.string().c_str(), subData.width, subData.height, 4, linearData, subData.width * 4);
		free(linearData);

	} else {
		// Directly write raw BGRA8 data to PNG.
		for(uint32_t pix = 0; pix < (uint32_t)subData.size_bytes; pix += 4){
			// BGRA to RGBA
			std::swap(((unsigned char*)subData.buff)[pix], ((unsigned char*)subData.buff)[pix+2]);
		}
		res = stbi_write_png(pngPath.string().c_str(), subData.width, subData.height, 4, subData.buff, subData.row_pitch_bytes);
	}

	free(ddsData);
	return res != 0;
}

bool convertTGAtoPNG(const fs::path& tgaPath, const fs::path& pngPath){
	int x, y, comp;

	unsigned char* data = stbi_load(tgaPath.string().c_str(), &x, &y, &comp, 4);
	if(data == nullptr){
		return false;
	}
	int res = stbi_write_png( pngPath.string().c_str(), x, y, 4, data, 4 * x);
	stbi_image_free(data);
	return res != 0;
}

bool writeDefaultTexture(const fs::path& pngPath){
	static const Color data[] = {
		{255,   0, 255, 255}, {255,   0, 255, 255}, {255, 255, 255, 255}, {255, 255, 255, 255},
		{255,   0, 255, 255}, {255,   0, 255, 255}, {255, 255, 255, 255}, {255, 255, 255, 255},
		{255, 255, 255, 255}, {255, 255, 255, 255}, {255,   0, 255, 255}, {255,   0, 255, 255},
		{255, 255, 255, 255}, {255, 255, 255, 255}, {255,   0, 255, 255}, {255,   0, 255, 255}
	};

	return stbi_write_png(pngPath.string().c_str(), 4, 4, 4, &data[0].r, 4 * 4) != 0;

}
