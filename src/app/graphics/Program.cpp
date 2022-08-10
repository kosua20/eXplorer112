#include "graphics/Program.hpp"
#include "graphics/GPU.hpp"
#include "graphics/GPUInternal.hpp"
#include <set>
uint Program::ALL_MIPS = 0xFFFF;

#include <cstring>


Program::Program(const std::string & name, const std::string & vertexContent, const std::string & fragmentContent, const std::string & tessControlContent, const std::string & tessEvalContent) :
	_name(name), _type(Type::GRAPHICS) {
	reload(vertexContent, fragmentContent, tessControlContent, tessEvalContent);
}

Program::Program(const std::string & name, const std::string & computeContent) :
	_name(name), _type(Type::COMPUTE) {
	reload(computeContent);
}

void Program::reload(const std::string & vertexContent, const std::string & fragmentContent, const std::string & tessControlContent, const std::string & tessEvalContent) {
	if(_type != Type::GRAPHICS){
		Log::error("GPU: %s is not a graphics program.", _name.c_str());
		return;
	}

	clean();
	_reloaded = true;
	
	const std::string debugName = _name;
	GPU::createGraphicsProgram(*this, vertexContent, fragmentContent, tessControlContent, tessEvalContent, debugName);
	reflect();
}

void Program::reload(const std::string & computeContent) {
	if(_type != Type::COMPUTE){
		Log::error("GPU: %s is not a compute program.", _name.c_str());
		return;
	}
	clean();
	_reloaded = true;

	const std::string debugName = _name;
	GPU::createComputeProgram(*this, computeContent, debugName);
	reflect();

}

void Program::reflect(){
	// Reflection information has been populated. Merge uniform infos, build descriptor layout, prepare descriptors.

#ifdef LOG_REFLECTION

	static const std::unordered_map<Program::UniformDef::Type, std::string> typeNames = {
		{ Program::UniformDef::Type::BOOL, "BOOL" },
		{ Program::UniformDef::Type::BVEC2, "BVEC2" },
		{ Program::UniformDef::Type::BVEC3, "BVEC3" },
		{ Program::UniformDef::Type::BVEC4, "BVEC4" },
		{ Program::UniformDef::Type::INT, "INT" },
		{ Program::UniformDef::Type::IVEC2, "IVEC2" },
		{ Program::UniformDef::Type::IVEC3, "IVEC3" },
		{ Program::UniformDef::Type::IVEC4, "IVEC4" },
		{ Program::UniformDef::Type::UINT, "UINT" },
		{ Program::UniformDef::Type::UVEC2, "UVEC2" },
		{ Program::UniformDef::Type::UVEC3, "UVEC3" },
		{ Program::UniformDef::Type::UVEC4, "UVEC4" },
		{ Program::UniformDef::Type::FLOAT, "FLOAT" },
		{ Program::UniformDef::Type::VEC2, "VEC2" },
		{ Program::UniformDef::Type::VEC3, "VEC3" },
		{ Program::UniformDef::Type::VEC4, "VEC4" },
		{ Program::UniformDef::Type::MAT2, "MAT2" },
		{ Program::UniformDef::Type::MAT3, "MAT3" },
		{ Program::UniformDef::Type::MAT4, "MAT4" },
		{ Program::UniformDef::Type::OTHER, "OTHER" },
	};

	Log::info("-- Reflection: ----");

	uint id = 0;
	for(const auto& stage : _stages){

		Log::info( "Stage: %u", id);
		for(const auto& buffer : stage.buffers){
			Log::info( "* Buffer %s: (%u, %u)", buffer.name.c_str(), buffer.set, buffer.binding);
			for(const auto& member : buffer.members){
				const auto& loc = member.locations[0];
				Log::info( "\t%s at (%u) off %u of type %s", member.name.c_str(), loc.binding, loc.offset, typeNames.at(member.type).c_str());
			}
		}

		for(const auto& image : stage.images){
			Log::info( "* Image %s at (%u, %u)", image.name.c_str(), image.set, image.binding);
		}
		++id;
	}

#endif

	// Merge all uniforms
	uint stageIndex = 0;
	for(const auto& stage : _stages){
		for(const auto& buffer : stage.buffers){
			const uint set = buffer.set;

			// We only internally manage dynamic UBOs, in set UNIFORMS_SET.
			//if(set != UNIFORMS_SET)
			{
				// The other buffer set is just initialized.
				if(set != BUFFERS_SET){
					Log::error( "Low frequency UBOs should be in set %u, skipping.", BUFFERS_SET);
					continue;
				}
				// Detect and skip push constants.
				if(buffer.binding == 0 && buffer.name.empty()){
					continue;
				}
				if(_staticBuffers.count(buffer.binding) != 0){
					if(_staticBuffers.at(buffer.binding).name != buffer.name){
						Log::warning("GPU: Program %s: Buffer already created, collision between stages for set %u at binding %u.", name().c_str(), buffer.set, buffer.binding);
						continue;
					}
				}

				_staticBuffers[buffer.binding] = StaticBufferState();
				_staticBuffers[buffer.binding].name = buffer.name;
				_staticBuffers[buffer.binding].storage = buffer.storage;
				_staticBuffers[buffer.binding].count = buffer.count;
				_staticBuffers[buffer.binding].buffers.resize(buffer.count, VK_NULL_HANDLE);
				_staticBuffers[buffer.binding].offsets.resize(buffer.count, 0);
				continue;
			}

			// Add uniforms to look-up table.
			for(const auto& uniform : buffer.members){
				auto uniDef = _uniforms.find(uniform.name);
				if(uniDef == _uniforms.end()){
					_uniforms[uniform.name] = uniform;
				} else {
					uniDef->second.locations.emplace_back(uniform.locations[0]);
				}
			}
		}

		for(const auto& image : stage.images){
			const uint set = image.set;

			// Special case for bindless texture table.
			if(set == BINDLESS_SET){
				_useBindless = true;
				continue;
			}

			if(set != IMAGES_SET){
				Log::error( "Program %s: : Image should be in set %u only, ignoring.", name().c_str(), IMAGES_SET);
				continue;
			}

			if(_textures.count(image.binding) != 0){
				if(_textures.at(image.binding).name != image.name){
					Log::warning("GPU: Program %s: Image already created, collision between stages for set %u at binding %u.", name().c_str(), image.set, image.binding);
					continue;
				}
			}
			const Texture* defaultTex = GPU::getDefaultTexture(image.shape);

			_textures[image.binding] = TextureState();
			_textures[image.binding].name = image.name;
			_textures[image.binding].count = image.count;
			_textures[image.binding].shape = image.shape;
			_textures[image.binding].storage = image.storage;
			_textures[image.binding].textures.resize(image.count);
			_textures[image.binding].views.resize(image.count);
			for(uint tid = 0; tid < image.count; ++tid){
				_textures[image.binding].textures[tid] = defaultTex;
				_textures[image.binding].views[tid] = _textures[image.binding].textures[tid]->gpu->view;
			}
			_textures[image.binding].mip = Program::ALL_MIPS;
		}

		// Push constants
		static const std::unordered_map<ShaderType, VkShaderStageFlagBits> stageBits = {
			{ShaderType::VERTEX, VK_SHADER_STAGE_VERTEX_BIT},
			{ShaderType::FRAGMENT, VK_SHADER_STAGE_FRAGMENT_BIT},
			{ShaderType::TESSCONTROL, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT},
			{ShaderType::TESSEVAL, VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT},
			{ShaderType::COMPUTE, VK_SHADER_STAGE_COMPUTE_BIT},
		};

		if(stage.pushConstants.size > 0){
			_pushConstants.size = std::max(stage.pushConstants.size, _pushConstants.size);
			_pushConstants.mask = _pushConstants.mask | stageBits.at(ShaderType(stageIndex));
		}
		++stageIndex;
	}

	GPUContext* context = GPU::getInternal();

	_dirtySets.fill(false);
	if(!_textures.empty()){
		_dirtySets[IMAGES_SET] = true;
	}

	_state.setLayouts.resize(_currentSets.size());

	// Textures.
	{
		std::vector<VkDescriptorSetLayoutBinding> bindingLayouts;
		for(const auto& image : _textures){
			VkDescriptorSetLayoutBinding imageBinding{};
			imageBinding.binding = image.first;
			imageBinding.descriptorType = image.second.storage ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
			imageBinding.descriptorCount = image.second.count;
			imageBinding.stageFlags = VK_SHADER_STAGE_ALL;
			bindingLayouts.emplace_back(imageBinding);
		}

		VkDescriptorSetLayoutCreateInfo setInfo{};
		setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		setInfo.flags = 0;
		setInfo.pBindings = bindingLayouts.data();
		setInfo.bindingCount = uint32_t(bindingLayouts.size());

		if(vkCreateDescriptorSetLayout(context->device, &setInfo, nullptr, &_state.setLayouts[IMAGES_SET]) != VK_SUCCESS){
			Log::error("GPU: Unable to create set layout.");
		}
	}

	// Static buffers.
	{
		std::vector<VkDescriptorSetLayoutBinding> bindingLayouts;
		for(const auto& buffer : _staticBuffers){
			VkDescriptorSetLayoutBinding bufferBinding{};
			bufferBinding.binding = buffer.first;
			bufferBinding.descriptorType = buffer.second.storage ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			bufferBinding.descriptorCount = buffer.second.count;
			bufferBinding.stageFlags = VK_SHADER_STAGE_ALL;
			bindingLayouts.emplace_back(bufferBinding);
		}

		VkDescriptorSetLayoutCreateInfo setInfo{};
		setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		setInfo.flags = 0;
		setInfo.pBindings = bindingLayouts.data();
		setInfo.bindingCount = uint32_t(bindingLayouts.size());

		if(vkCreateDescriptorSetLayout(context->device, &setInfo, nullptr, &_state.setLayouts[BUFFERS_SET]) != VK_SUCCESS){
			Log::error("GPU: Unable to create set layout.");
		}
	}

	// Samplers
	{
		_state.setLayouts[SAMPLERS_SET] = context->samplerLibrary.getLayout();
	}

	uint32_t layoutCount = _state.setLayouts.size()-1;
	// Bindless
	if(_useBindless){
		_state.setLayouts[BINDLESS_SET] = context->textureLibrary.getLayout();
		++layoutCount;
	} // else null handle or empty layout?

	VkPipelineLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount = layoutCount;
	layoutInfo.pSetLayouts = _state.setLayouts.data();
	layoutInfo.pushConstantRangeCount = 0;
	layoutInfo.pPushConstantRanges = nullptr;

	_state.pushConstantsStages = 0;

	// Push constants
	VkPushConstantRange pushConstantRange = {};
	if(_pushConstants.size > 0){
		_state.pushConstantsStages = _pushConstants.mask;
		pushConstantRange.size = _pushConstants.size;
		pushConstantRange.offset = 0;
		pushConstantRange.stageFlags = _pushConstants.mask;
		layoutInfo.pushConstantRangeCount = 1;
		layoutInfo.pPushConstantRanges = &pushConstantRange;
	}

	if(vkCreatePipelineLayout(context->device, &layoutInfo, nullptr, &_state.layout) != VK_SUCCESS){
		Log::error("GPU: Unable to create pipeline layout.");
	}

}

void Program::transitionResourcesTo(Program::Type type){
	VkCommandBuffer& commandBuffer = GPU::getInternal()->getRenderCommandBuffer();

	if(type == Type::GRAPHICS){
		// We need to ensure all images are in the shader read only optimal layout,
		// and that all writes to buffers are complete, whether we will use them
		// as index/verte/uniform/storage buffers.
		
		VkMemoryBarrier memoryBarrier = {};
		memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
		const VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, dstStage, 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr );

		// Move all textures to shader read only optimal.
		for(const auto& texInfos : _textures){
			// Transition proper subresource.
			const uint mip = texInfos.second.mip;
			const VkImageLayout tgtLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			for(const Texture* tex : texInfos.second.textures){
				if(mip == Program::ALL_MIPS){
					VkUtils::textureLayoutBarrier(commandBuffer, *tex, tgtLayout);
				} else {
					VkUtils::mipLayoutBarrier(commandBuffer, *tex, tgtLayout, mip);
				}
			}
		}
	} else if(type == Type::COMPUTE){

		// For buffers, two possible cases.
		// Either it was last used in a graphics program, and can't have been modified.
		// Or it was used in a previous compute pass, and a memory barrier should be enough.
		VkMemoryBarrier memoryBarrier = {};
		memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT ;
		const VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, dstStage, 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr );

		// Move sampled textures to shader read only optimal, and storage to general layout.
		// \warning We might be missing some masks in the layout barrier when moving from a compute to a compute, investigate.
		for(const auto& texInfos : _textures){
			// Transition proper subresource.
			const uint mip = texInfos.second.mip;
			const VkImageLayout tgtLayout = texInfos.second.storage ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			for(const Texture* tex : texInfos.second.textures){
				if(mip == Program::ALL_MIPS){
					VkUtils::textureLayoutBarrier(commandBuffer, *tex, tgtLayout);
				} else {
					VkUtils::mipLayoutBarrier(commandBuffer, *tex, tgtLayout, mip);
				}
			}
		}

	}

}

void Program::update(){
	GPUContext* context = GPU::getInternal();

	// Update the texture descriptors
	if(_dirtySets[IMAGES_SET]){

		// We can't just update the current descriptor set as it might be in use.
		context->descriptorAllocator.freeSet(_currentSets[IMAGES_SET]);
		_currentSets[IMAGES_SET] = context->descriptorAllocator.allocateSet(_state.setLayouts[IMAGES_SET]);

		std::vector<std::vector<VkDescriptorImageInfo>> imageInfos(_textures.size());
		std::vector<VkWriteDescriptorSet> writes;
		uint tid = 0;
		for(const auto& image : _textures){
			imageInfos[tid].resize(image.second.count);
			const VkImageLayout tgtLayout = image.second.storage ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			for(uint did = 0; did < image.second.count; ++did){
				imageInfos[tid][did].imageView = image.second.views[did];
				imageInfos[tid][did].imageLayout = tgtLayout;
			}

			VkWriteDescriptorSet write{};
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.dstSet = _currentSets[IMAGES_SET].handle;
			write.dstBinding = image.first;
			write.dstArrayElement = 0;
			write.descriptorCount = image.second.count;
			write.descriptorType = image.second.storage ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
			write.pImageInfo = &imageInfos[tid][0];
			writes.push_back(write);
			++tid;
		}

		vkUpdateDescriptorSets(context->device, uint32_t(writes.size()), writes.data(), 0, nullptr);
		_dirtySets[IMAGES_SET] = false;
	}

	// Update static buffer descriptors.
	if(_dirtySets[BUFFERS_SET]){
		// We can't just update the current descriptor set as it might be in use.
		context->descriptorAllocator.freeSet(_currentSets[BUFFERS_SET]);
		_currentSets[BUFFERS_SET] = context->descriptorAllocator.allocateSet(_state.setLayouts[BUFFERS_SET]);

		std::vector<std::vector<VkDescriptorBufferInfo>> infos(_staticBuffers.size());
		std::vector<VkWriteDescriptorSet> writes;
		uint tid = 0;
		for(const auto& buffer : _staticBuffers){
			infos[tid].resize(buffer.second.count);
			/// \todo Should we use the real buffer current offset here, if an update happened under the hood more than once per frame?
			for(uint did = 0; did < buffer.second.count; ++did){
				VkBuffer rawBuffer = buffer.second.buffers[did];
				if(rawBuffer != VK_NULL_HANDLE){
					infos[tid][did].buffer = rawBuffer;
					infos[tid][did].offset = buffer.second.offsets[did];
				} else {
					infos[tid][did].buffer = buffer.second.buffers[buffer.second.lastSet];
					infos[tid][did].offset = buffer.second.offsets[buffer.second.lastSet];
				}

				infos[tid][did].range = buffer.second.size;
			}
			
			VkWriteDescriptorSet write{};
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.dstSet = _currentSets[BUFFERS_SET].handle;
			write.dstBinding = buffer.first;
			write.dstArrayElement = 0;
			write.descriptorCount = buffer.second.count;
			write.descriptorType = buffer.second.storage ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			write.pBufferInfo = &infos[tid][0];
			writes.push_back(write);
			++tid;
		}

		vkUpdateDescriptorSets(context->device, uint32_t(writes.size()), writes.data(), 0, nullptr);
		_dirtySets[BUFFERS_SET] = false;
	}

	// Bind the descriptor sets.
	const VkPipelineBindPoint bindPoint = _type == Type::COMPUTE ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;
	VkCommandBuffer& commandBuffer = context->getRenderCommandBuffer();

	// Bind static samplers dummy set SAMPLERS_SET.
	const VkDescriptorSet samplersHandle = context->samplerLibrary.getSetHandle();
	vkCmdBindDescriptorSets(commandBuffer, bindPoint, _state.layout, SAMPLERS_SET, 1, &samplersHandle, 0, nullptr);

	// Other sets are bound if present.
	if(_currentSets[IMAGES_SET].handle != VK_NULL_HANDLE){
		vkCmdBindDescriptorSets(commandBuffer, bindPoint, _state.layout, IMAGES_SET, 1, &_currentSets[IMAGES_SET].handle, 0, nullptr);
	}
	if(_currentSets[BUFFERS_SET].handle != VK_NULL_HANDLE){
		vkCmdBindDescriptorSets(commandBuffer, bindPoint, _state.layout, BUFFERS_SET, 1, &_currentSets[BUFFERS_SET].handle, 0, nullptr);
	}

	if(_useBindless){
		const VkDescriptorSet texturesHandle = context->textureLibrary.getSetHandle();
		vkCmdBindDescriptorSets(commandBuffer, bindPoint, _state.layout, BINDLESS_SET, 1, &texturesHandle, 0, nullptr);
	}

}

bool Program::reloaded() const {
	return _reloaded;
}

bool Program::reloaded(bool absorb){
	const bool wasReloaded = _reloaded;
	if(absorb){
		_reloaded = false;
	}
	return wasReloaded;
}

void Program::use() const {
	GPU::bindProgram(*this);
}

void Program::clean() {
	
	GPU::clean(*this);
	// Clear CPU infos.
	_uniforms.clear();
	_textures.clear();
	_pushConstants.clear();
	//_dynamicBuffers.clear();
	_staticBuffers.clear();
	_useBindless = false;
	_state.setLayouts.clear();
	_state.layout = VK_NULL_HANDLE;
	_dirtySets.fill(false);

	for(uint i = 0; i < _currentSets.size(); ++i){
		// Skip the shared static samplers set.
		if(i == SAMPLERS_SET || i == BINDLESS_SET){
			continue;
		}
		GPU::getInternal()->descriptorAllocator.freeSet(_currentSets[i]);
		_currentSets[i].handle = VK_NULL_HANDLE;
		_currentSets[i].pool = 0;
	}
	_currentOffsets.clear();
}

void Program::buffer(const UniformBufferBase& buffer, uint slot){
	auto existingBuff = _staticBuffers.find(slot);
	if(existingBuff != _staticBuffers.end()) {
		StaticBufferState& refBuff = existingBuff->second;
		assert(refBuff.count == 1);
		if((refBuff.buffers[0] != buffer.gpu->buffer) || (refBuff.offsets[0] != buffer.currentOffset()) || (refBuff.size != buffer.baseSize())){
			refBuff.buffers[0] = buffer.gpu->buffer;
			refBuff.offsets[0] = uint(buffer.currentOffset());
			refBuff.size = uint(buffer.baseSize());
			_dirtySets[BUFFERS_SET] = true;
		}
	}
}

void Program::buffer(const Buffer& buffer, uint slot){
	auto existingBuff = _staticBuffers.find(slot);
	if(existingBuff != _staticBuffers.end()) {
		StaticBufferState& refBuff = existingBuff->second;
		assert(refBuff.count == 1);
		if((refBuff.buffers[0] != buffer.gpu->buffer) || (refBuff.size != buffer.sizeInBytes())){
			refBuff.buffers[0] = buffer.gpu->buffer;
			refBuff.offsets[0] = 0;
			refBuff.size = uint(buffer.sizeInBytes());
			_dirtySets[BUFFERS_SET] = true;
		}
	}
}

void Program::bufferArray(const std::vector<const Buffer * >& buffers, uint slot){
	auto existingBuff = _staticBuffers.find(slot);
	if(existingBuff != _staticBuffers.end()) {
		StaticBufferState& refBuff = existingBuff->second;
		const uint buffCount = buffers.size();
		assert(buffCount <= refBuff.count);

		for(uint did = 0; did < buffCount; ++did){
			const Buffer& buffer = *buffers[did];
			if((refBuff.buffers[did] != buffer.gpu->buffer) || (refBuff.size != buffer.sizeInBytes())){
				refBuff.buffers[did] = buffer.gpu->buffer;
				refBuff.offsets[did] = 0;
				refBuff.size = uint(buffer.sizeInBytes());
				refBuff.lastSet = did;
				_dirtySets[BUFFERS_SET] = true;
			}
		}
	}
}

void Program::texture(const Texture& texture, uint slot, uint mip){
	auto existingTex = _textures.find(slot);
	if(existingTex != _textures.end()) {
		TextureState & refTex = existingTex->second;
		assert(refTex.count == 1);
		// Find the view we need.
		assert(mip == Program::ALL_MIPS || mip < texture.gpu->views.size());
		VkImageView& view = mip == Program::ALL_MIPS ? texture.gpu->view : texture.gpu->views[mip].mipView;

		if(refTex.views[0] != view){
			refTex.textures[0] = &texture;
			refTex.views[0] = view;
			refTex.mip = mip;
			_dirtySets[IMAGES_SET] = true;
		}
	}
}

void Program::textureArray(const std::vector<const Texture *> & textures, uint slot, uint mip){
	auto existingTex = _textures.find(slot);
	if(existingTex != _textures.end()) {
		TextureState & refTex = existingTex->second;
		const uint texCount = textures.size();
		assert(texCount <= refTex.count);

		for(uint did = 0; did < texCount; ++did){
			// Find the view we need.
			assert(mip == Program::ALL_MIPS || mip < textures[did]->gpu->views.size());

			VkImageView& view = mip == Program::ALL_MIPS ? textures[did]->gpu->view : textures[did]->gpu->views[mip].mipView;
			if(refTex.views[did] != view){
				refTex.textures[did] = textures[did];
				refTex.views[did] = view;
				refTex.mip = mip;
				_dirtySets[IMAGES_SET] = true;
			}
		}
	}
}

void Program::texture(const Texture* texture, uint slot, uint mip){
	Program::texture(*texture, slot, mip);
}

void Program::textures(const std::vector<const Texture *> & textures, size_t startingSlot){
	const uint texCount = uint(textures.size());
	for(uint tid = 0; tid < texCount; ++tid){
		const uint slot = uint(startingSlot) + tid;
		texture(*textures[tid], slot);
	}
}

const glm::uvec3 & Program::size() const {
	return _stages[uint(ShaderType::COMPUTE)].size;
}

void Program::Stage::reset(){
	images.clear();
	buffers.clear();
	pushConstants.clear();
	module = VK_NULL_HANDLE;
}

void Program::updateUniformMetric() const {
#define UDPATE_METRICS
#ifdef UDPATE_METRICS
	//GPU::_metrics.uniforms += 1;
#endif
}
