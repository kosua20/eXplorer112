
#include "Scene.hpp"

#include "core/System.hpp"
#include "core/TextUtilities.hpp"
#include "core/Random.hpp"
#include "core/Common.hpp"

#include "system/Window.hpp"
#include "graphics/GPU.hpp"
#include "input/Input.hpp"
#include "input/ControllableCamera.hpp"
#include "Common.hpp"

#ifdef DEBUG
#define DEBUG_UI
#endif

// One bit for the shading
#define MODE_SHADING_NONE 0
#define MODE_SHADING_LIGHT 1
// Two bits for the albedo
#define MODE_ALBEDO_UNIFORM 0
#define MODE_ALBEDO_NORMAL 1
#define MODE_ALBEDO_TEXTURE 2

#define MODE_POSTPROCESS_BLOOM 1
#define MODE_POSTPROCESS_GRAIN 2
#define MODE_POSTPROCESS_NIGHT 4
#define MODE_POSTPROCESS_BANDW 8
#define MODE_POSTPROCESS_JITTER 16
#define MODE_POSTPROCESS_HEAT 32
#define MODE_POSTPROCESS_UNDERWATER 64

#define CLUSTER_XY_SIZE 64
#define CLUSTER_Z_COUNT 32

static const std::vector<uint> boxIndices = { 0, 1, 0, 0, 2, 0,
	1, 3, 1, 2, 3, 2,
	4, 5, 4, 4, 6, 4,
	5, 7, 5, 6, 7, 6,
	1, 5, 1, 0, 4, 0,
	2, 6, 2, 3, 7, 3};

static const std::vector<uint> octaIndices = {
	0, 2, 0, 0, 3, 0, 0, 4, 0, 0, 5, 0,
	1, 2, 1, 1, 3, 1, 1, 4, 1, 1, 5, 1,
	2, 4, 2, 2, 5, 2, 3, 4, 3, 3, 5, 3,
	0, 1, 0, 2, 3, 2, 4, 5, 4
};

static const std::array<BlendFunction, World::BLEND_COUNT> srcFuncs = {
	BlendFunction::ONE,
	BlendFunction::ONE,
	BlendFunction::DST_COLOR,
	BlendFunction::SRC_ALPHA,
	BlendFunction::ONE,
};
static const std::array<BlendFunction, World::BLEND_COUNT> dstFuncs = {
	BlendFunction::ZERO,
	BlendFunction::ONE,
	BlendFunction::ZERO,
	BlendFunction::ONE_MINUS_SRC_ALPHA,
	BlendFunction::ONE_MINUS_SRC_COLOR,
};

static const std::unordered_map<World::Blending, const char*> blendNames = {
			{ World::BLEND_OPAQUE, "Opaque" },
			{ World::BLEND_ADDITIVE, "Additive" },
			{ World::BLEND_MULTIPLY, "Multiply" },
			{ World::BLEND_ALPHA, "Alpha" },
			{ World::BLEND_COMPOSITE, "Composite" },
			{ World::BLEND_COUNT, "Unknown" },
};

static const std::vector<World::Blending> blendsPreFog = {
	World::BLEND_OPAQUE, World::BLEND_MULTIPLY, World::BLEND_ALPHA
};
static const std::vector<World::Blending> blendsPostFog = {
	World::BLEND_ADDITIVE, World::BLEND_COMPOSITE
};

static const std::unordered_map<World::Alignment, const char*> alignNames = {
			{ World::ALIGN_WORLD, "World" },
			{ World::ALIGN_AROUND_X, "Around X" },
			{ World::ALIGN_SCREEN, "Screen" },
	{ World::ALIGN_AROUND_Y, "Around Y" },
	{ World::ALIGN_COUNT, "Unknown" },
};



class ViewerConfig : public RenderingConfig {
public:

	ViewerConfig(const std::vector<std::string> & argv) : RenderingConfig(argv) {
		for(const auto & arg : arguments()) {
			const std::string key					= arg.key;
			const std::vector<std::string> & values = arg.values;

			if(key == "path") {
				path = values[0];
			}
		}

		registerSection("Viewer");
		registerArgument("path", "", "Path to the game 'resources' directory");

	}

	fs::path path;
};


struct FrameData {
	glm::mat4 v{1.0f};
	glm::mat4 p{1.0f};
	glm::mat4 vp{1.0f};
	glm::mat4 vpCulling{1.0f};
	glm::mat4 iv{1.0f};
	glm::mat4 ip{1.0f};
	glm::mat4 nvp{1.0f};
	glm::vec4 resolution{0.0f};
	glm::vec4 color{1.0f};
	glm::vec4 camPos{1.0f};
	glm::vec4 camPlanes{0.0f};

	uint showFog = 0;
	// Shading settings.
	uint shadingMode = 0;
	uint albedoMode = 0;
	uint postprocessMode = 0;

	float randomX;
	float randomY;
	float randomZ;
	float randomW;

	// Clustering
	uint frameIndex = 0;
	uint skipCulling = 0;
	uint lightsCount;
	uint zonesCount;

	glm::uvec4 clustersSize{1u}; // tile size in w
	glm::vec4 clustersParams{0.0f};

	uint meshCount = 0;
	// Selection data.
	int selectedMesh = -1;
	int selectedInstance= -1;
	int selectedTextureArray= -1;
	int selectedTextureLayer= -1;

};

struct TransparentFrameData {
	uint firstMesh;
	uint meshCount;
	uint instanceCount;
};

struct TransparentInstanceInfos {
	uint meshIndex;
	uint instanceIndex;
	uint distance;
};

struct SelectionState {
   int item = -1;
   int mesh = -1;
   int instance = -1;
   int texture = -1;
};


struct ProgramInfos {
	Program* program;
	std::vector<std::string> names;

	void use(){
		program->use();
	}
};

#define SHADERS_DIRECTORY (APP_RESOURCE_DIRECTORY / "shaders")

ProgramInfos loadProgram(const std::string& vertName, const std::string& fragName){
	std::vector<fs::path> names;
	const std::string vertContent = System::getStringWithIncludes(SHADERS_DIRECTORY / (vertName + ".vert"), names);
	names.clear();
	const std::string fragContent = System::getStringWithIncludes(SHADERS_DIRECTORY / (fragName + ".frag"), names);

	Program* prog = new Program(vertName + "_" + fragName, vertContent, fragContent);
	return {prog, {vertName, fragName}};
}

ProgramInfos loadProgram(const std::string& computeName){
	std::vector<fs::path> names;
	const std::string compContent = System::getStringWithIncludes(SHADERS_DIRECTORY / (computeName + ".comp"), names);
	Program* prog =  new Program(computeName, compContent);
	return {prog, {computeName}};
}

void reload(ProgramInfos& infos){
	if(infos.program->type() == Program::Type::COMPUTE){
		std::vector<fs::path> names;
		const std::string compContent = System::getStringWithIncludes(SHADERS_DIRECTORY / (infos.names[0] + ".comp"), names);
		infos.program->reload(compContent);
	} else {
		std::vector<fs::path> names;
		const std::string vertContent = System::getStringWithIncludes(SHADERS_DIRECTORY / (infos.names[0] + ".vert"), names);
		names.clear();
		const std::string fragContent = System::getStringWithIncludes(SHADERS_DIRECTORY / (infos.names[1] + ".frag"), names);
		infos.program->reload(vertContent, fragContent);
	}

}


void adjustCameraToBoundingBox(ControllableCamera& camera, const BoundingBox& bbox){
	// Center the camera.
	const glm::vec3 center = bbox.getCentroid();
	const glm::vec3 extent = bbox.getSize();
	// Keep the camera off the object.
	const float maxExtent = glm::max(extent[0], glm::max(extent[1], extent[2]));
	// Handle case where the object is a flat quad (leaves, decals...).
	glm::vec3 offset = std::abs(extent[0]) < 1.0f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 1.0f);
	camera.pose(center + maxExtent * offset, center, glm::vec3(0.0, 1.0f, 0.0f));
}

enum SelectionFilter {
	SCENE = 1 << 0,
	MESH = 1 << 1,
	INSTANCE = 1 << 2,
	TEXTURE = 1 << 3,
	OBJECT = MESH | INSTANCE,
	ALL = SCENE | MESH | INSTANCE | TEXTURE
};

void deselect(FrameData& _frame, SelectionState& _state, SelectionFilter _filter){
	if(_filter & SCENE){
		_state.item = -1;
	}
	if(_filter & MESH){
		_frame.selectedMesh = -1;
		_state.mesh = -1;
	}
	if(_filter & INSTANCE){
		_frame.selectedInstance = -1;
		_state.instance = -1;
	}
	if(_filter & TEXTURE){
		_frame.selectedTextureArray = -1;
		_frame.selectedTextureLayer = -1;
		_state.texture = -1;
	}

}

struct EngineTextures {
	Texture white{"whiteMap"};
	Texture black{"blackMap"};
	Texture noisePulse{ "noisePulseMap"};
	Texture fogXY{"fogXYMap"};
	Texture fogZ{"fogZMap"};
	Texture noise{ "noiseMap" };
	Texture water{ "waterMap" };
	Texture bg{ "backgroundMap" };
	Texture heat{"heatLookup"};
};

void loadTextureFromImage(const fs::path& path, Texture& dstTexture){
	dstTexture.clean();
	Image& img = dstTexture.images.emplace_back();
	img.load(path);
	dstTexture.width = img.width;
	dstTexture.height = img.height;
	dstTexture.shape = TextureShape::D2;
	dstTexture.depth = 1;
	dstTexture.levels = 1;
	dstTexture.upload(Layout::RGBA8, false);
}

void loadEngineTextures(const GameFiles& gameFiles, EngineTextures& textures){
	if(gameFiles.texturesPath.empty()){
		return;
	}

	loadTextureFromImage(gameFiles.texturesPath / "commons" / "fog_xy.png", textures.fogXY);
	loadTextureFromImage(gameFiles.texturesPath / "commons" / "fog_z.png", textures.fogZ);
	loadTextureFromImage(gameFiles.texturesPath / "commons" / "noise.tga", textures.noise);
	loadTextureFromImage(gameFiles.texturesPath / "ui" / "background.tga", textures.bg);
	loadTextureFromImage( gameFiles.texturesPath / "commons" / "heat.png", textures.heat);
	// Texture array
	{
		textures.noisePulse.clean();
		for( uint i = 0; i < 3; ++i )
		{
			const std::string name = "pulsenoise_" + std::to_string( i ) + ".tga";
			Image& img = textures.noisePulse.images.emplace_back();
			img.load( gameFiles.texturesPath / "caustics" / name );
		}
		textures.noisePulse.width = textures.noisePulse.images[0].width;
		textures.noisePulse.height = textures.noisePulse.images[ 0 ].height;
		textures.noisePulse.levels = 1;
		textures.noisePulse.depth = ( uint )textures.noisePulse.images.size();
		textures.noisePulse.shape = TextureShape::Array2D;
		textures.noisePulse.upload( Layout::RGBA8, false );
	}
	// 3D texture (hack)
	{
		const fs::path path = gameFiles.texturesPath / "commons" / "noisevolume.dds";
		textures.water.clean();
		uint layer = 0;
		while( true )
		{
			Image& img = textures.water.images.emplace_back();
			if( !img.load( path, layer ) )
			{
				// We reached the last slice.
				textures.water.images.pop_back();
				break;
			}
			++layer;
		}
		
		textures.water.width = textures.water.images[ 0 ].width;
		textures.water.height = textures.water.images[ 0 ].height;
		textures.water.levels = 1;
		textures.water.depth = ( uint )textures.water.images.size();
		textures.water.shape = TextureShape::D3;
		textures.water.upload( Layout::R8, false );
	}

	{
		Image& imgWhite = textures.white.images.emplace_back(4, 4, 4, 255);
		textures.white.width = imgWhite.width;
		textures.white.height = imgWhite.height;
		textures.white.shape = TextureShape::D2;
		textures.white.depth = 1;
		textures.white.levels = 1;
		textures.white.upload(Layout::RGBA8, false);

		Image& imgBlack = textures.black.images.emplace_back(4, 4, 4, 0);
		textures.black.width = imgBlack.width;
		textures.black.height = imgBlack.height;
		textures.black.shape = TextureShape::D2;
		textures.black.depth = 1;
		textures.black.levels = 1;
		textures.black.upload(Layout::RGBA8, false);
	}
}


struct DebugVisualisation {

   Mesh boundingBox{"bbox"};
   Mesh lights{"lights"};
   Mesh zones{"zones"};
   Mesh fxs{"fxs"};

   bool showWireframe = false;
   bool showLights = false;
   bool showFxs = false;
   bool showZones = false;

   int shadingMode = MODE_SHADING_LIGHT;
   int albedoMode = MODE_ALBEDO_TEXTURE;

   bool freezeCulling = false;

#ifdef DEBUG_UI
	int textureLayerIndex = 0;
	int textureMipIndex = 0;
#endif

	bool anyActive() const {
		return showWireframe || showLights || showFxs || showZones;
	}

	void addLightGizmo(Mesh& mesh, const World::Light& light){
		 static const uint circeSubdivs = 64;
		 static const float arrowScale = 80.0f;

		 // Generate angles if not already done.
		 static std::vector<glm::vec2> coordinates;
		 if(coordinates.size() != circeSubdivs){
			 coordinates.resize(circeSubdivs);
			 for(uint a = 0u; a < circeSubdivs; ++a){
				 const float angle = (float)a / (float)(circeSubdivs-1) * 360.0f / 180.0f * glm::pi<float>();
				 coordinates[a][0] = std::cos(angle);
				 coordinates[a][1] = std::sin(angle);
			 }
		 }

		 const uint firstVertexIndex = ( uint )mesh.positions.size();

		 // Always generate a small cross.
		 for(uint i = 0; i < 3; ++i){
			 const uint iIndex = ( uint )mesh.positions.size();
			 glm::vec3 offset(0.0f);
			 offset[i] = 10.0f;
			 mesh.positions.push_back(-offset);
			 mesh.positions.push_back( offset);
			 mesh.indices.push_back(iIndex );
			 mesh.indices.push_back(iIndex + 1);
			 mesh.indices.push_back(iIndex );
		 }

		 switch(light.type){
			 case World::Light::DIRECTIONAL:
			 {
				 // Arrow.
				 const uint iIndex = ( uint )mesh.positions.size();
				 const float arrowEdgesLength = 0.8f * arrowScale;
				 const float arrowEdgesSpread = 0.2f * arrowScale;
				 mesh.positions.emplace_back(0.0f, 0.0f, arrowScale);
				 mesh.positions.emplace_back(0.0f, 0.0f, 0.0f);
				 mesh.positions.emplace_back( arrowEdgesSpread, 0.0f, arrowEdgesLength);
				 mesh.positions.emplace_back(-arrowEdgesSpread, 0.0f, arrowEdgesLength);
				 mesh.positions.emplace_back(0.0f,  arrowEdgesSpread, arrowEdgesLength);
				 mesh.positions.emplace_back(0.0f, -arrowEdgesSpread, arrowEdgesLength);
				 for(uint i = 1; i < 6; ++i){
					 mesh.indices.push_back(iIndex);
					 mesh.indices.push_back(iIndex + i);
					 mesh.indices.push_back(iIndex);
				 }
				 break;
			 }
			 case World::Light::SPOT:
			 {
				 // Cone
				 const uint iIndex = ( uint )mesh.positions.size();
				 mesh.positions.emplace_back(0.0f, 0.0f, 0.0f);
				 mesh.positions.emplace_back(-light.radius.x, -light.radius.y, light.radius.z);
				 mesh.positions.emplace_back( light.radius.x, -light.radius.y, light.radius.z);
				 mesh.positions.emplace_back( light.radius.x,  light.radius.y, light.radius.z);
				 mesh.positions.emplace_back(-light.radius.x,  light.radius.y, light.radius.z);

				 for(uint i = 1; i < 5; ++i){
					 mesh.indices.push_back(iIndex);
					 mesh.indices.push_back(iIndex + i);
					 mesh.indices.push_back(iIndex);
				 }
				 for(uint i = 1; i < 5; ++i){
					 const uint iNext = i == 4 ? 1 : (i+1);
					 mesh.indices.push_back(iIndex + i);
					 mesh.indices.push_back(iIndex + iNext);
					 mesh.indices.push_back(iIndex + i);
				 }
				 break;
			 }
			 case World::Light::POINT:
			 {
				 // Three circles of radius 1, along the three axis.
				 const uint totalVertCount = 3 * circeSubdivs;
				 mesh.positions.reserve(mesh.positions.size() + (size_t)totalVertCount);
				 mesh.indices.reserve(mesh.indices.size() + 3 * (size_t)totalVertCount);

				 for(int i = 0; i < 3; ++i){
					 const uint xIndex = (i + 1) % 3;
					 const uint yIndex = (i + 2) % 3;
					 const uint iIndex = (uint)mesh.positions.size();
					 for(uint a = 0u; a < circeSubdivs; ++a){
						 glm::vec3& p = mesh.positions.emplace_back(0.0f);
						 p[xIndex] = coordinates[a][0] * light.radius[xIndex];
						 p[yIndex] = coordinates[a][1] * light.radius[yIndex];

						 // Degenerate triangle for line rendering.
						 if(a != 0){
							 mesh.indices.push_back(iIndex + a);
							 mesh.indices.push_back(iIndex + a - 1);
							 mesh.indices.push_back(iIndex + a);
						 }
					 }
				 }
				 break;
			 }
			 default:
				 break;
		 }

		 // Fill colors.
		 const uint vertexFinalCount = ( uint )mesh.positions.size() - firstVertexIndex;
		 mesh.colors.insert(mesh.colors.end(), vertexFinalCount, light.color);
		 // Apply light frame.
		 for(uint i = 0; i < vertexFinalCount; ++i){
			 glm::vec3& p = mesh.positions[firstVertexIndex + i];
			 p = glm::vec3(light.frame * glm::vec4(p, 1.0f));
		 }
	}

	void addEmitterGizmo(Mesh& mesh, const World::Emitter& fx){

		 const uint firstVertexIndex = ( uint )mesh.positions.size();
		 // Always generate a small cross.
		 for(uint i = 0; i < 3; ++i){
			 const uint iIndex = ( uint )mesh.positions.size();
			 glm::vec3 offset(0.0f);
			 offset[(i+1)%3] = 10.0f;
			 offset[(i+2)%3] = 10.0f;
			 mesh.positions.push_back(-offset);
			 mesh.positions.push_back( offset);
			 mesh.indices.push_back(iIndex );
			 mesh.indices.push_back(iIndex + 1);
			 mesh.indices.push_back(iIndex );
		 }

		 // Add box.
		 const auto& corners = fx.bbox.getCorners();
		 const uint indexShift = ( uint )mesh.positions.size();
		 mesh.positions.insert( mesh.positions.end(), corners.begin(), corners .end());
		 for( const uint ind : boxIndices ){
			 mesh.indices.push_back( indexShift + ind );
		 }

		 // Fill colors.
		 const uint vertexFinalCount = ( uint )mesh.positions.size() - firstVertexIndex;
		 const glm::vec3 color = 0.5f * (fx.colorMin + fx.colorMax);
		 mesh.colors.insert(mesh.colors.end(), vertexFinalCount, glm::vec3(color));
		 // Apply frame.
		 for(uint i = 0; i < vertexFinalCount; ++i){
			 glm::vec3& p = mesh.positions[firstVertexIndex + i];
			 p = glm::vec3(fx.frame * glm::vec4(p, 1.0f));
		 }
	}

	void addBillboardGizmo( Mesh& mesh, const World::Billboard& fx )
	{
		 const uint firstVertexIndex = (uint)mesh.positions.size();
		 // Generate a quad.
		 const glm::vec3 c00 = glm::vec3( -0.5f * fx.size, 0.f);
		 const glm::vec3 c11 = glm::vec3(  0.5f * fx.size, 0.f);
		 const glm::vec3 c01 = glm::vec3( c00.x, c11.y, 0.f );
		 const glm::vec3 c10 = glm::vec3( c11.x, c00.y, 0.f );

		 mesh.positions.push_back( c00 );
		 mesh.positions.push_back( c01 );
		 mesh.positions.push_back( c11 );
		 mesh.positions.push_back( c10 );

		 mesh.indices.push_back( firstVertexIndex + 0 );
		 mesh.indices.push_back( firstVertexIndex + 1 );
		 mesh.indices.push_back( firstVertexIndex + 0 );

		 mesh.indices.push_back( firstVertexIndex + 1 );
		 mesh.indices.push_back( firstVertexIndex + 2 );
		 mesh.indices.push_back( firstVertexIndex + 1 );

		 mesh.indices.push_back( firstVertexIndex + 2 );
		 mesh.indices.push_back( firstVertexIndex + 3 );
		 mesh.indices.push_back( firstVertexIndex + 2 );

		 mesh.indices.push_back( firstVertexIndex + 3 );
		 mesh.indices.push_back( firstVertexIndex + 0 );
		 mesh.indices.push_back( firstVertexIndex + 3 );

		 // Fill colors.
		 const uint vertexFinalCount = ( uint )mesh.positions.size() - firstVertexIndex;
		 mesh.colors.insert( mesh.colors.end(), vertexFinalCount, fx.color );
		 // Apply frame.
		 for( uint i = 0; i < vertexFinalCount; ++i )
		 {
			 glm::vec3& p = mesh.positions[ firstVertexIndex + i ];
			 p = glm::vec3( fx.frame * glm::vec4( p, 1.0f ) );
		 }
	}

	void addZoneGizmo( Mesh& mesh, const World::Zone& zone )
	{
		const uint indexShift = ( uint )mesh.positions.size();
		// Build box.
		const auto corners = zone.bbox.getCorners();
		const std::vector<glm::vec3> colors( corners.size(), 3.0f * zone.ambientColor );
		mesh.positions.insert( mesh.positions.end(), corners.begin(), corners.end() );
		mesh.colors.insert( mesh.colors.end(), colors.begin(), colors.end() );
		// Setup degenerate triangles for each line of a octahedron.
		for( const uint ind : boxIndices )
		{
			mesh.indices.push_back( indexShift + ind );
		}
	}

	void buildGizmos(const Scene& scene){
		lights.clean();
		zones.clean();
		fxs.clean();

		// Build light debug visualisation.
		if( !scene.world.lights().empty() ){
			for( const World::Light& light : scene.world.lights() ){
				addLightGizmo( lights, light );
			}
			lights.upload();
		}
		if( !scene.world.particles().empty() || !scene.world.billboards().empty() ){
			for( const World::Emitter& fx : scene.world.particles() ){
				addEmitterGizmo( fxs, fx );
			}
			for( const World::Billboard& fx : scene.world.billboards() ){
				addBillboardGizmo( fxs, fx );
			}
			fxs.upload();
		}
		// Build zones debug visualisation.
		if( !scene.world.zones().empty() )	{
			for( const World::Zone& zone : scene.world.zones() ){
				addZoneGizmo(zones, zone);
			}
			zones.upload();
		}
	}

	void buildBoundingBoxes(const std::vector<BoundingBox>& bboxes){
		boundingBox.clean();
		// Generate a mesh with bounding boxes of all instances.
		for(const BoundingBox& bbox : bboxes){
			const uint indexShift = (uint)boundingBox.positions.size();
			const auto corners = bbox.getCorners();
			boundingBox.positions.insert(boundingBox.positions.end(), corners.begin(), corners.end());
			// Setup degenerate triangles for each line of a cube.
			for(const uint ind : boxIndices){
				boundingBox.indices.push_back(indexShift + ind);
			}
		}
		boundingBox.colors.resize(boundingBox.positions.size(), glm::vec3(1.0f, 0.0f, 0.0f));
		boundingBox.upload();
	}
};

struct ShadowGeneration {

	Texture maps{"ShadowMaps"};
	UniformBuffer<FrameData> shadowInfos{1, 2, "ShadowInfos"};

	std::unique_ptr<Buffer> drawCommands = nullptr;
	std::unique_ptr<Buffer> drawInstances = nullptr;

    uint currentLight = 0u;
    uint currentLightFace = 0u;
    uint currentMapLayer = 0u;
    bool rendering = false;

	Program* drawArgsCompute = nullptr;
	Program* shadowInstancedObject = nullptr;

	void setup(const Scene& scene){
		uint shadowCount = 0u;
		uint shadowcasterCount = 0u;
		for( const World::Light& light : scene.world.lights() ){
			if( light.shadow ){
				shadowCount += ( light.type == World::Light::POINT ? 6u : 1u );
				++shadowcasterCount;
			}
		}
		shadowCount = std::max( shadowCount, 1u );
		maps.resize( maps.width, maps.height, shadowCount );

		currentLight = 0u;
		currentMapLayer = 0u;
		currentLightFace = 0u;

		const Scene::MeshRange& opaqueRange = scene.globalMeshMaterialRanges[Object::Material::OPAQUE];
		drawCommands = std::make_unique<Buffer>( opaqueRange.count * sizeof( GPU::DrawCommand ), BufferType::INDIRECT, "ShadowDrawCommands" );
		drawInstances = std::make_unique<Buffer>( opaqueRange.instanceCount * sizeof( uint ), BufferType::STORAGE, "ShadowDrawInstances" );
	}

	void renderMapIfNeeded(const Scene& scene){
		rendering = false;
		uint lightsCount = (uint)scene.world.lights().size();
		if(currentLight >= lightsCount || currentMapLayer >= maps.depth){
			return;
		}

		// As long as we have a light left to process and enough room in the texture array.
		const glm::mat4 pointViews[6] = {
			glm::lookAt(glm::vec3(0.0f), glm::vec3(-1.0f,0.0f,0.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
			glm::lookAt(glm::vec3(0.0f), glm::vec3( 1.0f,0.0f,0.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
			glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f,-1.0f,0.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
			glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f,0.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
			glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f,0.0f,-1.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
			glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f,0.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
		};

		// Find the next light and face to render.
		uint currentLightFaceCount = scene.world.lights()[currentLight].type == World::Light::POINT ? 6u : 1u;
		if(currentLightFace >= currentLightFaceCount){
			// Look at following lights
			++currentLight;
			currentLightFace = 0u;
			// Until we find a shadow casting one.
			for(; currentLight < lightsCount; ++currentLight){
				if(scene.world.lights()[currentLight].shadow){
					break;
				}
			}
		}
		// None.
		if(currentLight >= lightsCount){
			return;
		}

		const Scene::LightInfos& infos = ( *scene.lightInfos )[ currentLight ];
		const bool isPointLight = scene.world.lights()[ currentLight ].type == World::Light::POINT;
		uint layerId = currentLightFace;

		// Shadow map projection
		glm::mat4 vp = infos.vp;
		if( isPointLight ){
			vp = infos.vp * glm::translate(pointViews[layerId], -glm::vec3(infos.positionAndMaxRadius));
		}

		// Render opaques, skip decals and transparent.
		const auto& range = scene.globalMeshMaterialRanges[Object::Material::OPAQUE];
		shadowInfos[ 0 ].vp = vp;
		shadowInfos[ 0 ].vpCulling = vp;
		shadowInfos[ 0 ].skipCulling = 0;
		shadowInfos[ 0 ].meshCount = range.count;
		shadowInfos.upload();

		// Draw commands for the shadow maps.
		drawArgsCompute->use();
		drawArgsCompute->buffer( shadowInfos, 0 );
		drawArgsCompute->buffer( *scene.meshInfos, 1 );
		drawArgsCompute->buffer( *scene.instanceInfos, 2 );
		drawArgsCompute->buffer( *drawCommands, 3 );
		drawArgsCompute->buffer( *drawInstances, 4 );

		GPU::dispatch( range.count, 1, 1 );

		GPU::bindFramebuffer( currentMapLayer, 0, 0.0f, LoadOperation::DONTCARE, LoadOperation::DONTCARE, &maps, nullptr, nullptr, nullptr, nullptr );
		GPU::setViewport( maps );

		GPU::setPolygonState( PolygonMode::FILL );
		GPU::setCullState( false );
		GPU::setDepthState( true, TestFunction::GEQUAL, true );
		GPU::setBlendState( false );
		GPU::setColorState( false, false, false, false );

		shadowInstancedObject->use();
		shadowInstancedObject->buffer( shadowInfos, 0 );
		shadowInstancedObject->buffer( *scene.meshInfos, 1 );
		shadowInstancedObject->buffer( *scene.instanceInfos, 2 );
		shadowInstancedObject->buffer( *scene.materialInfos, 3 );
		shadowInstancedObject->buffer( *drawInstances, 4 );

		GPU::drawIndirectMesh(scene.globalMesh, *drawCommands, range.firstIndex, range.count);

		++currentMapLayer;
		++currentLightFace;
		rendering = true;
	}
};

uint roundUp(float a, uint step){
	return (int)std::floor(a - 1.f) / int(step) + 1;
}
uint roundUp( uint a, uint step ){
	return (int)std::floor( int(a) - 1) / int( step ) + 1;
}

int main(int argc, char ** argv) {

	// TODO:
	//	* Sort transparent objects back-to-front:
	//		we can't just have a list of (mesh, instance count, instance list) anymore
	//		for this pass, we need to interlace different meshes instances. Maybe just
	//		allocate an indirect arg buffer for the worst (total instance count) case.
	//		But with the MoltenVk limitation, this means one drawcall per instance per
	//		mesh, far from an indirect approach (excepts if this ends up being fixed)?
	//	* Bug in the light/zone clustering, for some camera angles froxels are missing
	//      some lights/zones. Especially visible on MoltenVK, but could be compounded
	//      with another bug or driver issue.

	// First, init/parse/load configuration.
	ViewerConfig config(std::vector<std::string>(argv, argv + argc));
	if(config.showHelp()) {
		return 0;
	}
	Random::seed(112112);

	bool allowEscapeQuit = false;
#ifdef DEBUG
	allowEscapeQuit = true;
#endif
	const std::string iniPath = (APP_RESOURCE_DIRECTORY / "imgui.ini").string();

	Window window("eXperience112 viewer", config, allowEscapeQuit);
	ImGui::GetIO().IniFilename = iniPath.c_str();

	// Try to load configuration.
	GameFiles gameFiles;
	if(!config.path.empty()){
		if(fs::exists(config.path)){
			gameFiles = GameFiles(config.path);
		} else {
			Log::error("Unable to load game installation at path %s", config.path.string().c_str());
		}
	}

	// Interactions
	const double dt = 1.0/120;
	double timer = Input::getTime();
	double remainingTime = 0;

	ControllableCamera camera(ControllableCamera::Mode::FPS);
	camera.speed() = 100.0f;
	camera.projection(config.screenResolution[0] / config.screenResolution[1], glm::pi<float>() * 0.4f, 10.f, 10000.0f);
	camera.pose(glm::vec3(0.0f, 0.0f, 100.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	camera.ratio(config.screenResolution[0] / config.screenResolution[1]);

	// Rendering

	std::vector<ProgramInfos> programPool;

	programPool.push_back(loadProgram("texture_passthrough", "debug/texture_debug"));
	Program* textureDebugQuad = programPool.back().program;

	programPool.push_back(loadProgram("texture_passthrough", "postprocess_noise"));
	Program* noiseGrainQuad = programPool.back().program;

	programPool.push_back(loadProgram("texture_passthrough", "postprocess_blur"));
	Program* bloomBlur = programPool.back().program;

	programPool.push_back(loadProgram("texture_passthrough", "texture_passthrough"));
	Program* passthrough = programPool.back().program;

	programPool.push_back(loadProgram("texture_passthrough", "image_passthrough"));
	Program* passthroughImage = programPool.back().program;

	programPool.push_back(loadProgram("debug/object_color", "debug/object_color"));
	Program* coloredDebugDraw = programPool.back().program;

	programPool.push_back(loadProgram("objects/object_instanced_gbuffer", "objects/object_instanced_gbuffer"));
	Program* gbufferInstancedObject = programPool.back().program;

	programPool.push_back(loadProgram("objects/object_instanced_decal", "objects/object_instanced_decal"));
	Program* decalInstancedObject = programPool.back().program;

	programPool.push_back(loadProgram("objects/object_instanced_forward", "objects/object_instanced_forward"));
	Program* forwardInstancedObject = programPool.back().program;

	programPool.push_back(loadProgram("objects/object_instanced_shadow", "objects/object_instanced_shadow"));
	Program* shadowInstancedObject = programPool.back().program;

	programPool.push_back(loadProgram("objects/object_instanced_debug", "objects/object_instanced_debug"));
	Program* debugInstancedObject = programPool.back().program;

	programPool.push_back(loadProgram("objects/object_instanced_selection", "objects/object_instanced_selection"));
	Program* selectionObject = programPool.back().program;

	programPool.push_back(loadProgram("objects/object_billboard", "objects/object_billboard"));
	Program* billboardObject = programPool.back().program;

	programPool.push_back(loadProgram("draw_arguments_all"));
	Program* drawArgsCompute = programPool.back().program;

	programPool.push_back(loadProgram("draw_arguments_transparent"));
	Program* drawArgsTransparentCompute = programPool.back().program;

	programPool.push_back(loadProgram("expand_transparent_instances"));
	Program* expandTransparentCompute = programPool.back().program;

	programPool.push_back(loadProgram("clear_buffer"));
	Program* clearBufferCompute = programPool.back().program;

	programPool.push_back(loadProgram("lights_clustering"));
	Program* clustersCompute = programPool.back().program;

	programPool.push_back(loadProgram("lighting_gbuffer"));
	Program* lightingCompute = programPool.back().program;

	UniformBuffer<FrameData> frameInfos(1, 64, "FrameInfos");
	UniformBuffer<TransparentFrameData> transparentInfos(1, 2, "TransparentInfos");
	UniformBuffer<glm::vec2> blurInfosV(1, 2, "BlurInfosV");
	UniformBuffer<glm::vec2> blurInfosH(1, 2, "BlurInfosH");

	const glm::uvec2 renderRes(config.resolutionRatio * config.screenResolution);

	// Gbuffer
	Texture sceneColor("sceneColor"), sceneNormal("sceneNormal"), sceneDepth("sceneDepth"), sceneHeat("sceneHeat");
	Texture::setupRendertarget(sceneColor, Layout::RGBA8, renderRes[0], renderRes[1]);
	Texture::setupRendertarget(sceneNormal, Layout::RGBA16F, renderRes[0], renderRes[1]);
	Texture::setupRendertarget(sceneDepth, Layout::DEPTH_COMPONENT32F, renderRes[0], renderRes[1]);
	Texture::setupRendertarget(sceneHeat, Layout::R8, renderRes[0], renderRes[1]);

	// Lit result
	Texture sceneLit("sceneLit");
	Texture::setupRendertarget(sceneLit, Layout::RGBA16F, renderRes[0], renderRes[1]);

	Texture sceneFog("sceneFog");
	Texture::setupRendertarget(sceneFog, Layout::RGBA16F, renderRes[0], renderRes[1]);

	// Bloom
	uint bloomWidth = (uint)(renderRes[0])/2u;
	uint bloomHeight = (uint)(renderRes[1])/2u;
	uint bloomBlurSteps = 4u;
	Texture bloom0("bloom0");
	Texture bloom1("bloom1");
	Texture::setupRendertarget(bloom0, Layout::RGBA16F, bloomWidth, bloomHeight);
	Texture::setupRendertarget(bloom1, Layout::RGBA16F, bloomWidth, bloomHeight);

	Texture textureView("textureViewer");
	Texture::setupRendertarget(textureView, Layout::RGBA8, 512, 512);
	GPU::clearTexture(textureView, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

	Texture selectionColor("selection");
	Texture::setupRendertarget(selectionColor, Layout::RG8, renderRes[0], renderRes[1]);

	glm::ivec2 clusterDims(CLUSTER_XY_SIZE, CLUSTER_Z_COUNT);

	Texture lightClusters("lightClusters");
	lightClusters.width = roundUp(renderRes[0], clusterDims.x);
	lightClusters.height = roundUp(renderRes[1], clusterDims.x);
	lightClusters.depth = clusterDims.y;
	lightClusters.shape = TextureShape::D3;
	lightClusters.levels = 1;
	GPU::setupTexture(lightClusters, Layout::RGBA32UI, false);

	Texture fogClusters("fogClusters");
	fogClusters.width = lightClusters.width;
	fogClusters.height = lightClusters.height;
	fogClusters.depth = lightClusters.depth;
	fogClusters.shape = TextureShape::D3;
	fogClusters.levels = 1;
	GPU::setupTexture(fogClusters, Layout::R16UI, false);

	std::unique_ptr<Buffer> drawCommands = nullptr;
	std::unique_ptr<Buffer> drawInstances = nullptr;
	std::unique_ptr<Buffer> transparentDrawInstances = nullptr;
	std::unique_ptr<Buffer> transparentDrawCommands = nullptr;

	Buffer transparentCounter(sizeof(uint32_t), BufferType::STORAGE, "TransparentCounter");


	auto resizeRenderTargets = [&](const glm::vec2& renderRes){
		sceneColor.resize(renderRes);
		sceneNormal.resize(renderRes);
		sceneDepth.resize(renderRes);
		sceneHeat.resize(renderRes);
		sceneLit.resize(renderRes);
		sceneFog.resize(renderRes);
		bloom0.resize(glm::uvec2(renderRes)/2u);
		bloom1.resize(glm::uvec2(renderRes)/2u);
		lightClusters.resize(roundUp(renderRes[0], clusterDims.x), roundUp(renderRes[1], clusterDims.x), clusterDims.y);
		fogClusters.resize(lightClusters.width, lightClusters.height, lightClusters.depth);
		camera.ratio(renderRes[0]/renderRes[1]);
	};


	SelectionState selected;
	DebugVisualisation debug;

	ShadowGeneration shadow;
	Texture::setupRendertarget(shadow.maps, Layout::DEPTH_COMPONENT32F, 512, 512, 1, TextureShape::Array2D, 16);
	shadow.drawArgsCompute = drawArgsCompute;
	shadow.shadowInstancedObject = shadowInstancedObject;

	// Data storage.
	Scene scene;

	// GUi state
	enum class ViewerMode {
		MODEL, AREA, WORLD
	};
	ViewerMode viewMode = ViewerMode::MODEL;
	float zoomPct = 100.f;
	glm::vec2 centerPct(50.f, 50.0f);

	bool showOpaques = true;
	bool showTransparents = true;
	bool showDecals = true;
	bool showBillboards = true;
	bool showParticles = true;
	bool showFog = true;
	uint showPostprocess = 0u;

	EngineTextures textures;
	loadEngineTextures(gameFiles, textures);

	deselect( frameInfos[ 0 ], selected, SelectionFilter::ALL );

	uint64_t frameIndex = 0;
	bool updateInstanceBoundingBox = false;
	bool scrollToItem = false;

	auto uploadScene = [&] ()
	{
		// Allocate commands buffer.
		const size_t meshCount = scene.meshInfos->size();
		const size_t instanceCount = scene.instanceInfos->size();
		drawCommands = std::make_unique<Buffer>( meshCount * sizeof( GPU::DrawCommand ), BufferType::INDIRECT, "DrawCommands" );
		drawInstances = std::make_unique<Buffer>( instanceCount * sizeof( uint ), BufferType::STORAGE, "DrawInstances" );

		const Scene::MeshRange& transparentRange = scene.globalMeshMaterialRanges[Object::Material::TRANSPARENT];
		transparentDrawInstances = std::make_unique<Buffer>( transparentRange.instanceCount * sizeof( TransparentInstanceInfos ), BufferType::STORAGE, "DrawTransparentInstances" );
		transparentDrawCommands = std::make_unique<Buffer>( transparentRange.instanceCount * sizeof( GPU::DrawCommand ), BufferType::INDIRECT, "DrawTransparentCommands" );

		shadow.setup(scene);

		// Center camera
		if( scene.world.cameras().empty() )
		{
			adjustCameraToBoundingBox( camera, scene.computeBoundingBox() );
		}
		else
		{
			// If a scene camera is available, place ourselves right next to it.
			const World::Camera& refCam = scene.world.cameras()[ 0 ];
			const glm::vec3 pos = glm::vec3( refCam.frame[ 3 ] ) - glm::vec3( 0.0f, 50.0f, 0.0f );
			const glm::vec3 up = glm::vec3( 0.0f, 1.0f, 0.0f );
			glm::vec3 front = refCam.frame * glm::vec4( 0.0f, 0.0f, 1.0f, 0.0f );
			glm::vec3 right = glm::normalize( glm::cross( glm::normalize( front ), up ) );
			front = glm::normalize( glm::cross( up, right ) );
			camera.pose( pos, pos + front, up );
		}
		deselect( frameInfos[ 0 ], selected, ( SelectionFilter )( OBJECT | TEXTURE ) );

		debug.buildGizmos(scene);

		Log::info("Loaded world %s with %u meshes, %u materials, %u instances (%u opaque, %u transparent, %u decals), %u lights, %u cameras, %u zones, %u emitters, %u billboards",
				  scene.world.name().c_str(),
				  scene.meshInfos->size(), scene.materialInfos->size(), scene.instanceInfos->size(),
				  scene.globalMeshMaterialRanges[Object::Material::OPAQUE].instanceCount,
				  scene.globalMeshMaterialRanges[Object::Material::TRANSPARENT].instanceCount,
				  scene.globalMeshMaterialRanges[Object::Material::DECAL].instanceCount,
				  scene.lightInfos->size(), scene.world.cameras().size(), scene.world.zones().size(),
				  scene.world.particles().size(), scene.world.billboards().size());
	};

#define FORCE_LOAD_TUTO_ECO
#ifdef FORCE_LOAD_TUTO_ECO
	{
		const fs::path worldpath = gameFiles.worldsPath / "tutoeco.world";
		viewMode = ViewerMode::WORLD;
		scene.load( worldpath, gameFiles );
		uploadScene();
		selected.item = 0;
		for( const auto& world : gameFiles.worldsList ){
			if( world.filename() == worldpath.filename() )
				break;
			++selected.item;
		}
		camera.pose( { 195.044f, 187.823f, -639.285f }, { 194.717f, 187.464f, -640.159f }, { 0.f, 1.f, 0.f } );
		camera.projection( camera.ratio(), 0.785398f, 10.f, 10000.f );
			
	}
	
#endif

	while(window.nextFrame()) {

		// Handle window resize.
		if(Input::manager().resized()) {
			const uint width = uint(Input::manager().size()[0]);
			const uint height = uint(Input::manager().size()[1]);
			config.screenResolution[0] = float(width > 0 ? width : 1);
			config.screenResolution[1] = float(height > 0 ? height : 1);
			// Resources are not resized here, but when the containing ImGui window resizes.

		}

		if(Input::manager().triggered(Input::Key::P)) {
			// Load something.
			for(ProgramInfos& infos : programPool){
				reload(infos);
			}
		}

		// Compute new time.
		double currentTime = Input::getTime();
		double frameTime   = currentTime - timer;
		timer			   = currentTime;

		// Update camera.
		camera.update();

		// First avoid super high frametime by clamping.
		const double frameTimeUpdate = std::min(frameTime, 0.2);
		// Accumulate new frame time.
		remainingTime += frameTimeUpdate;
		// Instead of bounding at dt, we lower our requirement (1 order of magnitude).

		while(remainingTime > 0.2 *  dt) {
			const double deltaTime = std::min(remainingTime, dt);
			// Update physics.
			camera.physics(deltaTime);
			remainingTime -= deltaTime;
		}

		ImGui::DockSpaceOverViewport(nullptr, ImGuiDockNodeFlags_PassthruCentralNode);

		const ImGuiTableFlags tableFlags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV | ImGuiTableFlags_Resizable;
		const ImGuiSelectableFlags selectableTableFlags = ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap;

		if(ImGui::BeginMainMenuBar()){

			if(ImGui::BeginMenu("File")){

				if(ImGui::MenuItem("Load...")){
					fs::path newInstallPath;
					if(Window::showDirectoryPicker(fs::path(""), newInstallPath)){
						gameFiles = GameFiles( newInstallPath);
						loadEngineTextures(gameFiles, textures);
						scene = Scene();
						deselect(frameInfos[0], selected, SelectionFilter::ALL);
					}
				}
				ImGui::Separator();
				if(ImGui::MenuItem("Close")){
					window.perform(Window::Action::Quit);
				}
				ImGui::EndMenu();
			}

			if(ImGui::BeginMenu("Options")){

				if(ImGui::MenuItem("Fullscreen", nullptr, config.fullscreen)){
					window.perform(Window::Action::Fullscreen);
				}
				if(ImGui::MenuItem("Vsync", nullptr, config.vsync)){
					window.perform(Window::Action::Vsync);
				}

				ImGui::EndMenu();
			}
			
			ImGui::EndMainMenuBar();
		}

		// Begin GUI setup.
		if(ImGui::Begin("Files")){

			if(ImGui::BeginTabBar("#FilesTabBar" )){

				struct TabSettings {
					const char* title;
					const char* names;
					const std::vector<fs::path>* files;
					ViewerMode mode;
					ControllableCamera::Mode camera;
					void (Scene::*load) (const fs::path& filePath, const GameFiles& );
				};

				const std::vector<TabSettings> tabSettings = {
					{ "Worlds", "worlds", &gameFiles.worldsList, ViewerMode::WORLD, ControllableCamera::Mode::FPS, &Scene::load},
					{ "Areas", "areas", &gameFiles.areasList, ViewerMode::AREA, ControllableCamera::Mode::TurnTable, &Scene::loadFile},
					{ "Models", "models", &gameFiles.modelsList, ViewerMode::MODEL, ControllableCamera::Mode::TurnTable, &Scene::loadFile},
				};

				for(const TabSettings& tab : tabSettings){

					if(ImGui::BeginTabItem(tab.title)){
						
						if(viewMode != tab.mode){
							deselect(frameInfos[0], selected, SelectionFilter::ALL);
							viewMode = tab.mode;
							camera.mode(tab.camera);
						}
						const unsigned int itemsCount = (uint)(*tab.files).size();
						ImGui::Text("Found %u %s", itemsCount, tab.names);

						static ImGuiTextFilter itemFilter;
						itemFilter.Draw();

						if(ImGui::BeginTable("Table", 2, tableFlags)){
							// Header
							ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
							ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None);
							ImGui::TableSetupColumn("Directory", ImGuiTableColumnFlags_None);
							ImGui::TableHeadersRow();

							for(int row = 0; row < (int)itemsCount; row++){

								const fs::path& itemPath = (*tab.files)[row];
								const std::string itemName = itemPath.filename().string();
								if(!itemFilter.PassFilter(itemName.c_str())){
									continue;
								}

								ImGui::PushID(row);
								ImGui::TableNextColumn();

								// Log two levels of hierarchy.
								const fs::path& parentPath = itemPath.parent_path();
								std::string itemParent = parentPath.parent_path().filename().string();
								itemParent += "/" + parentPath.filename().string();

								if(ImGui::Selectable(itemName.c_str(), selected.item == row, selectableTableFlags)){
									if(selected.item != row){
										selected.item = row;
										(scene.*tab.load)(itemPath, gameFiles);
										uploadScene();
									}
								}
								ImGui::TableNextColumn();
								ImGui::Text("%s", itemParent.c_str());

								ImGui::PopID();
							}
							ImGui::EndTable();
						}
						ImGui::EndTabItem();
					}
				}
				ImGui::EndTabBar();
			}
		}
		ImGui::End();

		if(ImGui::Begin("Inspector")){
			if(selected.item >= 0){

				ImGui::Text("%s: %lu vertices, %lu faces",
							scene.world.name().c_str(),
							scene.globalMesh.positions.size(),
							scene.globalMesh.indices.size() / 3 );
				ImGui::SameLine();
				if(ImGui::SmallButton("Deselect")){
					deselect(frameInfos[0], selected, OBJECT);
					debug.boundingBox.clean();
				}

				ImVec2 winSize = ImGui::GetContentRegionAvail();
				winSize.y *= 0.9f;
				winSize.y -= ImGui::GetTextLineHeightWithSpacing(); // For search field.


				const std::string meshesTabName = "Meshes (" + std::to_string(scene.meshDebugInfos.size()) + ")###MeshesTab";
				const std::string texturesTabName = "Textures (" + std::to_string(scene.textureDebugInfos.size()) + ")###TexturesTab";
				const std::string lightsTabName = "Lights (" + std::to_string(scene.world.lights().size()) + ")###LightsTab";
				const std::string camerasTabName = "Cameras (" + std::to_string(scene.world.cameras().size()) + ")###CamerasTab";
				const std::string billboardsTabName = "Billboards (" + std::to_string(scene.world.billboards().size()) + ")###BillboardsTab";
				const std::string particlesTabName = "Particles (" + std::to_string(scene.world.particles().size()) + ")###ParticlesTab";

				if(ImGui::BeginTabBar("InspectorTabbar")){

					if(ImGui::BeginTabItem(meshesTabName.c_str())){

						static ImGuiTextFilter meshFilter;
						meshFilter.Draw();

						if(ImGui::BeginTable("#MeshList", 3, tableFlags, winSize)){
							// Header
							ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
							ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None);
							ImGui::TableSetupColumn("Triangles", ImGuiTableColumnFlags_None);
							ImGui::TableSetupColumn("Material", ImGuiTableColumnFlags_None);
							ImGui::TableHeadersRow();

							const int rowCount = (int)scene.meshDebugInfos.size();
							for(int row = 0; row < rowCount; ++row){

								const Scene::MeshInfos& meshInfos = (*scene.meshInfos)[row];
								const Scene::MeshCPUInfos& meshDebugInfos = scene.meshDebugInfos[row];
								if(!meshFilter.PassFilter(meshDebugInfos.name.c_str())){
									continue;
								}

								ImGui::TableNextColumn();
								ImGui::PushID(row);
								if(ImGui::Selectable(meshDebugInfos.name.c_str(), selected.mesh == row, selectableTableFlags)){
									if(selected.mesh != row){
										selected.mesh = row;
										frameInfos[0].selectedMesh = row;

										std::vector<BoundingBox> bboxes(meshInfos.instanceCount);
										for(uint iid = 0u; iid < meshInfos.instanceCount; ++iid){
											const Scene::InstanceCPUInfos& debugInfos = scene.instanceDebugInfos[meshInfos.firstInstanceIndex + iid];
											bboxes[iid] = debugInfos.bbox;
										}
										debug.buildBoundingBoxes(bboxes);
										deselect(frameInfos[0], selected, INSTANCE);
										adjustCameraToBoundingBox(camera, debug.boundingBox.computeBoundingBox());
									}
								}
								if(scrollToItem && (selected.mesh == row)){
									ImGui::ScrollToItem();
								}

								ImGui::TableNextColumn();
								ImGui::Text("%u", meshInfos.indexCount / 3u);
								ImGui::TableNextColumn();
								ImGui::Text("%u", meshInfos.materialIndex);

								ImGui::PopID();
							}

							ImGui::EndTable();
						}
						ImGui::EndTabItem();
					}

					if(ImGui::BeginTabItem(texturesTabName.c_str())){

						static ImGuiTextFilter textureFilter;
						textureFilter.Draw();

						if(ImGui::BeginTable("#TextureList", 3, tableFlags, winSize)){
							// Header
							ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
							ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None);
							ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_None);
							ImGui::TableSetupColumn("Format", ImGuiTableColumnFlags_None);
							ImGui::TableHeadersRow();

							int rowCount = (int)scene.textureDebugInfos.size();
							for(int row = 0; row < rowCount; ++row){

								const Scene::TextureCPUInfos& debugInfos = scene.textureDebugInfos[row];
								const Texture& arrayTex = scene.textures[debugInfos.data.index];
								if(!textureFilter.PassFilter(debugInfos.name.c_str())){
									continue;
								}

								ImGui::TableNextColumn();
								ImGui::PushID(row);

								if(ImGui::Selectable(debugInfos.name.c_str(), selected.texture == row, selectableTableFlags)){
									selected.texture = row;
									frameInfos[0].selectedTextureArray = debugInfos.data.index;
									frameInfos[0].selectedTextureLayer = debugInfos.data.layer;
									zoomPct = 100.f;
									centerPct = glm::vec2(50.f, 50.0f);
									textureView.resize(arrayTex.width, arrayTex.height);
								}

								ImGui::TableNextColumn();
								ImGui::Text("%ux%u", arrayTex.width, arrayTex.height);
								ImGui::TableNextColumn();
								static const std::unordered_map<Image::Compression, const char*> compressionNames = {
									{ Image::Compression::NONE, "BGRA8" },
									{ Image::Compression::BC1, "BC1/DXT1" },
									{ Image::Compression::BC2, "BC2/DXT3" },
									{ Image::Compression::BC3, "BC3/DXT5" },
								};
								ImGui::Text("%s", compressionNames.at(arrayTex.images[0].compressedFormat));
								ImGui::PopID();
							}
							ImGui::EndTable();
						}
						ImGui::EndTabItem();
					}

					if(ImGui::BeginTabItem(lightsTabName.c_str())){

						static ImGuiTextFilter lightFilter;
						lightFilter.Draw();

						if(ImGui::BeginTable("#LightsList", 3, tableFlags, winSize)){
							// Header
							ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
							ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None);
							ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_None);
							ImGui::TableSetupColumn("Color", ImGuiTableColumnFlags_None);
							ImGui::TableHeadersRow();

							const int rowCount = (int)scene.world.lights().size();
							for(int row = 0; row < rowCount; ++row){

								const World::Light& light = scene.world.lights()[row];
								if(!lightFilter.PassFilter(light.name.c_str())){
									continue;
								}
								const Scene::LightInfos& infos = ( *scene.lightInfos )[ row ];

								ImGui::TableNextColumn();
								ImGui::PushID(row);

								if(ImGui::Selectable(light.name.c_str())){
									const glm::vec3 camCenter = glm::vec3(light.frame[3]);
									const glm::vec3 camPos = camCenter - glm::vec3(light.radius.x, 0.0f, 0.0f);
									camera.pose(camPos, camCenter, glm::vec3(0.0f, 1.0f, 0.0f));
									camera.fov(45.0f * glm::pi<float>() / 180.0f);
								}
								ImGui::TableNextColumn();
								static const std::unordered_map<World::Light::Type, const char*> lightTypeNames = {
									{ World::Light::POINT, "Point" },
									{ World::Light::SPOT, "Spot" },
									{ World::Light::DIRECTIONAL, "Directional" },
								};
								ImGui::Text("%s (%u)", lightTypeNames.at(light.type), infos.shadow);
								ImGui::TableNextColumn();
								glm::vec3 tmpColor = light.color;
								ImGui::ColorEdit3("##LightColor", &tmpColor[0], ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoInputs);
								ImGui::PopID();
							}
							ImGui::EndTable();
						}
						ImGui::EndTabItem();
					}

					if(ImGui::BeginTabItem(camerasTabName.c_str())){

						static ImGuiTextFilter cameraFilter;
						cameraFilter.Draw();

						if(ImGui::BeginTable("#CamerasList", 1, tableFlags, winSize)){
							// Header
							ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
							ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None);
							ImGui::TableHeadersRow();

							const int rowCount = (int)scene.world.cameras().size();
							for(int row = 0; row < rowCount; ++row){

								const World::Camera& cam = scene.world.cameras()[row];
								if(!cameraFilter.PassFilter(cam.name.c_str())){
									continue;
								}

								ImGui::TableNextColumn();
								ImGui::PushID(row);

								if(ImGui::Selectable(cam.name.c_str())){
									const glm::vec4 camPos = cam.frame * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
									const glm::vec4 camCenter = cam.frame * glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
									const glm::vec4 camAbove = cam.frame * glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
									camera.pose(glm::vec3(camPos), glm::vec3(camCenter), glm::normalize(glm::vec3(camAbove - camPos)));
									camera.fov(cam.fov);
								}
								ImGui::PopID();
							}
							ImGui::EndTable();
						}
						ImGui::EndTabItem();
					}

					if(ImGui::BeginTabItem(billboardsTabName.c_str())){

						static ImGuiTextFilter billboardFilter;
						billboardFilter.Draw();

						if(ImGui::BeginTable("#BillboardsList", 4, tableFlags, winSize)){
							// Header
							ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
							ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None);
							ImGui::TableSetupColumn("Alignment", ImGuiTableColumnFlags_None);
							ImGui::TableSetupColumn("Blending", ImGuiTableColumnFlags_None);
							ImGui::TableSetupColumn("Color", ImGuiTableColumnFlags_None);
							ImGui::TableHeadersRow();

							const int rowCount = (int)scene.world.billboards().size();
							for(int row = 0; row < rowCount; ++row){

								const World::Billboard& billboard = scene.world.billboards()[row];
								if(!billboardFilter.PassFilter(billboard.name.c_str())){
									continue;
								}
								ImGui::TableNextColumn();
								ImGui::PushID(row);

								if(ImGui::Selectable(billboard.name.c_str())){
									const glm::vec3 camCenter = glm::vec3(billboard.frame[3]);
									const float radius = glm::max(glm::length(billboard.size), 10.0f);
									const glm::vec3 camPos = camCenter + glm::vec3(billboard.frame * glm::vec4(0.f, 0.0f, radius, 0.f));
									camera.pose(camPos, camCenter, glm::vec3(0.0f, 1.0f, 0.0f));
									camera.fov(45.0f * glm::pi<float>() / 180.0f);
								}
								
								ImGui::TableNextColumn();
								ImGui::Text("%s", alignNames.at(billboard.alignment));
								ImGui::TableNextColumn();
								ImGui::Text("%s", blendNames.at(billboard.blending));
								ImGui::TableNextColumn();
								glm::vec3 tmpColor = billboard.color;
								ImGui::ColorEdit3("##BillboardColor", &tmpColor[0], ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoInputs);
								ImGui::PopID();
							}
							ImGui::EndTable();
						}
						ImGui::EndTabItem();
					}

					if(ImGui::BeginTabItem(particlesTabName.c_str())){

						static ImGuiTextFilter particleFilter;
						particleFilter.Draw();

						if(ImGui::BeginTable("#ParticlesList", 5, tableFlags, winSize)){
							// Header
							ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
							ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None);
							ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_None);
							ImGui::TableSetupColumn("Alignment", ImGuiTableColumnFlags_None);
							ImGui::TableSetupColumn("Blending", ImGuiTableColumnFlags_None);
							ImGui::TableSetupColumn("Color", ImGuiTableColumnFlags_None);
							ImGui::TableHeadersRow();

							const int rowCount = (int)scene.world.particles().size();
							for(int row = 0; row < rowCount; ++row){

								const World::Emitter& emitter = scene.world.particles()[row];
								if(!particleFilter.PassFilter(emitter.name.c_str())){
									continue;
								}
								ImGui::TableNextColumn();
								ImGui::PushID(row);

								if(ImGui::Selectable(emitter.name.c_str())){
									const glm::vec3 camCenter = glm::vec3(emitter.frame[3]);
									const float radius = glm::max(glm::length(emitter.bbox.getSize()), 10.0f);
									const glm::vec3 camPos = camCenter - glm::vec3(radius, 0.0f, 0.0f);
									camera.pose(camPos, camCenter, glm::vec3(0.0f, 1.0f, 0.0f));
									camera.fov(45.0f * glm::pi<float>() / 180.0f);
								}
								ImGui::TableNextColumn();
								ImGui::Text("%u", emitter.type);
								ImGui::TableNextColumn();
								ImGui::Text("%s", alignNames.at(emitter.alignment));
								ImGui::TableNextColumn();
								ImGui::Text("%s", blendNames.at(emitter.blending));
								ImGui::TableNextColumn();
								glm::vec3 tmpColor = 0.5f*(emitter.colorMin + emitter.colorMax);
								ImGui::ColorEdit3("##EmitterColor", &tmpColor[0], ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoInputs);
								ImGui::PopID();
							}
							ImGui::EndTable();
						}
						ImGui::EndTabItem();
					}

					ImGui::EndTabBar();
				}
			}
		}
		ImGui::End();

		const std::string instancesTabName = "Instances (" + std::to_string(scene.instanceDebugInfos.size()) + ")###Instances";
		if(ImGui::Begin(instancesTabName.c_str())){
			if(selected.item >= 0){
				ImVec2 winSize = ImGui::GetContentRegionAvail();
				if(ImGui::BeginTable("#InstanceList", 1, tableFlags, winSize)){
					// Header
					ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
					ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None);
					ImGui::TableHeadersRow();

					// Two modes, depending on if a mesh is selected above or not.
					int startRow = 0;
					int rowCount = (int)scene.instanceDebugInfos.size();
					if(selected.mesh >= 0){
					   startRow = (int)(*scene.meshInfos)[selected.mesh].firstInstanceIndex;
					   rowCount = (int)(*scene.meshInfos)[selected.mesh].instanceCount;
					}

					ImGuiListClipper clipper;
					clipper.Begin(rowCount);

					while(clipper.Step()){
						for(int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row){
							ImGui::TableNextColumn();
							ImGui::PushID(row);

							const int fullInstanceIndex = startRow + row;
							const Scene::InstanceCPUInfos& debugInfos = scene.instanceDebugInfos[fullInstanceIndex];

							if(ImGui::Selectable(debugInfos.name.c_str(), selected.instance == fullInstanceIndex, selectableTableFlags)){
								if(selected.instance != fullInstanceIndex){
									selected.instance = fullInstanceIndex;
									updateInstanceBoundingBox = true;
									const BoundingBox& bbox = scene.instanceDebugInfos[selected.instance].bbox;
									adjustCameraToBoundingBox(camera, bbox);
								}
							}

							if(scrollToItem && (selected.instance == fullInstanceIndex)){
								ImGui::ScrollToItem();
							}
							ImGui::PopID();
						}
					}
				    ImGui::EndTable();
			   }
			}
		}
		ImGui::End();

		if(ImGui::Begin("Texture")){
			ImGui::PushItemWidth(120);
			ImGui::SliderFloat("Zoom (%)", &zoomPct, 0.0f, 1000.0f);
			ImGui::SameLine();
			ImGui::SliderFloat("X (%)", &centerPct[0], 0.0f, 100.0f);
			ImGui::SameLine();
			ImGui::SliderFloat("Y (%)", &centerPct[1], 0.0f, 100.0f);
			ImGui::PopItemWidth();

			// Adjust the texture display to the window size.
			ImVec2 winSize = ImGui::GetContentRegionAvail();
			winSize.x = std::max(winSize.x, 2.f);
			winSize.y = std::max(winSize.y, 2.f);

			const glm::vec2 texCenter = centerPct / 100.f;
			const glm::vec2 texScale(100.0f / std::max(zoomPct, 1.f));
			const glm::vec2 miniUV = texCenter - texScale * 0.5f;
			const glm::vec2 maxiUV = texCenter + texScale * 0.5f;

			ImGui::ImageButton(textureView, 0, 0, ImVec2(winSize.x, winSize.y), miniUV, maxiUV, 0, ImVec4(1.0f, 0.0f, 1.0f, 1.0f));

		}
		ImGui::End();

		if(ImGui::Begin("Settings")) {
			int ratioPercentage = int(std::round(config.resolutionRatio * 100.0f));

			const uint w = sceneLit.width;
			const uint h = sceneLit.height;
			ImGui::Text("Rendering at %ux%upx", w, h);
			if(ImGui::InputInt("Rendering ratio %", &ratioPercentage, 10, 25)) {
				ratioPercentage = glm::clamp(ratioPercentage, 10, 200);

				const float newRatio = (float)ratioPercentage / 100.0f;

				glm::vec2 renderRes = (glm::vec2(w, h) / config.resolutionRatio) * newRatio;

				config.resolutionRatio = newRatio;
				resizeRenderTargets(renderRes);

			}
			if(ImGui::SliderInt2("Cluster params", &clusterDims[0], 2, 128)){
				lightClusters.resize(roundUp(sceneLit.width, clusterDims.x), roundUp(sceneLit.height, clusterDims.x), clusterDims.y);
				fogClusters.resize(lightClusters.width, lightClusters.height, lightClusters.depth);
			}

			ImGui::Text("Shading"); ImGui::SameLine();
			ImGui::RadioButton("None", &debug.shadingMode, MODE_SHADING_NONE); ImGui::SameLine();
			ImGui::RadioButton("Light", &debug.shadingMode, MODE_SHADING_LIGHT);

			ImGui::Text("Albedo"); ImGui::SameLine();
			ImGui::RadioButton("Color", &debug.albedoMode, MODE_ALBEDO_UNIFORM); ImGui::SameLine();
			ImGui::RadioButton("Normal", &debug.albedoMode, MODE_ALBEDO_NORMAL); ImGui::SameLine();
			ImGui::RadioButton("Texture", &debug.albedoMode, MODE_ALBEDO_TEXTURE);

			if(ImGui::ArrowButton("VisibilityArrow", ImGuiDir_Down)){
				ImGui::OpenPopup("visibilityPopup");
			}
			if(ImGui::BeginPopup("visibilityPopup")){
				ImGui::Checkbox("Opaques",  &showOpaques);
				ImGui::Checkbox("Decals", &showDecals);
				ImGui::Checkbox("Billboards", &showBillboards);
				ImGui::Checkbox("Particles", &showParticles);
				ImGui::Checkbox("Transparents", &showTransparents);
				ImGui::Checkbox("Fog", &showFog);
				ImGui::EndPopup();
			}
			ImGui::SameLine();
			ImGui::Text("Visibility");

			ImGui::SameLine();
			if(ImGui::ArrowButton("PostprocessArrow", ImGuiDir_Down)){
				ImGui::OpenPopup("postprocessPopup");
			}
			if(ImGui::BeginPopup("postprocessPopup")){
				ImGui::CheckboxFlags("Bloom", &showPostprocess, MODE_POSTPROCESS_BLOOM);
				ImGui::CheckboxFlags("Night vision", &showPostprocess, MODE_POSTPROCESS_NIGHT);
				ImGui::CheckboxFlags("B&W", &showPostprocess, MODE_POSTPROCESS_BANDW);
				ImGui::CheckboxFlags("Grain", &showPostprocess, MODE_POSTPROCESS_GRAIN);
				ImGui::CheckboxFlags("Jitter", &showPostprocess, MODE_POSTPROCESS_JITTER);
				ImGui::CheckboxFlags("Heat map", &showPostprocess, MODE_POSTPROCESS_HEAT);
				ImGui::CheckboxFlags("Underwater", &showPostprocess, MODE_POSTPROCESS_UNDERWATER);
				ImGui::EndPopup();
			}
			ImGui::SameLine();
			ImGui::Text("Postprocess");

			ImGui::SameLine();
			if( ImGui::ArrowButton( "DebugArrow", ImGuiDir_Down ) )
			{
				ImGui::OpenPopup( "DebugPopup" );
			}
			if( ImGui::BeginPopup( "DebugPopup" ) )
			{
				ImGui::Checkbox( "Objects", &debug.showWireframe );
				ImGui::Checkbox( "Lights", &debug.showLights );
				ImGui::Checkbox( "Zones", &debug.showZones );
				ImGui::Checkbox( "FXs", &debug.showFxs );
				ImGui::EndPopup();
			}
			ImGui::SameLine();
			ImGui::Text( "Debug" );

			ImGui::Checkbox("Freeze culling", &debug.freezeCulling);
			ImGui::SameLine();
			if(ImGui::Button("Reset camera")){
				camera.reset();
				adjustCameraToBoundingBox(camera, scene.computeBoundingBox());
			}
			ImGui::Separator();
			camera.interface();
		}
		ImGui::End();

		glm::vec4 mainViewport(0.0f);

		// Funny styling to mimick the game windows.
		const ImVec4 bgColor(0.9f, 0.9f, 0.9f, 0.5f);
		const ImVec4 fgColor(0.9f, 0.9f, 0.9f, 1.0f);

		ImGui::PushStyleColor(ImGuiCol_WindowBg, bgColor);
		ImGui::PushStyleColor(ImGuiCol_TitleBg, bgColor);
		ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, bgColor);
		ImGui::PushStyleColor(ImGuiCol_TitleBgActive, bgColor);
		ImGui::PushStyleColor(ImGuiCol_Border, fgColor);
		ImGui::PushStyleColor(ImGuiCol_Text, fgColor);

		if(ImGui::Begin("Main view", nullptr)){
			// Adjust the texture display to the window size.
			ImVec2 winSize = ImGui::GetContentRegionAvail();
			winSize.x = std::max(winSize.x, 2.f);
			winSize.y = std::max(winSize.y, 2.f);
			mainViewport.x = ImGui::GetWindowPos().x + ImGui::GetCursorPos().x;
			mainViewport.y = ImGui::GetWindowPos().y + ImGui::GetCursorPos().y;
			mainViewport.z = winSize.x;
			mainViewport.w = winSize.y;

			ImGui::ImageButton(sceneFog, 0,0, ImVec2(winSize.x, winSize.y), ImVec2(0.0,0.0), ImVec2(1.0,1.0), 0);
			if(ImGui::IsItemHovered()) {
				ImGui::SetNextFrameWantCaptureMouse(false);
				ImGui::SetNextFrameWantCaptureKeyboard(false);
			}

			// If the aspect ratio changed, trigger a resize.
			const float ratioCurr = float(sceneLit.width) / float(sceneLit.height);
			const float ratioWin = winSize.x / winSize.y;
			// \todo Derive a more robust threshold.
			if(std::abs(ratioWin - ratioCurr) > 0.01f){
				const glm::vec2 renderRes = config.resolutionRatio * glm::vec2(winSize.x, winSize.y);
				resizeRenderTargets(renderRes);
			}
		}
		ImGui::End();
		ImGui::PopStyleColor(6);

		if(shadow.rendering ){
			if( ImGui::Begin( "Work in progress", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse ) ) {
				ImGui::Text( "Generating shadows..." );
				std::string currProg = std::to_string( shadow.currentMapLayer + 1 ) + "/" + std::to_string( shadow.maps.depth );
				ImGui::ProgressBar( ( float )( shadow.currentMapLayer + 1 ) / ( float )shadow.maps.depth, ImVec2( -1.0f, 0.0f ), currProg.c_str() );
			}
			ImGui::End();
		}
		
#ifdef DEBUG_UI
		if(ImGui::Begin("Debug view", nullptr)){

			Texture& tex = sceneHeat;
			ImGui::PushItemWidth(220);
			if(ImGui::SliderInt("Layer", &debug.textureLayerIndex, 0, tex.depth-1)){
				debug.textureLayerIndex = glm::clamp(debug.textureLayerIndex, 0, (int)tex.depth-1);
			}
			ImGui::SameLine();
			if(ImGui::SliderInt("Mip", &debug.textureMipIndex, 0, tex.levels-1)){
				debug.textureMipIndex = glm::clamp(debug.textureMipIndex, 0, (int)tex.levels-1);
			}
			ImGui::PopItemWidth();
			// Adjust the texture display to the window size.
			ImVec2 winSize = ImGui::GetContentRegionAvail();
			winSize.x = std::max(winSize.x, 2.f);
			winSize.y = std::max(winSize.y, 2.f);
			ImGui::ImageButton(tex, debug.textureMipIndex, debug.textureLayerIndex, ImVec2(winSize.x, winSize.y), ImVec2(0.0,0.0), ImVec2(1.0,1.0), 0);
		}
		ImGui::End();
#endif
		/// Rendering

		// Fill frame infos
		{
			// Camera.
			const glm::mat4 vp = camera.projection() * camera.view();
			frameInfos[0].v = camera.view();
			frameInfos[0].p = camera.projection();
			frameInfos[0].vp = vp;
			// Only update the culling VP if needed.
			if(!debug.freezeCulling){
				frameInfos[0].vpCulling = vp;
			}
			frameInfos[0].iv = glm::inverse(frameInfos[0].v);
			frameInfos[0].ip = glm::inverse(frameInfos[0].p);
			frameInfos[0].nvp = glm::transpose(glm::inverse(frameInfos[0].vp));
			frameInfos[0].resolution = glm::vec4(sceneLit.width, sceneLit.height, 0u, 0u);

			frameInfos[0].showFog = showFog ? 1u : 0u;

			frameInfos[0].color = glm::vec4(1.0f);
			frameInfos[0].camPos = glm::vec4(camera.position(), 1.0f);
			const glm::vec2 nearFar = camera.clippingPlanes();
			frameInfos[0].camPlanes = glm::vec4(nearFar.x, nearFar.y / nearFar.x,
												(nearFar.y - nearFar.x)/(nearFar.x*nearFar.y),
												1.0f/nearFar.y );

			frameInfos[0].albedoMode = debug.albedoMode;
			frameInfos[0].shadingMode = debug.shadingMode;
			frameInfos[0].postprocessMode = showPostprocess;

			frameInfos[0].randomX = glm::linearRand( 0.f, 1.0f );
			frameInfos[0].randomY = glm::linearRand( 0.f, 1.0f );
			frameInfos[0].randomZ = glm::linearRand( 0.f, 1.0f );
			frameInfos[0].randomW = glm::linearRand( 0.f, 1.0f );

			frameInfos[0].lightsCount = uint(scene.world.lights().size());
			frameInfos[0].zonesCount = uint(scene.world.zones().size());
			frameInfos[0].meshCount = uint(scene.meshInfos->size());
			frameInfos[0].clustersSize = glm::uvec4(lightClusters.width, lightClusters.height, lightClusters.depth, clusterDims.x);
			const float logRatio = float(clusterDims.y) / std::log(nearFar.y / nearFar.x);
			frameInfos[0].clustersParams = glm::vec4(logRatio, std::log(nearFar.x) * logRatio, 0.0f, 0.0f);
			frameInfos[0].frameIndex = (uint)(frameIndex % UINT32_MAX);

			frameInfos.upload();

			// Scale calibrated on existing frame.
			const float scaling = 1.8f * sceneLit.width / 720.0f;
			blurInfosH[0] = scaling * glm::vec2(1.0f/(float)bloom0.width, 0.0f);
			blurInfosV[0] = scaling * glm::vec2(0.0f, 1.0f/(float)bloom0.height);

			blurInfosH.upload();
			blurInfosV.upload();
		}

		if(selected.item >= 0){

			// Bruteforce shadow map once per frame.
			shadow.renderMapIfNeeded(scene );

			// Culling and clustering.
			{
				// Populate drawCommands and drawInstances by using a compute shader that performs culling.
				drawArgsCompute->use();
				drawArgsCompute->buffer(frameInfos, 0);
				drawArgsCompute->buffer(*scene.meshInfos, 1);
				drawArgsCompute->buffer(*scene.instanceInfos, 2);
				drawArgsCompute->buffer(*drawCommands, 3);
				drawArgsCompute->buffer(*drawInstances, 4);
				GPU::dispatch((uint)scene.meshInfos->size(), 1, 1);

				// For transparent objects, we'll have to generate a draw call per instance.
				// TODO: also sort them back to front.
				if(showTransparents){

					const auto& transparentRange = scene.globalMeshMaterialRanges[Object::Material::TRANSPARENT];
					transparentInfos[0].firstMesh = transparentRange.firstIndex;
					transparentInfos[0].meshCount = transparentRange.count;
					transparentInfos[0].instanceCount = transparentRange.instanceCount;
					transparentInfos.upload();

					// Better safe than sorry for now.
					clearBufferCompute->use();
					clearBufferCompute->buffer(transparentCounter, 1);
					GPU::dispatch(1, 1, 1);

					expandTransparentCompute->use();
					expandTransparentCompute->buffer(frameInfos, 0);
					expandTransparentCompute->buffer(transparentInfos, 1);
					expandTransparentCompute->buffer(*scene.meshInfos, 2);
					expandTransparentCompute->buffer(*scene.instanceInfos, 3);
					expandTransparentCompute->buffer(*drawCommands, 4);
					expandTransparentCompute->buffer(*drawInstances, 5);
					expandTransparentCompute->buffer(*transparentDrawInstances, 6);
					expandTransparentCompute->buffer(transparentCounter, 7);
					GPU::dispatch(transparentRange.count, 1, 1);

					// TODO: radix sort
					
					// Convert this list of instances to as many draw calls
					drawArgsTransparentCompute->use();
					drawArgsTransparentCompute->buffer(frameInfos, 0);
					drawArgsTransparentCompute->buffer(*scene.meshInfos, 1);
					drawArgsTransparentCompute->buffer(*transparentDrawInstances, 2);
					drawArgsTransparentCompute->buffer(*transparentDrawCommands, 3);
					drawArgsTransparentCompute->buffer(transparentCounter, 4);
					GPU::dispatch(transparentRange.instanceCount, 1, 1);
				}


				clustersCompute->use();
				clustersCompute->buffer(frameInfos, 0);
				clustersCompute->buffer(*scene.lightInfos, 1);
				clustersCompute->buffer(*scene.zoneInfos, 2);
				clustersCompute->texture(lightClusters, 0);
				clustersCompute->texture(fogClusters, 1);
				// We need one thread per cluster cell.
				GPU::dispatch( lightClusters.width, lightClusters.height, lightClusters.depth );
			}

			// Use alpha to skip lighting.
			const glm::vec4 clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
			GPU::bind(sceneColor, sceneNormal, sceneHeat, sceneDepth, clearColor, 0.0f, LoadOperation::DONTCARE);
			GPU::setViewport(sceneColor);

			if(showOpaques){
				GPU::setPolygonState(PolygonMode::FILL);
				GPU::setCullState(true, Faces::BACK );
				GPU::setDepthState(true, TestFunction::GEQUAL, true);
				GPU::setBlendState(false);
				GPU::setColorState(true, true, true, true);

				gbufferInstancedObject->use();
				gbufferInstancedObject->buffer(frameInfos, 0);
				gbufferInstancedObject->buffer(*scene.meshInfos, 1);
				gbufferInstancedObject->buffer(*scene.instanceInfos, 2);
				gbufferInstancedObject->buffer(*scene.materialInfos, 3);
				gbufferInstancedObject->buffer(*drawInstances, 4);

				const auto& range = scene.globalMeshMaterialRanges[Object::Material::OPAQUE];
				GPU::drawIndirectMesh(scene.globalMesh, *drawCommands, range.firstIndex, range.count);
			}

			// Lighting
			{
				lightingCompute->use();
				lightingCompute->buffer(frameInfos, 0);
				lightingCompute->buffer(*scene.lightInfos, 1);
				lightingCompute->buffer(*scene.materialInfos, 2);
				lightingCompute->buffer(*scene.zoneInfos, 3);

				lightingCompute->texture(sceneColor, 0);
				lightingCompute->texture(sceneNormal, 1);
				lightingCompute->texture(sceneDepth, 2);
				lightingCompute->texture(sceneLit, 3);
				lightingCompute->texture(sceneFog, 4);
				lightingCompute->texture(lightClusters, 5);
				lightingCompute->texture(shadow.maps, 6);
				lightingCompute->texture(fogClusters, 7);
				lightingCompute->texture(textures.fogXY, 8);
				lightingCompute->texture(textures.fogZ, 9);

				GPU::dispatch( sceneLit.width, sceneLit.height, 1u);
			}

			// Render unlit decals on top with specific blending mode, using src * dst
			if(showDecals){
				GPU::bind(sceneLit, sceneDepth, LoadOperation::LOAD, LoadOperation::LOAD, LoadOperation::DONTCARE);
				GPU::setViewport(sceneLit);

				GPU::setPolygonState(PolygonMode::FILL);
				GPU::setCullState(true, Faces::BACK );
				GPU::setDepthState(true, TestFunction::GEQUAL, false);
				GPU::setBlendState(true, BlendEquation::ADD, BlendFunction::DST_COLOR, BlendFunction::ZERO);

				decalInstancedObject->use();
				decalInstancedObject->buffer(frameInfos, 0);
				decalInstancedObject->buffer(*scene.meshInfos, 1);
				decalInstancedObject->buffer(*scene.instanceInfos, 2);
				decalInstancedObject->buffer(*scene.materialInfos, 3);
				decalInstancedObject->buffer(*drawInstances, 4);

				const auto& range = scene.globalMeshMaterialRanges[Object::Material::DECAL];
				GPU::drawIndirectMesh(scene.globalMesh, *drawCommands, range.firstIndex, range.count);

			}

			// First billboard and particle passes
			if(showBillboards || showParticles){
				GPU::bind(sceneLit, sceneDepth, LoadOperation::LOAD, LoadOperation::LOAD, LoadOperation::DONTCARE);
				GPU::setViewport(sceneLit);
				GPU::setPolygonState(PolygonMode::FILL);
				GPU::setCullState(false);
				GPU::setDepthState(true, TestFunction::GEQUAL, false);
				billboardObject->use();
				billboardObject->buffer(frameInfos, 0);
				billboardObject->buffer(*scene.materialInfos, 1);
				// First billboards
				if(showBillboards){
					for(World::Blending blend : blendsPreFog){
						const Scene::Range & range = scene.billboardRanges[blend];
						if(range.count <= 0){
							continue;
						}
						GPU::setBlendState(true, BlendEquation::ADD, srcFuncs[blend], dstFuncs[blend]);
						GPU::drawMesh(scene.billboardsMesh, range.firstIndex, range.count);
					}
				}

				// Then particles
				if(showParticles){
					for(World::Blending blend : blendsPreFog){
						const Scene::Range & range = scene.particleRanges[blend];
						if(range.count <= 0){
							continue;
						}
						GPU::setBlendState(true, BlendEquation::ADD, srcFuncs[blend], dstFuncs[blend]);
						GPU::drawMesh(scene.billboardsMesh, range.firstIndex, range.count);
					}
				}
			}

			// Fog
			if(showFog){
				GPU::bind(sceneLit, LoadOperation::LOAD);
				GPU::setViewport(sceneLit);
				GPU::setDepthState(false);
				GPU::setCullState(true, Faces::BACK );
				GPU::setColorState(true, true, true, true);
				GPU::setBlendState( true, BlendEquation::ADD, BlendFunction::ONE, BlendFunction::ONE_MINUS_SRC_ALPHA);
				GPU::setPolygonState(PolygonMode::FILL);
				// Can't use passthrough because its input is a texture, and the internal layout system can't reconcile
				// a storage image used in a sampled image slot (even if creation flags are OK).
				// This stems from handling all transitions around compute shader executions, which restores storage image to general layout
				// and does not handle transitions just before a draw call (at the time it wasn't possible to issue a transition during
				// a render pass, but dynamic rendering might have relaxed the constraint?).
				passthroughImage->use();
				passthroughImage->texture(sceneFog, 0);
				GPU::drawQuad();

			}

			// Then render post fog emissive billboards
			if(showBillboards || showParticles){
				GPU::bind(sceneLit, sceneDepth, LoadOperation::LOAD, LoadOperation::LOAD, LoadOperation::DONTCARE);
				GPU::setViewport(sceneLit);
				GPU::setPolygonState(PolygonMode::FILL);
				GPU::setCullState(false);
				GPU::setDepthState(true, TestFunction::GEQUAL, false);
				billboardObject->use();
				billboardObject->buffer(frameInfos, 0);
				billboardObject->buffer(*scene.materialInfos, 1);

				// First billboards
				if(showBillboards){
					for(World::Blending blend : blendsPostFog){
						const Scene::Range & range = scene.billboardRanges[blend];
						if(range.count <= 0){
							continue;
						}
						GPU::setBlendState(true, BlendEquation::ADD, srcFuncs[blend], dstFuncs[blend]);
						GPU::drawMesh(scene.billboardsMesh, range.firstIndex, range.count);
					}
				}

				// Then particles
				if(showParticles){
					for(World::Blending blend : blendsPostFog){
						const Scene::Range & range = scene.particleRanges[blend];
						if(range.count <= 0){
							continue;
						}
						GPU::setBlendState(true, BlendEquation::ADD, srcFuncs[blend], dstFuncs[blend]);
						GPU::drawMesh(scene.billboardsMesh, range.firstIndex, range.count);
					}
				}

			}

			if(showTransparents){
				GPU::bind(sceneLit, sceneDepth, LoadOperation::LOAD, LoadOperation::LOAD, LoadOperation::DONTCARE);
				GPU::setViewport(sceneLit);

				GPU::setDepthState( true, TestFunction::GEQUAL, false );
				GPU::setCullState( true, Faces::BACK );
				GPU::setBlendState( true, BlendEquation::ADD, BlendFunction::SRC_ALPHA, BlendFunction::ONE_MINUS_SRC_ALPHA );
				GPU::setColorState( true, true, true, false );

				// Alternative possibility: real alpha blend and object sorting.
				forwardInstancedObject->use();
				forwardInstancedObject->buffer(frameInfos, 0);
				forwardInstancedObject->buffer(*scene.meshInfos, 1);
				forwardInstancedObject->buffer(*scene.instanceInfos, 2);
				forwardInstancedObject->buffer(*scene.materialInfos, 3);
				forwardInstancedObject->buffer(*transparentDrawInstances, 4);
				forwardInstancedObject->buffer(*scene.lightInfos, 5);
				forwardInstancedObject->buffer(*scene.zoneInfos, 6);
				forwardInstancedObject->texture(textures.fogXY, 0);
				forwardInstancedObject->texture(textures.fogZ, 1);
				forwardInstancedObject->texture(lightClusters, 2);
				forwardInstancedObject->texture(shadow.maps, 3);
				forwardInstancedObject->texture(fogClusters, 4);

				const auto& range = scene.globalMeshMaterialRanges[Object::Material::TRANSPARENT];
				GPU::drawIndirectMesh(scene.globalMesh, *transparentDrawCommands, 0, range.instanceCount);
			}

			// Postprocess stack
			{
				GPU::setDepthState(false);
				GPU::setCullState(true, Faces::BACK );
				GPU::setColorState(true, true, true, true);
				GPU::setBlendState( false );
				GPU::setPolygonState(PolygonMode::FILL);

				// Bloom
				if(showPostprocess & MODE_POSTPROCESS_BLOOM){

					GPU::blit(sceneLit, bloom0, 0, 0, Filter::LINEAR);
					GPU::setViewport(0, 0, bloom0.width, bloom0.height);

					for(uint blurStep = 0; blurStep < bloomBlurSteps; ++blurStep){
						GPU::bind(bloom1, LoadOperation::DONTCARE);
						bloomBlur->use();
						bloomBlur->texture(bloom0, 0);
						bloomBlur->buffer(blurInfosH, 0);
						GPU::drawQuad();
						GPU::bind(bloom0, LoadOperation::DONTCARE);
						bloomBlur->use();
						bloomBlur->texture(bloom1, 0);
						bloomBlur->buffer(blurInfosV, 0);
						GPU::drawQuad();
					}

				}

				// Postprocesses
				{
					GPU::bind(sceneFog, LoadOperation::DONTCARE);
					GPU::setViewport(sceneFog);
					GPU::setDepthState(false);
					GPU::setCullState(true, Faces::BACK );
					GPU::setColorState(true, true, true, true);
					GPU::setPolygonState(PolygonMode::FILL);
					noiseGrainQuad->use();
					noiseGrainQuad->texture(sceneLit, 0);
					noiseGrainQuad->texture(bloom0, 1);
					noiseGrainQuad->texture(textures.noise, 2);
					noiseGrainQuad->texture(textures.noisePulse, 3);
					noiseGrainQuad->texture(sceneHeat, 4);
					noiseGrainQuad->texture(textures.heat, 5);
					noiseGrainQuad->texture(textures.water, 6);
					noiseGrainQuad->buffer(frameInfos, 0);
					GPU::drawQuad();
				}
			}

			// Debug view.
			if(debug.anyActive()){

				GPU::bind( sceneFog, sceneDepth, LoadOperation::LOAD, LoadOperation::LOAD, LoadOperation::DONTCARE );
				GPU::setViewport( sceneFog );

				GPU::setPolygonState( PolygonMode::LINE );
				GPU::setCullState( false, Faces::BACK );
				GPU::setDepthState( true, TestFunction::GEQUAL, false );
				GPU::setBlendState( false );
				GPU::setColorState( true, true, true, true );

				if(debug.showWireframe){
					debugInstancedObject->use();
					debugInstancedObject->buffer(frameInfos, 0);
					debugInstancedObject->buffer(*scene.meshInfos, 1);
					debugInstancedObject->buffer(*scene.instanceInfos, 2);
					debugInstancedObject->buffer(*scene.materialInfos, 3);
					debugInstancedObject->buffer(*drawInstances, 4);
					// Always draw all meshes.
					GPU::drawIndirectMesh(scene.globalMesh, *drawCommands, 0, scene.meshInfos->size());
					// FIXME: hides other debug draws when activated.
				}

				coloredDebugDraw->use();
				coloredDebugDraw->buffer( frameInfos, 0 );
				if( ( selected.mesh >= 0 || selected.instance >= 0 ) && !debug.boundingBox.indices.empty() )
				{
					GPU::drawMesh( debug.boundingBox );
				}
				if(debug.showLights && !debug.lights.indices.empty() )
				{
					GPU::drawMesh( debug.lights );
				}
				if( debug.showZones && !debug.zones.indices.empty() )
				{
					GPU::drawMesh( debug.zones );
				}
				if( debug.showFxs && !debug.fxs.indices.empty() )
				{
					GPU::drawMesh( debug.fxs );
				}
			}


			if(Input::manager().released(Input::Mouse::Right)){

				// Compute mouse coordinates.
				glm::vec2 mousePos = Input::manager().mouse();
				// Convert to pixels
				const glm::vec2 winSize = Input::manager().size();
				mousePos *= winSize / Input::manager().density();
				// Scale to main viewport.
				mousePos = (mousePos - glm::vec2(mainViewport.x, mainViewport.y)) / glm::vec2(mainViewport.z, mainViewport.w);
				// Check that we are in the viewport.
				if(glm::all(glm::lessThan(mousePos, glm::vec2(1.0f))) && glm::all(glm::greaterThan(mousePos, glm::vec2(0.0f)))){

					// Render to selection ID texture
					{
						selectionColor.resize(sceneLit.width, sceneLit.height);
						GPU::setViewport(selectionColor);
						GPU::bind(selectionColor, sceneDepth, glm::vec4(0.0f), LoadOperation::LOAD, LoadOperation::DONTCARE);

						GPU::setPolygonState(PolygonMode::FILL);
						GPU::setCullState(true, Faces::BACK);
						GPU::setDepthState(true, TestFunction::EQUAL, false);
						GPU::setBlendState(false);

						selectionObject->use();
						selectionObject->buffer(frameInfos, 0 );
						selectionObject->buffer(*scene.meshInfos, 1);
						selectionObject->buffer(*scene.instanceInfos, 2);
						selectionObject->buffer(*scene.materialInfos, 3);
						selectionObject->buffer(*drawInstances, 4);
						// Draw all meshes.
						GPU::drawIndirectMesh(scene.globalMesh, *drawCommands, 0, scene.meshInfos->size());
					}

					const glm::vec2 texSize(selectionColor.width, selectionColor.height);
					glm::uvec2 readbackCoords = mousePos * texSize;
					readbackCoords = glm::min(readbackCoords, glm::uvec2(texSize) - 2u);
					GPU::downloadTextureAsync( selectionColor, readbackCoords, glm::uvec2(2u), 1, [&selected, &updateInstanceBoundingBox, &scene](const Texture& result){
						const auto& pixels = result.images[0].pixels;

						uint index = uint(pixels[0]) + (uint(pixels[1] ) << 8u);
						if(index != 0){
							selected.instance = index-1;
							selected.mesh = scene.instanceDebugInfos[selected.instance].meshIndex;
							updateInstanceBoundingBox = true;
						}
					});
				}
			}


		} else {
			GPU::clearTexture(sceneFog, glm::vec4(0.2f, 0.2f, 0.2f, 1.0f));
		}

		if(selected.texture >= 0){
			GPU::setViewport(textureView);
			GPU::bind(textureView, glm::vec4(1.0f, 0.0f, 0.5f, 1.0f));
			GPU::setDepthState(false);
			GPU::setCullState(true, Faces::BACK );
			GPU::setPolygonState(PolygonMode::FILL);
			GPU::setBlendState(true, BlendEquation::ADD, BlendFunction::SRC_ALPHA, BlendFunction::ONE_MINUS_SRC_ALPHA);
			textureDebugQuad->use();
			textureDebugQuad->buffer(frameInfos, 0);
			GPU::drawQuad();
		}

		scrollToItem = false;
		if(updateInstanceBoundingBox){
			frameInfos[0].selectedInstance = selected.instance;
			frameInfos[0].selectedMesh = selected.mesh;
			debug.buildBoundingBoxes({scene.instanceDebugInfos[selected.instance].bbox});
			updateInstanceBoundingBox = false;
			scrollToItem = true;
		}

		window.bind(glm::vec4(0.058f, 0.133f, 0.219f, 1.0f), LoadOperation::DONTCARE, LoadOperation::DONTCARE);

		if(textures.bg.gpu){
			GPU::setViewport(window.color());
			GPU::setDepthState(false);
			GPU::setCullState(true, Faces::BACK );
			GPU::setPolygonState(PolygonMode::FILL);
			GPU::setBlendState(false);
			passthrough->use();
			passthrough->texture(textures.bg, 0);
			GPU::drawQuad();
		}
		++frameIndex;
	}

	scene.clean();
	for(ProgramInfos& infos : programPool){
		infos.program->clean();
		delete infos.program;
	}

	return 0;
}
