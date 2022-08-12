#include "graphics/GPUObjects.hpp"
#include "graphics/GPU.hpp"
#include "graphics/GPUInternal.hpp"
#include "graphics/TextureLibrary.hpp"


void TextureLibrary::init(){
	GPUContext* context = GPU::getInternal();

	// Create descriptor set layout.

	VkDescriptorSetLayoutBinding binding{};
	binding.binding = 0;
	binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	binding.descriptorCount = BINDLESS_SET_MAX_SIZE;
	binding.stageFlags = VK_SHADER_STAGE_ALL;
	binding.pImmutableSamplers = nullptr;

	VkDescriptorSetLayoutCreateInfo setInfo{};
	setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	setInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT_EXT;
	setInfo.pBindings = &binding;
	setInfo.bindingCount = 1u;

	VkDescriptorBindingFlags bindlessFlags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT;
	VkDescriptorSetLayoutBindingFlagsCreateInfoEXT extendedInfo{};
	extendedInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT;
	extendedInfo.bindingCount = 1;
	extendedInfo.pBindingFlags = &bindlessFlags;

	setInfo.pNext = &extendedInfo;

	if(vkCreateDescriptorSetLayout(context->device, &setInfo, nullptr, &_layout) != VK_SUCCESS){
		Log::error("GPU: Unable to create bindless texture set layout.");
	}

	// Create descriptor set.
	_sets[0] = context->descriptorAllocator.allocateBindlessSet(_layout);
	_sets[1] = context->descriptorAllocator.allocateBindlessSet(_layout);
}

void TextureLibrary::update(const std::vector<Texture>& textures){
	const uint32_t count = std::min((uint32_t)BINDLESS_SET_MAX_SIZE, (uint32_t)textures.size());
	if(textures.size() > count){
		Log::error("Too many textures for the bindless set (%u > %u), clamping.", textures.size(), count);
	}

	std::vector<VkDescriptorImageInfo> imageInfos(count);
	std::vector<VkWriteDescriptorSet> writes(count);

	for(uint tid = 0; tid < count; ++tid){
		const auto& image = textures[tid];

		imageInfos[tid].imageView = image.gpu->view;
		imageInfos[tid].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageInfos[tid].sampler = VK_NULL_HANDLE;

		VkWriteDescriptorSet& write = writes[tid];
		write = {};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = _sets[1].handle; // Use the set which is not in use.
		write.dstBinding = 0;
		write.dstArrayElement = tid;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		write.pImageInfo = &imageInfos[tid];
		writes.push_back(write);

	}
	GPUContext* context = GPU::getInternal();
	vkUpdateDescriptorSets(context->device, uint32_t(writes.size()), writes.data(), 0, nullptr);

	// Use the new set.
	std::swap(_sets[0], _sets[1]);
}

void TextureLibrary::clean(){
	GPUContext* context = GPU::getInternal();
	context->descriptorAllocator.freeSet(_sets[0]);
	context->descriptorAllocator.freeSet(_sets[1]);
	vkDestroyDescriptorSetLayout(context->device, _layout, nullptr);
	
}


