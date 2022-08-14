#include "core/DFFParser.hpp"
#include "core/Log.hpp"
#include "core/TextUtilities.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <string>
#include <vector>
#include <string>
#include <unordered_map>
#include <sstream>
#include <algorithm>

//#define LOG_DFF_CONTENT

namespace Dff {

struct Triangle {
	uint16_t v1, v0, id, v2;
};

struct MorphSet {
	std::vector<glm::vec3> positions;
	std::vector<glm::vec3> normals;
};

using TexSet = std::vector<glm::vec2>;

struct Material {
	std::string diffuseName;
	std::string normalName;
	glm::vec3 ambSpecDiff;
};

struct Geometry {

	std::vector<MorphSet> sets;
	std::vector<TexSet> uvs;

	std::vector<Color> colors;
	std::vector<Triangle> faces;

	std::vector<Material> materials;
	std::vector<int32_t> mappings;

};

struct Frame {
	glm::mat4 mat{1.f};
	int32_t parent = -1;
};

struct Model {
	struct Pair {
		unsigned int geometry;
		unsigned int frame;
	};

	std::vector<Geometry> geometries;
	std::vector<Frame> frames;
	std::vector<Pair> pairings;
};

enum Type {
	Struct = 1,
	String = 2,
	Extension = 3,
	Camera = 5,
	Texture = 6,
	MaterialElem = 7,
	MaterialList = 8,
	AtomicSection = 9,
	PlaneSection = 10,
	World = 11,
	Spline = 12,
	Matrix = 13,
	FrameList = 14,
	GeometryElem = 15,
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
		{ MaterialElem, "Material" },
		{ MaterialList, "MaterialList" },
		{ AtomicSection, "AtomicSection" },
		{ PlaneSection, "PlaneSection" },
		{ World, "World" },
		{ Spline, "Spline" },
		{ Matrix, "Matrix" },
		{ FrameList, "FrameList" },
		{ GeometryElem, "Geometry" },
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
#ifdef LOG_DFF_CONTENT
		Log::info("[dffparser] Section %s of size %llu", typeToStr(type), size);
#endif
	} else {
		Log::warning("[dffparser] Unknown section 0x%x of size %llu", uint32_t(type), size);
	}
	
	return size;
}

bool checkType(Type type, Type expected){
	Log::check(type == expected, "[dffparser] Expected type %s (got %s)", typeToStr(expected), typeToStr(type));
	return type == expected;
}

bool parseStruct(FILE* file, size_t expectedSize){
	Type type;
	const size_t size = parseHeader(file, type);

	if(!checkType(type, Type::Struct)){
		return false;
	}

	Log::check((expectedSize == 0) || (size == expectedSize), "[dffparser] Expected size %zu (got %zu)", expectedSize, size );
	return (expectedSize == 0) || (size == expectedSize);
}


bool parseTexture(FILE* file, std::string& name){
	// Texture header
	Type type;
	parseHeader(file, type);
	if(!checkType(type, Type::Texture)){
		return false;
	}

	// Struct header.
	if(!parseStruct(file, sizeof(uint16_t) + sizeof(uint8_t) * 2 )){
		return false;
	}
	// Struct content
	uint8_t filterAndAddress[2];
	uint16_t hasMips;
	fread(filterAndAddress, sizeof(uint8_t), 2, file);
	fread(&hasMips, sizeof(uint16_t), 1, file);
	// Then parse a string.
	size_t strSize = parseHeader(file, type);
	if(!checkType(type, Type::String)){
		return false;
	}
	std::string tmp; tmp.resize(strSize);
	fread(&tmp[0], sizeof(char), strSize, file);
	name = std::string(tmp.c_str());

	// And another for alpha
	size_t strAlphaSize = parseHeader(file, type);
	if(!checkType(type, Type::String)){
		return false;
	}
	std::string tmp2; tmp2.resize(strAlphaSize);
	fread(&tmp2[0], sizeof(char), strAlphaSize, file);
	std::string nameAlpha = std::string(tmp2.c_str());

#ifdef LOG_DFF_CONTENT
	Log::info("[dffparser] Texture name: %s", name.c_str());
#endif
	return true;
}

bool parseExtension(FILE* file, std::string* name = nullptr){
	Type type;
	const size_t extSize = parseHeader(file, type);
	const long currPos = ftell(file);

	//if(!checkType(type, Type::Extension)){
	//	return false;
	//}

	// Parse header of the wrapped item.
	if(extSize != 0u){
		parseHeader(file, type);

		if((type == Type::NormalMap) && (name != nullptr)){
			uint32_t dummy;
			fread(&dummy, sizeof(uint32_t), 1, file);
			if(!parseTexture(file, *name)){
				return false;
			}
		}

		fseek(file, currPos + (long)extSize, SEEK_SET);
	}
	return true;
}

bool absorbExtensionsUpTo(FILE* file, size_t endPos){
	// Eat other extensions (right to render...).
	while(ftell(file) < (long)endPos){
		if(!parseExtension(file)){
			return false;
		}
	}
	fseek(file, (long)endPos, SEEK_SET);
	return true;
}

bool parseClump(FILE* file, Model& model){

	Type type;
	const size_t clumpSize = parseHeader(file, type);
	const size_t clumpEnd = clumpSize + ftell(file);

	if(!checkType(type, Type::Clump)){
		return false;
	}
	// Struct
	if(!parseStruct(file, 3 * sizeof(int32_t))){
		return false;
	}
	int32_t atomicCount, lightCount, cameraCount;
	fread(&atomicCount, sizeof(int32_t), 1, file);
	fread(&lightCount, sizeof(int32_t), 1, file);
	fread(&cameraCount, sizeof(int32_t), 1, file);

#ifdef LOG_DFF_CONTENT
	Log::info("[dffparser] Found %d atomics, %d lights and %d cameras", atomicCount, lightCount, cameraCount);
#endif

	// Frame list
	{
		const size_t frameListSize = parseHeader(file, type);
		const size_t currPos = ftell(file);
		const size_t endPos = currPos + frameListSize;
		
		if(!checkType(type, Type::FrameList)){
			return false;
		}
		// Target size not known in advance
		if(!parseStruct(file, 0)){
			return false;
		}
		int32_t frameCount = 0;
		fread(&frameCount, sizeof(int32_t), 1, file);

#ifdef LOG_DFF_CONTENT
		Log::info("[dffparser] Found %d frames", frameCount);
#endif

		model.frames.resize(frameCount);

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

			model.frames[i].parent = index;
			model.frames[i].mat = glm::translate(glm::mat4(1.0f), position) * glm::mat4(rotation);

#ifdef LOG_DFF_CONTENT
			Log::info("[dffparser] \tRotation: %f %f %f %f %f %f %f %f %f, position %f %f %f, index %d, flags %u",
				 rotation[0].x, rotation[0].y, rotation[0].z,
				 rotation[1].x, rotation[1].y, rotation[1].z,
				 rotation[2].x, rotation[2].y, rotation[2].z,
				 position.x, position.y, position.z,
				 index, flags);
#endif

		}

		// Bones probably
  		if(!absorbExtensionsUpTo(file, endPos)){
			return false;
		}
	}

	// Geometry list
	{

		const size_t geomListSize = parseHeader(file, type);
		if(!checkType(type, Type::GeometryList)){
			return false;
		}
		const size_t geomListEnd = ftell(file) + geomListSize;
		(void)geomListEnd;
		
		if(!parseStruct(file, sizeof(int32_t))){
			return false;
		}
		int32_t geometryCount;
		fread(&geometryCount, sizeof(int32_t), 1, file);

#ifdef LOG_DFF_CONTENT
		Log::info("[dffparser] Found %d geometries.", geometryCount);
#endif
		model.geometries.resize(geometryCount);

		for(int32_t k = 0; k < geometryCount; ++k){
			const size_t geomSize = parseHeader(file, type);
			if(!checkType(type, Type::GeometryElem)){
				return false;
			}

			const size_t geomEnd = ftell(file) + geomSize;

			Geometry& geometry = model.geometries[k];

			// Geometric data
			{
				// Not known in advance
				if(!parseStruct(file, 0u)){
					return false;
				}
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

#ifdef LOG_DFF_CONTENT
				Log::info("[dffparser] Geometry with %d vertices, %d triangles, %d texsets and %d morphsets (native: %s, colors: %s).",
					numVertices, numTriangles, numTexSets, numMorphs, nativeGeom ? "yes" : "no", prelitGeom ? "yes" : "no");
#endif
				
				if(!nativeGeom){
					// Colors
					if(prelitGeom){
						geometry.colors.resize(numVertices);
						fread(geometry.colors.data(), sizeof(Color), numVertices, file);
					}

					geometry.uvs.resize(numTexSets);
					for(int32_t i = 0; i < numTexSets; ++i){
						geometry.uvs[i].resize(numVertices);
						fread(geometry.uvs[i].data(), sizeof(glm::vec2), numVertices, file);
					}

					geometry.faces.resize(numTriangles);
					fread(geometry.faces.data(), sizeof(Triangle), numTriangles, file);

				}

				// Morphsets.
				geometry.sets.resize(numMorphs);

				for(int32_t i = 0; i < numMorphs; ++i){
					float sphereParams[4];
					fread(sphereParams, sizeof(float), 4, file);
					uint32_t hasVerts, hasNorms;
					fread(&hasVerts, sizeof(uint32_t), 1, file);
					fread(&hasNorms, sizeof(uint32_t), 1, file);

					MorphSet& set = geometry.sets[i];
					if(hasVerts != 0u){
						set.positions.resize(numVertices);
						fread(set.positions.data(), sizeof(glm::vec3), numVertices, file);
					}

					if(hasNorms != 0u){
						set.normals.resize(numVertices);
						fread(set.normals.data(), sizeof(glm::vec3), numVertices, file);
					}
				}

			}

			// Material data
			{
				parseHeader(file, type);
				if(!checkType(type, Type::MaterialList)){
					return false;
				}
				// Not known in advance
				if(!parseStruct(file, 0u)){
					return false;
				}
				uint32_t materialCount;
				fread(&materialCount, sizeof(uint32_t), 1, file);
				uint32_t effectiveCount = 0;

				geometry.mappings.resize(materialCount);

				for(uint32_t j = 0; j < materialCount; ++j){
					int32_t index;
					fread(&index, sizeof(int32_t), 1, file);
					// Non-negative indices are referencing existing materials.
					if(index == -1){
						geometry.mappings[j] = effectiveCount;
						++effectiveCount;
					} else {
						geometry.mappings[j] = index;
					}

				}

#ifdef LOG_DFF_CONTENT
				Log::info("[dffparser] Found %u/%u materials", effectiveCount, materialCount);
#endif
				geometry.materials.resize(effectiveCount);

				for(uint32_t j = 0; j < effectiveCount; ++j){
					const size_t materialSize = parseHeader(file, type);
					if(!checkType(type, Type::MaterialElem)){
						return false;
					}
					const size_t materialEnd = ftell(file) + materialSize;

					if(!parseStruct(file, 3 * sizeof(int32_t) + 3 * sizeof(float) + sizeof(Color))){
						return false;
						}

					Color color; 
					int32_t flags, unused, textured;
					glm::vec3 ambSpecDiff;
					fread(&flags, sizeof(int32_t), 1, file);
					fread(&color, sizeof(Color), 1, file);
					fread(&unused, sizeof(int32_t), 1, file);
					fread(&textured, sizeof(int32_t), 1 , file);
					fread(&ambSpecDiff[0], sizeof(float), 3, file);

#ifdef LOG_DFF_CONTENT
					Log::info("[dffparser] Material with flags %d, texture: %s, color: (%u,%u,%u,%u), ambient: %f, diffuse: %f, specular: %f",
						flags, textured != 0 ? "yes" : "no", color.r, color.g, color.b, color.a, ambSpecDiff[0], ambSpecDiff[2], ambSpecDiff[1]);
#endif

					Material& material = geometry.materials[j];

					material.ambSpecDiff = ambSpecDiff;

					if(textured != 0u){
						if(!parseTexture(file, material.diffuseName)){
							return false;
						}
						// Attempt to parse normal map if available.
						while(ftell(file) < (long)materialEnd){
							if(!parseExtension(file, &material.normalName)){
								return false;
							}
						}
					}

					if(!absorbExtensionsUpTo(file, materialEnd)){
						return false;
					}
				}
				
			}

			if(!absorbExtensionsUpTo(file, geomEnd)){
				return false;
			}
		}
		//absorbExtensionsUpTo(file, geomEnd);
	}

	// Atomics
	{
		model.pairings.resize(atomicCount);

		for(int32_t i = 0; i < atomicCount; ++i){
			size_t atomSize = parseHeader(file, type);
			if(!checkType(type, Type::Atomic)){
				return false;
			}
			const size_t endPos = ftell(file) + atomSize;
			
			if(!parseStruct(file, 4 * sizeof(uint32_t))){
				return false;
			}
			
			uint32_t values[4];
			fread(values, sizeof(uint32_t), 4, file);
			uint32_t frameIndex = values[0];
			uint32_t geometryIndex = values[1];
			// uint32_t flags = values[2]; // Unused

#ifdef LOG_DFF_CONTENT
			Log::info("[dffparser] Atomic %d: frame %u, geometry %u, flags %u", i, frameIndex, geometryIndex, flags);
#endif
			model.pairings[i] = {geometryIndex, frameIndex};

  			if(!absorbExtensionsUpTo(file, endPos)){
				return false;
			}

		}
	}

  	if(!absorbExtensionsUpTo(file, clumpEnd)){
		return false;
	}
	return true;
}

bool parse(const fs::path& path, Model& model){

	FILE* file = fopen( path.string().c_str(), "rb");

	if(file == nullptr){
		Log::error("[dffparser] Unable to open file at path \"%s\"", path.string().c_str());
		return false;
	}

	// Seek size.
	fseek(file, 0, SEEK_END);
	const size_t fileSize = ftell(file);
	rewind(file);

	// Parse
	if(!parseClump(file, model)){
		fclose(file);
		return false;
	}

	// Eat other remaining extensions.
	if(!absorbExtensionsUpTo(file, fileSize)){
		fclose(file);
		return false;
	}

	fclose(file);
	return true;
}

void convertToObj(Model& model, Object& outObject){

	// Nothing to export.
	if(model.pairings.empty()){
		return;
	}

	// Convert geometries.
	for(Dff::Geometry& geom : model.geometries){
		// Sort triangles based on material first.
		std::sort(geom.faces.begin(), geom.faces.end(), [](const Dff::Triangle& a, const Dff::Triangle& b){
			if(a.id < b.id){
				return true;
			}
			if(a.id > b.id){
				return false;
			}
			return (a.v0 < b.v0) || (a.v0 == b.v0 && a.v1 < b.v1) || (a.v0 == b.v0 && a.v1 == b.v1 && a.v2 < b.v2);
		});
	}

	// From all the pairs of frame/geometry, we need to build a valid set of objects, each with a texture.
	int pairId = 0;
	uint32_t vertexIndex = 0;
	uint32_t uvIndex = 0;
	uint32_t normalIndex = 0;
	uint32_t colorIndex = 0;

	for(const Dff::Model::Pair& pair : model.pairings){

		// Rebuild frame.
		int32_t frameIndex = pair.frame;
		glm::mat4 totalFrame(1.0f);
		do {
			totalFrame = model.frames[frameIndex].mat * totalFrame;
			frameIndex = model.frames[frameIndex].parent;
			if(frameIndex >= (int32_t)model.frames.size()){
				Log::warning("Unexpected frame index: %d, stopping.", frameIndex);
				break;
			}
		} while(frameIndex >= 0);

		const glm::mat3 totalFrameNormal = glm::transpose(glm::inverse(glm::mat3(totalFrame)));
		// Fetch geometry.
		const Dff::Geometry& geom = model.geometries[pair.geometry];
		// Assumption: always one morphset and one texset.
		const Dff::MorphSet& set = geom.sets[0];

		// Output vertices, normals and uvs if present.
		const uint32_t vertCount = (uint32_t)set.positions.size();

		const bool hasNormals = (uint32_t)set.normals.size() == vertCount;
		// Always use the first set of UVs.
		const bool hasUvs = !geom.uvs.empty() && ( (uint32_t) geom.uvs[0].size() == vertCount);
		const bool hasColors = (uint32_t) geom.colors.size() == vertCount;
		
		outObject.positions.reserve( (uint32_t) outObject.positions.size() + vertCount);
		for(size_t vid = 0; vid < vertCount; ++vid){
			const glm::vec3 tpos = glm::vec3(totalFrame * glm::vec4(set.positions[vid], 1.f));
			outObject.positions.push_back(tpos);
		}

		if(hasNormals){
			outObject.normals.reserve(outObject.normals.size() + vertCount);
			for(size_t vid = 0; vid < vertCount; ++vid){
				const glm::vec3 tnor = glm::normalize(totalFrameNormal * glm::normalize(set.normals[vid]));
				outObject.normals.push_back(tnor);
			}
		}
		if(hasUvs){
			const Dff::TexSet& uvs = geom.uvs[0];
			outObject.uvs.reserve(outObject.uvs.size() + vertCount);
			for(size_t vid = 0; vid < vertCount; ++vid){
				outObject.uvs.push_back(uvs[vid]);
			}
		}
		if(hasColors){
			outObject.colors.reserve(outObject.colors.size() + vertCount);
			for(size_t vid = 0; vid < vertCount; ++vid){
				const Color& col = geom.colors[vid];
				outObject.colors.emplace_back(col.r, col.g, col.b);
			}
		}

		// Then triangles, split by material.
		const size_t triCount = geom.faces.size();
		outObject.faceSets.reserve(geom.mappings.size());

		uint16_t materialId = 0xFF;
		for(size_t tid = 0; tid < triCount; ++tid){

			if(geom.faces[tid].id != materialId){
				// New material
				Object::Material& newMaterial = outObject.materials.emplace_back();
				// Retrieve raw material info.
				materialId = geom.faces[tid].id;
				const Dff::Material& material = geom.materials[geom.mappings[materialId]];
				// Albedo
				{
					std::string textureName = TextUtilities::lowercase(material.diffuseName);
					newMaterial.color = !textureName.empty() ? textureName : DEFAULT_ALBEDO_TEXTURE;
				}
				// Normal
				{
					std::string textureName = TextUtilities::lowercase(material.normalName);
					newMaterial.normal = !textureName.empty() ? textureName : DEFAULT_NORMAL_TEXTURE;
				}

				Object::Set& faceSet = outObject.faceSets.emplace_back();
				faceSet.faces.reserve(256);
				faceSet.material = outObject.materials.size()-1;
			}

			Object::Set& faceSet = outObject.faceSets.back();
			Object::Set::Face& face = faceSet.faces.emplace_back();

			const uint32_t v0 = geom.faces[tid].v0;
			const uint32_t v1 = geom.faces[tid].v1;
			const uint32_t v2 = geom.faces[tid].v2;

			face.v0 = v0+vertexIndex;
			face.v1 = v1+vertexIndex;
			face.v2 = v2+vertexIndex;

			if(hasUvs){
				face.t0 = (v0+uvIndex);
				face.t1 = (v1+uvIndex);
				face.t2 = (v2+uvIndex);
			}

			if(hasNormals){
				face.n0 = (v0+normalIndex);
				face.n1 = (v1+normalIndex);
				face.n2 = (v2+normalIndex);
			}

			if(hasColors){
				face.c0 = (v0+colorIndex);
				face.c1 = (v1+colorIndex);
				face.c2 = (v2+colorIndex);
			}
		}

		vertexIndex += vertCount;
		uvIndex += hasUvs ? vertCount : 0;
		normalIndex += hasNormals ? vertCount : 0;
		colorIndex += hasColors ? vertCount : 0;
		++pairId;
	}

}

bool load(const fs::path& path, Object& outObject){

	Model model;
	if(!parse(path, model)){
		Log::error("Failed to parse.");
		return false;
	}

	outObject.name = path.filename().replace_extension().string();

	convertToObj(model, outObject);

	return true;
}

}
