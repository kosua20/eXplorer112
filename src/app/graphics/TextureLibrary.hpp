#pragma once

#include "core/Common.hpp"
#include "graphics/GPUTypes.hpp"
#include "graphics/GPUObjects.hpp"
#include <array>

class TextureLibrary {
public:

	/** Initialize the lib. */
	void init();

	void update(const std::vector<Texture>& textures);

	/** Clean the lib */
	void clean();

	/** \return the  descriptor set layout */
	VkDescriptorSetLayout getLayout() const { return _layout; }

	/** \return the  descriptor set */
	VkDescriptorSet getSetHandle() const { return _sets[0].handle; }

private:

	VkDescriptorSetLayout _layout; ///< Samplers descriptor set layout.
	std::array<DescriptorSet, 2> _sets; ///< Samplers descriptor set allocation.
};
