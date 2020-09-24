#include <VulkanWrap/Buffer.h>
#include <VulkanWrap/Context.h>
#include <VulkanWrap/MemoryAllocator.h>

namespace vkw
{
	Buffer::Buffer(uint64_t size, VkBufferUsageFlags bufferUsage, VmaMemoryUsage memoryUsage, void* pData)
	{
		VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		bufferInfo.size = size;
		bufferInfo.usage = bufferUsage;

		//if memory is GPU only, set the buffer as the destination of the staging buffer transfer
		if ((memoryUsage & VMA_MEMORY_USAGE_GPU_ONLY) != 0) 
			bufferInfo.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

		VmaAllocationCreateInfo allocCreateInfo = {};
		allocCreateInfo.usage = memoryUsage;

		VmaAllocationInfo allocInfo;
		vmaCreateBuffer(MemoryAllocator::m_VmaAllocator, &bufferInfo, &allocCreateInfo, &m_VkBuffer, &m_VmaAllocation, &allocInfo);

		m_uiSize = allocInfo.size;

		VkMemoryPropertyFlags memFlags;
		vmaGetMemoryTypeProperties(MemoryAllocator::m_VmaAllocator, allocInfo.memoryType, &memFlags);
		if ((memFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0) //CPU side memory, map it directly
		{
			void* data;
			vmaMapMemory(MemoryAllocator::m_VmaAllocator, m_VmaAllocation, &data);
			memcpy(data, pData, size);
			vmaUnmapMemory(MemoryAllocator::m_VmaAllocator, m_VmaAllocation);
		}
		else //create a cpu side buffer to map data and transfer it to final buffer
		{
			Buffer stagingBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY, pData);

			VkCommandBufferAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			allocInfo.commandPool = Context::m_VkCommandPool;
			allocInfo.commandBufferCount = 1;

			VkCommandBuffer commandBuffer;
			vkAllocateCommandBuffers(Context::m_VkDevice, &allocInfo, &commandBuffer);

			VkCommandBufferBeginInfo beginInfo{};
			beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

			vkBeginCommandBuffer(commandBuffer, &beginInfo);

			VkBufferCopy copyRegion{};
			copyRegion.srcOffset = 0; // Optional
			copyRegion.dstOffset = 0; // Optional
			copyRegion.size = size;
			vkCmdCopyBuffer(commandBuffer, stagingBuffer.GetBufferHandle(), this->GetBufferHandle(), 1, &copyRegion);

			vkEndCommandBuffer(commandBuffer);

			VkSubmitInfo submitInfo{};
			submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &commandBuffer;

			vkQueueSubmit(Context::m_VkGraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);

			vkQueueWaitIdle(Context::m_VkGraphicsQueue);

			vkFreeCommandBuffers(Context::m_VkDevice, Context::m_VkCommandPool, 1, &commandBuffer);
			//stagingBuffer.CopyTo(this);
		}
	}

	Buffer::~Buffer()
	{
		vmaDestroyBuffer(MemoryAllocator::m_VmaAllocator, m_VkBuffer, m_VmaAllocation);
	}
}