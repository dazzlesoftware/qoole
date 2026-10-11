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

#ifndef __DRAWVK_H
#define __DRAWVK_H

#include "DrawGPU.h"

// vulkan-1.dll is loaded at runtime, so only the headers are needed.
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_win32.h>

class VKContext;

// ========== FDrawVulkan ==========

class FDrawVulkan : public FDrawGPU {
public:
	FDrawVulkan(CWnd *wnd);
	~FDrawVulkan();

	void Size(int cx, int cy);
	void Paint(CPaintDC *pPaint);

private:
	bool CreateSwapchain(void);
	void DestroySwapchain(void);

	VKContext *ctx;

	VkSurfaceKHR surface;
	VkSwapchainKHR swapchain;
	VkExtent2D extent;
	std::vector<VkImage> images;
	std::vector<VkImageView> imageViews;
	std::vector<VkFramebuffer> framebuffers;
	std::vector<VkSemaphore> renderDone;
	VkImage depthImage;
	VkDeviceMemory depthMemory;
	VkImageView depthView;
	bool needRecreate;

	VkCommandBuffer cmd;
	VkSemaphore acquired;
	VkFence fence;

	VkBuffer vertexBuffer;
	VkDeviceMemory vertexMemory;
	void *vertexMapped;
	VkDeviceSize vertexCapacity;

	GpuFrame frame;
};

#endif
