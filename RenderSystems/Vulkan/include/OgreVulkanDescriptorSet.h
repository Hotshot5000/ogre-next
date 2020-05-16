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

// #include "common/helpers.h"
#include "OgreVulkanUtils2.h"

namespace Ogre
{
class Device;
class VulkanDescriptorSetLayout;
class VulkanDescriptorPool;

/**
 * @brief A descriptor set handle allocated from a \ref VulkanDescriptorPool.
 *        Destroying the handle has no effect, as the pool manages the lifecycle of its descriptor sets.
 */
class VulkanDescriptorSet
{
  public:
	VulkanDescriptorSet(VkDevice                                   device,
	              VulkanDescriptorSetLayout &                     descriptor_set_layout,
	              VulkanDescriptorPool &                          descriptor_pool,
	              const BindingMap<VkDescriptorBufferInfo> &buffer_infos = {},
	              const BindingMap<VkDescriptorImageInfo> & image_infos  = {});

	VulkanDescriptorSet(const VulkanDescriptorSet &) = delete;

	VulkanDescriptorSet(VulkanDescriptorSet &&other);

	// The descriptor set handle will be destroyed when the pool is reset
	~VulkanDescriptorSet() = default;

	VulkanDescriptorSet &operator=(const VulkanDescriptorSet &) = delete;

	VulkanDescriptorSet &operator=(VulkanDescriptorSet &&) = delete;

	void update(const BindingMap<VkDescriptorBufferInfo> &buffer_infos,
	            const BindingMap<VkDescriptorImageInfo> & image_infos);

	const VulkanDescriptorSetLayout &get_layout() const;

	VkDescriptorSet get_handle() const;

	BindingMap<VkDescriptorBufferInfo> &get_buffer_infos();

	BindingMap<VkDescriptorImageInfo> &get_image_infos();

  private:
	VkDevice device;

	VulkanDescriptorSetLayout &descriptor_set_layout;

	VulkanDescriptorPool &descriptor_pool;

	BindingMap<VkDescriptorBufferInfo> buffer_infos;

	BindingMap<VkDescriptorImageInfo> image_infos;

	VkDescriptorSet handle{VK_NULL_HANDLE};
};
}        // namespace vkb
