#define GLFW_INCLUDE_VULKAN
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#ifdef NDEBUG
const bool g_EnableValidationlayers{ false };
#else
const bool g_EnableValidationlayers{ false };
#endif

const int g_MaxFramePerFlight{ 2 };
const int g_NumberOfMeshes{ 2 };

#include <glfw3.h>
#include <stdexcept>
#include <iostream>
#include <algorithm>
#include <iomanip>
#include <cstring>
#include <set>
#include <glm.hpp>
#include <gtc/matrix_transform.hpp>
#include <chrono>
#include <functional>

#include "Application.h"
#include "HelperFunctions.h"
#include "Mesh.h"
#include "Texture.h"
#include "Camera.h"

void GlobalKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	static_cast<Application*>(glfwGetWindowUserPointer(window))->KeyCallback(window, key, scancode, action, mods);
}

Application::Application(int width, int height) :
	m_Width{ width },
	m_Height{ height },
	m_Window{ nullptr },
	m_Instance{},
	m_DebugMessenger{},
	m_Surface{},
	m_InstanceValidationLayerNames{ "VK_LAYER_KHRONOS_validation" },	// Since we are already checking for presentation queue family this will also be checked
	m_InstanceExtensionNames{},
	m_PhysicalDeviceExtensionNames{ VK_KHR_SWAPCHAIN_EXTENSION_NAME },
	m_PhysicalDevice{ VK_NULL_HANDLE },
	m_Device{ VK_NULL_HANDLE },
	m_GrahicsQueue{ VK_NULL_HANDLE },
	m_PresentQueue{ VK_NULL_HANDLE },
	m_SwapChainImages{},
	m_SwapChainImageViews{},
	m_VertexShader{},
	m_FragmentShader{},
	m_RenderPass{},
	m_PipeLineLayout{},
	m_PipeLine{},
	m_SwapChainFrameBuffers{},
	m_CommandPool{},
	m_CommandBuffers{},
	m_ImageAvailable{},
	m_RenderFinished{},
	m_InFlight{},
	m_CurrentFrame{},
	m_FrameBufferResized{ false },
	m_Meshes{},
	m_UniformBuffers{},
	m_UniformBufferMemories{},
	m_UniformBufferMaps{},
	m_TexturesDescriptorSetLayout{},
	m_TransformsDescriptorSetLayout{},
	m_DescriptorPool{},
	m_TexturesDescriptorSets{},
	m_TransformsDescriptorSets{},
	m_BaseColorTextures{},
	m_NormalTextures{},
	m_GlossTextures{},
	m_SpecularTextures{},
	m_TextureSampler{},
	m_DepthImage{},
	m_DepthMemory{},
	m_DepthImageView{},
	m_ColorImage{},
	m_ColorMemory{},
	m_ColorImageView{},
	m_Camera{},
	m_MSAASamples{ VK_SAMPLE_COUNT_1_BIT },
	m_PushConstants{ 0 }
{
	InitializeWindow();
	InitializeVulkan();
	InitializeMeshes();

	m_Camera = new Camera{ glm::radians(45.0f), (float(m_ImageExtend.width) / float(m_ImageExtend.height)), 0.1f, 10.0f, 2.5f };
	m_Camera->SetStartPosition(glm::vec3{ 2.83f, 2.09f, 1.41f }, 0.63f, -0.39f);

	std::cout << "--- Mesh Controls ---" << std::endl;
	std::cout << "Stop rotating mesh with R" << std::endl << std::endl;	

	std::cout << "--- Render Controls ---" << std::endl;
	std::cout << "Combined render mode with 1" << std::endl;
	std::cout << "Base color map render mode with 2" << std::endl;
	std::cout << "Normal map render mode with 3" << std::endl;
	std::cout << "Glossiness map render mode with 4" << std::endl;
	std::cout << "Specular map render mode with 5" << std::endl << std::endl;
}

Application::~Application()
{
	delete m_Camera;
	vkDestroySampler(m_Device, m_TextureSampler, nullptr);
	for (int i{}; i < g_NumberOfMeshes; ++i)
	{
		delete m_BaseColorTextures.at(i);
		delete m_NormalTextures.at(i);
		delete m_GlossTextures.at(i);
		delete m_SpecularTextures.at(i);
	}
	for (auto mesh : m_Meshes)
	{
		delete mesh;
	}
	for (int i{}; i < g_MaxFramePerFlight; ++i)
	{
		for (size_t j{}; j < m_Meshes.size(); ++j)
		{
			vkDestroyBuffer(m_Device, m_UniformBuffers.at(i).at(j), nullptr);
			vkFreeMemory(m_Device, m_UniformBufferMemories.at(i).at(j), nullptr);
		}
	}
	for (int i{}; i < g_MaxFramePerFlight; ++i)
	{
		vkDestroySemaphore(m_Device, m_ImageAvailable[i], nullptr);
		vkDestroySemaphore(m_Device, m_RenderFinished[i], nullptr);
		vkDestroyFence(m_Device, m_InFlight[i], nullptr);
	}
	vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);
	CleanupSwapChain();
	vkDestroyPipeline(m_Device, m_PipeLine, nullptr);
	vkDestroyPipelineLayout(m_Device, m_PipeLineLayout, nullptr);
	vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
	vkDestroyDescriptorSetLayout(m_Device, m_TexturesDescriptorSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_Device, m_TransformsDescriptorSetLayout, nullptr);
	vkDestroyRenderPass(m_Device, m_RenderPass, nullptr);
	vkDestroyShaderModule(m_Device, m_VertexShader, nullptr);
	vkDestroyShaderModule(m_Device, m_FragmentShader, nullptr);
	vkDestroyDevice(m_Device, nullptr);
	vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
	if (g_EnableValidationlayers) DestroyDebugUtilsMessengerEXT(m_Instance, m_DebugMessenger, nullptr);
	vkDestroyInstance(m_Instance, nullptr);
	glfwDestroyWindow(m_Window);
	glfwTerminate();
}

void Application::Run()
{
	glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

	auto lastTime{ std::chrono::high_resolution_clock::now() };
	auto currentTime{ std::chrono::high_resolution_clock::now() };

	while (!glfwWindowShouldClose(m_Window))
	{
		currentTime = std::chrono::high_resolution_clock::now();
		auto time{ std::chrono::duration_cast<std::chrono::duration<float>>(currentTime - lastTime) };

		glfwPollEvents();
		m_Camera->Update(m_Window, time);
		for (int i{}; i < g_NumberOfMeshes; ++i) m_Meshes.at(i)->Update(time);
		DrawFrame();

		lastTime = currentTime;
	}

	vkDeviceWaitIdle(m_Device);
}

void Application::InitializeMeshes()
{
	// Vehicle
	m_Meshes.push_back(new Mesh{ m_PhysicalDevice, m_Device, m_CommandPool, m_GrahicsQueue, "Models/vehicle.obj" });
	m_Meshes.at(0)->SetModelMatrix(glm::scale(glm::rotate(glm::mat4{ 1.0f }, glm::radians(-90.0f), g_WorldForward), glm::vec3{ 0.1f, 0.1f, 0.1f }));

	// Mixer
	m_Meshes.push_back(new Mesh{ m_PhysicalDevice, m_Device, m_CommandPool, m_GrahicsQueue, "Models/mixer.obj" });
	m_Meshes.at(1)->SetModelMatrix
	(
		glm::translate
		(
			glm::rotate
			(
				glm::scale
				(
					glm::mat4{ 1.0f }, glm::vec3{ 0.07f, 0.07f, 0.07f }
				),
				glm::radians(-90.0f), g_WorldForward
			),
			glm::vec3{ 0.0f, 20.0, 0.0f }
		)
	);
}

void Application::InitializeWindow()
{
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
	m_Window = glfwCreateWindow(m_Width, m_Height, "Vulkan", nullptr, nullptr);
	glfwSetWindowUserPointer(m_Window, this);
	glfwSetFramebufferSizeCallback(m_Window, &FrameBufferResizedCallback);
	glfwSetKeyCallback(m_Window, GlobalKeyCallback);
	glfwSetWindowUserPointer(m_Window, this);

	if (m_Window == nullptr) throw std::runtime_error("failed to create window!");
}

void Application::InitializeVulkan()
{
	if (!ExtensionsPresent())
	{
		throw std::runtime_error("vulkan doesn't have the required extensions for glfw!");
	}

	if (g_EnableValidationlayers)
	{
		if (!ValidationLayersPresent())
		{
			throw std::runtime_error("validation layers requested, but not available!");
		}
	}

	if (CreateVulkanInstance() != VK_SUCCESS) throw std::runtime_error("failed to create vulkan instance!");
	if (SetupDebugMessenger() != VK_SUCCESS) throw std::runtime_error("failed to setup debug messenger!");
	if (CreateSurface() != VK_SUCCESS) throw std::runtime_error("failed to create surface!");
	if (!PickPhysicalDevice()) throw std::runtime_error("Failed to find suitable gpu!");
	if (CreateLogicalDevice() != VK_SUCCESS) throw std::runtime_error("failed to create logical device!");
	RetrieveQueueHandles();
	if (CreateSwapChain() != VK_SUCCESS) throw std::runtime_error("failed to create swap chain!");
	RetrieveSwapChainImages();
	if (CreateSwapChainImageViews() != VK_SUCCESS) throw std::runtime_error("failed to create swap chain image views!");
	if (CreateRenderPass() != VK_SUCCESS) throw std::runtime_error("failed to create render pass!");
	if (CreateTexturesDescriptorSetLayout() != VK_SUCCESS) throw std::runtime_error("failed to create textures descriptor set layout!");
	if (CreateTransformsDescriptorSetLayout() != VK_SUCCESS) throw std::runtime_error("failed to create transforms descriptor set layout!");
	if (CreateGraphicsPipeline() != VK_SUCCESS) throw std::runtime_error("failed to create grahpics pipeline!");
	if (CreateCommandPool() != VK_SUCCESS) throw std::runtime_error("failed to create command pool!");
	CreateColorResources();
	CreateDepthResources();
	if (CreateSwapChainFrameBuffers() != VK_SUCCESS) throw std::runtime_error("failed to create swap chain frame buffers!");
	if (CreateCommandBuffers() != VK_SUCCESS) throw std::runtime_error("failed to create command buffer!");
	if (CreateSyncObjects() != VK_SUCCESS) throw std::runtime_error("failed to create sync objects!");
	InitializeTextures();
	CreateTextureSampler();
	if (CreateUniformBuffers() != VK_SUCCESS) throw std::runtime_error("failed to create uniform buffers!");
	if (CreateDescriptorPool() != VK_SUCCESS) throw std::runtime_error("failed to create descriptor pool!");
	if (CreateTexturesDescriptorSets() != VK_SUCCESS) throw std::runtime_error("failed to create textures descriptor sets!");
	if (CreateTransformsDescriptorSets() != VK_SUCCESS) throw std::runtime_error("failed to create transforms descriptor sets!");
}

bool Application::ExtensionsPresent()
{
	uint32_t vulkanExtensionCount{};
	vkEnumerateInstanceExtensionProperties(nullptr, &vulkanExtensionCount, nullptr);
	std::vector<VkExtensionProperties> extensions(vulkanExtensionCount);
	vkEnumerateInstanceExtensionProperties(nullptr, &vulkanExtensionCount, extensions.data());

	uint32_t glfwExtensionCount{};
	const char** glfwExtensions{ glfwGetRequiredInstanceExtensions(&glfwExtensionCount) };
	m_InstanceExtensionNames = std::vector<const char*>{ glfwExtensions, glfwExtensions + glfwExtensionCount };
	if (g_EnableValidationlayers) m_InstanceExtensionNames.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

	bool extensionsPresent{ true };
	std::cout << std::endl << "-----Instance Extensions-----" << std::endl;
	for (const auto& extensionName : m_InstanceExtensionNames)
	{
		std::cout << std::setw(40) << std::left << extensionName;

		extensions.at(0).extensionName;

		if (std::ranges::find_if(extensions,
			[&extensionName](const auto& extension) -> bool
			{
				return strcmp(extensionName, extension.extensionName) == 0;
			}
		) != extensions.end())
		{
			std::cout << std::setw(40) << std::left << "PRESENT";
		}
		else
		{
			extensionsPresent = false;
			std::cout << std::setw(40) << std::left << "NOT PRESENT";
		}

		std::cout << std::endl;
	}
	std::cout << std::endl;

	return extensionsPresent;
}

bool Application::ValidationLayersPresent()
{
	uint32_t validationLayerCount{};
	vkEnumerateInstanceLayerProperties(&validationLayerCount, nullptr);
	std::vector<VkLayerProperties> validationLayers(validationLayerCount);
	vkEnumerateInstanceLayerProperties(&validationLayerCount, validationLayers.data());

	bool validationLayersPresent{ true };
	std::cout << std::endl << "-----Instance Validation layers-----" << std::endl;
	for (const auto& validationLayerName : m_InstanceValidationLayerNames)
	{
		std::cout << std::setw(40) << std::left << validationLayerName;

		if (std::ranges::find_if(validationLayers,
			[&validationLayerName](const auto& validationLayer) -> bool
			{
				return strcmp(validationLayerName, validationLayer.layerName) == 0;
			}
		) != validationLayers.end())
		{
			std::cout << std::setw(40) << std::left << "PRESENT";
		}
		else
		{
			validationLayersPresent = false;
			std::cout << std::setw(40) << std::left << "NOT PRESENT";
		}

		std::cout << std::endl;
	}
	std::cout << std::endl;

	return validationLayersPresent;
}

VkResult Application::CreateVulkanInstance()
{
	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkApplicationInfo.html
	const VkApplicationInfo applicationInfo
	{
		VK_STRUCTURE_TYPE_APPLICATION_INFO,			// sType
		nullptr,									// pNext
		"Vulkan Tutorial",							// pApplicationName
		VK_MAKE_VERSION(1, 0, 0),					// applicationVersion
		"No Engine",								// pEngineName
		VK_MAKE_VERSION(1, 0, 0),					// engineVersion
		VK_API_VERSION_1_3							// apiVersion
	};

	VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
	if(g_EnableValidationlayers) FillDebugMessengerCreateInfo(debugCreateInfo);

	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkInstanceCreateInfo.html
	const VkInstanceCreateInfo instanceCreateInfo
	{
		VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,																// sType
		(g_EnableValidationlayers) ? &debugCreateInfo : nullptr,											// pNext
		0,																									// flags
		&applicationInfo,																					// pApplicationInfo
		(g_EnableValidationlayers) ? static_cast<uint32_t>(m_InstanceValidationLayerNames.size()) : 0,		// enabledLayerCount
		(g_EnableValidationlayers) ? m_InstanceValidationLayerNames.data() : nullptr,						// pEnabledLayerNames
		static_cast<uint32_t>(m_InstanceExtensionNames.size()),												// enabledExtensionCount
		m_InstanceExtensionNames.data()																		// pEnabledExtensionNames
	};

	return vkCreateInstance(&instanceCreateInfo, nullptr, &m_Instance);
}

VkResult Application::SetupDebugMessenger()
{
	if (!g_EnableValidationlayers) return VK_SUCCESS;

	VkDebugUtilsMessengerCreateInfoEXT createInformation{};
	FillDebugMessengerCreateInfo(createInformation);

	if (CreateDebugUtilsMessengerEXT(m_Instance, &createInformation, nullptr, &m_DebugMessenger) != VK_SUCCESS)
	{
		throw std::runtime_error("failed to set up debug messenger!");
	}

	return VK_SUCCESS;
}

VkResult Application::CreateSurface()
{
	return glfwCreateWindowSurface(m_Instance, m_Window, nullptr, &m_Surface);
}

bool Application::PickPhysicalDevice()
{
	uint32_t deviceCount{};
	vkEnumeratePhysicalDevices(m_Instance, &deviceCount, nullptr);

	if (deviceCount == 0) throw std::runtime_error("Failed to find gpu with vulkan support!");

	std::vector<VkPhysicalDevice> devices(deviceCount);
	vkEnumeratePhysicalDevices(m_Instance, &deviceCount, devices.data());

	for (auto device : devices)
	{
		if (IsPhysicalDeviceSuitable(device, m_Surface, m_PhysicalDeviceExtensionNames))
		{
			m_PhysicalDevice = device;
			m_MSAASamples = GetMaxUsableSampleCount(m_PhysicalDevice);
			break;
		}
	}

	return m_PhysicalDevice != VK_NULL_HANDLE;
}

VkResult Application::CreateLogicalDevice()
{
	QueueFamilyIndices queueFamilyIndices{ FindQueueFamilies(m_PhysicalDevice, m_Surface) };
	std::vector<VkDeviceQueueCreateInfo> queueFamailyCreateInfos{};
	std::set<uint32_t> queueFamilieIndexes{ queueFamilyIndices.GraphicsFamily.value(), queueFamilyIndices.PresentFamily.value() };

	const float queuePriority{ 1.0f };

	for (auto queueFamilyIndex : queueFamilieIndexes)
	{
		// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkDeviceQueueCreateInfo.html
		VkDeviceQueueCreateInfo queueFamiliyCreateInfo{};
		queueFamiliyCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueFamiliyCreateInfo.queueFamilyIndex = queueFamilyIndex;
		queueFamiliyCreateInfo.queueCount = 1;
		queueFamiliyCreateInfo.pQueuePriorities = &queuePriority;

		queueFamailyCreateInfos.push_back(queueFamiliyCreateInfo);
	}

	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkPhysicalDeviceFeatures.html
	VkPhysicalDeviceFeatures physicalDeviceFeatures{};
	physicalDeviceFeatures.samplerAnisotropy = VK_TRUE;
	physicalDeviceFeatures.sampleRateShading = VK_TRUE;

	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkDeviceCreateInfo.html
	VkDeviceCreateInfo deviceCreateInfo{};
	deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueFamailyCreateInfos.size());
	deviceCreateInfo.pQueueCreateInfos = queueFamailyCreateInfos.data();
	deviceCreateInfo.pEnabledFeatures = &physicalDeviceFeatures;
	deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(m_PhysicalDeviceExtensionNames.size());
	deviceCreateInfo.ppEnabledExtensionNames = m_PhysicalDeviceExtensionNames.data();
	if (g_EnableValidationlayers)
	{
		deviceCreateInfo.enabledLayerCount = static_cast<uint32_t>(m_InstanceValidationLayerNames.size());
		deviceCreateInfo.ppEnabledLayerNames = m_InstanceValidationLayerNames.data();
	}
	else
	{
		deviceCreateInfo.enabledLayerCount = 0;
	}

	return vkCreateDevice(m_PhysicalDevice, &deviceCreateInfo, nullptr, &m_Device);
}

VkResult Application::CreateSwapChain()
{
	SwapChainSupportDetails details{ QuerySwapChainSupportDetails(m_PhysicalDevice, m_Surface) };

	VkSurfaceFormatKHR surfaceFormat{ ChooseSurfaceFormat(details.Formats) };
	VkPresentModeKHR presentMode{ ChoosePresentMode(details.PresentModes) };
	VkExtent2D extend{ ChooseExtent(details.Capabilities, m_Window) };

	m_ImageExtend = extend;
	m_ImageFormat = surfaceFormat.format;

	// Set surface count to min + 1, and make sure we don't exceed max count, if max is set to 0 = no max
	uint32_t surfacesCount{ details.Capabilities.minImageCount + 1 };
	if (details.Capabilities.maxImageCount > 0 && surfacesCount > details.Capabilities.maxImageCount)
	{
		surfacesCount = details.Capabilities.maxImageCount;
	}

	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkSwapchainCreateInfoKHR.html
	VkSwapchainCreateInfoKHR swapChaincreateInfo{};
	swapChaincreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swapChaincreateInfo.surface = m_Surface;
	swapChaincreateInfo.minImageCount = surfacesCount;
	swapChaincreateInfo.imageFormat = surfaceFormat.format;
	swapChaincreateInfo.imageColorSpace = surfaceFormat.colorSpace;
	swapChaincreateInfo.imageExtent = extend;
	swapChaincreateInfo.imageArrayLayers = 1;
	swapChaincreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	QueueFamilyIndices queueFamilyIndices{ FindQueueFamilies(m_PhysicalDevice, m_Surface) };
	uint32_t rawQueueFamilyIndices[]{ queueFamilyIndices.GraphicsFamily.value(), queueFamilyIndices.PresentFamily.value() };
	if (queueFamilyIndices.GraphicsFamily != queueFamilyIndices.PresentFamily)
	{
		swapChaincreateInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		swapChaincreateInfo.queueFamilyIndexCount = 2;
		swapChaincreateInfo.pQueueFamilyIndices = rawQueueFamilyIndices;
	}
	else
	{
		swapChaincreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		swapChaincreateInfo.queueFamilyIndexCount = 0;
		swapChaincreateInfo.pQueueFamilyIndices = nullptr;

	}

	swapChaincreateInfo.preTransform = details.Capabilities.currentTransform;
	swapChaincreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	swapChaincreateInfo.presentMode = presentMode;
	swapChaincreateInfo.clipped = VK_TRUE;
	swapChaincreateInfo.oldSwapchain = VK_NULL_HANDLE;

	return vkCreateSwapchainKHR(m_Device, &swapChaincreateInfo, nullptr, &m_SwapChain);
}

void Application::RetrieveQueueHandles()
{
	QueueFamilyIndices queueFamilyIndices{ FindQueueFamilies(m_PhysicalDevice, m_Surface) };

	vkGetDeviceQueue(m_Device, queueFamilyIndices.GraphicsFamily.value(), 0, &m_GrahicsQueue);
	vkGetDeviceQueue(m_Device, queueFamilyIndices.PresentFamily.value(), 0, &m_PresentQueue);
}

void Application::RetrieveSwapChainImages()
{
	uint32_t imageCount{};
	vkGetSwapchainImagesKHR(m_Device, m_SwapChain, &imageCount, nullptr);
	m_SwapChainImages.resize(imageCount);
	vkGetSwapchainImagesKHR(m_Device, m_SwapChain, &imageCount, m_SwapChainImages.data());
}

VkResult Application::CreateSwapChainImageViews()
{
	m_SwapChainImageViews.resize(m_SwapChainImages.size());
	for (size_t i{}; i < m_SwapChainImages.size(); ++i)
	{
		m_SwapChainImageViews.at(i) = CreateImageView(m_Device, m_SwapChainImages.at(i), m_ImageFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
	}

	return VK_SUCCESS;
}

VkResult Application::CreateGraphicsPipeline()
{

	m_VertexShader = CreateShaderModule(LoadSPIRV("shaders/vert.spv"), m_Device);
	m_FragmentShader = CreateShaderModule(LoadSPIRV("shaders/frag.spv"), m_Device);

	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkPipelineShaderStageCreateInfo.html
	VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
	vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vertShaderStageInfo.module = m_VertexShader;
	vertShaderStageInfo.pName = "main";

	VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
	fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	fragShaderStageInfo.module = m_FragmentShader;
	fragShaderStageInfo.pName = "main";

	VkPipelineShaderStageCreateInfo shaderStages[2]{ vertShaderStageInfo, fragShaderStageInfo };

	const auto vertexBindingDescription{ Vertex::GetBindingDescription() };
	const auto vertexAttributeDescriptions{ Vertex::GetAttributeDescriptions() };

	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkPipelineVertexInputStateCreateInfo.html
	const VkPipelineVertexInputStateCreateInfo vertexInputStateCreateInfo
	{
		VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,		// sType
		nullptr,														// pNext
		0,																// flags
		1,																// vertexBindingDescriptionCount
		&vertexBindingDescription,										// pVertexBindingDescriptions
		static_cast<uint32_t>(vertexAttributeDescriptions.size()),		// vertexAttributeDescriptionCount
		vertexAttributeDescriptions.data()								// pVertexAttributeDescriptions
	};

	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkPipelineInputAssemblyStateCreateInfo.html
	VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	inputAssembly.primitiveRestartEnable = VK_FALSE;

	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkViewport.html
	VkViewport viewport{};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (float)m_ImageExtend.width;
	viewport.height = (float)m_ImageExtend.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkRect2D.html
	VkRect2D scissor{};
	scissor.offset = VkOffset2D{ 0, 0 };
	scissor.extent = m_ImageExtend;

	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkDynamicState.html
	std::vector<VkDynamicState> dynamicStates{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkPipelineDynamicStateCreateInfo.html
	VkPipelineDynamicStateCreateInfo dynamicState{};
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
	dynamicState.pDynamicStates = dynamicStates.data();

	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkPipelineViewportStateCreateInfo.html
	VkPipelineViewportStateCreateInfo viewportState{};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkPipelineDepthStencilStateCreateInfo.html
	const VkPipelineDepthStencilStateCreateInfo pipelineDepthStencilCreateInfo
	{
		VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,				// sType
		nullptr,																// pNext
		0,																		// flags
		VK_TRUE,																// depthTestEnable
		VK_TRUE,																// depthWriteEnable
		VK_COMPARE_OP_LESS,														// depthCompareOp
		VK_FALSE,																// depthBoundsTestEnable
		VK_FALSE,																// stencilTestEnable
		VkStencilOpState{},														// front
		VkStencilOpState{},														// back
		0.0f,																	// minDepthBounds
		1.0f																	// maxDepthBounds
	};

	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkPipelineRasterizationStateCreateInfo.html
	VkPipelineRasterizationStateCreateInfo rasterizer{};
	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.depthClampEnable = VK_FALSE;
	rasterizer.rasterizerDiscardEnable = VK_FALSE;
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.lineWidth = 1.0f;
	rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizer.depthBiasEnable = VK_FALSE;

	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkPipelineMultisampleStateCreateInfo.html
	VkPipelineMultisampleStateCreateInfo multisampling{};
	multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.sampleShadingEnable = VK_TRUE;
	multisampling.minSampleShading = 0.2f;
	multisampling.rasterizationSamples = m_MSAASamples;

	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkPipelineColorBlendAttachmentState.html
	VkPipelineColorBlendAttachmentState colorBlendAttachment{};
	colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	colorBlendAttachment.blendEnable = VK_FALSE;

	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkPipelineColorBlendStateCreateInfo.html
	VkPipelineColorBlendStateCreateInfo colorBlending{};
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.logicOpEnable = VK_FALSE;
	colorBlending.logicOp = VK_LOGIC_OP_COPY; // Optional
	colorBlending.attachmentCount = 1;
	colorBlending.pAttachments = &colorBlendAttachment;
	colorBlending.blendConstants[0] = 0.0f; // Optional
	colorBlending.blendConstants[1] = 0.0f; // Optional
	colorBlending.blendConstants[2] = 0.0f; // Optional
	colorBlending.blendConstants[3] = 0.0f; // Optional

	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkPushConstantRange.html
	const VkPushConstantRange pushConstantRange
	{
		VK_SHADER_STAGE_FRAGMENT_BIT,
		0,
		sizeof(PushConstants)
	};

	const std::array<VkDescriptorSetLayout, 2> descriptorSetLayouts{ m_TransformsDescriptorSetLayout, m_TexturesDescriptorSetLayout };	

	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkPipelineLayoutCreateInfo.html
	const VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo
	{
		VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,				// sType
		nullptr,													// pNext
		0,															// flags
		static_cast<uint32_t>(descriptorSetLayouts.size()),			// setLayoutCount
		descriptorSetLayouts.data(),								// pSetLayouts
		1,															// pushConstantRangeCount
		&pushConstantRange											// pPushConstantRanges
	};

	if (vkCreatePipelineLayout(m_Device, &pipelineLayoutCreateInfo, nullptr, &m_PipeLineLayout) != VK_SUCCESS) throw std::runtime_error("failed to create pipeline layout!");

	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkGraphicsPipelineCreateInfo.html
	const VkGraphicsPipelineCreateInfo graphicsPipelineCreateInfo
	{
		VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,		// sType
		nullptr,												// pNext
		0,														// flags
		2,														// stageCount
		shaderStages,											// pStages
		&vertexInputStateCreateInfo,							// pVertexInputState
		&inputAssembly,											// pInputAssemblyState
		nullptr,												// pTessellationState
		&viewportState,											// pViewportState
		&rasterizer,											// pRasterizationState
		&multisampling,											// pMultisampleState
		&pipelineDepthStencilCreateInfo,						// pDepthStencilState
		&colorBlending,											// pColorBlendState
		&dynamicState,											// pDynamicState
		m_PipeLineLayout,										// layout
		m_RenderPass,											// renderPass
		0,														// subpass
		nullptr,												// basePipelineHandle
		0														// basePipelineIndex
	};

	return vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &graphicsPipelineCreateInfo, nullptr, &m_PipeLine);
}

VkResult Application::CreateRenderPass()
{
	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkAttachmentDescription.html
	std::array<VkAttachmentDescription, 3> attachmentDescriptions
	{
		// color attachment
		VkAttachmentDescription
		{
			0,													// flags
			m_ImageFormat,										// format
			m_MSAASamples,										// samples
			VK_ATTACHMENT_LOAD_OP_CLEAR,						// loadOp
			VK_ATTACHMENT_STORE_OP_STORE,						// storeOp
			VK_ATTACHMENT_LOAD_OP_DONT_CARE,					// stencilLoadOp
			VK_ATTACHMENT_STORE_OP_DONT_CARE,					// stencilStoreOp
			VK_IMAGE_LAYOUT_UNDEFINED,							// initialLayout
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL			// finalLayout
		},
		// depth buffering attachment
		VkAttachmentDescription
		{
			0,
			FindDepthFormat(m_PhysicalDevice),
			m_MSAASamples,
			VK_ATTACHMENT_LOAD_OP_CLEAR,
			VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
		},
		// color resolve attachment
		VkAttachmentDescription
		{
			0,
			m_ImageFormat,
			VK_SAMPLE_COUNT_1_BIT,
			VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			VK_ATTACHMENT_STORE_OP_STORE,
			VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
		}
	};

	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkAttachmentReference.html
	const VkAttachmentReference colorAttachmentReference
	{
		0,													// attachment
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL			// layout	
	};

	const VkAttachmentReference depthAttachmentReference
	{
		1,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
	};

	const VkAttachmentReference resolveAttachmentReference
	{
		2,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	};

	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkSubpassDescription.html
	const VkSubpassDescription subpassDescription
	{
		0,											// flags
		VK_PIPELINE_BIND_POINT_GRAPHICS,			// pipelineBindPoint
		0,											// inputAttachmentCount
		nullptr,									// pInputAttachments
		1,											// colorAttachmentCount
		&colorAttachmentReference,					// pColorAttachments
		&resolveAttachmentReference,				// pResolveAttachments
		&depthAttachmentReference,					// pDepthStencilAttachment
		0,											// preserveAttachmentCount
		nullptr										// pPreserveAttachments
	};

	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkSubpassDependency.html
	const VkSubpassDependency subpassDependency
	{
		VK_SUBPASS_EXTERNAL,																			// srcSubpass
		0,																								// dstSubpass
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,		// srcStageMask	
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,		// dstStageMask	
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,			// srcAccessMask	
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,			// dstAccessMask
		0																								// dependencyFlags
	};

	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkRenderPassCreateInfo.html
	const VkRenderPassCreateInfo renderPassCreateInfo
	{
		VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,			// sType
		nullptr,											// pNext
		0,													// flags
		uint32_t(attachmentDescriptions.size()),			// attachmentCount
		attachmentDescriptions.data(),						// pAttachments
		1,													// subpassCount
		&subpassDescription,								// pSubpasses
		1,													// dependencyCount
		&subpassDependency									// pDependencies
	};

	return vkCreateRenderPass(m_Device, &renderPassCreateInfo, nullptr, &m_RenderPass);
}

VkResult Application::CreateSwapChainFrameBuffers()
{
	VkResult result{ VK_SUCCESS };

	m_SwapChainFrameBuffers.resize(m_SwapChainImageViews.size());

	for (size_t i{ 0 }; i < m_SwapChainImageViews.size(); ++i)
	{
		const std::array<VkImageView, 3> attachments{ m_ColorImageView, m_DepthImageView, m_SwapChainImageViews.at(i) };

		// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkFramebufferCreateInfo.html
		const VkFramebufferCreateInfo frameBufferCreateInfo
		{
			VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,				// sType
			nullptr,												// pNext
			0,														// flags
			m_RenderPass,											// renderPass
			uint32_t(attachments.size()),							// AttachmentCount
			attachments.data(),										// pAttachments
			m_ImageExtend.width,									// width
			m_ImageExtend.height,									// height
			1														// layers
		};

		result = vkCreateFramebuffer(m_Device, &frameBufferCreateInfo, nullptr, &m_SwapChainFrameBuffers.at(i));
		if (result != VK_SUCCESS) return result;
	}

	return result;
}

VkResult Application::CreateCommandPool()
{
	QueueFamilyIndices queueFamilyIndices{ FindQueueFamilies(m_PhysicalDevice, m_Surface) };

	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkCommandPoolCreateInfo.html
	const VkCommandPoolCreateInfo commandPoolCreateInfo
	{
		VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,				// sType
		nullptr,												// pNext
		VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,		// flags
		queueFamilyIndices.GraphicsFamily.value()				// queueFamilyIndex
	};

	return vkCreateCommandPool(m_Device, &commandPoolCreateInfo, nullptr, &m_CommandPool);
}

VkResult Application::CreateCommandBuffers()
{
	m_CommandBuffers.resize(g_MaxFramePerFlight);

	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkCommandBufferAllocateInfo.html
	const VkCommandBufferAllocateInfo commandBufferAllocationInfo
	{
		VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,			// sType
		nullptr,												// pNext
		m_CommandPool,											// commandPool
		VK_COMMAND_BUFFER_LEVEL_PRIMARY,						// level
		static_cast<uint32_t>(m_CommandBuffers.size())			// commandBufferCount
	};

	return vkAllocateCommandBuffers(m_Device, &commandBufferAllocationInfo, m_CommandBuffers.data());
}

void Application::RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkCommandBufferBeginInfo.html
	const VkCommandBufferBeginInfo commandBufferBeginInfo
	{
		VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,			// sType
		nullptr,												// pNext
		0,														// flags
		nullptr													// pInheritanceInfo
	};

	if (vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo) != VK_SUCCESS)
	{
		throw std::runtime_error("Failed to begin recording the command buffer");
	}

	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkClearValue.html
	std::array<VkClearValue, 2> clearColors
	{
		VkClearValue{ 0.39f, 0.59f, 0.93f, 1.0f },
		VkClearValue{ 1.0f, 1 }
	};

	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkRenderPassBeginInfo.html
	const VkRenderPassBeginInfo renderPassBeginInfo
	{
		VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,													// sType
		nullptr,																					// pNext
		m_RenderPass,																				// renderPass
		m_SwapChainFrameBuffers[imageIndex],														// framebuffer
		// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkRect2D.html
		VkRect2D																					// renderArea
		{
			VkOffset2D{ 0, 0 },		// offset
			m_ImageExtend			// extent
		},
		uint32_t(clearColors.size()),																// clearValueCount
		clearColors.data()																			// pClearValues
	};

	vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipeLine);

	VkViewport viewport{};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = static_cast<float>(m_ImageExtend.width);
	viewport.height = static_cast<float>(m_ImageExtend.height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

	VkRect2D scissor{};
	scissor.offset = { 0, 0 };
	scissor.extent = m_ImageExtend;
	vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

	vkCmdPushConstants(commandBuffer, m_PipeLineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &m_PushConstants);

	for (int i{}; i < g_NumberOfMeshes; ++i)
	{
		const VkBuffer vertexBuffers[]{ m_Meshes.at(i)->GetVertexBuffer() };
		const VkDeviceSize offsets[]{ 0 };

		vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
		vkCmdBindIndexBuffer(commandBuffer, m_Meshes.at(i)->GetIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

		const std::array<VkDescriptorSet, 2> descriptorSets { m_TransformsDescriptorSets.at(m_CurrentFrame).at(i), m_TexturesDescriptorSets.at(i) };
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipeLineLayout, 0, static_cast<uint32_t>(descriptorSets.size()), descriptorSets.data(), 0, nullptr);

		vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(m_Meshes.at(i)->GetIndices().size()), 1, 0, 0, 0);
	}

	vkCmdEndRenderPass(commandBuffer);

	if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
	{
		throw std::runtime_error("failed to record command buffer");
	}
}

void Application::UpdateUniformBuffers(uint32_t currentImage)
{
	for (size_t i{}; i < m_Meshes.size(); ++i)
	{
		UniformBufferObject ubo
		{
			m_Meshes.at(i)->GetModelMatrix(),
			m_Camera->GetViewMatrx(),
			m_Camera->GetProjectionMatrix(),
			m_Camera->GetPosition()
		};
		ubo.ProjectionMatrix[1][1] *= -1;

		memcpy(m_UniformBufferMaps.at(currentImage).at(i), &ubo, sizeof(UniformBufferObject));
	}
}

void Application::DrawFrame()
{
	vkWaitForFences(m_Device, 1, &m_InFlight[m_CurrentFrame], VK_TRUE, UINT64_MAX);

	uint32_t imageIndex;
	VkResult result{ vkAcquireNextImageKHR(m_Device, m_SwapChain, UINT64_MAX, m_ImageAvailable[m_CurrentFrame], VK_NULL_HANDLE, &imageIndex) };
	if (result == VK_ERROR_OUT_OF_DATE_KHR)
	{
		RecreateSwapChain();
		return;
	}
	else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
	{
		throw std::runtime_error("failed to acquire the swap chain image");
	}

	UpdateUniformBuffers(m_CurrentFrame);

	vkResetFences(m_Device, 1, &m_InFlight[m_CurrentFrame]);

	vkResetCommandBuffer(m_CommandBuffers[m_CurrentFrame], 0);
	RecordCommandBuffer(m_CommandBuffers[m_CurrentFrame], imageIndex);

	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkSubmitInfo.html
	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	VkPipelineStageFlags waitStages[]{ VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &m_ImageAvailable[m_CurrentFrame];
	submitInfo.pWaitDstStageMask = waitStages;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &m_CommandBuffers[m_CurrentFrame];
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &m_RenderFinished[m_CurrentFrame];

	if (vkQueueSubmit(m_GrahicsQueue, 1, &submitInfo, m_InFlight[m_CurrentFrame]) != VK_SUCCESS)
	{
		throw std::runtime_error("failed to submit draw command buffer!");
	}

	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkPresentInfoKHR.html
	VkPresentInfoKHR presentInfo{};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &m_RenderFinished[m_CurrentFrame];
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &m_SwapChain;
	presentInfo.pImageIndices = &imageIndex;

	result = vkQueuePresentKHR(m_PresentQueue, &presentInfo);
	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_FrameBufferResized)
	{
		m_FrameBufferResized = false;
		RecreateSwapChain();
	}
	else if (result != VK_SUCCESS)
	{
		throw std::runtime_error("failed to present swap chain image!");
	}

	m_CurrentFrame = (m_CurrentFrame + 1) % g_MaxFramePerFlight;
}

VkResult Application::CreateSyncObjects()
{
	VkResult result{ VK_SUCCESS };

	m_ImageAvailable.resize(g_MaxFramePerFlight);
	m_RenderFinished.resize(g_MaxFramePerFlight);
	m_InFlight.resize(g_MaxFramePerFlight);

	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkSemaphoreCreateInfo.html
	const VkSemaphoreCreateInfo semaphoreCreateInfo
	{
		VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,		// sType
		nullptr,										// pNext
		0												// flags
	};

	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkFenceCreateInfo.html
	const VkFenceCreateInfo fenceCreateInfo
	{
		VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,			// sType
		nullptr,										// pNext
		VK_FENCE_CREATE_SIGNALED_BIT					// flags
	};

	for (int i{}; i < g_MaxFramePerFlight; ++i)
	{
		result = vkCreateSemaphore(m_Device, &semaphoreCreateInfo, nullptr, &m_ImageAvailable[i]);
		if (result != VK_SUCCESS) return result;

		result = vkCreateSemaphore(m_Device, &semaphoreCreateInfo, nullptr, &m_RenderFinished[i]);
		if (result != VK_SUCCESS) return result;

		result = vkCreateFence(m_Device, &fenceCreateInfo, nullptr, &m_InFlight[i]);
		if (result != VK_SUCCESS) return result;
	}

	return result;
}

void Application::RecreateSwapChain()
{
	int width{ 0 }, height{ 0 };
	glfwGetFramebufferSize(m_Window, &width, &height);
	while (width == 0 || height == 0)
	{
		glfwGetFramebufferSize(m_Window, &width, &height);
		glfwWaitEvents();
	}

	vkDeviceWaitIdle(m_Device);

	CleanupSwapChain();

	if (CreateSwapChain() != VK_SUCCESS) throw std::runtime_error("Failed to recreate swap chain");
	RetrieveSwapChainImages();
	if (CreateSwapChainImageViews() != VK_SUCCESS) throw std::runtime_error("Failed to recreate swap chain image views");
	CreateColorResources();
	CreateDepthResources();
	if (CreateSwapChainFrameBuffers() != VK_SUCCESS) throw std::runtime_error("Failed to recreate swap chain frame buffers");
}

void Application::CleanupSwapChain()
{
	vkDestroyImageView(m_Device, m_DepthImageView, nullptr);
	vkDestroyImage(m_Device, m_DepthImage, nullptr);
	vkFreeMemory(m_Device, m_DepthMemory, nullptr);
	vkDestroyImageView(m_Device, m_ColorImageView, nullptr);
	vkDestroyImage(m_Device, m_ColorImage, nullptr);
	vkFreeMemory(m_Device, m_ColorMemory, nullptr);

	for (auto frameBuffer : m_SwapChainFrameBuffers)
	{
		vkDestroyFramebuffer(m_Device, frameBuffer, nullptr);
	}

	for (auto imageView : m_SwapChainImageViews)
	{
		vkDestroyImageView(m_Device, imageView, nullptr);
	}

	vkDestroySwapchainKHR(m_Device, m_SwapChain, nullptr);
}

void Application::FrameBufferResizedCallback(GLFWwindow* window, int width, int height)
{
	width;
	height;

	Application* app{ reinterpret_cast<Application*>(glfwGetWindowUserPointer(window)) };
	app->m_FrameBufferResized = true;
}

void Application::KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	window;
	key;
	scancode;
	action;
	mods;

	if (key == GLFW_KEY_R && action == GLFW_RELEASE)
	{
		for (int i{}; i < g_NumberOfMeshes; ++i)  m_Meshes.at(i)->SwitchRotate();
	}
	else if (key == GLFW_KEY_1 && action == GLFW_RELEASE)
	{
		m_PushConstants.RenderType = static_cast<int>(RenderType::Combined);
	}
	else if (key == GLFW_KEY_2 && action == GLFW_RELEASE)
	{
		m_PushConstants.RenderType = static_cast<int>(RenderType::BaseColor);
	}
	else if (key == GLFW_KEY_3 && action == GLFW_RELEASE)
	{
		m_PushConstants.RenderType = static_cast<int>(RenderType::Normal);
	}
	else if (key == GLFW_KEY_4 && action == GLFW_RELEASE)
	{
		m_PushConstants.RenderType = static_cast<int>(RenderType::Glossiness);
	}
	else if (key == GLFW_KEY_5 && action == GLFW_RELEASE)
	{
		m_PushConstants.RenderType = static_cast<int>(RenderType::Specular);
	}
}

VkResult Application::CreateUniformBuffers()
{
	VkResult result{};

	const size_t bufferSize{ sizeof(UniformBufferObject) };

	m_UniformBuffers.resize(g_MaxFramePerFlight);
	m_UniformBufferMemories.resize(g_MaxFramePerFlight);
	m_UniformBufferMaps.resize(g_MaxFramePerFlight);

	for (int i{}; i < g_MaxFramePerFlight; ++i)
	{
		m_UniformBuffers.emplace_back();
		m_UniformBufferMemories.emplace_back();
		m_UniformBufferMaps.emplace_back();

		m_UniformBuffers.at(i).resize(static_cast<size_t>(g_NumberOfMeshes));
		m_UniformBufferMemories.at(i).resize(static_cast<size_t>(g_NumberOfMeshes));
		m_UniformBufferMaps.at(i).resize(static_cast<size_t>(g_NumberOfMeshes));
	}

	for (int i{ 0 }; i < g_MaxFramePerFlight; ++i)
	{
		for (int j{}; j < g_NumberOfMeshes; ++j)
		{
			CreateBuffer
			(
				m_PhysicalDevice,
				m_Device,
				bufferSize,
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				m_UniformBuffers.at(i).at(j),
				m_UniformBufferMemories.at(i).at(j)
			);

			result = vkMapMemory(m_Device, m_UniformBufferMemories.at(i).at(j), 0, bufferSize, 0, &m_UniformBufferMaps.at(i).at(j));
			if (result != VK_SUCCESS) return result;
		}
	}

	return result;
}

VkResult Application::CreateTexturesDescriptorSetLayout()
{
	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkDescriptorSetLayoutBinding.html
	const std::array<VkDescriptorSetLayoutBinding, 4> descriptorSetLayoutBindings
	{
		// Base color Texture
		VkDescriptorSetLayoutBinding
		{
			0,													// binding
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,			// descriptorType	
			1,													// descriptorCount
			VK_SHADER_STAGE_FRAGMENT_BIT,						// stageFlags
			nullptr												// pImmutableSamplers
		},
		// Normal texture
		VkDescriptorSetLayoutBinding
		{
			1,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_FRAGMENT_BIT,
			nullptr
		},
		// Glossiness texture
		VkDescriptorSetLayoutBinding
		{
			2,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_FRAGMENT_BIT,
			nullptr
		},
		// Specular texture
		VkDescriptorSetLayoutBinding
		{
			3,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_FRAGMENT_BIT,
			nullptr
		}
	};

	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkDescriptorSetLayoutCreateInfo.html
	const VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo
	{
		VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,		// sType
		nullptr,													// pNext
		0,															// flags
		uint32_t(descriptorSetLayoutBindings.size()),				// bindingCount
		descriptorSetLayoutBindings.data()							// pBindings
	};

	return vkCreateDescriptorSetLayout(m_Device, &descriptorSetLayoutCreateInfo, nullptr, &m_TexturesDescriptorSetLayout);
}

VkResult Application::CreateTransformsDescriptorSetLayout()
{
	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkDescriptorSetLayoutBinding.html
	const std::array<VkDescriptorSetLayoutBinding, 1> descriptorSetLayoutBindings
	{
		// Uniform buffer object
		VkDescriptorSetLayoutBinding
		{
			0,											// binding
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,			// descriptorType	
			1,											// descriptorCount	
			VK_SHADER_STAGE_VERTEX_BIT,					// stageFlags	
			nullptr										// pImmutableSamplers	
		}
	};

	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkDescriptorSetLayoutCreateInfo.html
	const VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo
	{
		VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,		// sType
		nullptr,													// pNext
		0,															// flags
		uint32_t(descriptorSetLayoutBindings.size()),				// bindingCount
		descriptorSetLayoutBindings.data()							// pBindings
	};

	return vkCreateDescriptorSetLayout(m_Device, &descriptorSetLayoutCreateInfo, nullptr, &m_TransformsDescriptorSetLayout);
}

VkResult Application::CreateTexturesDescriptorSets()
{
	VkResult result{ VK_SUCCESS };

	m_TexturesDescriptorSets.resize(g_NumberOfMeshes);
	std::vector<VkDescriptorSetLayout> descriptorSetlayouts{ g_NumberOfMeshes, m_TexturesDescriptorSetLayout };

	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkDescriptorSetAllocateInfo.html
	const VkDescriptorSetAllocateInfo descriptorSetAllocateInfo
	{
		VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,			// sType
		nullptr,												// pNext
		m_DescriptorPool,										// descriptorPool
		static_cast<uint32_t>(g_NumberOfMeshes),				// descriptorSetCount
		descriptorSetlayouts.data()								// pSetLayouts
	};

	result = vkAllocateDescriptorSets(m_Device, &descriptorSetAllocateInfo, m_TexturesDescriptorSets.data());
	if (result != VK_SUCCESS) return result;

	for (int i{}; i < g_NumberOfMeshes; i++)
	{
		// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkDescriptorImageInfo.html
		const VkDescriptorImageInfo descriptorBaseColorInfo
		{
			m_TextureSampler,								// sampler	
			m_BaseColorTextures.at(i)->GetImageView(),		// imageView
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL		// imageLayout
		};

		const VkDescriptorImageInfo descriptorNormalInfo
		{
			m_TextureSampler,
			m_NormalTextures.at(i)->GetImageView(),
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		};

		const VkDescriptorImageInfo descriptorGlossInfo
		{
			m_TextureSampler,
			m_GlossTextures.at(i)->GetImageView(),
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		};

		const VkDescriptorImageInfo descriptorSpecularInfo
		{
			m_TextureSampler,
			m_SpecularTextures.at(i)->GetImageView(),
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		};

		// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkWriteDescriptorSet.html
		std::array<VkWriteDescriptorSet, 4> writeDescriptorSets
		{
			// Base color texture
			VkWriteDescriptorSet
			{
				VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,			// sType
				nullptr,										// pNext
				m_TexturesDescriptorSets.at(i),					// dstSet	
				0,												// dstBinding
				0,												// dstArrayElement
				1,												// descriptorCount
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,		// descriptorType
				&descriptorBaseColorInfo,						// pImageInfo
				nullptr,										// pBufferInfo
				nullptr											// pTexelBufferView
			},
			// normal texture
			VkWriteDescriptorSet
			{
				VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				nullptr,
				m_TexturesDescriptorSets.at(i),
				1,
				0,
				1,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				&descriptorNormalInfo,	
				nullptr,
				nullptr
			},
			// glossiness texture
			VkWriteDescriptorSet
			{
				VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				nullptr,
				m_TexturesDescriptorSets.at(i),
				2,
				0,
				1,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				&descriptorGlossInfo,
				nullptr,
				nullptr
			},
			// specular texture
			VkWriteDescriptorSet
			{
				VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				nullptr,
				m_TexturesDescriptorSets.at(i),
				3,
				0,
				1,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				&descriptorSpecularInfo,
				nullptr,
				nullptr
			}
		};

		vkUpdateDescriptorSets(m_Device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
	}

	return result;
}

VkResult Application::CreateTransformsDescriptorSets()
{
	VkResult result{ VK_SUCCESS };

	m_TransformsDescriptorSets.resize(g_MaxFramePerFlight);	

	std::vector<VkDescriptorSetLayout> descriptorSetlayouts{ g_NumberOfMeshes, m_TransformsDescriptorSetLayout };	

	for (int i{}; i < g_MaxFramePerFlight; ++i)
	{
		m_TransformsDescriptorSets.emplace_back();
		m_TransformsDescriptorSets.at(i).resize(static_cast<size_t>(g_NumberOfMeshes));	
	}

	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkDescriptorSetAllocateInfo.html
	const VkDescriptorSetAllocateInfo descriptorSetAllocateInfo
	{
		VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,			// sType
		nullptr,												// pNext
		m_DescriptorPool,										// descriptorPool
		static_cast<uint32_t>(g_NumberOfMeshes),				// descriptorSetCount
		descriptorSetlayouts.data()								// pSetLayouts
	};

	for (int i{}; i < g_MaxFramePerFlight; ++i)
	{
		result = vkAllocateDescriptorSets(m_Device, &descriptorSetAllocateInfo, m_TransformsDescriptorSets.at(i).data());	
		if (result != VK_SUCCESS) return result;
	}

	for (int i{}; i < g_MaxFramePerFlight; ++i)
	{
		for (int j{}; j < g_NumberOfMeshes; ++j)
		{
			// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkDescriptorBufferInfo.html
			const VkDescriptorBufferInfo descriptorBufferInfo
			{
				m_UniformBuffers.at(i).at(j),		// buffer
				0,									// offset
				sizeof(UniformBufferObject)			// range
			};

			// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkWriteDescriptorSet.html
			std::array<VkWriteDescriptorSet, 1> writeDescriptorSets
			{
				// Uniform buffer object
				VkWriteDescriptorSet
				{
					VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,			// sType
					nullptr,										// pNext
					m_TransformsDescriptorSets.at(i).at(j),			// dstSet	
					0,												// dstBinding
					0,												// dstArrayElement
					1,												// descriptorCount
					VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,				// descriptorType
					nullptr,										// pImageInfo
					&descriptorBufferInfo,							// pBufferInfo
					nullptr											// pTexelBufferView
				}
			};

			vkUpdateDescriptorSets(m_Device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
		}
	}

	return result;
}

VkResult Application::CreateDescriptorPool()
{
	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkDescriptorPoolSize.html
	std::array< VkDescriptorPoolSize, 2> descriptorPoolSizes
	{
		VkDescriptorPoolSize
		{
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,									// type
			static_cast<uint32_t>(g_MaxFramePerFlight * g_NumberOfMeshes)		// descriptorCount	
		},
		VkDescriptorPoolSize
		{
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			static_cast<uint32_t>(g_NumberOfMeshes * 4)
		}
	};

	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkDescriptorPoolCreateInfo.html
	const VkDescriptorPoolCreateInfo descriptorPoolCreateInfo
	{
		VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,														// sType
		nullptr,																							// pNext
		0,																									// flags
		static_cast<uint32_t>((g_MaxFramePerFlight * g_NumberOfMeshes) + (g_NumberOfMeshes)),				// maxSets
		static_cast<uint32_t>(descriptorPoolSizes.size()),													// poolSizeCount
		descriptorPoolSizes.data()																			// pPoolSizes
	};

	return vkCreateDescriptorPool(m_Device, &descriptorPoolCreateInfo, nullptr, &m_DescriptorPool);
}

void Application::CreateTextureSampler()
{
	VkPhysicalDeviceProperties physicalDeviceProperties{};
	vkGetPhysicalDeviceProperties(m_PhysicalDevice, &physicalDeviceProperties);

	// https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkSamplerCreateInfo.html
	const VkSamplerCreateInfo samplerCreateInfo
	{
		VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,							// sType
		nullptr,														// pNext
		0,																// flags
		VK_FILTER_LINEAR,												// magFilter
		VK_FILTER_LINEAR,												// minFilter
		VK_SAMPLER_MIPMAP_MODE_LINEAR,									// mipmapMode	
		VK_SAMPLER_ADDRESS_MODE_REPEAT,									// addressModeU	
		VK_SAMPLER_ADDRESS_MODE_REPEAT,									// addressModeV	
		VK_SAMPLER_ADDRESS_MODE_REPEAT,									// addressModeW	
		0.0f,															// mipLodBias
		VK_TRUE,														// anisotropyEnable
		physicalDeviceProperties.limits.maxSamplerAnisotropy,			// maxAnisotropy
		VK_FALSE,														// compareEnable
		VK_COMPARE_OP_ALWAYS,											// compareOp	
		0.0f,															// minLod
		VK_LOD_CLAMP_NONE,												// maxLod
		VK_BORDER_COLOR_INT_OPAQUE_BLACK,								// borderColor
		VK_FALSE														// unnormalizedCoordinates
	};

	if (vkCreateSampler(m_Device, &samplerCreateInfo, nullptr, &m_TextureSampler) != VK_SUCCESS)
	{
		throw std::runtime_error("Failed to create texture sampler!");
	}
}

void Application::CreateDepthResources()
{
	VkFormat depthFormat{ FindDepthFormat(m_PhysicalDevice) };

	CreateImage
	(
		m_PhysicalDevice,
		m_Device,
		m_ImageExtend,
		depthFormat,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		m_DepthImage,
		m_DepthMemory,
		1,
		m_MSAASamples
	);

	m_DepthImageView = CreateImageView(m_Device, m_DepthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, 1);

	TransitionImageLayout(m_Device, m_CommandPool, m_GrahicsQueue, m_DepthImage, depthFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 1);
}

void Application::CreateColorResources()
{
	CreateImage
	(
		m_PhysicalDevice,
		m_Device,
		m_ImageExtend,
		m_ImageFormat,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		m_ColorImage,
		m_ColorMemory,
		1,
		m_MSAASamples
	);

	m_ColorImageView = CreateImageView(m_Device, m_ColorImage, m_ImageFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
}

void Application::InitializeTextures()
{
	m_BaseColorTextures.push_back(new Texture{ m_PhysicalDevice, m_Device, m_CommandPool, m_GrahicsQueue, "Textures/vehicle_base.png", VK_FORMAT_R8G8B8A8_SRGB });
	m_NormalTextures.push_back(new Texture{ m_PhysicalDevice, m_Device, m_CommandPool, m_GrahicsQueue, "Textures/vehicle_normal.png", VK_FORMAT_R8G8B8A8_UNORM });
	m_GlossTextures.push_back(new Texture{ m_PhysicalDevice, m_Device, m_CommandPool, m_GrahicsQueue, "Textures/vehicle_gloss.png", VK_FORMAT_R8G8B8A8_UNORM });
	m_SpecularTextures.push_back(new Texture{ m_PhysicalDevice, m_Device, m_CommandPool, m_GrahicsQueue, "Textures/vehicle_specular.png", VK_FORMAT_R8G8B8A8_UNORM });

	m_BaseColorTextures.push_back(new Texture{ m_PhysicalDevice, m_Device, m_CommandPool, m_GrahicsQueue, "Textures/mixer_base.png", VK_FORMAT_R8G8B8A8_SRGB });
	m_NormalTextures.push_back(new Texture{ m_PhysicalDevice, m_Device, m_CommandPool, m_GrahicsQueue, "Textures/mixer_normal.png", VK_FORMAT_R8G8B8A8_UNORM });
	m_GlossTextures.push_back(new Texture{ m_PhysicalDevice, m_Device, m_CommandPool, m_GrahicsQueue, "Textures/mixer_gloss.png", VK_FORMAT_R8G8B8A8_UNORM });
	m_SpecularTextures.push_back(new Texture{ m_PhysicalDevice, m_Device, m_CommandPool, m_GrahicsQueue, "Textures/mixer_specular.png", VK_FORMAT_R8G8B8A8_UNORM });
}