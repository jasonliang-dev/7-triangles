// cl /std:c++17 /nologo /Zi /Iinclude sdl2-webgpu.cpp lib/sdl2.lib lib/sdl2main.lib lib/webgpu.lib

#define SDL_MAIN_HANDLED

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <webgpu.h>

int main() {
  SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);

  int width = 800, height = 600;
  SDL_Window *window = SDL_CreateWindow("SDL2 + WebGPU", SDL_WINDOWPOS_CENTERED,
                                        SDL_WINDOWPOS_CENTERED, width, height,
                                        SDL_WINDOW_RESIZABLE);

  WGPUInstance instance = wgpuCreateInstance(nullptr);

  WGPUSurface surface = nullptr;
  {
    SDL_SysWMinfo wm = {};
    SDL_GetWindowWMInfo(window, &wm);

    WGPUSurfaceDescriptorFromWindowsHWND win_desc = {};
    win_desc.chain.sType = WGPUSType_SurfaceDescriptorFromWindowsHWND;
    win_desc.hwnd = wm.info.win.window;
    WGPUSurfaceDescriptor desc = {};
    desc.nextInChain = &win_desc.chain;
    surface = wgpuInstanceCreateSurface(instance, &desc);
  }

  WGPUAdapter adapter = nullptr;
  {
    WGPURequestAdapterOptions options = {};
    options.compatibleSurface = surface;
    wgpuInstanceRequestAdapter(
        instance, &options,
        [](WGPURequestAdapterStatus status, WGPUAdapter adapter,
           const char *msg, void *udata) {
          WGPUAdapter *dst = (WGPUAdapter *)udata;
          *dst = adapter;
        },
        &adapter);
  }

  WGPUDevice device = nullptr;
  {
    WGPUDeviceDescriptor desc = {};
    wgpuAdapterRequestDevice(
        adapter, &desc,
        [](WGPURequestDeviceStatus status, WGPUDevice device, char const *msg,
           void *udata) {
          WGPUDevice *dst = (WGPUDevice *)udata;
          *dst = device;
        },
        &device);
  }

  WGPUQueue queue = wgpuDeviceGetQueue(device);

  struct Vertex {
    float position[3];
    float color[4];
  };

  WGPUBuffer vbuf = nullptr;
  {
    Vertex vertices[] = {
        {{+0.0f, +0.5f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
        {{-0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
        {{+0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
    };

    WGPUBufferDescriptor desc = {};

    desc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    desc.size = sizeof(vertices);
    vbuf = wgpuDeviceCreateBuffer(device, &desc);
    wgpuQueueWriteBuffer(queue, vbuf, 0, vertices, sizeof(vertices));
  }

  WGPUShaderModule shaders = nullptr;
  {
    WGPUShaderModuleWGSLDescriptor wgsl = {};
    wgsl.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
    wgsl.code = R"(
      struct VertexIn {
        @location(0) position: vec3f,
        @location(1) color: vec4f,
      }

      struct VertexOut {
        @builtin(position) position: vec4f,
        @location(1) color: vec4f,
      }

      @vertex
      fn vs_main(in: VertexIn) -> VertexOut {
        var out: VertexOut;
        out.position = vec4f(in.position, 1.0f);
        out.color = in.color;
        return out;
      }

      @fragment
      fn fs_main(in: VertexOut) -> @location(0) vec4f {
        return in.color;
      }
    )";

    WGPUShaderModuleDescriptor desc = {};
    desc.nextInChain = &wgsl.chain;
    shaders = wgpuDeviceCreateShaderModule(device, &desc);
  }

  WGPURenderPipeline pipeline = nullptr;
  {
    WGPUVertexAttribute attrs[] = {
        {WGPUVertexFormat_Float32x3, offsetof(Vertex, position), 0},
        {WGPUVertexFormat_Float32x4, offsetof(Vertex, color), 1},
    };

    WGPUVertexBufferLayout vs_layout = {};
    vs_layout.arrayStride = sizeof(Vertex);
    vs_layout.stepMode = WGPUVertexStepMode_Vertex;
    vs_layout.attributeCount = 2;
    vs_layout.attributes = attrs;

    WGPUVertexState vs = {};
    vs.module = shaders;
    vs.entryPoint = "vs_main";
    vs.bufferCount = 1;
    vs.buffers = &vs_layout;

    WGPUColorTargetState fs_target = {};
    fs_target.format = WGPUTextureFormat_BGRA8Unorm;
    fs_target.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fs = {};
    fs.module = shaders;
    fs.entryPoint = "fs_main";
    fs.targetCount = 1;
    fs.targets = &fs_target;

    WGPURenderPipelineDescriptor desc = {};
    desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    desc.primitive.frontFace = WGPUFrontFace_CCW;
    desc.primitive.cullMode = WGPUCullMode_None;
    desc.vertex = vs;
    desc.fragment = &fs;
    desc.multisample.count = 1;
    desc.multisample.mask = 0xffffffff;
    desc.multisample.alphaToCoverageEnabled = false;

    pipeline = wgpuDeviceCreateRenderPipeline(device, &desc);
  }

  WGPUSwapChain swapchain = nullptr;
  int swapchain_width = 0, swapchain_height = 0;

  bool should_quit = false;
  while (!should_quit) {
    SDL_Event e = {};
    while (SDL_PollEvent(&e)) {
      switch (e.type) {
      case SDL_QUIT:
        should_quit = true;
        break;
      }
    }

    wgpuDeviceTick(device);

    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window, &width, &height);

    if (width != swapchain_width || height != swapchain_height) {
      if (swapchain) {
        wgpuSwapChainRelease(swapchain);
      }

      swapchain_width = width;
      swapchain_height = height;

      WGPUSwapChainDescriptor desc = {};
      desc.width = width;
      desc.height = height;
      desc.usage = WGPUTextureUsage_RenderAttachment;
      desc.format = WGPUTextureFormat_BGRA8Unorm;
      desc.presentMode = WGPUPresentMode_Fifo;
      swapchain = wgpuDeviceCreateSwapChain(device, surface, &desc);
    }

    if (swapchain == nullptr) {
      continue;
    }

    WGPUCommandEncoder encoder =
        wgpuDeviceCreateCommandEncoder(device, nullptr);

    WGPUTextureView target = wgpuSwapChainGetCurrentTextureView(swapchain);

    WGPURenderPassEncoder pass = nullptr;
    {
      WGPURenderPassColorAttachment color = {};
      color.view = target;
      color.loadOp = WGPULoadOp_Clear;
      color.storeOp = WGPUStoreOp_Store;
      color.clearValue = {0.5f, 0.5f, 0.5f, 1.0f};

      WGPURenderPassDescriptor desc = {};
      desc.colorAttachmentCount = 1;
      desc.colorAttachments = &color;
      pass = wgpuCommandEncoderBeginRenderPass(encoder, &desc);
    }

    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vbuf, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);

    wgpuRenderPassEncoderEnd(pass);

    WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder, NULL);

    wgpuQueueSubmit(queue, 1, &command);

    wgpuCommandEncoderRelease(encoder);
    wgpuTextureViewRelease(target);
    wgpuCommandBufferRelease(command);

    wgpuSwapChainPresent(swapchain);
  }
}
