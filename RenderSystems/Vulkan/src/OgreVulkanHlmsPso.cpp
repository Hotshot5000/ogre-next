#include "OgreVulkanHlmsPso.h"

Ogre::VulkanHlmsPso::VulkanHlmsPso( VkPipeline pso, VulkanProgram *vertexShader,
                                    VulkanProgram *pixelShader,
                                    const DescriptorSetLayoutBindingArray &
                                    descriptorLayoutBindingSets,
                                    const DescriptorSetLayoutArray &descriptorSets,
                                    VkPipelineLayout layout ) :
    pso( pso ),
    vertexShader( vertexShader ),
    pixelShader( pixelShader ),
    descriptorLayoutBindingSets( descriptorLayoutBindingSets ),
    descriptorSets( descriptorSets ),
    pipelineLayout( layout )
{
    DescriptorSetLayoutBindingArray::const_iterator itor = descriptorLayoutBindingSets.begin();
    DescriptorSetLayoutBindingArray::const_iterator end = descriptorLayoutBindingSets.end();

    size_t i = 0;

    while( itor != end )
    {
        descriptorLayoutSets.emplace_back( descriptorSets[i++], * itor );
        ++itor;
    }
}

Ogre::VulkanHlmsPso::~VulkanHlmsPso()
{
    // std::vector<VulkanDescriptorSetLayout *>::iterator itor = descriptorLayoutSets.begin();
    // std::vector<VulkanDescriptorSetLayout *>::iterator end = descriptorLayoutSets.end();
    //
    // while( itor != end )
    // {
    //     delete *itor;
    //     ++itor;
    // }
}
