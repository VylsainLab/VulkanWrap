#include <VulkanWrap/Application.h>
#include <VulkanWrap/SwapChain.h>
#include <cassert>
#include <string>
#include <set>
#include <stdexcept>
#include <iostream>

#define MAX_FRAMES_IN_FLIGHT	2

namespace vkw
{
	bool Application::m_bGLFWInitialized = false;

	Application::Application(const std::string& name, uint16_t width, uint16_t height)
	{
		if (m_bGLFWInitialized == false)
		{
			bool ret = glfwInit();
			assert(ret == GLFW_TRUE);
		}

		vkw::Context::InitInstance();

		m_pWindow = std::make_unique<vkw::Window>(name,width,height);

		vkw::Context::Init(m_pWindow->GetSurface());

		m_pWindow->InitSwapChain();
	}

	Application::~Application()
	{
		for (uint8_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
		{
			vkDestroySemaphore(vkw::Context::m_VkDevice, m_vVkRenderFinishedSemaphores[i], nullptr);
			vkDestroySemaphore(vkw::Context::m_VkDevice, m_vVkImageAvailableSemaphores[i], nullptr);
			vkDestroyFence(vkw::Context::m_VkDevice, m_vVkInFlightFences[i], nullptr);
		}

		vkDestroyCommandPool(vkw::Context::m_VkDevice, m_VkCommandPool, nullptr);

		m_pWindow.reset();

		vkw::Context::Release();

		if (m_bGLFWInitialized == true)
		{
			glfwTerminate();
		}
	}

	void Application::Setup(const VkRenderPass& renderPass, const VkPipeline& graphicsPipeline)
	{
		m_pWindow->InitFramebuffers(renderPass);
		CreateCommandPool();
		CreateSwapChainCommandBuffers(renderPass, graphicsPipeline);
		CreateSyncObjects();
	}

	void Application::Run()
	{
		while (!m_pWindow->IsClosed())
		{
			glfwPollEvents();
			Render();
		}

		vkDeviceWaitIdle(vkw::Context::m_VkDevice);
	}

	void Application::Render()
	{
		vkWaitForFences(vkw::Context::m_VkDevice, 1, &m_vVkInFlightFences[m_uiCurrentFrame], VK_TRUE, UINT64_MAX);

		uint32_t uiImageIndex = 0;
		vkAcquireNextImageKHR(vkw::Context::m_VkDevice, m_pWindow->GetSwapChainHandle(), UINT64_MAX, m_vVkImageAvailableSemaphores[m_uiCurrentFrame], VK_NULL_HANDLE, &uiImageIndex);

		// Check if a previous frame is using this image (i.e. there is its fence to wait on)
		if (m_vVkImagesInFlightFences[uiImageIndex] != VK_NULL_HANDLE)
			vkWaitForFences(vkw::Context::m_VkDevice, 1, &m_vVkImagesInFlightFences[uiImageIndex], VK_TRUE, UINT64_MAX);

		// Mark the image as now being in use by this frame
		m_vVkImagesInFlightFences[uiImageIndex] = m_vVkInFlightFences[m_uiCurrentFrame];

		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		VkSemaphore waitSemaphores[] = { m_vVkImageAvailableSemaphores[m_uiCurrentFrame] };
		VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = waitSemaphores;
		submitInfo.pWaitDstStageMask = waitStages;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &m_vVkSwapChainCommandBuffers[uiImageIndex];
		VkSemaphore signalSemaphores[] = { m_vVkRenderFinishedSemaphores[m_uiCurrentFrame] };
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = signalSemaphores;

		vkResetFences(vkw::Context::m_VkDevice, 1, &m_vVkInFlightFences[m_uiCurrentFrame]);

		if (vkQueueSubmit(vkw::Context::m_VkGraphicsQueue, 1, &submitInfo, m_vVkInFlightFences[m_uiCurrentFrame]) != VK_SUCCESS)
			throw std::runtime_error("Failed to submit draw command buffer!");

		VkPresentInfoKHR presentInfo = {};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = signalSemaphores;
		VkSwapchainKHR swapChains[] = { m_pWindow->GetSwapChainHandle() };
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = swapChains;
		presentInfo.pImageIndices = &uiImageIndex;
		presentInfo.pResults = nullptr;
		vkQueuePresentKHR(vkw::Context::m_VkPresentQueue, &presentInfo);

		m_uiCurrentFrame = (m_uiCurrentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
	}

	void Application::CreateCommandPool()
	{
		vkw::QueueFamilyIndices queueFamilyIndices = vkw::Context::GetQueueFamilies(m_pWindow->GetSurface());

		VkCommandPoolCreateInfo poolInfo = {};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.queueFamilyIndex = queueFamilyIndices.uiGraphicsFamily.value();
		poolInfo.flags = 0;

		if (vkCreateCommandPool(vkw::Context::m_VkDevice, &poolInfo, nullptr, &m_VkCommandPool) != VK_SUCCESS)
			throw std::runtime_error("Failed to create command pool!");
	}

	void Application::CreateSwapChainCommandBuffers(const VkRenderPass& renderPass, const VkPipeline& graphicsPipeline)
	{
		m_vVkSwapChainCommandBuffers.resize(m_pWindow->GetSwapChainImageCount());

		VkCommandBufferAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = m_VkCommandPool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = (uint32_t)m_vVkSwapChainCommandBuffers.size();

		if (vkAllocateCommandBuffers(vkw::Context::m_VkDevice, &allocInfo, m_vVkSwapChainCommandBuffers.data()) != VK_SUCCESS)
			throw std::runtime_error("Failed to allocate command buffers!");

		for (size_t i = 0; i < m_vVkSwapChainCommandBuffers.size(); i++)
		{
			//Begin command buffer recording
			VkCommandBufferBeginInfo beginInfo = {};
			beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			beginInfo.flags = 0;
			beginInfo.pInheritanceInfo = nullptr;

			if (vkBeginCommandBuffer(m_vVkSwapChainCommandBuffers[i], &beginInfo) != VK_SUCCESS)
				throw std::runtime_error("Failed to begin recording command buffer!");

			//Render pass
			VkRenderPassBeginInfo renderPassInfo = {};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			renderPassInfo.renderPass = renderPass;
			renderPassInfo.framebuffer = m_pWindow->GetSwapChainFramebuffer(i);
			renderPassInfo.renderArea.offset = { 0, 0 };
			renderPassInfo.renderArea.extent = m_pWindow->GetSwapChainExtent();
			VkClearValue clearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
			renderPassInfo.clearValueCount = 1;
			renderPassInfo.pClearValues = &clearColor;
			vkCmdBeginRenderPass(m_vVkSwapChainCommandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

			vkCmdBindPipeline(m_vVkSwapChainCommandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

			vkCmdDraw(m_vVkSwapChainCommandBuffers[i], 3, 1, 0, 0);

			vkCmdEndRenderPass(m_vVkSwapChainCommandBuffers[i]);

			if (vkEndCommandBuffer(m_vVkSwapChainCommandBuffers[i]) != VK_SUCCESS)
				throw std::runtime_error("Failed to record command buffer!");
		}
	}

	void Application::CreateSyncObjects()
	{
		m_vVkImageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
		m_vVkRenderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
		m_vVkInFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
		m_vVkImagesInFlightFences.resize(m_pWindow->GetSwapChainImageCount(), VK_NULL_HANDLE);

		VkSemaphoreCreateInfo semaphoreInfo = {};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		VkFenceCreateInfo fenceInfo = {};
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

		for (uint8_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
		{
			if (vkCreateSemaphore(vkw::Context::m_VkDevice, &semaphoreInfo, nullptr, &m_vVkImageAvailableSemaphores[i]) != VK_SUCCESS ||
				vkCreateSemaphore(vkw::Context::m_VkDevice, &semaphoreInfo, nullptr, &m_vVkRenderFinishedSemaphores[i]) != VK_SUCCESS ||
				vkCreateFence(vkw::Context::m_VkDevice, &fenceInfo, nullptr, &m_vVkInFlightFences[i]) != VK_SUCCESS)
			{
				throw std::runtime_error("Failed to create sync objects for a frame!");
			}
		}
	}
}