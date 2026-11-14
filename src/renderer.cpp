#include "renderer.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

#include "mesh_frag.h"
#include "mesh_vert.h"
#include "particle_frag.h"
#include "particle_vert.h"
#include "sky_frag.h"
#include "sky_vert.h"
#include "ui_frag.h"
#include "ui_vert.h"

namespace {
const size_t kMaxInstances = 24000;
const size_t kMaxParticles = 40000;
const size_t kMaxUIVerts = 160000;

void check(VkResult r, const char* what) {
  if (r != VK_SUCCESS) throw std::runtime_error(std::string(what) + " failed (VkResult " + std::to_string((int)r) + ")");
}

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
  if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) std::fprintf(stderr, "[vulkan] %s\n", data->pMessage);
  return VK_FALSE;
}
} // namespace

bool Renderer::init(GLFWwindow* window, const RendererOptions& opts) {
  window_ = window;
  opts_ = opts;
  try {
    createInstance();
    check(glfwCreateWindowSurface(instance_, window_, nullptr, &surface_), "glfwCreateWindowSurface");
    pickDevice();
    createDevice();
    createSwapchain();
    createRenderPass();
    createFramebuffers();
    createDescriptors();
    createPipelines();
    createMeshBuffers();
    createFrameResources();
  } catch (const std::exception& e) {
    std::fprintf(stderr, "Renderer init failed: %s\n", e.what());
    return false;
  }
  return true;
}

void Renderer::createInstance() {
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "Hullbreaker";
  app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
  app.pEngineName = "hullbreaker";
  app.apiVersion = VK_API_VERSION_1_0;

  uint32_t n = 0;
  const char** glfwExt = glfwGetRequiredInstanceExtensions(&n);
  if (!glfwExt) throw std::runtime_error("GLFW found no Vulkan surface extensions (is a Vulkan driver installed?)");
  std::vector<const char*> exts(glfwExt, glfwExt + n);
  std::vector<const char*> layers;

  if (opts_.validation) {
    uint32_t lc = 0;
    vkEnumerateInstanceLayerProperties(&lc, nullptr);
    std::vector<VkLayerProperties> lp(lc);
    vkEnumerateInstanceLayerProperties(&lc, lp.data());
    bool found = false;
    for (auto& l : lp) found |= std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0;
    if (found) {
      layers.push_back("VK_LAYER_KHRONOS_validation");
      exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    } else {
      std::fprintf(stderr, "validation requested but VK_LAYER_KHRONOS_validation is not installed\n");
      opts_.validation = false;
    }
  }

  VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ci.pApplicationInfo = &app;
  ci.enabledExtensionCount = (uint32_t)exts.size();
  ci.ppEnabledExtensionNames = exts.data();
  ci.enabledLayerCount = (uint32_t)layers.size();
  ci.ppEnabledLayerNames = layers.data();
  check(vkCreateInstance(&ci, nullptr, &instance_), "vkCreateInstance");

  if (opts_.validation) {
    VkDebugUtilsMessengerCreateInfoEXT di{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    di.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    di.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    di.pfnUserCallback = debugCallback;
    auto fn = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT");
    if (fn) fn(instance_, &di, nullptr, &debug_);
  }
}

void Renderer::pickDevice() {
  uint32_t n = 0;
  vkEnumeratePhysicalDevices(instance_, &n, nullptr);
  if (n == 0) throw std::runtime_error("no Vulkan-capable GPU found");
  std::vector<VkPhysicalDevice> devs(n);
  vkEnumeratePhysicalDevices(instance_, &n, devs.data());

  int forced = -1;
  if (const char* e = std::getenv("HULLBREAKER_GPU")) forced = std::atoi(e);

  int bestScore = -1;
  for (uint32_t di = 0; di < n; ++di) {
    VkPhysicalDevice d = devs[di];
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(d, &props);

    uint32_t ec = 0;
    vkEnumerateDeviceExtensionProperties(d, nullptr, &ec, nullptr);
    std::vector<VkExtensionProperties> ep(ec);
    vkEnumerateDeviceExtensionProperties(d, nullptr, &ec, ep.data());
    bool hasSwap = false;
    for (auto& e : ep) hasSwap |= std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
    if (!hasSwap) continue;

    uint32_t qc = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(d, &qc, nullptr);
    std::vector<VkQueueFamilyProperties> qp(qc);
    vkGetPhysicalDeviceQueueFamilyProperties(d, &qc, qp.data());
    int gfx = -1, present = -1;
    for (uint32_t i = 0; i < qc; ++i) {
      VkBool32 sup = VK_FALSE;
      vkGetPhysicalDeviceSurfaceSupportKHR(d, i, surface_, &sup);
      bool g = (qp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
      if (g && sup) { gfx = present = (int)i; break; }
      if (g && gfx < 0) gfx = (int)i;
      if (sup && present < 0) present = (int)i;
    }
    if (gfx < 0 || present < 0) continue;

    uint32_t fc = 0, pc = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(d, surface_, &fc, nullptr);
    vkGetPhysicalDeviceSurfacePresentModesKHR(d, surface_, &pc, nullptr);
    if (fc == 0 || pc == 0) continue;

    int score = 1;
    switch (props.deviceType) {
      case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: score = 1000; break;
      case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: score = 500; break;
      case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: score = 200; break;
      default: score = 10; break;
    }
    if (forced == (int)di) score = 100000;
    if (score > bestScore) {
      bestScore = score;
      phys_ = d;
      gfxFamily_ = (uint32_t)gfx;
      presentFamily_ = (uint32_t)present;
      gpuName_ = props.deviceName;
    }
  }
  if (phys_ == VK_NULL_HANDLE) throw std::runtime_error("no GPU can present to this window");

  // sample count
  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(phys_, &props);
  VkSampleCountFlags ok = props.limits.framebufferColorSampleCounts & props.limits.framebufferDepthSampleCounts;
  int want = opts_.msaa;
  samples_ = VK_SAMPLE_COUNT_1_BIT;
  for (int s : {8, 4, 2}) {
    if (want >= s && (ok & (VkSampleCountFlags)s)) { samples_ = (VkSampleCountFlagBits)s; break; }
  }

  // depth format
  for (VkFormat f : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM}) {
    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(phys_, f, &fp);
    if (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) { depthFormat_ = f; break; }
  }
  if (depthFormat_ == VK_FORMAT_UNDEFINED) throw std::runtime_error("no supported depth format");
}

void Renderer::createDevice() {
  std::vector<VkDeviceQueueCreateInfo> qcis;
  float prio = 1.0f;
  for (uint32_t fam : {gfxFamily_, presentFamily_}) {
    bool dup = false;
    for (auto& q : qcis) dup |= q.queueFamilyIndex == fam;
    if (dup) continue;
    VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    q.queueFamilyIndex = fam;
    q.queueCount = 1;
    q.pQueuePriorities = &prio;
    qcis.push_back(q);
  }
  const char* exts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  VkPhysicalDeviceFeatures feats{};
  VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  ci.queueCreateInfoCount = (uint32_t)qcis.size();
  ci.pQueueCreateInfos = qcis.data();
  ci.enabledExtensionCount = 1;
  ci.ppEnabledExtensionNames = exts;
  ci.pEnabledFeatures = &feats;
  check(vkCreateDevice(phys_, &ci, nullptr, &dev_), "vkCreateDevice");
  vkGetDeviceQueue(dev_, gfxFamily_, 0, &gfxQueue_);
  vkGetDeviceQueue(dev_, presentFamily_, 0, &presentQueue_);

  VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pi.queueFamilyIndex = gfxFamily_;
  check(vkCreateCommandPool(dev_, &pi, nullptr, &cmdPool_), "vkCreateCommandPool");
}

uint32_t Renderer::findMemoryType(uint32_t bits, VkMemoryPropertyFlags props) {
  VkPhysicalDeviceMemoryProperties mp;
  vkGetPhysicalDeviceMemoryProperties(phys_, &mp);
  for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
    if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props) return i;
  throw std::runtime_error("no suitable memory type");
}

Renderer::Buffer Renderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, bool map) {
  Buffer b;
  VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bi.size = size;
  bi.usage = usage;
  bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  check(vkCreateBuffer(dev_, &bi, nullptr, &b.buf), "vkCreateBuffer");
  VkMemoryRequirements mr;
  vkGetBufferMemoryRequirements(dev_, b.buf, &mr);
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  ai.allocationSize = mr.size;
  ai.memoryTypeIndex = findMemoryType(mr.memoryTypeBits, props);
  check(vkAllocateMemory(dev_, &ai, nullptr, &b.mem), "vkAllocateMemory");
  check(vkBindBufferMemory(dev_, b.buf, b.mem, 0), "vkBindBufferMemory");
  if (map) check(vkMapMemory(dev_, b.mem, 0, VK_WHOLE_SIZE, 0, &b.map), "vkMapMemory");
  return b;
}
void Renderer::destroyBuffer(Buffer& b) {
  if (b.buf) vkDestroyBuffer(dev_, b.buf, nullptr);
  if (b.mem) vkFreeMemory(dev_, b.mem, nullptr);
  b = Buffer();
}

Renderer::Image Renderer::createImage(VkFormat fmt, VkImageUsageFlags usage, VkImageAspectFlags aspect, VkSampleCountFlagBits samples) {
  Image im;
  VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ii.imageType = VK_IMAGE_TYPE_2D;
  ii.format = fmt;
  ii.extent = {extent_.width, extent_.height, 1};
  ii.mipLevels = 1;
  ii.arrayLayers = 1;
  ii.samples = samples;
  ii.tiling = VK_IMAGE_TILING_OPTIMAL;
  ii.usage = usage;
  ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  check(vkCreateImage(dev_, &ii, nullptr, &im.img), "vkCreateImage");
  VkMemoryRequirements mr;
  vkGetImageMemoryRequirements(dev_, im.img, &mr);
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  ai.allocationSize = mr.size;
  ai.memoryTypeIndex = findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  check(vkAllocateMemory(dev_, &ai, nullptr, &im.mem), "vkAllocateMemory(image)");
  check(vkBindImageMemory(dev_, im.img, im.mem, 0), "vkBindImageMemory");
  VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vi.image = im.img;
  vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vi.format = fmt;
  vi.subresourceRange = {aspect, 0, 1, 0, 1};
  check(vkCreateImageView(dev_, &vi, nullptr, &im.view), "vkCreateImageView");
  return im;
}
void Renderer::destroyImage(Image& i) {
  if (i.view) vkDestroyImageView(dev_, i.view, nullptr);
  if (i.img) vkDestroyImage(dev_, i.img, nullptr);
  if (i.mem) vkFreeMemory(dev_, i.mem, nullptr);
  i = Image();
}

void Renderer::createSwapchain() {
  VkSurfaceCapabilitiesKHR caps;
  check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys_, surface_, &caps), "surface caps");
  uint32_t fc = 0, pc = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(phys_, surface_, &fc, nullptr);
  std::vector<VkSurfaceFormatKHR> formats(fc);
  vkGetPhysicalDeviceSurfaceFormatsKHR(phys_, surface_, &fc, formats.data());
  vkGetPhysicalDeviceSurfacePresentModesKHR(phys_, surface_, &pc, nullptr);
  std::vector<VkPresentModeKHR> modes(pc);
  vkGetPhysicalDeviceSurfacePresentModesKHR(phys_, surface_, &pc, modes.data());

  VkSurfaceFormatKHR fmt = formats[0];
  bool srgb = false;
  for (auto& f : formats)
    if ((f.format == VK_FORMAT_B8G8R8A8_SRGB || f.format == VK_FORMAT_R8G8B8A8_SRGB) && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      fmt = f;
      srgb = true;
      break;
    }
  manualGamma_ = !srgb;
  swapFormat_ = fmt.format;

  VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
  if (!opts_.vsync) {
    for (auto m : modes) if (m == VK_PRESENT_MODE_IMMEDIATE_KHR) mode = m;
    for (auto m : modes) if (m == VK_PRESENT_MODE_MAILBOX_KHR) mode = m;
  }

  if (caps.currentExtent.width != 0xFFFFFFFFu) {
    extent_ = caps.currentExtent;
  } else {
    int w, h;
    glfwGetFramebufferSize(window_, &w, &h);
    extent_.width = std::clamp((uint32_t)w, caps.minImageExtent.width, caps.maxImageExtent.width);
    extent_.height = std::clamp((uint32_t)h, caps.minImageExtent.height, caps.maxImageExtent.height);
  }
  uint32_t count = caps.minImageCount + 1;
  if (caps.maxImageCount > 0 && count > caps.maxImageCount) count = caps.maxImageCount;

  VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  ci.surface = surface_;
  ci.minImageCount = count;
  ci.imageFormat = fmt.format;
  ci.imageColorSpace = fmt.colorSpace;
  ci.imageExtent = extent_;
  ci.imageArrayLayers = 1;
  ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  uint32_t fams[2] = {gfxFamily_, presentFamily_};
  if (gfxFamily_ != presentFamily_) {
    ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    ci.queueFamilyIndexCount = 2;
    ci.pQueueFamilyIndices = fams;
  } else {
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  }
  ci.preTransform = caps.currentTransform;
  ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  if (!(caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)) ci.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
  ci.presentMode = mode;
  ci.clipped = VK_TRUE;
  check(vkCreateSwapchainKHR(dev_, &ci, nullptr, &swapchain_), "vkCreateSwapchainKHR");

  uint32_t ic = 0;
  vkGetSwapchainImagesKHR(dev_, swapchain_, &ic, nullptr);
  swapImages_.resize(ic);
  vkGetSwapchainImagesKHR(dev_, swapchain_, &ic, swapImages_.data());
  swapViews_.resize(ic);
  renderDone_.resize(ic);
  for (uint32_t i = 0; i < ic; ++i) {
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = swapImages_[i];
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = swapFormat_;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    check(vkCreateImageView(dev_, &vi, nullptr, &swapViews_[i]), "swap image view");
    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    check(vkCreateSemaphore(dev_, &si, nullptr, &renderDone_[i]), "vkCreateSemaphore");
  }

  depth_ = createImage(depthFormat_, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, samples_);
  if (samples_ != VK_SAMPLE_COUNT_1_BIT)
    msaaColor_ = createImage(swapFormat_, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
                             VK_IMAGE_ASPECT_COLOR_BIT, samples_);
}

void Renderer::destroySwapchain() {
  for (auto f : framebuffers_) vkDestroyFramebuffer(dev_, f, nullptr);
  framebuffers_.clear();
  destroyImage(depth_);
  destroyImage(msaaColor_);
  for (auto v : swapViews_) vkDestroyImageView(dev_, v, nullptr);
  for (auto s : renderDone_) vkDestroySemaphore(dev_, s, nullptr);
  swapViews_.clear();
  renderDone_.clear();
  if (swapchain_) vkDestroySwapchainKHR(dev_, swapchain_, nullptr);
  swapchain_ = VK_NULL_HANDLE;
}

void Renderer::recreateSwapchain() {
  int w = 0, h = 0;
  glfwGetFramebufferSize(window_, &w, &h);
  while (w == 0 || h == 0) { // minimised
    glfwWaitEvents();
    if (glfwWindowShouldClose(window_)) return;
    glfwGetFramebufferSize(window_, &w, &h);
  }
  vkDeviceWaitIdle(dev_);
  destroySwapchain();
  createSwapchain();
  createFramebuffers();
}

void Renderer::createRenderPass() {
  bool ms = samples_ != VK_SAMPLE_COUNT_1_BIT;
  std::vector<VkAttachmentDescription> atts;
  VkAttachmentDescription color{};
  color.format = swapFormat_;
  color.samples = samples_;
  color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color.storeOp = ms ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
  color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  color.finalLayout = ms ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  atts.push_back(color);
  VkAttachmentDescription depth{};
  depth.format = depthFormat_;
  depth.samples = samples_;
  depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  atts.push_back(depth);
  if (ms) {
    VkAttachmentDescription resolve{};
    resolve.format = swapFormat_;
    resolve.samples = VK_SAMPLE_COUNT_1_BIT;
    resolve.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    resolve.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    resolve.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    resolve.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    resolve.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    resolve.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    atts.push_back(resolve);
  }
  VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkAttachmentReference resolveRef{2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription sub{};
  sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  sub.colorAttachmentCount = 1;
  sub.pColorAttachments = &colorRef;
  sub.pDepthStencilAttachment = &depthRef;
  sub.pResolveAttachments = ms ? &resolveRef : nullptr;
  VkSubpassDependency dep{};
  dep.srcSubpass = VK_SUBPASS_EXTERNAL;
  dep.dstSubpass = 0;
  dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  dep.dstStageMask = dep.srcStageMask;
  dep.srcAccessMask = 0;
  dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  VkRenderPassCreateInfo ci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  ci.attachmentCount = (uint32_t)atts.size();
  ci.pAttachments = atts.data();
  ci.subpassCount = 1;
  ci.pSubpasses = &sub;
  ci.dependencyCount = 1;
  ci.pDependencies = &dep;
  check(vkCreateRenderPass(dev_, &ci, nullptr, &renderPass_), "vkCreateRenderPass");
}

void Renderer::createFramebuffers() {
  bool ms = samples_ != VK_SAMPLE_COUNT_1_BIT;
  framebuffers_.resize(swapViews_.size());
  for (size_t i = 0; i < swapViews_.size(); ++i) {
    std::vector<VkImageView> views;
    if (ms) views = {msaaColor_.view, depth_.view, swapViews_[i]};
    else views = {swapViews_[i], depth_.view};
    VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fi.renderPass = renderPass_;
    fi.attachmentCount = (uint32_t)views.size();
    fi.pAttachments = views.data();
    fi.width = extent_.width;
    fi.height = extent_.height;
    fi.layers = 1;
    check(vkCreateFramebuffer(dev_, &fi, nullptr, &framebuffers_[i]), "vkCreateFramebuffer");
  }
}

void Renderer::createDescriptors() {
  VkDescriptorSetLayoutBinding b{};
  b.binding = 0;
  b.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  b.descriptorCount = 1;
  b.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  li.bindingCount = 1;
  li.pBindings = &b;
  check(vkCreateDescriptorSetLayout(dev_, &li, nullptr, &setLayout_), "descriptor set layout");

  VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pl.setLayoutCount = 1;
  pl.pSetLayouts = &setLayout_;
  check(vkCreatePipelineLayout(dev_, &pl, nullptr, &pipeLayout_), "pipeline layout");

  VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kFrames};
  VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pi.maxSets = kFrames;
  pi.poolSizeCount = 1;
  pi.pPoolSizes = &ps;
  check(vkCreateDescriptorPool(dev_, &pi, nullptr, &descPool_), "descriptor pool");
}

VkShaderModule Renderer::shader(const uint32_t* code, size_t bytes) {
  VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  ci.codeSize = bytes;
  ci.pCode = code;
  VkShaderModule m;
  check(vkCreateShaderModule(dev_, &ci, nullptr, &m), "vkCreateShaderModule");
  return m;
}

void Renderer::createPipelines() {
  enum Blend { OPAQUE, ALPHA, ADD, PREMUL };
  struct Desc {
    const uint32_t* vs; size_t vsBytes;
    const uint32_t* fs; size_t fsBytes;
    std::vector<VkVertexInputBindingDescription> bindings;
    std::vector<VkVertexInputAttributeDescription> attrs;
    bool depthTest, depthWrite;
    Blend blend;
    VkCullModeFlags cull;
  };
  auto build = [&](const Desc& d) {
    VkShaderModule vs = shader(d.vs, d.vsBytes), fs = shader(d.fs, d.fsBytes);
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;   stages[0].module = vs; stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fs; stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = (uint32_t)d.bindings.size();
    vi.pVertexBindingDescriptions = d.bindings.data();
    vi.vertexAttributeDescriptionCount = (uint32_t)d.attrs.size();
    vi.pVertexAttributeDescriptions = d.attrs.data();
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = d.cull;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = samples_;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = d.depthTest;
    ds.depthWriteEnable = d.depthWrite;
    ds.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL; // reversed Z
    VkPipelineColorBlendAttachmentState ba{};
    ba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (d.blend != OPAQUE) {
      ba.blendEnable = VK_TRUE;
      ba.colorBlendOp = ba.alphaBlendOp = VK_BLEND_OP_ADD;
      ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      if (d.blend == ALPHA) { ba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; }
      if (d.blend == ADD) { ba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE; ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE; }
      if (d.blend == PREMUL) { ba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; }
    }
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &ba;
    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = 2;
    dy.pDynamicStates = dyn;

    VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    ci.stageCount = 2;
    ci.pStages = stages;
    ci.pVertexInputState = &vi;
    ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vp;
    ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms;
    ci.pDepthStencilState = &ds;
    ci.pColorBlendState = &cb;
    ci.pDynamicState = &dy;
    ci.layout = pipeLayout_;
    ci.renderPass = renderPass_;
    VkPipeline p;
    check(vkCreateGraphicsPipelines(dev_, VK_NULL_HANDLE, 1, &ci, nullptr, &p), "vkCreateGraphicsPipelines");
    vkDestroyShaderModule(dev_, vs, nullptr);
    vkDestroyShaderModule(dev_, fs, nullptr);
    return p;
  };

  const VkFormat V2 = VK_FORMAT_R32G32_SFLOAT, V3 = VK_FORMAT_R32G32B32_SFLOAT, V4 = VK_FORMAT_R32G32B32A32_SFLOAT;

  Desc sky{sky_vert_spv, sizeof(sky_vert_spv), sky_frag_spv, sizeof(sky_frag_spv), {}, {}, false, false, OPAQUE, VK_CULL_MODE_NONE};
  skyPipe_ = build(sky);

  Desc mesh{mesh_vert_spv, sizeof(mesh_vert_spv), mesh_frag_spv, sizeof(mesh_frag_spv),
            {{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX}, {1, sizeof(InstanceData), VK_VERTEX_INPUT_RATE_INSTANCE}},
            {{0, 0, V3, 0}, {1, 0, V3, 12}, {2, 1, V4, 0}, {3, 1, V4, 16}, {4, 1, V4, 32}, {5, 1, V4, 48}, {6, 1, V4, 64}, {7, 1, V4, 80}},
            true, true, OPAQUE, VK_CULL_MODE_BACK_BIT};
  meshPipe_ = build(mesh);
  mesh.depthWrite = false;
  mesh.blend = ADD;
  mesh.cull = VK_CULL_MODE_NONE;
  addPipe_ = build(mesh);

  Desc part{particle_vert_spv, sizeof(particle_vert_spv), particle_frag_spv, sizeof(particle_frag_spv),
            {{0, sizeof(ParticleData), VK_VERTEX_INPUT_RATE_INSTANCE}},
            {{0, 0, V4, 0}, {1, 0, V4, 16}, {2, 0, V4, 32}},
            true, false, PREMUL, VK_CULL_MODE_NONE};
  particlePipe_ = build(part);

  Desc ui{ui_vert_spv, sizeof(ui_vert_spv), ui_frag_spv, sizeof(ui_frag_spv),
          {{0, sizeof(UIVertex), VK_VERTEX_INPUT_RATE_VERTEX}},
          {{0, 0, V2, 0}, {1, 0, V2, 8}, {2, 0, V4, 16}, {3, 0, VK_FORMAT_R32G32_UINT, 32}},
          false, false, ALPHA, VK_CULL_MODE_NONE};
  uiPipe_ = build(ui);
}

void Renderer::createMeshBuffers() {
  std::vector<MeshData> meshes;
  buildMeshes(meshes);
  std::vector<Vertex> verts;
  std::vector<uint32_t> idx;
  for (int i = 0; i < MESH_COUNT; ++i) {
    meshRanges_[i] = {(uint32_t)idx.size(), (uint32_t)meshes[i].idx.size(), (int32_t)verts.size()};
    verts.insert(verts.end(), meshes[i].verts.begin(), meshes[i].verts.end());
    idx.insert(idx.end(), meshes[i].idx.begin(), meshes[i].idx.end());
  }
  VkDeviceSize vb = verts.size() * sizeof(Vertex), ib = idx.size() * sizeof(uint32_t);
  const VkMemoryPropertyFlags host = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  Buffer stageV = createBuffer(vb, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, host, true);
  Buffer stageI = createBuffer(ib, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, host, true);
  std::memcpy(stageV.map, verts.data(), vb);
  std::memcpy(stageI.map, idx.data(), ib);
  meshVB_ = createBuffer(vb, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false);
  meshIB_ = createBuffer(ib, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false);

  VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  ai.commandPool = cmdPool_;
  ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ai.commandBufferCount = 1;
  VkCommandBuffer cmd;
  check(vkAllocateCommandBuffers(dev_, &ai, &cmd), "alloc upload cmd");
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &bi);
  VkBufferCopy c1{0, 0, vb}, c2{0, 0, ib};
  vkCmdCopyBuffer(cmd, stageV.buf, meshVB_.buf, 1, &c1);
  vkCmdCopyBuffer(cmd, stageI.buf, meshIB_.buf, 1, &c2);
  vkEndCommandBuffer(cmd);
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cmd;
  check(vkQueueSubmit(gfxQueue_, 1, &si, VK_NULL_HANDLE), "upload submit");
  vkQueueWaitIdle(gfxQueue_);
  vkFreeCommandBuffers(dev_, cmdPool_, 1, &cmd);
  destroyBuffer(stageV);
  destroyBuffer(stageI);
}

void Renderer::createFrameResources() {
  const VkMemoryPropertyFlags host = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  for (auto& f : frames_) {
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = cmdPool_;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    check(vkAllocateCommandBuffers(dev_, &ai, &f.cmd), "alloc frame cmd");
    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    check(vkCreateSemaphore(dev_, &si, nullptr, &f.imageAvailable), "semaphore");
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    check(vkCreateFence(dev_, &fi, nullptr, &f.inFlight), "fence");

    f.ubo = createBuffer(sizeof(GlobalUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, host, true);
    f.instances = createBuffer(kMaxInstances * sizeof(InstanceData), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, host, true);
    f.particles = createBuffer(kMaxParticles * sizeof(ParticleData), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, host, true);
    f.ui = createBuffer(kMaxUIVerts * sizeof(UIVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, host, true);

    VkDescriptorSetAllocateInfo di{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    di.descriptorPool = descPool_;
    di.descriptorSetCount = 1;
    di.pSetLayouts = &setLayout_;
    check(vkAllocateDescriptorSets(dev_, &di, &f.set), "alloc descriptor set");
    VkDescriptorBufferInfo bi{f.ubo.buf, 0, sizeof(GlobalUBO)};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = f.set;
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w.pBufferInfo = &bi;
    vkUpdateDescriptorSets(dev_, 1, &w, 0, nullptr);
  }
}

void Renderer::record(VkCommandBuffer cmd, uint32_t imageIndex, const FrameData&) {
  Frame& f = frames_[frame_];
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  check(vkBeginCommandBuffer(cmd, &bi), "vkBeginCommandBuffer");

  VkClearValue clears[3]{};
  clears[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  clears[1].depthStencil = {0.0f, 0}; // reversed Z: far = 0
  clears[2].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  rp.renderPass = renderPass_;
  rp.framebuffer = framebuffers_[imageIndex];
  rp.renderArea = {{0, 0}, extent_};
  rp.clearValueCount = samples_ != VK_SAMPLE_COUNT_1_BIT ? 3 : 2;
  rp.pClearValues = clears;
  vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

  VkViewport vp{0, 0, (float)extent_.width, (float)extent_.height, 0.0f, 1.0f};
  VkRect2D sc{{0, 0}, extent_};
  vkCmdSetViewport(cmd, 0, 1, &vp);
  vkCmdSetScissor(cmd, 0, 1, &sc);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout_, 0, 1, &f.set, 0, nullptr);

  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipe_);
  vkCmdDraw(cmd, 3, 1, 0, 0);

  VkBuffer vbs[2] = {meshVB_.buf, f.instances.buf};
  VkDeviceSize offs[2] = {0, 0};
  vkCmdBindVertexBuffers(cmd, 0, 2, vbs, offs);
  vkCmdBindIndexBuffer(cmd, meshIB_.buf, 0, VK_INDEX_TYPE_UINT32);
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipe_);
  for (int m = 0; m < MESH_COUNT; ++m)
    if (opaqueCount_[m]) vkCmdDrawIndexed(cmd, meshRanges_[m].indexCount, opaqueCount_[m], meshRanges_[m].firstIndex, meshRanges_[m].vertexOffset, opaqueFirst_[m]);
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, addPipe_);
  for (int m = 0; m < MESH_COUNT; ++m)
    if (addCount_[m]) vkCmdDrawIndexed(cmd, meshRanges_[m].indexCount, addCount_[m], meshRanges_[m].firstIndex, meshRanges_[m].vertexOffset, addFirst_[m]);

  if (particleCount_) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, particlePipe_);
    vkCmdBindVertexBuffers(cmd, 0, 1, &f.particles.buf, offs);
    vkCmdDraw(cmd, 6, particleCount_, 0, 0);
  }
  if (uiCount_) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, uiPipe_);
    vkCmdBindVertexBuffers(cmd, 0, 1, &f.ui.buf, offs);
    vkCmdDraw(cmd, uiCount_, 1, 0, 0);
  }
  vkCmdEndRenderPass(cmd);
  check(vkEndCommandBuffer(cmd), "vkEndCommandBuffer");
}

void Renderer::draw(const FrameData& fd) {
  if (glfwWindowShouldClose(window_)) return;
  Frame& f = frames_[frame_];
  vkWaitForFences(dev_, 1, &f.inFlight, VK_TRUE, UINT64_MAX);

  uint32_t imageIndex = 0;
  VkResult r = vkAcquireNextImageKHR(dev_, swapchain_, UINT64_MAX, f.imageAvailable, VK_NULL_HANDLE, &imageIndex);
  if (r == VK_ERROR_OUT_OF_DATE_KHR) { recreateSwapchain(); return; }
  if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) check(r, "vkAcquireNextImageKHR");
  vkResetFences(dev_, 1, &f.inFlight);

  // ---- upload this frame's data
  GlobalUBO u = fd.ubo;
  u.screen[0] = (float)extent_.width; u.screen[1] = (float)extent_.height;
  u.screen[2] = 1.0f / (float)extent_.width; u.screen[3] = 1.0f / (float)extent_.height;
  u.misc[3] = manualGamma_ ? 1.0f : 0.0f;
  std::memcpy(f.ubo.map, &u, sizeof(u));

  size_t cursor = 0;
  auto* inst = (InstanceData*)f.instances.map;
  auto pack = [&](const std::vector<InstanceData>* lists, uint32_t* first, uint32_t* count) {
    for (int m = 0; m < MESH_COUNT; ++m) {
      size_t n = std::min(lists[m].size(), kMaxInstances - cursor);
      first[m] = (uint32_t)cursor;
      count[m] = (uint32_t)n;
      if (n) std::memcpy(inst + cursor, lists[m].data(), n * sizeof(InstanceData));
      cursor += n;
    }
  };
  pack(fd.opaque, opaqueFirst_, opaqueCount_);
  pack(fd.additive, addFirst_, addCount_);
  particleCount_ = (uint32_t)std::min(fd.particles.size(), kMaxParticles);
  if (particleCount_) std::memcpy(f.particles.map, fd.particles.data(), particleCount_ * sizeof(ParticleData));
  uiCount_ = (uint32_t)std::min(fd.ui.size(), kMaxUIVerts);
  uiCount_ -= uiCount_ % 3;
  if (uiCount_) std::memcpy(f.ui.map, fd.ui.data(), uiCount_ * sizeof(UIVertex));

  vkResetCommandBuffer(f.cmd, 0);
  record(f.cmd, imageIndex, fd);

  VkPipelineStageFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.waitSemaphoreCount = 1;
  si.pWaitSemaphores = &f.imageAvailable;
  si.pWaitDstStageMask = &wait;
  si.commandBufferCount = 1;
  si.pCommandBuffers = &f.cmd;
  si.signalSemaphoreCount = 1;
  si.pSignalSemaphores = &renderDone_[imageIndex];
  check(vkQueueSubmit(gfxQueue_, 1, &si, f.inFlight), "vkQueueSubmit");

  VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
  pi.waitSemaphoreCount = 1;
  pi.pWaitSemaphores = &renderDone_[imageIndex];
  pi.swapchainCount = 1;
  pi.pSwapchains = &swapchain_;
  pi.pImageIndices = &imageIndex;
  r = vkQueuePresentKHR(presentQueue_, &pi);
  if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR || resized_) {
    resized_ = false;
    recreateSwapchain();
  } else if (r != VK_SUCCESS) {
    check(r, "vkQueuePresentKHR");
  }
  frame_ = (frame_ + 1) % kFrames;
}

void Renderer::shutdown() {
  if (dev_) {
    vkDeviceWaitIdle(dev_);
    for (auto& f : frames_) {
      destroyBuffer(f.ubo); destroyBuffer(f.instances); destroyBuffer(f.particles); destroyBuffer(f.ui);
      if (f.imageAvailable) vkDestroySemaphore(dev_, f.imageAvailable, nullptr);
      if (f.inFlight) vkDestroyFence(dev_, f.inFlight, nullptr);
    }
    destroyBuffer(meshVB_);
    destroyBuffer(meshIB_);
    for (VkPipeline p : {skyPipe_, meshPipe_, addPipe_, particlePipe_, uiPipe_})
      if (p) vkDestroyPipeline(dev_, p, nullptr);
    if (descPool_) vkDestroyDescriptorPool(dev_, descPool_, nullptr);
    if (pipeLayout_) vkDestroyPipelineLayout(dev_, pipeLayout_, nullptr);
    if (setLayout_) vkDestroyDescriptorSetLayout(dev_, setLayout_, nullptr);
    destroySwapchain();
    if (renderPass_) vkDestroyRenderPass(dev_, renderPass_, nullptr);
    if (cmdPool_) vkDestroyCommandPool(dev_, cmdPool_, nullptr);
    vkDestroyDevice(dev_, nullptr);
    dev_ = VK_NULL_HANDLE;
  }
  if (instance_) {
    if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (debug_) {
      auto fn = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT");
      if (fn) fn(instance_, debug_, nullptr);
    }
    vkDestroyInstance(instance_, nullptr);
    instance_ = VK_NULL_HANDLE;
  }
}
