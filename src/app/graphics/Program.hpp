#pragma once

#include "core/Common.hpp"

#include "graphics/GPUTypes.hpp"
#include "resources/Buffer.hpp"
#include "resources/Texture.hpp"

#include <unordered_map>
#include <array>

// Forward declarations
VK_DEFINE_HANDLE(VkBuffer)
VK_DEFINE_HANDLE(VkImageView)
VK_DEFINE_HANDLE(VkSampler)
VK_DEFINE_HANDLE(VkShaderModule)
VK_DEFINE_HANDLE(VkPipelineLayout)
VK_DEFINE_HANDLE(VkDescriptorSetLayout)

// #define UNIFORMS_SET 0 unused
#define SAMPLERS_SET 1
#define IMAGES_SET 2
#define BUFFERS_SET 0

/**
 \brief Represents a group of shaders used for rendering.
 \details Internally responsible for handling uniforms locations, shaders reloading and values caching.
 Uniform sets are predefined: set 0 is for dynamic uniforms, set 1 for image-samplers, set 2 for static/per-frame uniform buffers.
 \ingroup Graphics
 */
class Program {
public:

	/** Bind all mip levels of a texture */
	static uint ALL_MIPS;

	/** Type of program */
	enum class Type {
		GRAPHICS, ///< Graphics program for draw calls.
		COMPUTE ///< Compute program for dispatch calls.
	};

	/** \brief Uniform reflection information.
	 */
	struct UniformDef {

		/// Uniform basic type.
		enum class Type {
			BOOL, BVEC2, BVEC3, BVEC4,
			INT, IVEC2, IVEC3, IVEC4,
			UINT, UVEC2, UVEC3, UVEC4,
			FLOAT, VEC2, VEC3, VEC4,
			MAT2, MAT3, MAT4,
			OTHER
		};

		std::string name; ///< The uniform name.
		Type type; ///< The uniform type.

		/// Uniform location.
		struct Location {
			uint binding; ///< Buffer binding.
			uint offset; ///< Offset in buffer.
		};

		std::vector<Location> locations; ///< Locations where this uniform is present.

	};

	/** \brief Image-sampler reflection information.
	 */
	struct ImageDef {
		std::string name; ///< Image name.
		TextureShape shape; ///< Image shape.
		uint binding; ///< Image binding location.
		uint set; ///< Image binding set.
		uint count; ///< Number of similar images for this binding point.
		bool storage; ///< Is this a storage image.
	};

	/** \brief Buffer reflection information.
	 */
	struct BufferDef {
		std::vector<UniformDef> members; ///< Uniforms in buffer.
		std::string name; ///< Buffer name.
		uint binding = 0; ///< Buffer binding location.
		uint size = 0; ///< Buffer size.
		uint set = 0; ///< Buffer binding set.
		uint count = 0; ///< Number of similar buffers for this binding point.
		bool storage; ///< Is this a storage buffer.
	};

	struct ConstantsDef {
		uint size = 0;
		uint mask = 0;

		void clear(){
			size = 0; mask = 0;
		}
	};

	using Uniforms = std::unordered_map<std::string, UniformDef>; ///< List of named uniforms.

	/**
	 Load, compile and link shaders into a GPU graphics program.
	 \param name the program name for logging
	 \param vertexContent the content of the vertex shader
	 \param fragmentContent the content of the fragment shader
	 \param tessControlContent the content of the tessellation control shader (can be empty)
	 \param tessEvalContent the content of the tessellation evaluation shader (can be empty)
	 */
	Program(const std::string & name, const std::string & vertexContent, const std::string & fragmentContent, const std::string & tessControlContent = "", const std::string & tessEvalContent = "");

	/**
	 Load, compile and link shaders into a GPU compute program.
	 \param name the program name for logging
	 \param computeContent the content of the compute shader
	 */
	Program(const std::string & name, const std::string & computeContent);

	/**
	 Load the program, compiling the shader and updating all uniform locations.
	 \param vertexContent the content of the vertex shader
	 \param fragmentContent the content of the fragment shader
	 \param tessControlContent the content of the tessellation control shader (can be empty)
	 \param tessEvalContent the content of the tessellation evaluation shader (can be empty)
	 */
	void reload(const std::string & vertexContent, const std::string & fragmentContent, const std::string & tessControlContent = "", const std::string & tessEvalContent = "");

	/**
	 Load the program, compiling the shader and updating all uniform locations.
	 \param computeContent the content of the compute shader
	 */
	void reload(const std::string & computeContent);

	/** \return true if the program has been recently reloaded. */
	bool reloaded() const;
	
	/** Check if the program has been recently reloaded.
	 * \param absorb should the reloaded flag be set to false afterwards
	 * \return true if reloaded
	 */
	bool reloaded(bool absorb);

	/** Activate the program shaders.
	 */
	void use() const;

	/** Delete the program on the GPU.
	 */
	void clean();

	/** Bind a buffer to a given location.
	 * \param buffer the buffer to bind
	 * \param slot the location to bind to
	 */
	void buffer(const UniformBufferBase& buffer, uint slot);

	/** Bind a buffer to a given location.
	 * \param buffer the buffer to bind
	 * \param slot the location to bind to
	 */
	void buffer(const Buffer& buffer, uint slot);

	/** Bind a set of buffers to a given location.
	 * \param buffers the buffers to bind
	 * \param slot the location to bind to
	 */
	void bufferArray(const std::vector<const Buffer *> & buffers, uint slot);

	/** Bind a texture to a given location.
	 * \param texture the texture to bind
	 * \param slot the location to bind to
	 * \param mip the mip of the texture to bind (or all mips if left at its default value)
	 */
	void texture(const Texture* texture, uint slot, uint mip = Program::ALL_MIPS);

	/** Bind a texture to a given location.
	 * \param texture the texture to bind
	 * \param slot the location to bind to
	 * \param mip the mip of the texture to bind (or all mips if left at its default value)
	 */
	void texture(const Texture& texture, uint slot, uint mip = Program::ALL_MIPS);

	/** Bind a set of textures to a given location.
	 * \param textures the textures to bind
	 * \param slot the location to bind to
	 * \param mip the mip of the textures to bind (or all mips if left at its default value)
	 */
	void textureArray(const std::vector<const Texture *> & textures, uint slot, uint mip = Program::ALL_MIPS);

	/** Bind a set of textures to successive locations.
	 * \param textures the textures to bind
	 * \param slot the location to bind the first texture to
	 * \note Successive textures will be bound to locations slot+1, slot+2,...
	 */
	void textures(const std::vector<const Texture *> & textures, size_t slot = 0);


	/** Ensure all currently set resources are in the proper layout/state and are synchronized
	 for a given type of use (graphics or compute).
	 \param type the type of use to prepare resources for
	 */
	void transitionResourcesTo(Program::Type type);

	/** Update internal data (descriptors,...) and bind them before a draw/dispatch. */
	void update();

	/** \return the program name */
	const std::string & name() const {
		return _name;
	}

	/** \return whether this is a graphics or compute program */
	Program::Type type() const {
		return _type;
	}

	/** \return the local group size for compute programs */
	const glm::uvec3 & size() const;

	/** Copy assignment operator (disabled).
	 \return a reference to the object assigned to
	 */
	Program & operator=(const Program &) = delete;
	
	/** Copy constructor (disabled). */
	Program(const Program &) = delete;
	
	/** Move assignment operator.
	 \return a reference to the object assigned to
	 */
	Program & operator=(Program &&) = delete;
	
	/** Move constructor. */
	Program(Program &&) = default;

	/// \brief Program pipeline state.
	struct State {
		std::vector<VkDescriptorSetLayout> setLayouts; ///< Descriptor sets layouts.
		VkPipelineLayout layout = VK_NULL_HANDLE; ///< Layout handle (pre-created).
		uint pushConstantsStages = 0;
	};

	/// \return the program state for a pipeline
	const State& getState() const {
		return _state;
	}

	/// \brief Per-stage reflection information.
	struct Stage {
		std::vector<ImageDef> images; ///< Image definitions.
		std::vector<BufferDef> buffers; ///< Buffers definitions.
		ConstantsDef pushConstants;
		VkShaderModule module = VK_NULL_HANDLE; ///< Native shader data.
		glm::uvec3 size = glm::uvec3(0); ///< Local group size.
		/// Reset the stage state.
		void reset();
	};

	/** Query shader information for a stage.
	 * \param type the stage to query
	 * \return the stage reflection information
	 */
	Stage& stage(ShaderType type){
		return _stages[uint(type)];
	}

private:

	/// Reflect all uniforms/textures/storage buffers and images based on the shader content.
	void reflect();


	/// Update internal metrics.
	void updateUniformMetric() const;


	/// \brief Internal state for an image.
	struct TextureState {
		std::string name; ///< Name.
		TextureShape shape = TextureShape::D2; ///< Texture shape.
		std::vector<const Texture*> textures = { nullptr }; ///< The source texture.
		std::vector<VkImageView> views = { VK_NULL_HANDLE }; ///< Texture view.
		uint count = 1; ///< Number of images bound at this slot..
		uint mip = 0xFFFF; ///< The corresponding mip.
		bool storage = false; ///< Is the image used as storage.
	};

	/// \brief Internal state for a static (external) uniform buffer.
	struct StaticBufferState {
		std::string name; ///< Name.
		std::vector<VkBuffer> buffers = { VK_NULL_HANDLE}; ///< Native buffer handle.
		std::vector<uint> offsets = {0}; ///< Start offset in the buffer.
		uint size = 0; ///< Region size in the buffer.
		uint count = 1; ///< Number of buffers bound at this slot.
		uint lastSet = 0; ///< Keep track of one of the buffers that has been set (and is thus valid).
		bool storage = false; ///< Is the buffer used as storage.
	};


	std::string _name; ///< Debug name.
	std::array<Stage, int(ShaderType::COUNT)> _stages; ///< Per-stage reflection data.
	State _state; ///< Program pipeline state.

	std::unordered_map<std::string, UniformDef> _uniforms; ///< All dynamic uniform definitions.

	//std::unordered_map<int, DynamicBufferState> _dynamicBuffers; ///< Dynamic uniform buffer definitions (set 0).
	std::unordered_map<int, TextureState> _textures; ///< Dynamic image-sampler definitions (set 2).
	std::unordered_map<int, StaticBufferState> _staticBuffers; ///< Static uniform buffer definitions (set 0).
	ConstantsDef _pushConstants;

	std::array<bool, 3> _dirtySets; ///< Marks which descriptor sets are dirty.
	std::array<DescriptorSet, 3> _currentSets; ///< Descriptor sets.
	std::vector<uint32_t> _currentOffsets; ///< Offsets in the descriptor set for dynamic uniform buffers.

	bool _reloaded = false; ///< Has the program been reloaded.
	const Type _type; ///< Is this a compute shader.

	friend class GPU; ///< Utilities will need to access GPU handle.
};
