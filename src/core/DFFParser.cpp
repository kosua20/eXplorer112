#include "core/DFFParser.hpp"
#include "core/Log.hpp"

#include <string>
#include <vector>
#include <string>
#include <unordered_map>

namespace Dff {

enum Type {
	Struct = 1,
	String = 2,
	Extension = 3,
	Camera = 5,
	Texture = 6,
	Material = 7,
	MaterialList = 8,
	AtomicSection = 9,
	PlaneSection = 10,
	World = 11,
	Spline = 12,
	Matrix = 13,
	FrameList = 14,
	Geometry = 15,
	Clump = 16,
	Light = 18,
	UnicodeString = 19,
	Atomic = 20,
	Raster = 21,
	TextureDictionary = 22,
	AnimationDatabase = 23,
	Image = 24,
	SkinAnimation = 25,
	GeometryList = 26,
	AnimAnimation = 27,
	Team = 28,
	Crowd = 29,
	DeltaMorphAnimation = 30,
	RightToRender = 31,
	MultiTextureEffectNative = 32,
	MultiTextureEffectDictionary = 33,
	TeamDictionary = 34,
	PlatformIndependentTextureDictionary = 35,
	TableofContents = 36,
	ParticleStandardGlobalData = 37,
	AltPipeCore = 38,
	PlatformIndependentPeds = 39,
	PatchMesh = 40,
	ChunkGroupStart = 41,
	ChunkGroupEnd = 42,
	UVAnimationDictionary = 43,
	CollTree = 44,
	HAnim = 286,
	NormalMap = 307,
	BinMesh = 1294,
	UserData = 0x11f,
	Unknown = 0xFFFF,
};


const char* typeToStr(const Dff::Type& type){

	static const std::unordered_map<Dff::Type, std::string> strs = {
		{ Struct, "Struct" },
		{ String, "String" },
		{ Extension, "Extension" },
		{ Camera, "Camera" },
		{ Texture, "Texture" },
		{ Material, "Material" },
		{ MaterialList, "MaterialList" },
		{ AtomicSection, "AtomicSection" },
		{ PlaneSection, "PlaneSection" },
		{ World, "World" },
		{ Spline, "Spline" },
		{ Matrix, "Matrix" },
		{ FrameList, "FrameList" },
		{ Geometry, "Geometry" },
		{ Clump, "Clump" },
		{ Light, "Light" },
		{ UnicodeString, "UnicodeString" },
		{ Atomic, "Atomic" },
		{ Raster, "Raster" },
		{ TextureDictionary, "TextureDictionary" },
		{ AnimationDatabase, "AnimationDatabase" },
		{ Image, "Image" },
		{ SkinAnimation, "SkinAnimation" },
		{ GeometryList, "GeometryList" },
		{ AnimAnimation, "AnimAnimation" },
		{ Team, "Team" },
		{ Crowd, "Crowd" },
		{ DeltaMorphAnimation, "DeltaMorphAnimation" },
		{ RightToRender, "RightToRender" },
		{ MultiTextureEffectNative, "MultiTextureEffectNative" },
		{ MultiTextureEffectDictionary, "MultiTextureEffectDictionary" },
		{ TeamDictionary, "TeamDictionary" },
		{ PlatformIndependentTextureDictionary, "PlatformIndependentTextureDictionary" },
		{ TableofContents, "TableofContents" },
		{ ParticleStandardGlobalData, "ParticleStandardGlobalData" },
		{ AltPipeCore, "AltPipeCore" },
		{ PlatformIndependentPeds, "PlatformIndependentPeds" },
		{ PatchMesh, "PatchMesh" },
		{ ChunkGroupStart, "ChunkGroupStart" },
		{ ChunkGroupEnd, "ChunkGroupEnd" },
		{ UVAnimationDictionary, "UVAnimationDictionary" },
		{ CollTree, "CollTree" },
		{ HAnim, "HAnim" },
		{ NormalMap, "NormalMap" },
		{ BinMesh, "BinMesh" },
		{ UserData, "UserData" },
	};
	if(strs.count(type) != 0){
		return strs.at(type).c_str();
	}
	return nullptr;
}

size_t parseHeader(FILE* file, Type& type){
	uint32_t header[3];
	fread(header, sizeof(uint32_t), 3, file);

	type = Type(header[0]);
	size_t size = header[1];
	//size_t version = (header[2] >> 16u) + 0x30000;

	// Check if we know this type of section
	const char* typeStr = typeToStr(type);
	if(typeStr){
		//Log::info("Section %s of size %llu", typeToStr(type), size);
	} else {
		Log::info("Section unknown 0x%x of size %llu", uint32_t(type), size);
	}
	
	return size;
}

void checkType(Type type, Type expected){
	Log::check(type == expected, "Expected type %s (got %s)", typeToStr(expected), typeToStr(type));
}

void parseStruct(FILE* file, size_t expectedSize){
	Type type;
	const size_t size = parseHeader(file, type);

	checkType(type, Type::Struct);

	Log::check((expectedSize == 0) || (size == expectedSize), "Expected size %zu (got %zu)", expectedSize, size );
}


void parseTexture(FILE* file, std::string& name){
	// Texture header
	Type type;
	parseHeader(file, type);
	checkType(type, Type::Texture);

	// Struct header.
	parseStruct(file, sizeof(uint16_t) + sizeof(uint8_t) * 2 );
	// Struct content
	uint8_t filterAndAddress[2];
	uint16_t hasMips;
	fread(filterAndAddress, sizeof(uint8_t), 2, file);
	fread(&hasMips, sizeof(uint16_t), 1, file);
	// Then parse a string.
	size_t strSize = parseHeader(file, type);
	checkType(type, Type::String);
	name.resize(strSize);
	fread(&name[0], sizeof(char), strSize, file);
	// And another for alpha
	size_t strAlphaSize = parseHeader(file, type);
	checkType(type, Type::String);
	std::string nameAlpha;
	nameAlpha.resize(strAlphaSize);
	fread(&nameAlpha[0], sizeof(char), strAlphaSize, file);

	//Log::info("Texture name: %s", name.c_str());
	
}

void parseExtension(FILE* file, std::string* name = nullptr){
	Type type;
	size_t extSize = parseHeader(file, type);
	size_t currPos = ftell(file);

	checkType(type, Type::Extension);
	// Parse header of the wrapped item.
	if(extSize != 0u){
		parseHeader(file, type);

		if((type == Type::NormalMap) && (name != nullptr)){
			uint32_t dummy;
			fread(&dummy, sizeof(uint32_t), 1, file);
			parseTexture(file, *name);
		}

		fseek(file, currPos + extSize, SEEK_SET);
	}
}

void absorbExtensionsUpTo(FILE* file, size_t endPos){
	// Eat other extensions (right to render...).
	while(ftell(file) < (long)endPos){
		parseExtension(file);
	}
	fseek(file, endPos, SEEK_SET); 
}

void parseClump(FILE* file, Context& context){
	(void)context;

	Type type;
	const size_t clumpSize = parseHeader(file, type);
	const size_t clumpEnd = clumpSize + ftell(file);

	checkType(type, Type::Clump);
	// Struct
	parseStruct(file, 3 * sizeof(int32_t));
	int32_t atomicCount, lightCount, cameraCount;
	fread(&atomicCount, sizeof(int32_t), 1, file);
	fread(&lightCount, sizeof(int32_t), 1, file);
	fread(&cameraCount, sizeof(int32_t), 1, file);

	Log::info("Found %d atomics, %d lights and %d cameras", atomicCount, lightCount, cameraCount);

	// Frame list
	{
		const size_t frameListSize = parseHeader(file, type);
		const size_t currPos = ftell(file);
		const size_t endPos = currPos + frameListSize;
		
		checkType(type, Type::FrameList);

		parseStruct(file, 0); // Target size not known in advance
		int32_t frameCount = 0;
		fread(&frameCount, sizeof(int32_t), 1, file);
		Log::info("Found %d frames", frameCount);

		glm::mat3 rotation;
		glm::vec3 position;
		int32_t index;
		uint32_t flags;
		for(int32_t i = 0; i < frameCount; ++i){
			// Read rotation and position
			fread(&rotation[0][0], sizeof(glm::mat3), 1, file);
			fread(&position[0], sizeof(glm::vec3), 1, file);
			// Other info.
			fread(&index, sizeof(int32_t), 1, file);
			fread(&flags, sizeof(uint32_t), 1, file);

			/*Log::info("\tRotation: %f %f %f %f %f %f %f %f %f, position %f %f %f, index %d, flags %u",
				 rotation[0].x, rotation[0].y, rotation[0].z,
				 rotation[1].x, rotation[1].y, rotation[1].z,
				 rotation[2].x, rotation[2].y, rotation[2].z,
				 position.x, position.y, position.z,
				 index, flags);
			 */

		}

		// Bones probably
  		absorbExtensionsUpTo(file, endPos);
	}

	// Geometry list
	{

		const size_t geomListSize = parseHeader(file, type);
		checkType(type, Type::GeometryList);
		const size_t geomListEnd = ftell(file) + geomListSize;

		
		parseStruct(file, sizeof(int32_t));
		int32_t geometryCount;
		fread(&geometryCount, sizeof(int32_t), 1, file);

		Log::info("Found %d geometries.", geometryCount);

		for(int32_t k = 0; k < geometryCount; ++k){
			const size_t geomSize = parseHeader(file, type);
			checkType(type, Type::Geometry);

			const size_t geomEnd = ftell(file) + geomSize;

			// Geometric data
			{
				
				parseStruct(file, 0u); // Not known in advance
				int32_t values[4];
				fread(values, sizeof(int32_t), 4, file);
				const int32_t numTexSets = (values[0] >> 16) & 255;
				const int32_t numTriangles = values[1];
				const int32_t numVertices = values[2];
				const int32_t numMorphs = values[3];
				const bool nativeGeom = ((values[0] >> 24) & 1) != 0;
				const bool prelitGeom = ((values[0] >>  3) & 1) != 0;
				// We know version is < 212992 but these are not present
				// float  "ambient"
				// float  "specular"
				// float  "diffuse"
				Log::info("Geometry with %d vertices, %d triangles, %d texsets and %d morphsets (native: %s, colors: %s).",
					numVertices, numTriangles, numTexSets, numMorphs, nativeGeom ? "yes" : "no", prelitGeom ? "yes" : "no");

				Object object;
				if(!nativeGeom){
					// Colors
					if(prelitGeom){
						object.colors.resize(numVertices);
						fread(object.colors.data(), sizeof(Color), numVertices, file);
					}

					object.uvs.resize(numTexSets);
					for(int32_t i = 0; i < numTexSets; ++i){
						object.uvs[i].resize(numVertices);
						fread(object.uvs[i].data(), sizeof(glm::vec2), numVertices, file);
					}

					object.faces.resize(numTriangles);
					fread(object.faces.data(), sizeof(Triangle), numTriangles, file);

				}

				// Morphsets.
				for(int32_t i = 0; i < numMorphs; ++i){
					float sphereParams[4];
					fread(sphereParams, sizeof(float), 4, file);
					uint32_t hasVerts, hasNorms;
					fread(&hasVerts, sizeof(uint32_t), 1, file);
					fread(&hasNorms, sizeof(uint32_t), 1, file);

					if(hasVerts != 0u){
						object.positions.resize(numVertices);
						fread(object.positions.data(), sizeof(glm::vec3), numVertices, file);
					}

					if(hasNorms != 0u){
						object.normals.resize(numVertices);
						fread(object.normals.data(), sizeof(glm::vec3), numVertices, file);
					}
				}

			}

			// Material data
			{
				parseHeader(file, type);
				checkType(type, Type::MaterialList);

				parseStruct(file, 0u); // Not knwon in advance
				uint32_t materialCount;
				fread(&materialCount, sizeof(uint32_t), 1, file);
				uint32_t effectiveCount = 0;

				for(uint32_t j = 0; j < materialCount; ++j){
					int32_t index;
					fread(&index, sizeof(int32_t), 1, file);
					// Non-negative indices are referencing existing materials.
					if(index == -1){
						++effectiveCount;
					}
				}
				Log::info("Found %u/%u materials", effectiveCount, materialCount);

				for(uint32_t j = 0; j < effectiveCount; ++j){
					const size_t materialSize = parseHeader(file, type);
					checkType(type, Type::Material);
					const size_t materialEnd = ftell(file) + materialSize;

					parseStruct(file, 3 * sizeof(int32_t) + 3 * sizeof(float) + sizeof(Color));

					Color color; 
					int32_t flags, unused, textured;
					float ambSpecDiff[3];
					fread(&flags, sizeof(int32_t), 1, file);
					fread(&color, sizeof(Color), 1, file);
					fread(&unused, sizeof(int32_t), 1, file);
					fread(&textured, sizeof(int32_t), 1 , file);
					fread(ambSpecDiff, sizeof(float), 3, file);

					Log::info("Material with flags %d, texture: %s, color: (%u,%u,%u,%u), ambient: %f, diffuse: %f, specular: %f",
						flags, textured != 0 ? "yes" : "no", color.r, color.g, color.b, color.a, ambSpecDiff[0], ambSpecDiff[2], ambSpecDiff[1]);

					if(textured != 0u){
						std::string textureName;
						parseTexture(file, textureName);

					}

					absorbExtensionsUpTo(file, materialEnd);
				}
				
			}

			absorbExtensionsUpTo(file, geomEnd);
		}
		//absorbExtensionsUpTo(file, geomEnd);
	}

	// Atomics
	{
		for(int32_t i = 0; i < atomicCount; ++i){
			size_t atomSize = parseHeader(file, type);
			checkType(type, Type::Atomic);
			const size_t endPos = ftell(file) + atomSize;
			
			parseStruct(file, 4 * sizeof(uint32_t));
			
			uint32_t values[4];
			fread(values, sizeof(uint32_t), 4, file);
			uint32_t frameIndex = values[0];
			uint32_t geometryIndex = values[1];
			uint32_t flags = values[2];

			Log::info("Atomic %d: frame %u, geometry %u, flags %u", i, frameIndex, geometryIndex, flags);

  			absorbExtensionsUpTo(file, endPos);

		}
	}

  	absorbExtensionsUpTo(file, clumpEnd);

}

bool parse(const fs::path& path, Context& context){

	FILE* file = fopen(path.c_str(), "rb");

	if(file == nullptr){
		Log::error("Unable to open file at path \"%s\"", path.c_str());
		return false;
	}

	// Seek size.
	fseek(file, 0, SEEK_END);
	const size_t fileSize = ftell(file);
	rewind(file);
	Log::info("File size: %llu", fileSize );

	// Parse
	Dff::parseClump(file, context);

	// Eat other remaining extensions.
	Dff::absorbExtensionsUpTo(file, fileSize);

	fclose(file);

	return true;
}

}
