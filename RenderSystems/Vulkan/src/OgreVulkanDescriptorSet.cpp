/* Copyright (c) 2019-2020, Arm Limited and Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 the "License";
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "OgreVulkanDescriptorSet.h"

// #include "common/logging.h"
#include "OgreVulkanDescriptorPool.h"
#include "OgreVulkanDescriptorSetLayout.h"
#include "OgreVulkanUtils.h"

// #include "device.h"

namespace Ogre
{
VulkanDescriptorSet::VulkanDescriptorSet(VkDevice                                  device,
                             VulkanDescriptorSetLayout &                     descriptor_set_layout,
                             VulkanDescriptorPool &                          descriptor_pool,
                             const BindingMap<VkDescriptorBufferInfo> &buffer_infos,
                             const BindingMap<VkDescriptorImageInfo> & image_infos) :
    device{device},
    descriptor_set_layout{descriptor_set_layout},
    descriptor_pool{descriptor_pool},
    handle{descriptor_pool.allocate()}
{
	if (!buffer_infos.empty() || !image_infos.empty())
	{
		update(buffer_infos, image_infos);
	}
}

void VulkanDescriptorSet::update(const BindingMap<VkDescriptorBufferInfo> &buffer_infos, const BindingMap<VkDescriptorImageInfo> &image_infos)
{
	this->buffer_infos = buffer_infos;
	this->image_infos  = image_infos;

	std::vector<VkWriteDescriptorSet> set_updates;

	// Iterate over all buffer bindings
	for ( const BindingMap<VkDescriptorBufferInfo>::value_type &binding_it : buffer_infos)
	{
        unsigned binding = binding_it.first;
        const std::map<unsigned, struct VkDescriptorBufferInfo> &buffer_bindings = binding_it.second;

		if ( std::unique_ptr<VkDescriptorSetLayoutBinding> binding_info = descriptor_set_layout.
            get_layout_binding( binding ) )
		{
			// Iterate over all binding buffers in array
			for ( const std::map<unsigned, struct VkDescriptorBufferInfo>::value_type &element_it : buffer_bindings)
			{
                unsigned arrayElement = element_it.first;
                const struct VkDescriptorBufferInfo &buffer_info = element_it.second;

				VkWriteDescriptorSet write_descriptor_set;
                makeVkStruct( write_descriptor_set, VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET );

				write_descriptor_set.dstBinding      = binding;
				write_descriptor_set.descriptorType  = binding_info->descriptorType;
				write_descriptor_set.pBufferInfo     = &buffer_info;
				write_descriptor_set.dstSet          = handle;
                write_descriptor_set.dstArrayElement = arrayElement;
				write_descriptor_set.descriptorCount = 1;

				set_updates.push_back(write_descriptor_set);
			}
		}
		else
		{
			// LOGE("Shader layout set does not use buffer binding at #{}", binding);
		}
	}

	// Iterate over all image bindings
	for ( const BindingMap<VkDescriptorImageInfo>::value_type &binding_it : image_infos)
	{
        unsigned binding_index = binding_it.first;
        const std::map<unsigned, struct VkDescriptorImageInfo> &binding_resources = binding_it.second;

		if ( std::unique_ptr<VkDescriptorSetLayoutBinding> binding_info = descriptor_set_layout.
            get_layout_binding( binding_index ) )
		{
			// Iterate over all binding images in array
			for ( const std::map<unsigned, struct VkDescriptorImageInfo>::value_type &element_it : binding_resources)
			{
                unsigned arrayElement = element_it.first;
                const struct VkDescriptorImageInfo &image_info = element_it.second;

				VkWriteDescriptorSet write_descriptor_set;
                makeVkStruct( write_descriptor_set, VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET );

				write_descriptor_set.dstBinding      = binding_index;
				write_descriptor_set.descriptorType  = binding_info->descriptorType;
				write_descriptor_set.pImageInfo      = &image_info;
				write_descriptor_set.dstSet          = handle;
				write_descriptor_set.dstArrayElement = arrayElement;
				write_descriptor_set.descriptorCount = 1;

				set_updates.push_back(write_descriptor_set);
			}
		}
		else
		{
			// LOGE("Shader layout set does not use image binding at #{}", binding_index);
		}
	}

	vkUpdateDescriptorSets(device, set_updates.size(), set_updates.data(), 0, nullptr);
}

VulkanDescriptorSet::VulkanDescriptorSet(VulkanDescriptorSet &&other) :
    device{other.device},
    descriptor_set_layout{other.descriptor_set_layout},
    descriptor_pool{other.descriptor_pool},
    buffer_infos{std::move(other.buffer_infos)},
    image_infos{std::move(other.image_infos)},
    handle{other.handle}
{
	other.handle = VK_NULL_HANDLE;
}

VkDescriptorSet VulkanDescriptorSet::get_handle() const
{
	return handle;
}

const VulkanDescriptorSetLayout &VulkanDescriptorSet::get_layout() const
{
	return descriptor_set_layout;
}

BindingMap<VkDescriptorBufferInfo> &VulkanDescriptorSet::get_buffer_infos()
{
	return buffer_infos;
}

BindingMap<VkDescriptorImageInfo> &VulkanDescriptorSet::get_image_infos()
{
	return image_infos;
}

}        // namespace vkb
