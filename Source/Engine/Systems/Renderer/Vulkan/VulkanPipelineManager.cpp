#include "PCH.h"
#include "VulkanPipelineManager.h"
#include <fstream>

namespace Engine
{

	static VkPipelineShaderStageCreateInfo MakeStage(VkShaderStageFlagBits stage, VkShaderModule mod)
	{
		VkPipelineShaderStageCreateInfo s{};
		s.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		s.stage = stage;
		s.module = mod;
		s.pName = "main";
		return s;
	}

	static VkPipelineShaderStageCreateInfo MakeComputeStage(VkShaderModule mod, const char* entry)
	{
		VkPipelineShaderStageCreateInfo s{};
		s.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		s.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		s.module = mod;
		s.pName = entry;
		return s;
	}

	static void DestroyPipelinePair(VkDevice device, VkPipeline& p, VkPipelineLayout& l)
	{
		if (p)
		{
			vkDestroyPipeline(device, p, nullptr);
			p = VK_NULL_HANDLE;
		}
		if (l)
		{
			vkDestroyPipelineLayout(device, l, nullptr);
			l = VK_NULL_HANDLE;
		}
	}

	static void DestroyPipelineOnly(VkDevice device, VkPipeline& p)
	{
		if (p)
		{
			vkDestroyPipeline(device, p, nullptr);
			p = VK_NULL_HANDLE;
		}
	}

	VulkanPipelineManager::VulkanPipelineManager(VkDevice device)
		: device(device)
	{}

	std::vector<char> VulkanPipelineManager::ReadFile(const std::string& filename)
	{
		std::string exeDir = SwimEngine::GetExecutableDirectory();
		std::string fullPath = exeDir + "\\" + filename;

		std::ifstream file(fullPath, std::ios::ate | std::ios::binary);

		if (!file.is_open())
		{
			throw std::runtime_error("Failed to open file: " + filename);
		}

		size_t fileSize = (size_t)file.tellg();
		std::vector<char> buffer(fileSize);

		file.seekg(0);
		file.read(buffer.data(), fileSize);

		file.close();
		return buffer;
	}

	VkShaderModule VulkanPipelineManager::CreateShaderModule(const std::vector<char>& code)
	{
		VkShaderModuleCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		createInfo.codeSize = code.size();
		createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

		VkShaderModule shaderModule;
		if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create shader module!");
		}

		return shaderModule;
	}

	void VulkanPipelineManager::CreateRenderPass(VkFormat swapChainImageFormat, VkFormat depthFormat, VkSampleCountFlagBits msaaSamples)
	{
		this->msaaSamples = msaaSamples;

		// If we already have a render pass, destroy it before recreating
		if (renderPass)
		{
			vkDestroyRenderPass(device, renderPass, nullptr);
			renderPass = VK_NULL_HANDLE;
		}

		const bool useMsaa = (msaaSamples != VK_SAMPLE_COUNT_1_BIT);

		if (useMsaa)
		{
			// Attachment 0: MSAA color (render to)
			VkAttachmentDescription colorAttachment{};
			colorAttachment.format = swapChainImageFormat;
			colorAttachment.samples = msaaSamples;
			colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; // resolve handles storage
			colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			// Attachment 1: MSAA depth
			VkAttachmentDescription depthAttachment{};
			depthAttachment.format = depthFormat;
			depthAttachment.samples = msaaSamples;
			depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			// Attachment 2: Resolve target (swapchain image)
			VkAttachmentDescription resolveAttachment{};
			resolveAttachment.format = swapChainImageFormat;
			resolveAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
			resolveAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			resolveAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			resolveAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			resolveAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			resolveAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			resolveAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

			VkAttachmentReference colorRef{};
			colorRef.attachment = 0;
			colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			VkAttachmentReference depthRef{};
			depthRef.attachment = 1;
			depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			VkAttachmentReference resolveRef{};
			resolveRef.attachment = 2;
			resolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			VkSubpassDescription subpass{};
			subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass.colorAttachmentCount = 1;
			subpass.pColorAttachments = &colorRef;
			subpass.pResolveAttachments = &resolveRef;
			subpass.pDepthStencilAttachment = &depthRef;

			std::array<VkAttachmentDescription, 3> attachments = {
				colorAttachment,
				depthAttachment,
				resolveAttachment
			};

			VkRenderPassCreateInfo rp{};
			rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			rp.attachmentCount = static_cast<uint32_t>(attachments.size());
			rp.pAttachments = attachments.data();
			rp.subpassCount = 1;
			rp.pSubpasses = &subpass;

			if (vkCreateRenderPass(device, &rp, nullptr, &renderPass) != VK_SUCCESS)
			{
				throw std::runtime_error("Failed to create render pass!");
			}

			return;
		}

		// ===== Non-MSAA: 2 attachments (swapchain color + depth) =====

		VkAttachmentDescription colorAttachment{};
		colorAttachment.format = swapChainImageFormat;
		colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		VkAttachmentDescription depthAttachment{};
		depthAttachment.format = depthFormat;
		depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference colorRef{};
		colorRef.attachment = 0;
		colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkAttachmentReference depthRef{};
		depthRef.attachment = 1;
		depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorRef;
		subpass.pDepthStencilAttachment = &depthRef;

		std::array<VkAttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };

		VkRenderPassCreateInfo rp{};
		rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		rp.attachmentCount = static_cast<uint32_t>(attachments.size());
		rp.pAttachments = attachments.data();
		rp.subpassCount = 1;
		rp.pSubpasses = &subpass;

		if (vkCreateRenderPass(device, &rp, nullptr, &renderPass) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create render pass!");
		}
	}

	void VulkanPipelineManager::CreateGraphicsPipeline(
		const std::string& vertPath,
		const std::string& fragPath,
		VkDescriptorSetLayout set0Layout,
		VkDescriptorSetLayout set1BindlessLayout,
		const std::vector<VkVertexInputBindingDescription>& bindings,
		const std::vector<VkVertexInputAttributeDescription>& attribs,
		uint32_t instanceStride
	)
	{
		DestroyPipelinePair(device, graphicsPipeline, graphicsPipelineLayout);

		auto vertCode = ReadFile(vertPath);
		auto fragCode = ReadFile(fragPath);

		VkShaderModule vertMod = CreateShaderModule(vertCode);
		VkShaderModule fragMod = CreateShaderModule(fragCode);

		VkPipelineShaderStageCreateInfo stages[] = {
			MakeStage(VK_SHADER_STAGE_VERTEX_BIT, vertMod),
			MakeStage(VK_SHADER_STAGE_FRAGMENT_BIT, fragMod)
		};

		VkPipelineVertexInputStateCreateInfo vi{};
		vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vi.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
		vi.pVertexBindingDescriptions = bindings.data();
		vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribs.size());
		vi.pVertexAttributeDescriptions = attribs.data();

		VkPipelineInputAssemblyStateCreateInfo ia{};
		ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		VkPipelineViewportStateCreateInfo vp{};
		vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		vp.viewportCount = 1;
		vp.scissorCount = 1;

		VkPipelineRasterizationStateCreateInfo rs{};
		rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rs.polygonMode = VK_POLYGON_MODE_FILL;
		rs.cullMode = VK_CULL_MODE_BACK_BIT;
		rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rs.lineWidth = 1.0f;

		VkPipelineMultisampleStateCreateInfo ms{};
		ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		ms.rasterizationSamples = msaaSamples;

		VkPipelineDepthStencilStateCreateInfo ds{};
		ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		ds.depthTestEnable = VK_TRUE;
		ds.depthWriteEnable = VK_TRUE;
		ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

		VkPipelineColorBlendAttachmentState cbAttach{};
		cbAttach.colorWriteMask =
			VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

		VkPipelineColorBlendStateCreateInfo cb{};
		cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		cb.attachmentCount = 1;
		cb.pAttachments = &cbAttach;

		std::array<VkDynamicState, 2> dyn = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynInfo{};
		dynInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynInfo.dynamicStateCount = static_cast<uint32_t>(dyn.size());
		dynInfo.pDynamicStates = dyn.data();

		std::array<VkDescriptorSetLayout, 2> setLayouts = { set0Layout, set1BindlessLayout };

		VkPipelineLayoutCreateInfo pl{};
		pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pl.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
		pl.pSetLayouts = setLayouts.data();

		if (vkCreatePipelineLayout(device, &pl, nullptr, &graphicsPipelineLayout) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create graphics pipeline layout!");
		}

		VkGraphicsPipelineCreateInfo gp{};
		gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		gp.stageCount = 2;
		gp.pStages = stages;
		gp.pVertexInputState = &vi;
		gp.pInputAssemblyState = &ia;
		gp.pViewportState = &vp;
		gp.pRasterizationState = &rs;
		gp.pMultisampleState = &ms;
		gp.pDepthStencilState = &ds;
		gp.pColorBlendState = &cb;
		gp.pDynamicState = &dynInfo;
		gp.layout = graphicsPipelineLayout;
		gp.renderPass = renderPass;
		gp.subpass = 0;

		if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gp, nullptr, &graphicsPipeline) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create graphics pipeline!");
		}

		vkDestroyShaderModule(device, vertMod, nullptr);
		vkDestroyShaderModule(device, fragMod, nullptr);
	}

	void VulkanPipelineManager::CreateDecoratedMeshPipeline(
		const std::string& vertPath,
		const std::string& fragPath,
		VkDescriptorSetLayout set0Layout,
		VkDescriptorSetLayout set1BindlessLayout,
		const std::vector<VkVertexInputBindingDescription>& bindings,
		const std::vector<VkVertexInputAttributeDescription>& attribs,
		uint32_t instanceStride
	)
	{
		DestroyPipelinePair(device, decoratorPipeline, decoratorPipelineLayout);

		auto vertCode = ReadFile(vertPath);
		auto fragCode = ReadFile(fragPath);

		VkShaderModule vertMod = CreateShaderModule(vertCode);
		VkShaderModule fragMod = CreateShaderModule(fragCode);

		VkPipelineShaderStageCreateInfo stages[] = {
			MakeStage(VK_SHADER_STAGE_VERTEX_BIT, vertMod),
			MakeStage(VK_SHADER_STAGE_FRAGMENT_BIT, fragMod)
		};

		VkPipelineVertexInputStateCreateInfo vi{};
		vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vi.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
		vi.pVertexBindingDescriptions = bindings.data();
		vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribs.size());
		vi.pVertexAttributeDescriptions = attribs.data();

		VkPipelineInputAssemblyStateCreateInfo ia{};
		ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		VkPipelineViewportStateCreateInfo vp{};
		vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		vp.viewportCount = 1;
		vp.scissorCount = 1;

		VkPipelineRasterizationStateCreateInfo rs{};
		rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rs.polygonMode = VK_POLYGON_MODE_FILL;
		rs.cullMode = VK_CULL_MODE_NONE;
		rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rs.lineWidth = 1.0f;

		VkPipelineMultisampleStateCreateInfo ms{};
		ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		ms.rasterizationSamples = msaaSamples;

		VkPipelineDepthStencilStateCreateInfo ds{};
		ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		ds.depthTestEnable = VK_TRUE;
		ds.depthWriteEnable = VK_TRUE;
		ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

		VkPipelineColorBlendAttachmentState cbAttach{};
		cbAttach.colorWriteMask =
			VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		cbAttach.blendEnable = VK_TRUE;
		cbAttach.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		cbAttach.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		cbAttach.colorBlendOp = VK_BLEND_OP_ADD;
		cbAttach.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		cbAttach.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		cbAttach.alphaBlendOp = VK_BLEND_OP_ADD;

		VkPipelineColorBlendStateCreateInfo cb{};
		cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		cb.attachmentCount = 1;
		cb.pAttachments = &cbAttach;

		std::array<VkDynamicState, 2> dyn = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynInfo{};
		dynInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynInfo.dynamicStateCount = static_cast<uint32_t>(dyn.size());
		dynInfo.pDynamicStates = dyn.data();

		std::array<VkDescriptorSetLayout, 2> setLayouts = { set0Layout, set1BindlessLayout };

		VkPipelineLayoutCreateInfo pl{};
		pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pl.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
		pl.pSetLayouts = setLayouts.data();

		if (vkCreatePipelineLayout(device, &pl, nullptr, &decoratorPipelineLayout) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create decorator pipeline layout!");
		}

		VkGraphicsPipelineCreateInfo gp{};
		gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		gp.stageCount = 2;
		gp.pStages = stages;
		gp.pVertexInputState = &vi;
		gp.pInputAssemblyState = &ia;
		gp.pViewportState = &vp;
		gp.pRasterizationState = &rs;
		gp.pMultisampleState = &ms;
		gp.pDepthStencilState = &ds;
		gp.pColorBlendState = &cb;
		gp.pDynamicState = &dynInfo;
		gp.layout = decoratorPipelineLayout;
		gp.renderPass = renderPass;
		gp.subpass = 0;

		if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gp, nullptr, &decoratorPipeline) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create decorator pipeline!");
		}

		vkDestroyShaderModule(device, vertMod, nullptr);
		vkDestroyShaderModule(device, fragMod, nullptr);
	}

	void VulkanPipelineManager::CreateMsdfTextPipeline(
		const std::string& vertPath,
		const std::string& fragPath,
		VkDescriptorSetLayout set0Layout,
		VkDescriptorSetLayout set1BindlessLayout,
		const std::vector<VkVertexInputBindingDescription>& bindings,
		const std::vector<VkVertexInputAttributeDescription>& attribs,
		uint32_t instanceStride
	)
	{
		DestroyPipelinePair(device, msdfTextPipeline, msdfTextPipelineLayout);

		auto vertCode = ReadFile(vertPath);
		auto fragCode = ReadFile(fragPath);

		VkShaderModule vertMod = CreateShaderModule(vertCode);
		VkShaderModule fragMod = CreateShaderModule(fragCode);

		VkPipelineShaderStageCreateInfo stages[] = {
			MakeStage(VK_SHADER_STAGE_VERTEX_BIT, vertMod),
			MakeStage(VK_SHADER_STAGE_FRAGMENT_BIT, fragMod)
		};

		VkPipelineVertexInputStateCreateInfo vi{};
		vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vi.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
		vi.pVertexBindingDescriptions = bindings.data();
		vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribs.size());
		vi.pVertexAttributeDescriptions = attribs.data();

		VkPipelineInputAssemblyStateCreateInfo ia{};
		ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		VkPipelineViewportStateCreateInfo vp{};
		vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		vp.viewportCount = 1;
		vp.scissorCount = 1;

		VkPipelineRasterizationStateCreateInfo rs{};
		rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rs.polygonMode = VK_POLYGON_MODE_FILL;
		rs.cullMode = VK_CULL_MODE_NONE;
		rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rs.lineWidth = 1.0f;

		VkPipelineMultisampleStateCreateInfo ms{};
		ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		ms.rasterizationSamples = msaaSamples;

		VkPipelineDepthStencilStateCreateInfo ds{};
		ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		ds.depthTestEnable = VK_TRUE;
		ds.depthWriteEnable = VK_FALSE;
		ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

		VkPipelineColorBlendAttachmentState cbAttach{};
		cbAttach.colorWriteMask =
			VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		cbAttach.blendEnable = VK_TRUE;
		cbAttach.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		cbAttach.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		cbAttach.colorBlendOp = VK_BLEND_OP_ADD;
		cbAttach.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		cbAttach.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		cbAttach.alphaBlendOp = VK_BLEND_OP_ADD;

		VkPipelineColorBlendStateCreateInfo cb{};
		cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		cb.attachmentCount = 1;
		cb.pAttachments = &cbAttach;

		std::array<VkDynamicState, 2> dyn = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynInfo{};
		dynInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynInfo.dynamicStateCount = static_cast<uint32_t>(dyn.size());
		dynInfo.pDynamicStates = dyn.data();

		std::array<VkDescriptorSetLayout, 2> setLayouts = { set0Layout, set1BindlessLayout };

		VkPipelineLayoutCreateInfo pl{};
		pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pl.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
		pl.pSetLayouts = setLayouts.data();

		if (vkCreatePipelineLayout(device, &pl, nullptr, &msdfTextPipelineLayout) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create msdf pipeline layout!");
		}

		VkGraphicsPipelineCreateInfo gp{};
		gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		gp.stageCount = 2;
		gp.pStages = stages;
		gp.pVertexInputState = &vi;
		gp.pInputAssemblyState = &ia;
		gp.pViewportState = &vp;
		gp.pRasterizationState = &rs;
		gp.pMultisampleState = &ms;
		gp.pDepthStencilState = &ds;
		gp.pColorBlendState = &cb;
		gp.pDynamicState = &dynInfo;
		gp.layout = msdfTextPipelineLayout;
		gp.renderPass = renderPass;
		gp.subpass = 0;

		if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gp, nullptr, &msdfTextPipeline) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create msdf pipeline!");
		}

		vkDestroyShaderModule(device, vertMod, nullptr);
		vkDestroyShaderModule(device, fragMod, nullptr);
	}

	void VulkanPipelineManager::CreateCullComputePipeline(const std::string& compPath, VkDescriptorSetLayout set0Layout)
	{
		DestroyPipelinePair(device, cullComputePipeline, cullComputePipelineLayout);

		auto compCode = ReadFile(compPath);
		VkShaderModule compMod = CreateShaderModule(compCode);

		VkPipelineShaderStageCreateInfo stage = MakeStage(VK_SHADER_STAGE_COMPUTE_BIT, compMod);

		VkPushConstantRange pc{};
		pc.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		pc.offset = 0;
		pc.size = sizeof(uint32_t) * 4; // instanceCount + padding

		VkPipelineLayoutCreateInfo pl{};
		pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pl.setLayoutCount = 1;
		pl.pSetLayouts = &set0Layout;
		pl.pushConstantRangeCount = 1;
		pl.pPushConstantRanges = &pc;

		if (vkCreatePipelineLayout(device, &pl, nullptr, &cullComputePipelineLayout) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create compute pipeline layout!");
		}

		VkComputePipelineCreateInfo cp{};
		cp.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		cp.stage = stage;
		cp.layout = cullComputePipelineLayout;

		if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cp, nullptr, &cullComputePipeline) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create compute pipeline!");
		}

		vkDestroyShaderModule(device, compMod, nullptr);
	}

	void VulkanPipelineManager::CreateCullTrueBatchComputePipelines(
		const std::string& countPath,
		const char* countEntry,
		const std::string& scan512Path,
		const char* scan512Entry,
		const std::string& scanGroupsPath,
		const char* scanGroupsEntry,
		const std::string& fixupPath,
		const char* fixupEntry,
		const std::string& scatterPath,
		const char* scatterEntry,
		const std::string& buildPath,
		const char* buildEntry,
		VkDescriptorSetLayout set0Layout
	)
	{
		DestroyPipelineOnly(device, cullTrueBatchCountPipeline);
		DestroyPipelineOnly(device, cullTrueBatchScan512Pipeline);
		DestroyPipelineOnly(device, cullTrueBatchScanGroupsPipeline);
		DestroyPipelineOnly(device, cullTrueBatchFixupPipeline);
		DestroyPipelineOnly(device, cullTrueBatchScatterPipeline);
		DestroyPipelineOnly(device, cullTrueBatchBuildPipeline);

		if (cullTrueBatchPipelineLayout)
		{
			vkDestroyPipelineLayout(device, cullTrueBatchPipelineLayout, nullptr);
			cullTrueBatchPipelineLayout = VK_NULL_HANDLE;
		}

		VkPushConstantRange pc{};
		pc.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		pc.offset = 0;
		pc.size = sizeof(uint32_t) * 4;

		VkPipelineLayoutCreateInfo pl{};
		pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pl.setLayoutCount = 1;
		pl.pSetLayouts = &set0Layout;
		pl.pushConstantRangeCount = 1;
		pl.pPushConstantRanges = &pc;

		if (vkCreatePipelineLayout(device, &pl, nullptr, &cullTrueBatchPipelineLayout) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create true-batch compute pipeline layout!");
		}

		auto MakeComputePipeline = [&](const std::string& spvPath, const char* entry, VkPipeline& outPipe)
		{
			auto compCode = ReadFile(spvPath);
			VkShaderModule compMod = CreateShaderModule(compCode);

			VkPipelineShaderStageCreateInfo stage = MakeComputeStage(compMod, entry);

			VkComputePipelineCreateInfo cp{};
			cp.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
			cp.stage = stage;
			cp.layout = cullTrueBatchPipelineLayout;

			if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cp, nullptr, &outPipe) != VK_SUCCESS)
			{
				vkDestroyShaderModule(device, compMod, nullptr);
				throw std::runtime_error("Failed to create true-batch compute pipeline: " + spvPath);
			}

			vkDestroyShaderModule(device, compMod, nullptr);
		};

		MakeComputePipeline(countPath, countEntry, cullTrueBatchCountPipeline);
		MakeComputePipeline(scan512Path, scan512Entry, cullTrueBatchScan512Pipeline);
		MakeComputePipeline(scanGroupsPath, scanGroupsEntry, cullTrueBatchScanGroupsPipeline);
		MakeComputePipeline(fixupPath, fixupEntry, cullTrueBatchFixupPipeline);
		MakeComputePipeline(scatterPath, scatterEntry, cullTrueBatchScatterPipeline);
		MakeComputePipeline(buildPath, buildEntry, cullTrueBatchBuildPipeline);
	}

	void VulkanPipelineManager::Cleanup()
	{
		if (renderPass)
		{
			vkDestroyRenderPass(device, renderPass, nullptr);
			renderPass = VK_NULL_HANDLE;
		}

		DestroyPipelinePair(device, graphicsPipeline, graphicsPipelineLayout);
		DestroyPipelinePair(device, decoratorPipeline, decoratorPipelineLayout);
		DestroyPipelinePair(device, msdfTextPipeline, msdfTextPipelineLayout);
		DestroyPipelinePair(device, cullComputePipeline, cullComputePipelineLayout);

		DestroyPipelineOnly(device, cullTrueBatchCountPipeline);
		DestroyPipelineOnly(device, cullTrueBatchScan512Pipeline);
		DestroyPipelineOnly(device, cullTrueBatchScanGroupsPipeline);
		DestroyPipelineOnly(device, cullTrueBatchFixupPipeline);
		DestroyPipelineOnly(device, cullTrueBatchScatterPipeline);
		DestroyPipelineOnly(device, cullTrueBatchBuildPipeline);

		if (cullTrueBatchPipelineLayout)
		{
			vkDestroyPipelineLayout(device, cullTrueBatchPipelineLayout, nullptr);
			cullTrueBatchPipelineLayout = VK_NULL_HANDLE;
		}
	}

}
