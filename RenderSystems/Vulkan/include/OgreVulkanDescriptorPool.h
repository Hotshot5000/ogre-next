/*
-----------------------------------------------------------------------------
This source file is part of OGRE
(Object-oriented Graphics Rendering Engine)
For the latest info, see http://www.ogre3d.org

Copyright (c) 2000-2014 Torus Knot Software Ltd

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
-----------------------------------------------------------------------------
*/

#pragma once

#include <vulkan/vulkan.h>

#include <unordered_map>
#include <vector>

// #include "common/helpers.h"
// #include "common/vk_common.h"

namespace Ogre
{
class Device;
class VulkanDescriptorSetLayout;

/**
 * @brief Manages an array of fixed size VkDescriptorPool and is able to allocate descriptor sets
 */
class VulkanDescriptorPool
{
  public:
	static const uint32_t MAX_SETS_PER_POOL = 16;

	VulkanDescriptorPool(VkDevice                   device,
	               const VulkanDescriptorSetLayout &descriptor_set_layout,
	               uint32_t                   pool_size = MAX_SETS_PER_POOL);

	VulkanDescriptorPool(const VulkanDescriptorPool &) = delete;

	VulkanDescriptorPool(VulkanDescriptorPool &&) = default;

	~VulkanDescriptorPool();

	VulkanDescriptorPool &operator=(const VulkanDescriptorPool &) = delete;

	VulkanDescriptorPool &operator=(VulkanDescriptorPool &&) = delete;

	void reset();

	const VulkanDescriptorSetLayout &get_descriptor_set_layout() const;

	void set_descriptor_set_layout(const VulkanDescriptorSetLayout &set_layout);

	VkDescriptorSet allocate();

	VkResult free(VkDescriptorSet descriptor_set);

  private:
	VkDevice device;

	const VulkanDescriptorSetLayout *descriptor_set_layout{nullptr};

	// Descriptor pool size
	std::vector<VkDescriptorPoolSize> pool_sizes;

	// Number of sets to allocate for each pool
	uint32_t pool_max_sets{0};

	// Total descriptor pools created
	std::vector<VkDescriptorPool> pools;

	// Count sets for each pool
	std::vector<uint32_t> pool_sets_count;

	// Current pool index to allocate descriptor set
	uint32_t pool_index{0};

	// Map between descriptor set and pool index
	std::unordered_map<VkDescriptorSet, uint32_t> set_pool_mapping;

	// Find next pool index or create new pool
	uint32_t find_available_pool(uint32_t pool_index);
};
}        // namespace vkb
