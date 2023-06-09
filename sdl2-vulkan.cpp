// cl /std:c++17 /nologo /Zi /EHsc /Iinclude sdl2-vulkan.cpp lib/sdl2.lib lib/sdl2main.lib
// glslangValidator shaders/shader.vert -V -o shaders/shader.vert.spv
// glslangValidator shaders/shader.frag -V -o shaders/shader.frag.spv

#define _CRT_SECURE_NO_WARNINGS
#define SDL_MAIN_HANDLED
#define VK_USE_PLATFORM_WIN32_KHR
#define VKBIND_IMPLEMENTATION

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <SDL2/SDL_vulkan.h>
#include <string.h>
#include <vector>
#include <vkbind.h>

constexpr int MAX_FRAMES_IN_FLIGHT = 3;

#define array_size(a) (sizeof(a) / sizeof(a[0]))

static std::vector<uint8_t> read_entire_file(const char *file) {
  FILE *fd = fopen(file, "rb");
  if (fd == nullptr) {
    return {};
  }

  fseek(fd, 0, SEEK_END);
  int size = ftell(fd);
  rewind(fd);

  std::vector<uint8_t> buf(size);
  fread(buf.data(), 1, size, fd);

  fclose(fd);
  return buf;
}

struct GPUBufferInfo {
  VkDevice device;
  VkPhysicalDeviceMemoryProperties *memory_props;
  VkDeviceSize size;
  VkBufferUsageFlags usage;
  VkMemoryPropertyFlags prop_flags;
};

struct GPUBuffer {
  VkBuffer buffer;
  VkMemoryRequirements requirements;
  VkDeviceMemory memory;
};

static bool create_buffer(GPUBufferInfo *create_info, GPUBuffer *out) {
  VkResult res;

  VkBufferCreateInfo buffer_info = {};
  buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.size = create_info->size;
  buffer_info.usage = create_info->usage;
  buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VkBuffer buffer = {};
  res = vkCreateBuffer(create_info->device, &buffer_info, nullptr, &buffer);
  if (res != VK_SUCCESS) {
    return false;
  }

  VkMemoryRequirements requirements = {};
  vkGetBufferMemoryRequirements(create_info->device, buffer, &requirements);

  VkMemoryAllocateInfo allocate_info = {};
  allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocate_info.allocationSize = requirements.size;
  allocate_info.memoryTypeIndex = (uint32_t)-1;

  for (uint32_t i = 0; i < create_info->memory_props->memoryTypeCount; i++) {
    if ((requirements.memoryTypeBits & (1 << i)) != 0) {
      if ((create_info->memory_props->memoryTypes[i].propertyFlags &
           create_info->prop_flags) == create_info->prop_flags) {
        allocate_info.memoryTypeIndex = i;
        break;
      }
    }
  }
  if (allocate_info.memoryTypeIndex == (uint32_t)-1) {
    return false;
  }

  VkDeviceMemory memory = nullptr;
  res = vkAllocateMemory(create_info->device, &allocate_info, nullptr, &memory);
  if (res != VK_SUCCESS) {
    return false;
  }

  res = vkBindBufferMemory(create_info->device, buffer, memory, 0);
  if (res != VK_SUCCESS) {
    return false;
  }

  out->buffer = buffer;
  out->requirements = requirements;
  out->memory = memory;
  return true;
}

struct SwapchainInfo {
  VkPhysicalDevice physical_device;
  VkDevice device;
  VkSurfaceKHR surface;
  VkRenderPass render_pass;
  VkSurfaceFormatKHR surface_format;
  int recreate_width;
  int recreate_height;
};

struct SwapchainResult {
  VkSwapchainKHR swapchain;
  std::vector<VkImageView> image_views;
  std::vector<VkFramebuffer> framebuffers;
};

uint32_t clamp(uint32_t x, uint32_t lhs, uint32_t rhs) {
  if (x < lhs) {
    return lhs;
  }
  if (x > rhs) {
    return rhs;
  }
  return x;
}

static bool create_swapchain(SwapchainInfo *create_info,
                             SwapchainResult *in_out) {
  VkResult res;

  VkSurfaceCapabilitiesKHR capabilities = {};
  res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
      create_info->physical_device, create_info->surface, &capabilities);

  VkExtent2D extent = capabilities.currentExtent;
  if (create_info->recreate_width != 0 && create_info->recreate_height != 0) {
    extent.width =
        clamp(create_info->recreate_width, capabilities.minImageExtent.width,
              capabilities.maxImageExtent.width);
    extent.height =
        clamp(create_info->recreate_height, capabilities.minImageExtent.height,
              capabilities.maxImageExtent.height);
  }

  uint32_t image_count = capabilities.minImageCount + 1;
  if (capabilities.maxImageCount > 0 &&
      image_count > capabilities.maxImageCount) {
    image_count = capabilities.maxImageCount;
  }

  VkSwapchainCreateInfoKHR swapchain_info = {};
  swapchain_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  swapchain_info.surface = create_info->surface;
  swapchain_info.minImageCount = image_count;
  swapchain_info.imageFormat = create_info->surface_format.format;
  swapchain_info.imageColorSpace = create_info->surface_format.colorSpace;
  swapchain_info.imageExtent = extent;
  swapchain_info.imageArrayLayers = 1;
  swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  swapchain_info.preTransform = capabilities.currentTransform;
  swapchain_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  swapchain_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
  swapchain_info.clipped = VK_TRUE;

  res = vkCreateSwapchainKHR(create_info->device, &swapchain_info, nullptr,
                             &in_out->swapchain);
  if (res != VK_SUCCESS) {
    return false;
  }

  vkGetSwapchainImagesKHR(create_info->device, in_out->swapchain, &image_count,
                          nullptr);
  std::vector<VkImage> swapchain_images(image_count);
  vkGetSwapchainImagesKHR(create_info->device, in_out->swapchain, &image_count,
                          swapchain_images.data());

  in_out->image_views.clear();
  in_out->image_views.reserve(image_count);
  for (VkImage image : swapchain_images) {
    VkImageViewCreateInfo image_view_info = {};
    image_view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    image_view_info.image = image;
    image_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    image_view_info.format = create_info->surface_format.format;
    image_view_info.components.r = VK_COMPONENT_SWIZZLE_R;
    image_view_info.components.g = VK_COMPONENT_SWIZZLE_G;
    image_view_info.components.b = VK_COMPONENT_SWIZZLE_B;
    image_view_info.components.a = VK_COMPONENT_SWIZZLE_A;
    image_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_view_info.subresourceRange.levelCount = 1;
    image_view_info.subresourceRange.layerCount = 1;

    VkImageView image_view = nullptr;
    res = vkCreateImageView(create_info->device, &image_view_info, nullptr,
                            &image_view);
    if (res != VK_SUCCESS) {
      return false;
    }
    in_out->image_views.push_back(image_view);
  }

  in_out->framebuffers.clear();
  in_out->framebuffers.resize(in_out->image_views.size());
  for (int i = 0; i < in_out->framebuffers.size(); i++) {
    VkImageView attachments[] = {in_out->image_views[i]};

    VkFramebufferCreateInfo framebuffer_info = {};
    framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer_info.renderPass = create_info->render_pass;
    framebuffer_info.attachmentCount = array_size(attachments);
    framebuffer_info.pAttachments = attachments;
    framebuffer_info.width = extent.width;
    framebuffer_info.height = extent.height;
    framebuffer_info.layers = 1;
    res = vkCreateFramebuffer(create_info->device, &framebuffer_info, nullptr,
                              &in_out->framebuffers[i]);
    if (res != VK_SUCCESS) {
      return false;
    }
  }

  return true;
}

static void destory_swapchain(VkDevice device, SwapchainResult *scr) {
  for (VkFramebuffer framebuffer : scr->framebuffers) {
    vkDestroyFramebuffer(device, framebuffer, nullptr);
  }

  for (VkImageView image_view : scr->image_views) {
    vkDestroyImageView(device, image_view, nullptr);
  }

  vkDestroySwapchainKHR(device, scr->swapchain, nullptr);
}

int main() {
  VkbAPI vk = {};
  vkbInit(&vk);

  const char *title = "SDL2 + Vulkan";

  SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);

  int width = 800, height = 600;
  SDL_Window *window =
      SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                       width, height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN);

  const char *instance_layers[] = {"VK_LAYER_KHRONOS_validation"};

  std::vector<const char *> instance_extensions;
  {
    uint32_t count = 0;
    SDL_Vulkan_GetInstanceExtensions(window, &count, nullptr);
    instance_extensions.resize(count);
    SDL_Vulkan_GetInstanceExtensions(window, &count,
                                     instance_extensions.data());
  }

  VkInstance instance = nullptr;
  {
    VkInstanceCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.enabledLayerCount = array_size(instance_layers);
    info.ppEnabledLayerNames = instance_layers;
    info.enabledExtensionCount = instance_extensions.size();
    info.ppEnabledExtensionNames = instance_extensions.data();

    vkCreateInstance(&info, nullptr, &instance);
  }

  vkbInitInstanceAPI(instance, &vk);
  vkbBindAPI(&vk);

  VkSurfaceKHR surface = nullptr;
  {
    SDL_SysWMinfo wm = {};
    SDL_GetWindowWMInfo(window, &wm);

    VkWin32SurfaceCreateInfoKHR info = {};
    info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    info.hwnd = wm.info.win.window;

    vkCreateWin32SurfaceKHR(instance, &info, nullptr, &surface);
  }

  VkPhysicalDevice physical_device = nullptr;
  uint32_t queue_family_index = (uint32_t)-1;
  {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> physical_devices(count);
    vkEnumeratePhysicalDevices(instance, &count, physical_devices.data());

    for (VkPhysicalDevice physical : physical_devices) {
      VkPhysicalDeviceProperties properties = {};
      vkGetPhysicalDeviceProperties(physical, &properties);

      VkSurfaceCapabilitiesKHR capabilities = {};
      VkResult res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
          physical, surface, &capabilities);
      if (res != VK_SUCCESS) {
        continue;
      }

      if (capabilities.maxImageCount < 2) {
        continue;
      }

      uint32_t count = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
      std::vector<VkQueueFamilyProperties> queue_families(count);
      vkGetPhysicalDeviceQueueFamilyProperties(physical, &count,
                                               queue_families.data());

      uint32_t graphics_family = (uint32_t)-1;
      for (int i = 0; i < queue_families.size(); i++) {
        if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
          graphics_family = i;
          break;
        }
      }

      VkBool32 supported = VK_FALSE;
      res = vkGetPhysicalDeviceSurfaceSupportKHR(physical, graphics_family,
                                                 surface, &supported);
      if (res == VK_SUCCESS && supported) {
        physical_device = physical;
        queue_family_index = graphics_family;
        break;
      }
    }
  }

  VkPhysicalDeviceMemoryProperties memory_props = {};
  vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_props);

  VkDevice device = nullptr;
  {
    VkPhysicalDeviceFeatures features = {};
    vkGetPhysicalDeviceFeatures(physical_device, &features);

    float queue_priorities[] = {1.0f};

    VkDeviceQueueCreateInfo queue_info = {};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = queue_family_index;
    queue_info.queueCount = array_size(queue_priorities);
    queue_info.pQueuePriorities = queue_priorities;

    const char *extensions[] = {"VK_KHR_swapchain"};

    VkDeviceCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queue_info;
    info.enabledExtensionCount = array_size(extensions);
    info.ppEnabledExtensionNames = extensions;
    info.pEnabledFeatures = &features;

    vkCreateDevice(physical_device, &info, nullptr, &device);
  }

  VkSurfaceFormatKHR surface_format = {};
  {
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &count,
                                         nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &count,
                                         formats.data());

    for (VkSurfaceFormatKHR format : formats) {
      if (format.format == VK_FORMAT_R8G8B8A8_UNORM ||
          format.format == VK_FORMAT_B8G8R8A8_UNORM) {
        surface_format = format;
        break;
      }
    }
  }

  VkSemaphore acquire_semaphores[MAX_FRAMES_IN_FLIGHT] = {};
  VkSemaphore release_semaphores[MAX_FRAMES_IN_FLIGHT] = {};
  {
    VkSemaphoreCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      vkCreateSemaphore(device, &info, nullptr, &acquire_semaphores[i]);
      vkCreateSemaphore(device, &info, nullptr, &release_semaphores[i]);
    }
  }

  VkCommandPool cmd_pool = nullptr;
  {
    VkCommandPoolCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    info.queueFamilyIndex = queue_family_index;

    vkCreateCommandPool(device, &info, nullptr, &cmd_pool);
  }

  VkCommandBuffer cmd_buffers[MAX_FRAMES_IN_FLIGHT] = {};
  {
    VkCommandBufferAllocateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    info.commandPool = cmd_pool;
    info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    info.commandBufferCount = MAX_FRAMES_IN_FLIGHT;

    vkAllocateCommandBuffers(device, &info, cmd_buffers);
  }

  VkFence cmd_buffer_fences[MAX_FRAMES_IN_FLIGHT] = {};
  {
    VkFenceCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < array_size(cmd_buffer_fences); i++) {
      vkCreateFence(device, &info, nullptr, &cmd_buffer_fences[i]);
    }
  }

  VkQueue queue = nullptr;
  vkGetDeviceQueue(device, queue_family_index, 0, &queue);

  struct Vertex {
    float position[3];
    float color[4];
  };

  Vertex vertices[] = {
      {{+0.0f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
      {{-0.5f, +0.5f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
      {{+0.5f, +0.5f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
  };

  GPUBuffer staging_buffer = {};
  {
    GPUBufferInfo info = {};
    info.device = device;
    info.memory_props = &memory_props;
    info.size = sizeof(vertices);
    info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    info.prop_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;

    create_buffer(&info, &staging_buffer);
  }

  {
    void *data = nullptr;
    vkMapMemory(device, staging_buffer.memory, 0, sizeof(vertices), 0, &data);
    memcpy(data, vertices, sizeof(vertices));
    vkUnmapMemory(device, staging_buffer.memory);
  }

  GPUBuffer vertex_buffer = {};
  {
    GPUBufferInfo info = {};
    info.device = device;
    info.memory_props = &memory_props;
    info.size = sizeof(vertices);
    info.usage =
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.prop_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    create_buffer(&info, &vertex_buffer);
  }

  {
    VkCommandBufferBeginInfo begin = {};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd_buffers[0], &begin);
    {
      VkBufferCopy region = {};
      region.srcOffset = 0;
      region.dstOffset = 0;
      region.size = sizeof(vertices);
      vkCmdCopyBuffer(cmd_buffers[0], staging_buffer.buffer,
                      vertex_buffer.buffer, 1, &region);
    }
    vkEndCommandBuffer(cmd_buffers[0]);

    VkSubmitInfo submit = {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd_buffers[0];
    vkQueueSubmit(queue, 1, &submit, 0);

    vkQueueWaitIdle(queue);
  }

  vkDestroyBuffer(device, staging_buffer.buffer, nullptr);
  vkFreeMemory(device, staging_buffer.memory, nullptr);

  VkPipelineLayout pipeline_layout = nullptr;
  {
    VkPipelineLayoutCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

    vkCreatePipelineLayout(device, &info, nullptr, &pipeline_layout);
  }

  VkRenderPass render_pass = {};
  {
    VkAttachmentDescription attachments[1] = {};
    attachments[0].format = surface_format.format;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference color_attachment = {};
    color_attachment.attachment = 0;
    color_attachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment;

    VkRenderPassCreateInfo render_pass_info = {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = array_size(attachments);
    render_pass_info.pAttachments = attachments;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;

    vkCreateRenderPass(device, &render_pass_info, nullptr, &render_pass);
  }

  VkShaderModule vertex_shader = nullptr;
  {
    std::vector<uint8_t> spv = read_entire_file("shaders/shader.vert.spv");
    VkShaderModuleCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = spv.size();
    info.pCode = (uint32_t *)spv.data();

    vkCreateShaderModule(device, &info, nullptr, &vertex_shader);
  }

  VkShaderModule fragment_shader = nullptr;
  {
    std::vector<uint8_t> spv = read_entire_file("shaders/shader.frag.spv");
    VkShaderModuleCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = spv.size();
    info.pCode = (uint32_t *)spv.data();

    vkCreateShaderModule(device, &info, nullptr, &fragment_shader);
  }

  VkPipeline pipeline = nullptr;
  {
    VkPipelineShaderStageCreateInfo shader_stages[2] = {};
    shader_stages[0].sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shader_stages[0].module = vertex_shader;
    shader_stages[0].pName = "main";

    shader_stages[1].sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shader_stages[1].module = fragment_shader;
    shader_stages[1].pName = "main";

    VkVertexInputBindingDescription vertex_bindings[1] = {};
    vertex_bindings[0] = {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};

    VkVertexInputAttributeDescription vertex_attributes[2] = {};
    vertex_attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT,
                            offsetof(Vertex, position)};
    vertex_attributes[1] = {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
                            offsetof(Vertex, color)};

    VkPipelineVertexInputStateCreateInfo vertex_input_state = {};
    vertex_input_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_state.vertexBindingDescriptionCount = 1;
    vertex_input_state.pVertexBindingDescriptions = vertex_bindings;
    vertex_input_state.vertexAttributeDescriptionCount =
        array_size(vertex_attributes);
    vertex_input_state.pVertexAttributeDescriptions = vertex_attributes;

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {};
    input_assembly_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewport_state = {};
    viewport_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterization_state = {};
    rasterization_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization_state.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterization_state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization_state.depthBiasClamp = 1;
    rasterization_state.lineWidth = 1;

    VkPipelineMultisampleStateCreateInfo multisample_state = {};
    multisample_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample_state.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisample_state.minSampleShading = 1;

    VkPipelineColorBlendAttachmentState color_blend_attachments[1] = {};
    color_blend_attachments[0].colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_blend_state = {};
    color_blend_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend_state.attachmentCount = array_size(color_blend_attachments);
    color_blend_state.pAttachments = color_blend_attachments;

    VkDynamicState dynamic_states[2] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };

    VkPipelineDynamicStateCreateInfo dynamic_state = {};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = array_size(dynamic_states);
    dynamic_state.pDynamicStates = dynamic_states;

    VkGraphicsPipelineCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.stageCount = array_size(shader_stages);
    info.pStages = shader_stages;
    info.pVertexInputState = &vertex_input_state;
    info.pInputAssemblyState = &input_assembly_state;
    info.pViewportState = &viewport_state;
    info.pRasterizationState = &rasterization_state;
    info.pMultisampleState = &multisample_state;
    info.pColorBlendState = &color_blend_state;
    info.pDynamicState = &dynamic_state;
    info.layout = pipeline_layout;
    info.renderPass = render_pass;

    vkCreateGraphicsPipelines(device, 0, 1, &info, nullptr, &pipeline);
  }

  SwapchainResult swapchain = {};
  {
    SwapchainInfo info = {};
    info.physical_device = physical_device;
    info.device = device;
    info.surface = surface;
    info.render_pass = render_pass;
    info.surface_format = surface_format;
    create_swapchain(&info, &swapchain);
  }

  int resize_width = width;
  int resize_height = height;

  int in_flight_frame = 0;
  bool should_quit = false;
  while (!should_quit) {
    SDL_Event e = {};
    while (SDL_PollEvent(&e)) {
      switch (e.type) {
      case SDL_QUIT:
        should_quit = true;
        break;
      }

      switch (e.window.event) {
      case SDL_WINDOWEVENT_MINIMIZED:
        while (SDL_WaitEvent(&e)) {
          if (e.window.event == SDL_WINDOWEVENT_RESTORED) {
            break;
          }
        }
        break;
      }
    }

    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window, &width, &height);
    bool resized = width != resize_width || height != resize_height;

    VkCommandBuffer cmd_buf = cmd_buffers[in_flight_frame];

    vkWaitForFences(device, 1, &cmd_buffer_fences[in_flight_frame], VK_TRUE,
                    UINT64_MAX);

    uint32_t image_index = 0;
    VkResult res = vkAcquireNextImageKHR(
        device, swapchain.swapchain, UINT64_MAX,
        acquire_semaphores[in_flight_frame], VK_NULL_HANDLE, &image_index);
    if (res == VK_ERROR_OUT_OF_DATE_KHR || resized) {
      resize_width = width;
      resize_height = height;

      vkDeviceWaitIdle(device);

      destory_swapchain(device, &swapchain);

      SwapchainInfo info = {};
      info.physical_device = physical_device;
      info.device = device;
      info.surface = surface;
      info.render_pass = render_pass;
      info.surface_format = surface_format;
      info.recreate_width = width;
      info.recreate_height = height;
      create_swapchain(&info, &swapchain);

      continue;
    }

    vkResetFences(device, 1, &cmd_buffer_fences[in_flight_frame]);

    VkCommandBufferBeginInfo command_buffer_begin = {};
    command_buffer_begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    command_buffer_begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkResetCommandBuffer(cmd_buf, 0);
    vkBeginCommandBuffer(cmd_buf, &command_buffer_begin);

    VkClearValue clear = {};
    clear.color.float32[0] = 0.5f;
    clear.color.float32[1] = 0.5f;
    clear.color.float32[2] = 0.5f;
    clear.color.float32[3] = 1.0f;

    VkRenderPassBeginInfo pass_begin = {};
    pass_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    pass_begin.renderPass = render_pass;
    pass_begin.framebuffer = swapchain.framebuffers[image_index];
    pass_begin.renderArea.extent.width = width;
    pass_begin.renderArea.extent.height = height;
    pass_begin.clearValueCount = 1;
    pass_begin.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd_buf, &pass_begin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport = {};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = (float)width;
    viewport.height = (float)height;
    viewport.minDepth = 0;
    viewport.maxDepth = 1;
    vkCmdSetViewport(cmd_buf, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent.width = width;
    scissor.extent.height = height;
    vkCmdSetScissor(cmd_buf, 0, 1, &scissor);

    vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd_buf, 0, 1, &vertex_buffer.buffer, &offset);

    vkCmdDraw(cmd_buf, 3, 1, 0, 0);

    vkCmdEndRenderPass(cmd_buf);
    vkEndCommandBuffer(cmd_buf);

    VkPipelineStageFlags wait_stages[1] = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    };

    VkSubmitInfo submit = {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &acquire_semaphores[in_flight_frame];
    submit.pWaitDstStageMask = wait_stages;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd_buf;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &release_semaphores[in_flight_frame];
    vkQueueSubmit(queue, 1, &submit, cmd_buffer_fences[in_flight_frame]);

    VkPresentInfoKHR info = {};
    info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &release_semaphores[in_flight_frame];
    info.swapchainCount = 1;
    info.pSwapchains = &swapchain.swapchain;
    info.pImageIndices = &image_index;
    res = vkQueuePresentKHR(queue, &info);
    if (res == VK_ERROR_OUT_OF_DATE_KHR || resized) {
      resize_width = width;
      resize_height = height;

      vkDeviceWaitIdle(device);

      destory_swapchain(device, &swapchain);

      SwapchainInfo info = {};
      info.physical_device = physical_device;
      info.device = device;
      info.surface = surface;
      info.render_pass = render_pass;
      info.surface_format = surface_format;
      info.recreate_width = width;
      info.recreate_height = height;
      create_swapchain(&info, &swapchain);
    }

    in_flight_frame = (in_flight_frame + 1) % MAX_FRAMES_IN_FLIGHT;
  }
}
