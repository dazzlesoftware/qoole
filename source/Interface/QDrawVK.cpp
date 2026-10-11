/*
Copyright (C) 1996-1997 GX Media, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "stdafx.h"
#include "QDraw.h"
#include "QDrawVK.h"

#include <map>

// Generated from QDrawShaders.hlsl by the project's CustomBuild step.
#include "QDrawShaders_VK_VS.h"
#include "QDrawShaders_VK_PSColor.h"
#include "QDrawShaders_VK_PSTexture.h"

static const VkFormat COLOR_FORMAT = VK_FORMAT_B8G8R8A8_UNORM;
static const uint32_t TEXTURE_SETS = 8192;

#ifdef IS_QDRAWDLL
QDrawVulkan *
NewQDraw(CWnd *wnd) {
	return new QDrawVulkan(wnd);
}
#endif

// ========== Vulkan entry points ==========

#define VK_INSTANCE_FUNCS(X) \
	X(vkDestroyInstance) \
	X(vkEnumeratePhysicalDevices) \
	X(vkGetPhysicalDeviceProperties) \
	X(vkGetPhysicalDeviceQueueFamilyProperties) \
	X(vkGetPhysicalDeviceMemoryProperties) \
	X(vkGetPhysicalDeviceFormatProperties) \
	X(vkEnumerateDeviceExtensionProperties) \
	X(vkCreateDevice) \
	X(vkDestroySurfaceKHR) \
	X(vkGetPhysicalDeviceSurfaceSupportKHR) \
	X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
	X(vkGetPhysicalDeviceSurfaceFormatsKHR) \
	X(vkGetPhysicalDeviceSurfacePresentModesKHR) \
	X(vkCreateWin32SurfaceKHR) \
	X(vkGetPhysicalDeviceWin32PresentationSupportKHR) \
	X(vkDestroyDevice) \
	X(vkGetDeviceQueue) \
	X(vkDeviceWaitIdle) \
	X(vkQueueSubmit) \
	X(vkQueueWaitIdle) \
	X(vkCreateSwapchainKHR) \
	X(vkDestroySwapchainKHR) \
	X(vkGetSwapchainImagesKHR) \
	X(vkAcquireNextImageKHR) \
	X(vkQueuePresentKHR) \
	X(vkCreateImageView) \
	X(vkDestroyImageView) \
	X(vkCreateImage) \
	X(vkDestroyImage) \
	X(vkGetImageMemoryRequirements) \
	X(vkAllocateMemory) \
	X(vkFreeMemory) \
	X(vkBindImageMemory) \
	X(vkCreateBuffer) \
	X(vkDestroyBuffer) \
	X(vkGetBufferMemoryRequirements) \
	X(vkBindBufferMemory) \
	X(vkMapMemory) \
	X(vkUnmapMemory) \
	X(vkCreateRenderPass) \
	X(vkDestroyRenderPass) \
	X(vkCreateFramebuffer) \
	X(vkDestroyFramebuffer) \
	X(vkCreateShaderModule) \
	X(vkDestroyShaderModule) \
	X(vkCreatePipelineLayout) \
	X(vkDestroyPipelineLayout) \
	X(vkCreateGraphicsPipelines) \
	X(vkDestroyPipeline) \
	X(vkCreateDescriptorSetLayout) \
	X(vkDestroyDescriptorSetLayout) \
	X(vkCreateDescriptorPool) \
	X(vkDestroyDescriptorPool) \
	X(vkAllocateDescriptorSets) \
	X(vkFreeDescriptorSets) \
	X(vkUpdateDescriptorSets) \
	X(vkCreateSampler) \
	X(vkDestroySampler) \
	X(vkCreateCommandPool) \
	X(vkDestroyCommandPool) \
	X(vkAllocateCommandBuffers) \
	X(vkFreeCommandBuffers) \
	X(vkResetCommandBuffer) \
	X(vkBeginCommandBuffer) \
	X(vkEndCommandBuffer) \
	X(vkCmdBeginRenderPass) \
	X(vkCmdEndRenderPass) \
	X(vkCmdBindPipeline) \
	X(vkCmdBindVertexBuffers) \
	X(vkCmdBindDescriptorSets) \
	X(vkCmdPushConstants) \
	X(vkCmdSetViewport) \
	X(vkCmdSetScissor) \
	X(vkCmdDraw) \
	X(vkCmdPipelineBarrier) \
	X(vkCmdCopyBufferToImage) \
	X(vkCreateSemaphore) \
	X(vkDestroySemaphore) \
	X(vkCreateFence) \
	X(vkDestroyFence) \
	X(vkWaitForFences) \
	X(vkResetFences)

#define VK_DECLARE(name) static PFN_##name name;
static PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
static PFN_vkCreateInstance vkCreateInstance;
VK_INSTANCE_FUNCS(VK_DECLARE)
#undef VK_DECLARE

static HMODULE vulkanLib = NULL;

static void
Check(VkResult result, const char *what) {
	if(result != VK_SUCCESS)
		LFatal("Vulkan: %s failed (error %d).", what, (int)result);
}

static void
LoadLoader(void) {
	if(!vulkanLib)
		vulkanLib = LoadLibraryA("vulkan-1.dll");
	if(!vulkanLib)
		LFatal("Vulkan is not available on this system (vulkan-1.dll not found).\n"
			   "Install a current graphics driver or select a different renderer.");

	vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)GetProcAddress(vulkanLib, "vkGetInstanceProcAddr");
	if(!vkGetInstanceProcAddr)
		LFatal("Vulkan: vulkan-1.dll is missing vkGetInstanceProcAddr.");

	vkCreateInstance = (PFN_vkCreateInstance)vkGetInstanceProcAddr(NULL, "vkCreateInstance");
	if(!vkCreateInstance)
		LFatal("Vulkan: vulkan-1.dll is missing vkCreateInstance.");
}

static void
LoadInstanceFuncs(VkInstance instance) {
#define VK_LOAD(name) \
	name = (PFN_##name)vkGetInstanceProcAddr(instance, #name); \
	if(!name) LFatal("Vulkan: entry point %s not found.", #name);
	VK_INSTANCE_FUNCS(VK_LOAD)
#undef VK_LOAD
}

// ========== VKContext ==========
//
// Instance, device, pipelines and texture cache shared by every view.
// Each Paint() records one command buffer and waits for it to finish,
// so nothing here needs per-frame lifetime tracking.

class VKContext {
public:
	static VKContext *Acquire(void);
	void Release(void);

	void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
					  VkBuffer &buffer, VkDeviceMemory &memory);
	void CreateImage(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage,
					 VkImage &image, VkDeviceMemory &memory);
	VkImageView CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspect);

	VkDescriptorSet GetTexture(VkCommandBuffer cmd, Texture *texture, const palette_t *pal);
	void FrameDone(void);

	VkInstance instance;
	VkPhysicalDevice gpu;
	VkDevice device;
	uint32_t queueFamily;
	VkQueue queue;
	VkCommandPool cmdPool;
	VkFormat depthFormat;
	VkRenderPass renderPass;
	VkPipelineLayout pipeLayout;
	VkPipeline pipeLines, pipeSolid, pipeTexture;

private:
	struct TexEntry {
		VkImage image;
		VkDeviceMemory memory;
		VkImageView view;
		VkDescriptorSet set;
		void *mip0;
		int width, height, bits;
	};

	struct Staging {
		VkBuffer buffer;
		VkDeviceMemory memory;
	};

	VKContext();
	~VKContext();

	void CreateInstance(void);
	void PickDevice(void);
	void CreateDevice(void);
	void CreateRenderPass(void);
	void CreatePipelines(void);
	VkShaderModule CreateShader(const unsigned char *code, size_t size);
	VkPipeline CreatePipeline(VkPrimitiveTopology topology, VkShaderModule vs, VkShaderModule fs,
							  const char *fsEntry, bool depth);
	uint32_t FindMemory(uint32_t typeBits, VkMemoryPropertyFlags props);
	void DestroyTexture(TexEntry &e);

	static VKContext *instancePtr;
	int refs;

	VkPhysicalDeviceMemoryProperties memProps;
	VkDescriptorSetLayout setLayout;
	VkDescriptorPool descPool;
	VkSampler sampler;

	std::map<Texture *, TexEntry> textures;
	std::vector<Staging> staging;
};

VKContext *VKContext::instancePtr = NULL;

VKContext *
VKContext::Acquire(void) {
	if(!instancePtr)
		instancePtr = new VKContext();
	instancePtr->refs++;
	return instancePtr;
}

void
VKContext::Release(void) {
	if(--refs == 0) {
		instancePtr = NULL;
		delete this;
	}
}

VKContext::VKContext() {
	refs = 0;

	LoadLoader();
	CreateInstance();
	LoadInstanceFuncs(instance);
	PickDevice();
	CreateDevice();

	VkCommandPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	poolInfo.queueFamilyIndex = queueFamily;
	Check(vkCreateCommandPool(device, &poolInfo, NULL, &cmdPool), "vkCreateCommandPool");

	VkSamplerCreateInfo samplerInfo = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	Check(vkCreateSampler(device, &samplerInfo, NULL, &sampler), "vkCreateSampler");

	VkDescriptorSetLayoutBinding binding = {};
	binding.binding = 0;
	binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	binding.descriptorCount = 1;
	binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo layoutInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	layoutInfo.bindingCount = 1;
	layoutInfo.pBindings = &binding;
	Check(vkCreateDescriptorSetLayout(device, &layoutInfo, NULL, &setLayout), "vkCreateDescriptorSetLayout");

	VkDescriptorPoolSize poolSize = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, TEXTURE_SETS };
	VkDescriptorPoolCreateInfo descPoolInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
	descPoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	descPoolInfo.maxSets = TEXTURE_SETS;
	descPoolInfo.poolSizeCount = 1;
	descPoolInfo.pPoolSizes = &poolSize;
	Check(vkCreateDescriptorPool(device, &descPoolInfo, NULL, &descPool), "vkCreateDescriptorPool");

	VkPushConstantRange push = { VK_SHADER_STAGE_VERTEX_BIT, 0, 4 * sizeof(float) };
	VkPipelineLayoutCreateInfo pipeLayoutInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	pipeLayoutInfo.setLayoutCount = 1;
	pipeLayoutInfo.pSetLayouts = &setLayout;
	pipeLayoutInfo.pushConstantRangeCount = 1;
	pipeLayoutInfo.pPushConstantRanges = &push;
	Check(vkCreatePipelineLayout(device, &pipeLayoutInfo, NULL, &pipeLayout), "vkCreatePipelineLayout");

	CreateRenderPass();
	CreatePipelines();
}

VKContext::~VKContext() {
	vkDeviceWaitIdle(device);
	FrameDone();

	for(std::map<Texture *, TexEntry>::iterator it = textures.begin(); it != textures.end(); ++it)
		DestroyTexture(it->second);
	textures.clear();

	vkDestroyPipeline(device, pipeLines, NULL);
	vkDestroyPipeline(device, pipeSolid, NULL);
	vkDestroyPipeline(device, pipeTexture, NULL);
	vkDestroyPipelineLayout(device, pipeLayout, NULL);
	vkDestroyRenderPass(device, renderPass, NULL);
	vkDestroyDescriptorPool(device, descPool, NULL);
	vkDestroyDescriptorSetLayout(device, setLayout, NULL);
	vkDestroySampler(device, sampler, NULL);
	vkDestroyCommandPool(device, cmdPool, NULL);
	vkDestroyDevice(device, NULL);
	vkDestroyInstance(instance, NULL);
}

void
VKContext::CreateInstance(void) {
	VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
	appInfo.pApplicationName = "Qoole 99";
	appInfo.pEngineName = "QDrawVulkan";
	appInfo.apiVersion = VK_API_VERSION_1_0;

	const char *extensions[] = {
		VK_KHR_SURFACE_EXTENSION_NAME,
		VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
	};

	VkInstanceCreateInfo info = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	info.pApplicationInfo = &appInfo;
	info.enabledExtensionCount = 2;
	info.ppEnabledExtensionNames = extensions;

	VkResult result = vkCreateInstance(&info, NULL, &instance);
	if(result != VK_SUCCESS)
		LFatal("Vulkan: could not create an instance (error %d).\n"
			   "Select a different renderer in the drawing options.", (int)result);
}

void
VKContext::PickDevice(void) {
	uint32_t count = 0;
	vkEnumeratePhysicalDevices(instance, &count, NULL);
	std::vector<VkPhysicalDevice> gpus(count);
	if(count)
		vkEnumeratePhysicalDevices(instance, &count, &gpus[0]);

	int bestScore = -1;
	gpu = VK_NULL_HANDLE;

	for(uint32_t i = 0; i < count; i++) {
		uint32_t extCount = 0;
		vkEnumerateDeviceExtensionProperties(gpus[i], NULL, &extCount, NULL);
		std::vector<VkExtensionProperties> exts(extCount);
		if(extCount)
			vkEnumerateDeviceExtensionProperties(gpus[i], NULL, &extCount, &exts[0]);

		bool hasSwapchain = false;
		for(uint32_t e = 0; e < extCount; e++)
			if(!strcmp(exts[e].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
				hasSwapchain = true;
		if(!hasSwapchain)
			continue;

		uint32_t familyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(gpus[i], &familyCount, NULL);
		std::vector<VkQueueFamilyProperties> families(familyCount);
		if(familyCount)
			vkGetPhysicalDeviceQueueFamilyProperties(gpus[i], &familyCount, &families[0]);

		for(uint32_t f = 0; f < familyCount; f++) {
			if(!(families[f].queueFlags & VK_QUEUE_GRAPHICS_BIT) ||
			   !vkGetPhysicalDeviceWin32PresentationSupportKHR(gpus[i], f))
				continue;

			VkPhysicalDeviceProperties props;
			vkGetPhysicalDeviceProperties(gpus[i], &props);
			int score = 0;
			if(props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
				score = 3;
			else if(props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
				score = 2;
			else if(props.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU)
				score = 1;

			if(score > bestScore) {
				bestScore = score;
				gpu = gpus[i];
				queueFamily = f;
			}
			break;
		}
	}

	if(gpu == VK_NULL_HANDLE)
		LFatal("Vulkan: no graphics device can present to a window.\n"
			   "Select a different renderer in the drawing options.");

	vkGetPhysicalDeviceMemoryProperties(gpu, &memProps);

	// D32 or D24 is always available as a depth attachment.
	VkFormat candidates[] = { VK_FORMAT_D32_SFLOAT, VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_D24_UNORM_S8_UINT };
	depthFormat = VK_FORMAT_D16_UNORM;
	for(int i = 0; i < 3; i++) {
		VkFormatProperties fp;
		vkGetPhysicalDeviceFormatProperties(gpu, candidates[i], &fp);
		if(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
			depthFormat = candidates[i];
			break;
		}
	}
}

void
VKContext::CreateDevice(void) {
	float priority = 1.0f;
	VkDeviceQueueCreateInfo queueInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
	queueInfo.queueFamilyIndex = queueFamily;
	queueInfo.queueCount = 1;
	queueInfo.pQueuePriorities = &priority;

	const char *extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

	VkDeviceCreateInfo info = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	info.queueCreateInfoCount = 1;
	info.pQueueCreateInfos = &queueInfo;
	info.enabledExtensionCount = 1;
	info.ppEnabledExtensionNames = extensions;

	Check(vkCreateDevice(gpu, &info, NULL, &device), "vkCreateDevice");
	vkGetDeviceQueue(device, queueFamily, 0, &queue);
}

void
VKContext::CreateRenderPass(void) {
	VkAttachmentDescription attachments[2] = {};

	attachments[0].format = COLOR_FORMAT;
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	attachments[1].format = depthFormat;
	attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference colorRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
	VkAttachmentReference depthRef = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorRef;
	subpass.pDepthStencilAttachment = &depthRef;

	// Order the attachment writes after the swapchain acquire and after
	// the previous frame's use of the shared depth buffer.
	VkSubpassDependency dependency = {};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
							  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
							  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	dependency.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
							   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

	VkRenderPassCreateInfo info = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
	info.attachmentCount = 2;
	info.pAttachments = attachments;
	info.subpassCount = 1;
	info.pSubpasses = &subpass;
	info.dependencyCount = 1;
	info.pDependencies = &dependency;
	Check(vkCreateRenderPass(device, &info, NULL, &renderPass), "vkCreateRenderPass");
}

VkShaderModule
VKContext::CreateShader(const unsigned char *code, size_t size) {
	// pCode must be 4-byte aligned; the generated arrays are bytes.
	std::vector<uint32_t> words((size + 3) / 4);
	memcpy(&words[0], code, size);

	VkShaderModuleCreateInfo info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
	info.codeSize = size;
	info.pCode = &words[0];

	VkShaderModule module;
	Check(vkCreateShaderModule(device, &info, NULL, &module), "vkCreateShaderModule");
	return module;
}

void
VKContext::CreatePipelines(void) {
	VkShaderModule vs = CreateShader(g_VK_VSMain, sizeof(g_VK_VSMain));
	VkShaderModule psColor = CreateShader(g_VK_PSColor, sizeof(g_VK_PSColor));
	VkShaderModule psTexture = CreateShader(g_VK_PSTexture, sizeof(g_VK_PSTexture));

	pipeLines = CreatePipeline(VK_PRIMITIVE_TOPOLOGY_LINE_LIST, vs, psColor, "PSColor", false);
	pipeSolid = CreatePipeline(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, vs, psColor, "PSColor", true);
	pipeTexture = CreatePipeline(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, vs, psTexture, "PSTexture", true);

	vkDestroyShaderModule(device, vs, NULL);
	vkDestroyShaderModule(device, psColor, NULL);
	vkDestroyShaderModule(device, psTexture, NULL);
}

VkPipeline
VKContext::CreatePipeline(VkPrimitiveTopology topology, VkShaderModule vs, VkShaderModule fs,
						  const char *fsEntry, bool depth) {
	VkPipelineShaderStageCreateInfo stages[2] = {};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vs;
	stages[0].pName = "VSMain";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = fs;
	stages[1].pName = fsEntry;

	VkVertexInputBindingDescription binding = { 0, sizeof(GpuVertex), VK_VERTEX_INPUT_RATE_VERTEX };
	VkVertexInputAttributeDescription attribs[3] = {
		{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GpuVertex, x) },
		{ 1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(GpuVertex, s) },
		{ 2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(GpuVertex, color) },
	};

	VkPipelineVertexInputStateCreateInfo vertexInput = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	vertexInput.vertexBindingDescriptionCount = 1;
	vertexInput.pVertexBindingDescriptions = &binding;
	vertexInput.vertexAttributeDescriptionCount = 3;
	vertexInput.pVertexAttributeDescriptions = attribs;

	VkPipelineInputAssemblyStateCreateInfo inputAssembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
	inputAssembly.topology = topology;

	VkPipelineViewportStateCreateInfo viewport = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
	viewport.viewportCount = 1;
	viewport.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo raster = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
	raster.polygonMode = VK_POLYGON_MODE_FILL;
	raster.cullMode = VK_CULL_MODE_NONE;
	raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	raster.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo multisample = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineDepthStencilStateCreateInfo depthStencil = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
	depthStencil.depthTestEnable = depth ? VK_TRUE : VK_FALSE;
	depthStencil.depthWriteEnable = depth ? VK_TRUE : VK_FALSE;
	depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

	VkPipelineColorBlendAttachmentState blendAttachment = {};
	blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
									 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	VkPipelineColorBlendStateCreateInfo blend = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
	blend.attachmentCount = 1;
	blend.pAttachments = &blendAttachment;

	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamic = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
	dynamic.dynamicStateCount = 2;
	dynamic.pDynamicStates = dynamicStates;

	VkGraphicsPipelineCreateInfo info = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	info.stageCount = 2;
	info.pStages = stages;
	info.pVertexInputState = &vertexInput;
	info.pInputAssemblyState = &inputAssembly;
	info.pViewportState = &viewport;
	info.pRasterizationState = &raster;
	info.pMultisampleState = &multisample;
	info.pDepthStencilState = &depthStencil;
	info.pColorBlendState = &blend;
	info.pDynamicState = &dynamic;
	info.layout = pipeLayout;
	info.renderPass = renderPass;
	info.subpass = 0;

	VkPipeline pipeline;
	Check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, NULL, &pipeline),
		"vkCreateGraphicsPipelines");
	return pipeline;
}

uint32_t
VKContext::FindMemory(uint32_t typeBits, VkMemoryPropertyFlags props) {
	for(uint32_t i = 0; i < memProps.memoryTypeCount; i++)
		if((typeBits & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props)
			return i;
	LFatal("Vulkan: no suitable memory type.");
	return 0;
}

void
VKContext::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
						VkBuffer &buffer, VkDeviceMemory &memory) {
	VkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	info.size = size;
	info.usage = usage;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	Check(vkCreateBuffer(device, &info, NULL, &buffer), "vkCreateBuffer");

	VkMemoryRequirements req;
	vkGetBufferMemoryRequirements(device, buffer, &req);

	VkMemoryAllocateInfo alloc = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	alloc.allocationSize = req.size;
	alloc.memoryTypeIndex = FindMemory(req.memoryTypeBits, props);
	Check(vkAllocateMemory(device, &alloc, NULL, &memory), "vkAllocateMemory (buffer)");
	Check(vkBindBufferMemory(device, buffer, memory, 0), "vkBindBufferMemory");
}

void
VKContext::CreateImage(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage,
					   VkImage &image, VkDeviceMemory &memory) {
	VkImageCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	info.imageType = VK_IMAGE_TYPE_2D;
	info.format = format;
	info.extent.width = width;
	info.extent.height = height;
	info.extent.depth = 1;
	info.mipLevels = 1;
	info.arrayLayers = 1;
	info.samples = VK_SAMPLE_COUNT_1_BIT;
	info.tiling = VK_IMAGE_TILING_OPTIMAL;
	info.usage = usage;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	Check(vkCreateImage(device, &info, NULL, &image), "vkCreateImage");

	VkMemoryRequirements req;
	vkGetImageMemoryRequirements(device, image, &req);

	VkMemoryAllocateInfo alloc = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	alloc.allocationSize = req.size;
	alloc.memoryTypeIndex = FindMemory(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	Check(vkAllocateMemory(device, &alloc, NULL, &memory), "vkAllocateMemory (image)");
	Check(vkBindImageMemory(device, image, memory, 0), "vkBindImageMemory");
}

VkImageView
VKContext::CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspect) {
	VkImageViewCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	info.image = image;
	info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	info.format = format;
	info.subresourceRange.aspectMask = aspect;
	info.subresourceRange.levelCount = 1;
	info.subresourceRange.layerCount = 1;

	VkImageView view;
	Check(vkCreateImageView(device, &info, NULL, &view), "vkCreateImageView");
	return view;
}

void
VKContext::DestroyTexture(TexEntry &e) {
	vkFreeDescriptorSets(device, descPool, 1, &e.set);
	vkDestroyImageView(device, e.view, NULL);
	vkDestroyImage(device, e.image, NULL);
	vkFreeMemory(device, e.memory, NULL);
}

void
VKContext::FrameDone(void) {
	for(size_t i = 0; i < staging.size(); i++) {
		vkDestroyBuffer(device, staging[i].buffer, NULL);
		vkFreeMemory(device, staging[i].memory, NULL);
	}
	staging.clear();
}

VkDescriptorSet
VKContext::GetTexture(VkCommandBuffer cmd, Texture *texture, const palette_t *pal) {
	std::map<Texture *, TexEntry>::iterator it = textures.find(texture);

	if(it != textures.end()) {
		TexEntry &e = it->second;
		if(e.mip0 == texture->mip[0] && e.width == texture->width &&
		   e.height == texture->height && e.bits == texture->bits)
			return e.set;
		// The Texture was reloaded or its memory reused; the GPU is idle
		// between frames, so the old copy can go right away.
		DestroyTexture(e);
		textures.erase(it);
	}

	TexEntry e;
	e.mip0 = texture->mip[0];
	e.width = texture->width;
	e.height = texture->height;
	e.bits = texture->bits;

	VkDescriptorSetAllocateInfo setInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	setInfo.descriptorPool = descPool;
	setInfo.descriptorSetCount = 1;
	setInfo.pSetLayouts = &setLayout;
	if(vkAllocateDescriptorSets(device, &setInfo, &e.set) != VK_SUCCESS)
		return VK_NULL_HANDLE;	// Pool exhausted; draw untextured.

	unsigned char *rgba = QDrawGPU::ConvertTexture(texture, pal);
	if(!rgba) {
		vkFreeDescriptorSets(device, descPool, 1, &e.set);
		return VK_NULL_HANDLE;
	}

	VkDeviceSize size = (VkDeviceSize)texture->width * texture->height * 4;
	Staging s;
	CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, s.buffer, s.memory);
	void *mapped;
	Check(vkMapMemory(device, s.memory, 0, size, 0, &mapped), "vkMapMemory (texture)");
	memcpy(mapped, rgba, (size_t)size);
	vkUnmapMemory(device, s.memory);
	delete [] rgba;
	staging.push_back(s);

	CreateImage(texture->width, texture->height, VK_FORMAT_R8G8B8A8_UNORM,
		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, e.image, e.memory);
	e.view = CreateImageView(e.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);

	VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = e.image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.layerCount = 1;

	barrier.srcAccessMask = 0;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		0, 0, NULL, 0, NULL, 1, &barrier);

	VkBufferImageCopy region = {};
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.layerCount = 1;
	region.imageExtent.width = texture->width;
	region.imageExtent.height = texture->height;
	region.imageExtent.depth = 1;
	vkCmdCopyBufferToImage(cmd, s.buffer, e.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		0, 0, NULL, 0, NULL, 1, &barrier);

	VkDescriptorImageInfo imageInfo = { sampler, e.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
	write.dstSet = e.set;
	write.dstBinding = 0;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	write.pImageInfo = &imageInfo;
	vkUpdateDescriptorSets(device, 1, &write, 0, NULL);

	textures[texture] = e;
	return e.set;
}

// ========== QDrawVulkan ==========

QDrawVulkan::QDrawVulkan(CWnd *wnd) : QDrawGPU(wnd) {
	swapchain = VK_NULL_HANDLE;
	depthImage = VK_NULL_HANDLE;
	depthMemory = VK_NULL_HANDLE;
	depthView = VK_NULL_HANDLE;
	needRecreate = true;
	vertexBuffer = VK_NULL_HANDLE;
	vertexMemory = VK_NULL_HANDLE;
	vertexMapped = NULL;
	vertexCapacity = 0;
	extent.width = extent.height = 0;

	ctx = VKContext::Acquire();

	CRect rect;
	wnd->GetClientRect(&rect);
	QDraw::Size(rect.Width(), rect.Height());

	VkWin32SurfaceCreateInfoKHR surfaceInfo = { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
	surfaceInfo.hinstance = (HINSTANCE)GetWindowLongPtr(wnd->m_hWnd, GWLP_HINSTANCE);
	surfaceInfo.hwnd = wnd->m_hWnd;
	Check(vkCreateWin32SurfaceKHR(ctx->instance, &surfaceInfo, NULL, &surface), "vkCreateWin32SurfaceKHR");

	VkBool32 supported = VK_FALSE;
	vkGetPhysicalDeviceSurfaceSupportKHR(ctx->gpu, ctx->queueFamily, surface, &supported);
	if(!supported)
		LFatal("Vulkan: the selected graphics device cannot present to this window.");

	VkCommandBufferAllocateInfo cmdInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
	cmdInfo.commandPool = ctx->cmdPool;
	cmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cmdInfo.commandBufferCount = 1;
	Check(vkAllocateCommandBuffers(ctx->device, &cmdInfo, &cmd), "vkAllocateCommandBuffers");

	VkSemaphoreCreateInfo semInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
	Check(vkCreateSemaphore(ctx->device, &semInfo, NULL, &acquired), "vkCreateSemaphore");

	VkFenceCreateInfo fenceInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
	Check(vkCreateFence(ctx->device, &fenceInfo, NULL, &fence), "vkCreateFence");
}

QDrawVulkan::~QDrawVulkan() {
	VkDevice device = ctx->device;

	vkQueueWaitIdle(ctx->queue);
	DestroySwapchain();

	if(vertexBuffer) {
		vkDestroyBuffer(device, vertexBuffer, NULL);
		vkFreeMemory(device, vertexMemory, NULL);
	}

	vkDestroyFence(device, fence, NULL);
	vkDestroySemaphore(device, acquired, NULL);
	vkFreeCommandBuffers(device, ctx->cmdPool, 1, &cmd);
	vkDestroySurfaceKHR(ctx->instance, surface, NULL);

	ctx->Release();
}

bool
QDrawVulkan::CreateSwapchain(void) {
	VkDevice device = ctx->device;

	VkSurfaceCapabilitiesKHR caps;
	Check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx->gpu, surface, &caps),
		"vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

	if(caps.currentExtent.width != 0xFFFFFFFF)
		extent = caps.currentExtent;
	else {
		extent.width = max(caps.minImageExtent.width, min(caps.maxImageExtent.width, (uint32_t)width));
		extent.height = max(caps.minImageExtent.height, min(caps.maxImageExtent.height, (uint32_t)height));
	}
	if(extent.width == 0 || extent.height == 0)
		return false;	// Minimized; try again on the next paint.

	uint32_t count = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->gpu, surface, &count, NULL);
	std::vector<VkSurfaceFormatKHR> formats(count);
	if(count)
		vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->gpu, surface, &count, &formats[0]);

	bool formatOK = (count == 1 && formats[0].format == VK_FORMAT_UNDEFINED);
	VkColorSpaceKHR colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	for(uint32_t i = 0; i < count; i++) {
		if(formats[i].format == COLOR_FORMAT) {
			formatOK = true;
			colorSpace = formats[i].colorSpace;
			break;
		}
	}
	if(!formatOK)
		LFatal("Vulkan: the window surface does not support B8G8R8A8_UNORM.");

	// No vsync: every view presents after each update, and waiting for
	// vblank once per view makes multi-view layouts sluggish.
	vkGetPhysicalDeviceSurfacePresentModesKHR(ctx->gpu, surface, &count, NULL);
	std::vector<VkPresentModeKHR> modes(count);
	if(count)
		vkGetPhysicalDeviceSurfacePresentModesKHR(ctx->gpu, surface, &count, &modes[0]);
	VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
	for(uint32_t i = 0; i < count; i++) {
		if(modes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
			presentMode = modes[i];
		else if(modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR && presentMode == VK_PRESENT_MODE_FIFO_KHR)
			presentMode = modes[i];
	}

	uint32_t imageCount = caps.minImageCount + 1;
	if(caps.maxImageCount && imageCount > caps.maxImageCount)
		imageCount = caps.maxImageCount;

	VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	if(!(caps.supportedCompositeAlpha & alpha))
		alpha = (VkCompositeAlphaFlagBitsKHR)(caps.supportedCompositeAlpha & -(int)caps.supportedCompositeAlpha);

	VkSwapchainCreateInfoKHR info = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
	info.surface = surface;
	info.minImageCount = imageCount;
	info.imageFormat = COLOR_FORMAT;
	info.imageColorSpace = colorSpace;
	info.imageExtent = extent;
	info.imageArrayLayers = 1;
	info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	info.preTransform = caps.currentTransform;
	info.compositeAlpha = alpha;
	info.presentMode = presentMode;
	info.clipped = VK_TRUE;
	Check(vkCreateSwapchainKHR(device, &info, NULL, &swapchain), "vkCreateSwapchainKHR");

	vkGetSwapchainImagesKHR(device, swapchain, &count, NULL);
	images.resize(count);
	vkGetSwapchainImagesKHR(device, swapchain, &count, &images[0]);

	ctx->CreateImage(extent.width, extent.height, ctx->depthFormat,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, depthImage, depthMemory);
	depthView = ctx->CreateImageView(depthImage, ctx->depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

	VkSemaphoreCreateInfo semInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

	imageViews.resize(count);
	framebuffers.resize(count);
	renderDone.resize(count);
	for(uint32_t i = 0; i < count; i++) {
		imageViews[i] = ctx->CreateImageView(images[i], COLOR_FORMAT, VK_IMAGE_ASPECT_COLOR_BIT);

		VkImageView attachments[2] = { imageViews[i], depthView };
		VkFramebufferCreateInfo fbInfo = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
		fbInfo.renderPass = ctx->renderPass;
		fbInfo.attachmentCount = 2;
		fbInfo.pAttachments = attachments;
		fbInfo.width = extent.width;
		fbInfo.height = extent.height;
		fbInfo.layers = 1;
		Check(vkCreateFramebuffer(device, &fbInfo, NULL, &framebuffers[i]), "vkCreateFramebuffer");

		// One per image: a present may still be reading the previous one.
		Check(vkCreateSemaphore(device, &semInfo, NULL, &renderDone[i]), "vkCreateSemaphore");
	}

	needRecreate = false;
	return true;
}

void
QDrawVulkan::DestroySwapchain(void) {
	VkDevice device = ctx->device;

	for(size_t i = 0; i < images.size(); i++) {
		vkDestroyFramebuffer(device, framebuffers[i], NULL);
		vkDestroyImageView(device, imageViews[i], NULL);
		vkDestroySemaphore(device, renderDone[i], NULL);
	}
	images.clear();
	imageViews.clear();
	framebuffers.clear();
	renderDone.clear();

	if(depthImage) {
		vkDestroyImageView(device, depthView, NULL);
		vkDestroyImage(device, depthImage, NULL);
		vkFreeMemory(device, depthMemory, NULL);
		depthImage = VK_NULL_HANDLE;
		depthMemory = VK_NULL_HANDLE;
		depthView = VK_NULL_HANDLE;
	}

	if(swapchain) {
		vkDestroySwapchainKHR(device, swapchain, NULL);
		swapchain = VK_NULL_HANDLE;
	}
}

void
QDrawVulkan::Size(int cx, int cy) {
	QDraw::Size(cx, cy);

	if((uint32_t)width != extent.width || (uint32_t)height != extent.height)
		needRecreate = true;
}

void
QDrawVulkan::Paint(CPaintDC *pPaint) {
	VkDevice device = ctx->device;
	uint32_t index;

	for(int attempt = 0; ; attempt++) {
		if(needRecreate || !swapchain) {
			vkQueueWaitIdle(ctx->queue);
			DestroySwapchain();
			if(!CreateSwapchain())
				return;
		}

		VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquired,
			VK_NULL_HANDLE, &index);
		if(result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR)
			break;
		if(result != VK_ERROR_OUT_OF_DATE_KHR || attempt > 0)
			Check(result, "vkAcquireNextImageKHR");
		needRecreate = true;
	}

	BuildFrame(frame);

	Check(vkResetCommandBuffer(cmd, 0), "vkResetCommandBuffer");
	VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	Check(vkBeginCommandBuffer(cmd, &beginInfo), "vkBeginCommandBuffer");

	// Upload any new textures before the render pass.
	std::vector<VkDescriptorSet> texSet(frame.triBatches.size(), VK_NULL_HANDLE);
	for(size_t i = 0; i < frame.triBatches.size(); i++) {
		if(frame.triBatches[i].texture)
			texSet[i] = ctx->GetTexture(cmd, frame.triBatches[i].texture, GetPalette());
	}

	VkDeviceSize vbBytes = frame.verts.size() * sizeof(GpuVertex);
	if(vbBytes) {
		if(vbBytes > vertexCapacity) {
			if(vertexBuffer) {
				vkDestroyBuffer(device, vertexBuffer, NULL);
				vkFreeMemory(device, vertexMemory, NULL);
			}
			vertexCapacity = max(vbBytes + vbBytes / 2, (VkDeviceSize)64 * 1024);
			ctx->CreateBuffer(vertexCapacity, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				vertexBuffer, vertexMemory);
			Check(vkMapMemory(device, vertexMemory, 0, vertexCapacity, 0, &vertexMapped), "vkMapMemory (vertices)");
		}
		memcpy(vertexMapped, &frame.verts[0], (size_t)vbBytes);
	}

	VkClearValue clears[2];
	clears[0].color.float32[0] = 0.0f;
	clears[0].color.float32[1] = 0.0f;
	clears[0].color.float32[2] = 0.0f;
	clears[0].color.float32[3] = 1.0f;
	clears[1].depthStencil.depth = 1.0f;
	clears[1].depthStencil.stencil = 0;

	VkRenderPassBeginInfo rpInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
	rpInfo.renderPass = ctx->renderPass;
	rpInfo.framebuffer = framebuffers[index];
	rpInfo.renderArea.extent = extent;
	rpInfo.clearValueCount = 2;
	rpInfo.pClearValues = clears;
	vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

	VkViewport viewport = { 0.0f, 0.0f, (float)extent.width, (float)extent.height, 0.0f, 1.0f };
	VkRect2D scissor = { { 0, 0 }, extent };
	vkCmdSetViewport(cmd, 0, 1, &viewport);
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	if(vbBytes) {
		float scale[4] = { 2.0f / width, 2.0f / height, 0.0f, 0.0f };
		vkCmdPushConstants(cmd, ctx->pipeLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(scale), scale);

		VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);

		for(size_t i = 0; i < frame.triBatches.size(); i++) {
			const GpuBatch &batch = frame.triBatches[i];
			if(texSet[i]) {
				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->pipeTexture);
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->pipeLayout,
					0, 1, &texSet[i], 0, NULL);
			}
			else
				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->pipeSolid);
			vkCmdDraw(cmd, batch.count, 1, batch.first, 0);
		}

		if(frame.lineCount) {
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->pipeLines);
			vkCmdDraw(cmd, frame.lineCount, 1, frame.lineFirst, 0);
		}
	}

	vkCmdEndRenderPass(cmd);
	Check(vkEndCommandBuffer(cmd), "vkEndCommandBuffer");

	VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
	submit.waitSemaphoreCount = 1;
	submit.pWaitSemaphores = &acquired;
	submit.pWaitDstStageMask = &waitStage;
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &cmd;
	submit.signalSemaphoreCount = 1;
	submit.pSignalSemaphores = &renderDone[index];
	Check(vkQueueSubmit(ctx->queue, 1, &submit, fence), "vkQueueSubmit");

	VkPresentInfoKHR present = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
	present.waitSemaphoreCount = 1;
	present.pWaitSemaphores = &renderDone[index];
	present.swapchainCount = 1;
	present.pSwapchains = &swapchain;
	present.pImageIndices = &index;
	VkResult result = vkQueuePresentKHR(ctx->queue, &present);
	if(result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
		needRecreate = true;
	else
		Check(result, "vkQueuePresentKHR");

	Check(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
	vkResetFences(device, 1, &fence);
	ctx->FrameDone();
}
