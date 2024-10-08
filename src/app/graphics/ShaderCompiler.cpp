#include "graphics/ShaderCompiler.hpp"
#include "graphics/GPU.hpp"
#include "graphics/GPUInternal.hpp"

#include "core/TextUtilities.hpp"

#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>
#include <spirv_cross/spirv_cross.hpp>

#include <map>
#include <sstream>

// SPIRV compilation settings based on glslang standalone example
static const TBuiltInResource defaultBuiltInResources = {
	/* .MaxLights = */ 32,
	   /* .MaxClipPlanes = */ 6,
	   /* .MaxTextureUnits = */ 32,
	   /* .MaxTextureCoords = */ 32,
	   /* .MaxVertexAttribs = */ 64,
	   /* .MaxVertexUniformComponents = */ 4096,
	   /* .MaxVaryingFloats = */ 64,
	   /* .MaxVertexTextureImageUnits = */ 32,
	   /* .MaxCombinedTextureImageUnits = */ 80,
	   /* .MaxTextureImageUnits = */ 32,
	   /* .MaxFragmentUniformComponents = */ 4096,
	   /* .MaxDrawBuffers = */ 32,
	   /* .MaxVertexUniformVectors = */ 128,
	   /* .MaxVaryingVectors = */ 8,
	   /* .MaxFragmentUniformVectors = */ 16,
	   /* .MaxVertexOutputVectors = */ 16,
	   /* .MaxFragmentInputVectors = */ 15,
	   /* .MinProgramTexelOffset = */ -8,
	   /* .MaxProgramTexelOffset = */ 7,
	   /* .MaxClipDistances = */ 8,
	   /* .MaxComputeWorkGroupCountX = */ 65535,
	   /* .MaxComputeWorkGroupCountY = */ 65535,
	   /* .MaxComputeWorkGroupCountZ = */ 65535,
	   /* .MaxComputeWorkGroupSizeX = */ 1024,
	   /* .MaxComputeWorkGroupSizeY = */ 1024,
	   /* .MaxComputeWorkGroupSizeZ = */ 64,
	   /* .MaxComputeUniformComponents = */ 1024,
	   /* .MaxComputeTextureImageUnits = */ 16,
	   /* .MaxComputeImageUniforms = */ 8,
	   /* .MaxComputeAtomicCounters = */ 8,
	   /* .MaxComputeAtomicCounterBuffers = */ 1,
	   /* .MaxVaryingComponents = */ 60,
	   /* .MaxVertexOutputComponents = */ 64,
	   /* .MaxGeometryInputComponents = */ 64,
	   /* .MaxGeometryOutputComponents = */ 128,
	   /* .MaxFragmentInputComponents = */ 128,
	   /* .MaxImageUnits = */ 8,
	   /* .MaxCombinedImageUnitsAndFragmentOutputs = */ 8,
	   /* .MaxCombinedShaderOutputResources = */ 8,
	   /* .MaxImageSamples = */ 0,
	   /* .MaxVertexImageUniforms = */ 0,
	   /* .MaxTessControlImageUniforms = */ 0,
	   /* .MaxTessEvaluationImageUniforms = */ 0,
	   /* .MaxGeometryImageUniforms = */ 0,
	   /* .MaxFragmentImageUniforms = */ 8,
	   /* .MaxCombinedImageUniforms = */ 8,
	   /* .MaxGeometryTextureImageUnits = */ 16,
	   /* .MaxGeometryOutputVertices = */ 256,
	   /* .MaxGeometryTotalOutputComponents = */ 1024,
	   /* .MaxGeometryUniformComponents = */ 1024,
	   /* .MaxGeometryVaryingComponents = */ 64,
	   /* .MaxTessControlInputComponents = */ 128,
	   /* .MaxTessControlOutputComponents = */ 128,
	   /* .MaxTessControlTextureImageUnits = */ 16,
	   /* .MaxTessControlUniformComponents = */ 1024,
	   /* .MaxTessControlTotalOutputComponents = */ 4096,
	   /* .MaxTessEvaluationInputComponents = */ 128,
	   /* .MaxTessEvaluationOutputComponents = */ 128,
	   /* .MaxTessEvaluationTextureImageUnits = */ 16,
	   /* .MaxTessEvaluationUniformComponents = */ 1024,
	   /* .MaxTessPatchComponents = */ 120,
	   /* .MaxPatchVertices = */ 32,
	   /* .MaxTessGenLevel = */ 64,
	   /* .MaxViewports = */ 16,
	   /* .MaxVertexAtomicCounters = */ 0,
	   /* .MaxTessControlAtomicCounters = */ 0,
	   /* .MaxTessEvaluationAtomicCounters = */ 0,
	   /* .MaxGeometryAtomicCounters = */ 0,
	   /* .MaxFragmentAtomicCounters = */ 8,
	   /* .MaxCombinedAtomicCounters = */ 8,
	   /* .MaxAtomicCounterBindings = */ 1,
	   /* .MaxVertexAtomicCounterBuffers = */ 0,
	   /* .MaxTessControlAtomicCounterBuffers = */ 0,
	   /* .MaxTessEvaluationAtomicCounterBuffers = */ 0,
	   /* .MaxGeometryAtomicCounterBuffers = */ 0,
	   /* .MaxFragmentAtomicCounterBuffers = */ 1,
	   /* .MaxCombinedAtomicCounterBuffers = */ 1,
	   /* .MaxAtomicCounterBufferSize = */ 16384,
	   /* .MaxTransformFeedbackBuffers = */ 4,
	   /* .MaxTransformFeedbackInterleavedComponents = */ 64,
	   /* .MaxCullDistances = */ 8,
	   /* .MaxCombinedClipAndCullDistances = */ 8,
	   /* .MaxSamples = */ 4,
	   /* .maxMeshOutputVerticesNV = */ 256,
	   /* .maxMeshOutputPrimitivesNV = */ 512,
	   /* .maxMeshWorkGroupSizeX_NV = */ 32,
	   /* .maxMeshWorkGroupSizeY_NV = */ 1,
	   /* .maxMeshWorkGroupSizeZ_NV = */ 1,
	   /* .maxTaskWorkGroupSizeX_NV = */ 32,
	   /* .maxTaskWorkGroupSizeY_NV = */ 1,
	   /* .maxTaskWorkGroupSizeZ_NV = */ 1,
	   /* .maxMeshViewCountNV = */ 4,
	   /* .maxMeshOutputVerticesEXT = */ 256,
	   /* .maxMeshOutputPrimitivesEXT = */ 256,
	   /* .maxMeshWorkGroupSizeX_EXT = */ 128,
	   /* .maxMeshWorkGroupSizeY_EXT = */ 128,
	   /* .maxMeshWorkGroupSizeZ_EXT = */ 128,
	   /* .maxTaskWorkGroupSizeX_EXT = */ 128,
	   /* .maxTaskWorkGroupSizeY_EXT = */ 128,
	   /* .maxTaskWorkGroupSizeZ_EXT = */ 128,
	   /* .maxMeshViewCountEXT = */ 4,
	   /* .maxDualSourceDrawBuffersEXT = */ 1,

	   /* .limits = */ {
		   /* .nonInductiveForLoops = */ 1,
		   /* .whileLoops = */ 1,
		   /* .doWhileLoops = */ 1,
		   /* .generalUniformIndexing = */ 1,
		   /* .generalAttributeMatrixVectorIndexing = */ 1,
		   /* .generalVaryingIndexing = */ 1,
		   /* .generalSamplerIndexing = */ 1,
		   /* .generalVariableIndexing = */ 1,
		   /* .generalConstantMatrixVectorIndexing = */ 1,
	   }};

bool ShaderCompiler::init(){
	return glslang::InitializeProcess();
}

void ShaderCompiler::cleanup(){
	glslang::FinalizeProcess();
}

void ShaderCompiler::clean(Program::Stage & stage){
	if(stage.module != VK_NULL_HANDLE){
		GPUContext* context = GPU::getInternal();
		vkDestroyShaderModule(context->device, stage.module, nullptr);
	}
	stage.reset();
}

void ShaderCompiler::compile(const std::string & prog, ShaderType type, Program::Stage & stage, bool generateModule, std::string & finalLog) {

	// Add GLSL version.
	std::string outputProg = "#version 450\n\n";
	outputProg.append("#extension GL_ARB_separate_shader_objects : enable\n");
	outputProg.append("#extension GL_EXT_samplerless_texture_functions : enable\n");
#if defined(DRAW_ID_FALLBACK)
	outputProg.append("#define DRAW_ID_FALLBACK 1\n");
#else
	outputProg.append("#extension GL_ARB_shader_draw_parameters : enable\n");
#endif
	outputProg.append("#line 1 0\n");
	outputProg.append(prog);

	// Create shader object.
	static const std::unordered_map<ShaderType, EShLanguage> types = {
		{ShaderType::VERTEX, EShLangVertex},
		{ShaderType::FRAGMENT, EShLangFragment},
		{ShaderType::TESSCONTROL, EShLangTessControl},
		{ShaderType::TESSEVAL, EShLangTessEvaluation},
		{ShaderType::COMPUTE, EShLangCompute}
	};
	const char* progStr = outputProg.c_str();
	const EShLanguage stageDest = types.at(type);
	glslang::TShader shader(stageDest);
	shader.setStrings(&progStr, 1);
	shader.setEntryPoint("main");
	shader.setEnvInput(glslang::EShSourceGlsl, stageDest, glslang::EShClientVulkan, 100);
	shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
	shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_3);


	finalLog = "";
	const EShMessages messages = (EShMessages)(EShMsgDefault | EShMsgSpvRules | EShMsgVulkanRules);
	bool success = shader.parse(&defaultBuiltInResources, 110, true, messages);
	if(!success){
		std::string infoLogString(shader.getInfoLog());
		TextUtilities::replace(infoLogString, "\n", "\n\t");
		infoLogString.insert(0, "\t");
		finalLog = infoLogString;
		return;
	}

	glslang::TProgram program;
	program.addShader(&shader);
	success = program.link(messages);
	if(!success){
		std::string infoLogString(program.getInfoLog());
		finalLog = infoLogString;
		return;
	}
	if(!program.mapIO()){
		finalLog = "Unable to map IO.";
		return;
	}

	std::vector<unsigned int> spirv;
	glslang::SpvOptions spvOptions;
	spvOptions.generateDebugInfo = false;
	spvOptions.disableOptimizer = false;
	spvOptions.optimizeSize = true;
	spvOptions.disassemble = false;
	spvOptions.validate = false;
	glslang::GlslangToSpv(*program.getIntermediate(stageDest), spirv, &spvOptions);
	if(spirv.empty()){
		finalLog = "Unable to generate SPIRV.";
		return;
	}

	reflect(spirv, stage);

	if(generateModule){
		VkShaderModuleCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		createInfo.codeSize = spirv.size() * sizeof(unsigned int);
		createInfo.pCode = reinterpret_cast<const uint32_t*>(spirv.data());
		VkShaderModule shaderModule;
		GPUContext* context = GPU::getInternal();
		if(vkCreateShaderModule(context->device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
			finalLog = "Unable to create shader module.";
			return;
		}
		stage.module = shaderModule;
	} else {
		stage.module = VK_NULL_HANDLE;
	}
}

/// Internal reflection helpers

bool reflectBuffer( const spirv_cross::Compiler& compiler, const spirv_cross::Resource& ubo, bool storage, Program::BufferDef& def )
{
	const spirv_cross::SPIRType& baseType = compiler.get_type( ubo.base_type_id );
	const spirv_cross::SPIRType& type = compiler.get_type( ubo.type_id );

	def.size = ( uint )compiler.get_declared_struct_size( baseType );
	def.binding = compiler.get_decoration( ubo.id, spv::DecorationBinding );
	def.name = ubo.name;
	def.storage = storage;
	def.set = compiler.get_decoration( ubo.id, spv::DecorationDescriptorSet );
	def.count = 1;
	if( type.array.size() > 0 )
	{
		if( type.array.size() > 1 || type.array[ 0 ] == 0 )
		{
			Log::warning( "GPU: Unsupported unsized/multi-level array of buffers in shader." );
			return false;
		}
		def.count = static_cast< uint >( type.array[ 0 ] );
	}
	return true;
}

bool reflectImage( const spirv_cross::Compiler& compiler, const spirv_cross::Resource& tex, Program::ImageDef& def )
{
	static const std::map<spv::Dim, TextureShape> texShapes = {
			{spv::Dim1D, TextureShape::D1},
			{spv::Dim2D, TextureShape::D2},
			{spv::Dim3D, TextureShape::D3},
			{spv::DimCube, TextureShape::Cube} };

	const spirv_cross::SPIRType& baseType = compiler.get_type( tex.base_type_id );
	const spirv_cross::SPIRType& type = compiler.get_type( tex.type_id );

	if( texShapes.count( baseType.image.dim ) == 0 )
	{
		Log::error("Unsupported texture shape in shader.");
		return false;
	}
	def.binding = compiler.get_decoration( tex.id, spv::DecorationBinding );
	def.name = compiler.get_name( tex.id );
	def.storage = baseType.image.sampled == 2;
	def.set = compiler.get_decoration( tex.id, spv::DecorationDescriptorSet );
	def.shape = texShapes.at( baseType.image.dim );
	def.count = 1;
	if( baseType.image.arrayed )
	{
		def.shape = def.shape | TextureShape::Array;
	}
	if( type.array.size() > 0 )
	{
		if( type.array.size() > 1 )
		{
			Log::warning("GPU: Unsupported unsized/multi-level array of textures in shader.");
			return false;
		}
		// Size will be 0 for bindless texture array (unsized array).
		def.count = static_cast< uint >( type.array[ 0 ] );
	}
	return true;
}

void ShaderCompiler::reflect( const std::vector<uint32_t>& spirv, Program::Stage& stage )
{
	spirv_cross::Compiler comp( spirv );

	// Retrieve group size.
	for( uint i = 0; i < 3; ++i )
	{
		stage.size[ i ] = comp.get_execution_mode_argument( spv::ExecutionModeLocalSize, i );
	}

	spirv_cross::ShaderResources res = comp.get_shader_resources();
	// No combined sampler.
	assert( res.sampled_images.size() == 0 );

	// Push constants
	if( res.push_constant_buffers.size() != 0 )
	{
		const spirv_cross::Resource& pushBuffer = res.push_constant_buffers[ 0 ];
		const spirv_cross::SPIRType& baseType = comp.get_type( pushBuffer.base_type_id );
		stage.pushConstants.size = ( uint )comp.get_declared_struct_size( baseType );
	}

	// No pseudo 'free' uniforms in this project.
	for( const spirv_cross::Resource& buffer : res.uniform_buffers )
	{
		stage.buffers.emplace_back();
		Program::BufferDef& def = stage.buffers.back();
		if( !reflectBuffer( comp, buffer, false, def ) )
		{
			stage.buffers.pop_back();
			continue;
		}
	}

	// All storage buffers are real buffers.
	for( const spirv_cross::Resource& buffer : res.storage_buffers )
	{
		stage.buffers.emplace_back();
		if( !reflectBuffer( comp, buffer, true, stage.buffers.back() ) )
		{
			stage.buffers.pop_back();
		}
	}

	// Texture and storage images are processed in the same way.
	for( const spirv_cross::Resource& img : res.separate_images )
	{
		stage.images.emplace_back();
		if( !reflectImage( comp, img, stage.images.back() ) )
		{
			stage.images.pop_back();
		}
	}
	for( const spirv_cross::Resource& img : res.storage_images )
	{
		stage.images.emplace_back();
		if( !reflectImage( comp, img, stage.images.back() ) )
		{
			stage.images.pop_back();
		}
	}
}
