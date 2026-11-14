// Vulkan renderer: one render pass (optionally multisampled), five pipelines:
// sky -> instanced opaque meshes -> additive meshes -> particles -> UI.
#pragma once
#include "math.h"
#include "meshes.h"
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

struct GLFWwindow;

struct InstanceData {
  float model[16];
  float color[4];  // rgb, a = emissive strength
  float params[4]; // material, damage, seed, intensity
};
struct ParticleData {
  float pos[3];
  float size;
  float color[4];
  float axis[3];
  float kind;
};
struct UIVertex {
  float x, y, u, v;
  float r, g, b, a;
  uint32_t g0, g1;
};

struct GlobalUBO { // must match shaders/common.glsl (std140, vec4/mat4 only)
  float viewProj[16];
  float camPos[4], camRight[4], camUp[4], camFwd[4];
  float sunDir[4], sunColor[4], ambient[4], fog[4];
  float nebA[4], nebB[4], nebC[4], nebParams[4];
  float planetPos[4][4], planetColA[4][4], planetColB[4][4], planetParams[4][4];
  float screen[4], misc[4];
  float skySun[4]; // direction of the star as seen in the sky (the hangar relights objects but not the sky)
};

struct FrameData {
  GlobalUBO ubo{};
  std::vector<InstanceData> opaque[MESH_COUNT];
  std::vector<InstanceData> additive[MESH_COUNT];
  std::vector<ParticleData> particles;
  std::vector<UIVertex> ui;
  void clear() {
    for (auto& v : opaque) v.clear();
    for (auto& v : additive) v.clear();
    particles.clear();
    ui.clear();
  }
};

struct RendererOptions {
  bool vsync = true;
  int msaa = 4;
  bool validation = false;
  int textureSize = 2048;
};

class Renderer {
 public:
  bool init(GLFWwindow* window, const RendererOptions& opts);
  void shutdown();
  void draw(const FrameData& fd);
  void notifyResize() { resized_ = true; }
  int width() const { return (int)extent_.width; }
  int height() const { return (int)extent_.height; }
  const std::string& gpuName() const { return gpuName_; }
  int samples() const { return (int)samples_; }
  int textureSize() const { return texSize_; }
  double textureSeconds() const { return texSeconds_; }

 private:
  struct MeshRange { uint32_t firstIndex, indexCount; int32_t vertexOffset; };
  struct Buffer { VkBuffer buf = VK_NULL_HANDLE; VkDeviceMemory mem = VK_NULL_HANDLE; void* map = nullptr; };
  struct Image { VkImage img = VK_NULL_HANDLE; VkDeviceMemory mem = VK_NULL_HANDLE; VkImageView view = VK_NULL_HANDLE; };

  void createInstance();
  void pickDevice();
  void createDevice();
  void createSwapchain();
  void destroySwapchain();
  void recreateSwapchain();
  void createRenderPass();
  void createFramebuffers();
  void createDescriptors();
  void createPipelines();
  void createMeshBuffers();
  void createTextures();
  void createFrameResources();
  uint32_t findMemoryType(uint32_t bits, VkMemoryPropertyFlags props);
  Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, bool map);
  void destroyBuffer(Buffer& b);
  Image createImage(VkFormat fmt, VkImageUsageFlags usage, VkImageAspectFlags aspect, VkSampleCountFlagBits samples);
  void destroyImage(Image& i);
  VkShaderModule shader(const uint32_t* code, size_t bytes);
  void record(VkCommandBuffer cmd, uint32_t imageIndex, const FrameData& fd);

  static const int kFrames = 2;
  GLFWwindow* window_ = nullptr;
  RendererOptions opts_;
  VkInstance instance_ = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT debug_ = VK_NULL_HANDLE;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  VkPhysicalDevice phys_ = VK_NULL_HANDLE;
  VkDevice dev_ = VK_NULL_HANDLE;
  uint32_t gfxFamily_ = 0, presentFamily_ = 0;
  VkQueue gfxQueue_ = VK_NULL_HANDLE, presentQueue_ = VK_NULL_HANDLE;
  std::string gpuName_;

  VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
  VkFormat swapFormat_ = VK_FORMAT_UNDEFINED;
  VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;
  VkExtent2D extent_{};
  bool manualGamma_ = false;
  std::vector<VkImage> swapImages_;
  std::vector<VkImageView> swapViews_;
  std::vector<VkFramebuffer> framebuffers_;
  std::vector<VkSemaphore> renderDone_;
  Image depth_, msaaColor_;
  VkSampleCountFlagBits samples_ = VK_SAMPLE_COUNT_1_BIT;
  VkRenderPass renderPass_ = VK_NULL_HANDLE;

  VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
  VkPipelineLayout pipeLayout_ = VK_NULL_HANDLE;
  VkDescriptorPool descPool_ = VK_NULL_HANDLE;
  VkPipeline skyPipe_ = VK_NULL_HANDLE, meshPipe_ = VK_NULL_HANDLE, addPipe_ = VK_NULL_HANDLE,
             particlePipe_ = VK_NULL_HANDLE, uiPipe_ = VK_NULL_HANDLE;

  Buffer meshVB_, meshIB_;
  Image tex_[2]; // surface texture arrays A (colour, roughness) and B (normal, occlusion, wear)
  VkSampler sampler_ = VK_NULL_HANDLE;
  bool aniso_ = false;
  float maxAniso_ = 1;
  MeshRange meshRanges_[MESH_COUNT]{};

  VkCommandPool cmdPool_ = VK_NULL_HANDLE;
  struct Frame {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    VkFence inFlight = VK_NULL_HANDLE;
    Buffer ubo, instances, particles, ui;
    VkDescriptorSet set = VK_NULL_HANDLE;
  } frames_[kFrames];
  int frame_ = 0;
  int texSize_ = 0;
  double texSeconds_ = 0;
  bool resized_ = false;

  // per-draw scratch
  uint32_t opaqueFirst_[MESH_COUNT]{}, opaqueCount_[MESH_COUNT]{}, addFirst_[MESH_COUNT]{}, addCount_[MESH_COUNT]{};
  uint32_t particleCount_ = 0, uiCount_ = 0;
};
