#include "graphics/DescriptorAllocator.hpp"
#include "graphics/GPUInternal.hpp"

#define DEFAULT_SET_COUNT 1000

void DescriptorAllocator::init(GPUContext* context, uint poolCount){
	_context = context;
	_currentPoolCount = 0;
	_maxPoolCount = poolCount;
	
	_pools.push_back(createPool(DEFAULT_SET_COUNT, false, false));
	_imguiPool = createPool(DEFAULT_SET_COUNT, true, false);
	_bindlessPool = createPool(4 * BINDLESS_SET_MAX_SIZE, false, true);
}

DescriptorSet DescriptorAllocator::allocateSet(VkDescriptorSetLayout& setLayout){

	VkDescriptorSetAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &setLayout;

	DescriptorSet set;

	// Attempt to allocate from the current pool.
	DescriptorPool& currentPool = _pools.back();
	allocInfo.descriptorPool = currentPool.handle;
	if(vkAllocateDescriptorSets(_context->device, &allocInfo, &set.handle) == VK_SUCCESS) {
		// Success.
		currentPool.allocated += 1;
		currentPool.lastFrame = _context->frameIndex;
		set.pool = currentPool.id;
		return set;
	}

	// Else, try to find an existing pool where all sets have been freed.
	bool found = false;
	for(auto poolIt = _pools.begin(); poolIt != _pools.end(); ++poolIt){
		if(poolIt->allocated == 0 && (poolIt->lastFrame + 2 < _context->frameIndex)){
			// Copy the pool infos.
			DescriptorPool pool = DescriptorPool(*poolIt);
			VK_RET(vkResetDescriptorPool(_context->device, pool.handle, 0));
			_pools.erase(poolIt);
			_pools.push_back(pool);
			found = true;
			break;
		}
	}
	// Finally, if all pools are in use, create a new one.
	if(!found){
		DescriptorPool newPool = createPool(DEFAULT_SET_COUNT, false, false);
		_pools.push_back(newPool);
	}

	// Try to allocate in reused/new pool
	DescriptorPool& newPool = _pools.back();
	allocInfo.descriptorPool = newPool.handle;
	if(vkAllocateDescriptorSets(_context->device, &allocInfo, &set.handle) == VK_SUCCESS) {
		// Success.
		newPool.allocated += 1;
		newPool.lastFrame = _context->frameIndex;
		set.pool = newPool.id;
		return set;
	}

	Log::error( "Allocation failed.");
	return DescriptorSet();

}


DescriptorSet DescriptorAllocator::allocateBindlessSet(VkDescriptorSetLayout& setLayout){
	VkDescriptorSetAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &setLayout;
	allocInfo.descriptorPool = _bindlessPool.handle;

	VkDescriptorSetVariableDescriptorCountAllocateInfoEXT countInfos{};
	countInfos.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT;
	countInfos.descriptorSetCount = 1;
	uint32_t maxBinding = BINDLESS_SET_MAX_SIZE;
	countInfos.pDescriptorCounts = &maxBinding;
	allocInfo.pNext = &countInfos;

	DescriptorSet set;
	if(vkAllocateDescriptorSets(_context->device, &allocInfo, &set.handle) == VK_SUCCESS) {
		// Success.
		_bindlessPool.allocated += 1;
		_bindlessPool.lastFrame = _context->frameIndex;
		set.pool = _bindlessPool.id;
		return set;
	}

	Log::error( "Allocation failed.");
	return DescriptorSet();
}

void DescriptorAllocator::freeSet(const DescriptorSet& set){
	// Set was never allocated.
	if(set.handle == VK_NULL_HANDLE){
		return;
	}

	for(auto& pool : _pools){
		if(pool.id == set.pool){
#ifdef DEBUG
			if(pool.allocated == 0){
				Log::error( "A descriptor set has probably been double-freed.");
				return;
			}
#endif
			pool.allocated -= 1;
			pool.lastFrame = _context->frameIndex;
		}
	}

	if(set.pool == _bindlessPool.id){
		_bindlessPool.allocated -= 1;
		_bindlessPool.lastFrame = _context->frameIndex;
	}
}

void DescriptorAllocator::clean(){
	for(auto& pool : _pools){
		vkDestroyDescriptorPool(_context->device, pool.handle, nullptr);
	}
	_pools.clear();
	vkDestroyDescriptorPool(_context->device, _imguiPool.handle, nullptr);
	_imguiPool.handle = VK_NULL_HANDLE;
	vkDestroyDescriptorPool(_context->device, _bindlessPool.handle, nullptr);
	_bindlessPool.handle = VK_NULL_HANDLE;
}


DescriptorAllocator::DescriptorPool DescriptorAllocator::createPool(uint count, bool combinedImages, bool imagesOnly){
	if(_currentPoolCount > _maxPoolCount){
		return DescriptorPool();
	}
	++_currentPoolCount;

	std::vector<VkDescriptorPoolSize> poolSizes;

	if(!imagesOnly){
		poolSizes = {
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, count },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, count },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, count },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, count },
		};
	}

	if(combinedImages){
		poolSizes.push_back({ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, count });
	} else {
		if(!imagesOnly){
			poolSizes.push_back({ VK_DESCRIPTOR_TYPE_SAMPLER, count });
		}
		poolSizes.push_back({ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, count });
	}

	DescriptorPool pool;
	pool.id = _currentPoolCount - 1;
	pool.allocated = 0;
	pool.lastFrame = _context->frameIndex;

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	if(imagesOnly){
		poolInfo.flags |= VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT_EXT;
	}
	poolInfo.maxSets = uint32_t(count * poolSizes.size());
	poolInfo.poolSizeCount = uint32_t(poolSizes.size());
	poolInfo.pPoolSizes = poolSizes.data();

	if(vkCreateDescriptorPool(_context->device, &poolInfo, nullptr, &pool.handle) != VK_SUCCESS){
		return DescriptorPool();
	}
	return pool;
}
