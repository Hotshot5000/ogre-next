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

#include <memory>
#include <unordered_map>
#include <vector>

#include <assert.h>

#include "OgreFastArray.h"


// #include "common/helpers.h"
// #include "common/vk_common.h"

namespace Ogre
{
class VulkanDescriptorPool;
class Device;

struct ShaderResource;

/**
 * @brief Caches VulkanDescriptorSet objects for the shader's set index.
 *        Creates a VulkanDescriptorPool to allocate the VulkanDescriptorSet objects
 */
class VulkanDescriptorSetLayout
{
  public:
	/**
	 * @brief Creates a descriptor set layout from a set of resources
	 * @param device A valid Vulkan device
	 * @param resource_set A grouping of shader resources belonging to the same set
	 */
	// VulkanDescriptorSetLayout(Device &device, const std::vector<ShaderResource> &resource_set);

	// VulkanDescriptorSetLayout(const VulkanDescriptorSetLayout &) = delete;

	// VulkanDescriptorSetLayout(VulkanDescriptorSetLayout &&other);
    VulkanDescriptorSetLayout(
        VkDescriptorSetLayout descriptorSetLayout,
        const FastArray<struct VkDescriptorSetLayoutBinding> &descriptorSetLayoutBindings );

    ~VulkanDescriptorSetLayout();

	// VulkanDescriptorSetLayout &operator=(const VulkanDescriptorSetLayout &) = delete;
	//
	// VulkanDescriptorSetLayout &operator=(VulkanDescriptorSetLayout &&) = delete;

	VkDescriptorSetLayout get_handle() const;

	const std::vector<VkDescriptorSetLayoutBinding> &get_bindings() const;

	std::unique_ptr<VkDescriptorSetLayoutBinding> get_layout_binding(uint32_t binding_index) const;

	std::unique_ptr<VkDescriptorSetLayoutBinding> get_layout_binding(const std::string &name) const;

  private:
	// Device &device;

	VkDescriptorSetLayout handle{VK_NULL_HANDLE};

	std::vector<VkDescriptorSetLayoutBinding> bindings;

	std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> bindings_lookup;

	std::unordered_map<std::string, uint32_t> resources_lookup;
};
}        // namespace vkb
