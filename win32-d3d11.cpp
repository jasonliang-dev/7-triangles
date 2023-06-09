// cl /std:c++17 /nologo /Zi win32-d3d11.cpp

#include <d3d11.h>
#include <d3dcompiler.h>
#include <windows.h>

#pragma comment(lib, "user32")
#pragma comment(lib, "d3d11")

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  switch (msg) {
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  default:
    return DefWindowProc(hwnd, msg, wparam, lparam);
  }
}

int main() {
  const char *title = "Win32 + D3D11";

  WNDCLASSA wc = {};
  wc.lpfnWndProc = wnd_proc;
  wc.hInstance = nullptr;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.lpszClassName = title;
  RegisterClassA(&wc);

  int width = 800, height = 600;
  HWND hwnd = CreateWindowExA(0, title, title, WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              CW_USEDEFAULT, CW_USEDEFAULT, width, height,
                              nullptr, nullptr, nullptr, nullptr);

  IDXGISwapChain *swapchain = nullptr;
  ID3D11Device *device = nullptr;
  ID3D11DeviceContext *ctx = nullptr;
  {
    DXGI_SWAP_CHAIN_DESC desc = {};
    desc.BufferCount = 2;
    desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = hwnd;
    desc.SampleDesc.Count = 1;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Windowed = true;

    D3D_FEATURE_LEVEL features[] = {D3D_FEATURE_LEVEL_11_0};
    D3D11CreateDeviceAndSwapChain(
        0, D3D_DRIVER_TYPE_HARDWARE, 0, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        features, 1, D3D11_SDK_VERSION, &desc, &swapchain, &device, 0, &ctx);
  }

  ID3D11Texture2D *render_target = nullptr;
  swapchain->GetBuffer(0, IID_PPV_ARGS(&render_target));

  ID3D11RenderTargetView *rtv = nullptr;
  device->CreateRenderTargetView(render_target, 0, &rtv);

  ID3D11RasterizerState *rasterizer = nullptr;
  {
    D3D11_RASTERIZER_DESC desc = {};
    desc.FillMode = D3D11_FILL_SOLID;
    desc.CullMode = D3D11_CULL_BACK;

    device->CreateRasterizerState(&desc, &rasterizer);
  }

  struct Vertex {
    float position[3];
    float color[4];
  };

  ID3D11Buffer *vbuf = nullptr;
  {
    Vertex vertices[] = {
        {{+0.0f, +0.5f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
        {{+0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
        {{-0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
    };

    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(vertices);
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA data = {};
    data.pSysMem = vertices;
    device->CreateBuffer(&desc, &data, &vbuf);
  }

  HINSTANCE dll = LoadLibraryA("d3dcompiler_47.dll");
  pD3DCompile d3d_compile_proc = (pD3DCompile)GetProcAddress(dll, "D3DCompile");
#define D3DCompile d3d_compile_proc

  const char *hlsl = R"(
    struct VertexIn {
      float3 position : POS;
      float4 color : COL;
    };

    struct VertexOut {
      float4 position : SV_POSITION;
      float4 color : COL;
    };

    VertexOut vs_main(VertexIn input) {
      VertexOut output;
      output.position = float4(input.position, 1.0);
      output.color = input.color;
      return output;
    }

    float4 ps_main(VertexOut input) : SV_TARGET {
      return input.color;
    }
  )";

  UINT compile_flags = D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR |
                       D3DCOMPILE_ENABLE_STRICTNESS |
                       D3DCOMPILE_WARNINGS_ARE_ERRORS;
  compile_flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;

  ID3D11InputLayout *input_layout = nullptr;
  ID3D11VertexShader *vertex_shader = nullptr;
  {
    ID3DBlob *vs_blob = nullptr;
    D3DCompile(hlsl, strlen(hlsl), nullptr, nullptr,
               D3D_COMPILE_STANDARD_FILE_INCLUDE, "vs_main", "vs_5_0",
               compile_flags, 0, &vs_blob, nullptr);

    device->CreateVertexShader(vs_blob->GetBufferPointer(),
                               vs_blob->GetBufferSize(), nullptr,
                               &vertex_shader);

    D3D11_INPUT_ELEMENT_DESC desc[] = {
        {"POS", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
         D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };

    device->CreateInputLayout(desc, ARRAYSIZE(desc),
                              vs_blob->GetBufferPointer(),
                              vs_blob->GetBufferSize(), &input_layout);
  }

  ID3D11PixelShader *pixel_shader = nullptr;
  {
    ID3DBlob *ps_blob = nullptr;
    D3DCompile(hlsl, strlen(hlsl), nullptr, nullptr,
               D3D_COMPILE_STANDARD_FILE_INCLUDE, "ps_main", "ps_5_0",
               compile_flags, 0, &ps_blob, nullptr);

    device->CreatePixelShader(ps_blob->GetBufferPointer(),
                              ps_blob->GetBufferSize(), nullptr, &pixel_shader);
    ps_blob->Release();
  }

  int swapchain_width = width, swapchain_height = height;

  bool should_quit = false;
  while (!should_quit) {
    MSG msg = {};
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);

      if (msg.message == WM_QUIT) {
        should_quit = true;
      }
    }

    RECT rect;
    GetClientRect(hwnd, &rect);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;

    if (width != swapchain_width || height != swapchain_height) {
      swapchain_width = width;
      swapchain_height = height;

      render_target->Release();
      rtv->Release();

      swapchain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);

      swapchain->GetBuffer(0, IID_PPV_ARGS(&render_target));
      device->CreateRenderTargetView(render_target, 0, &rtv);
    }

    float bg_color[4] = {0.5f, 0.5f, 0.5f, 1.0f};

    UINT stride = sizeof(Vertex);
    UINT offset = 0;

    D3D11_VIEWPORT viewport = {0.0f,          0.0f, (float)width,
                               (float)height, 0.0f, 1.0f};

    ctx->ClearRenderTargetView(rtv, bg_color);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetInputLayout(input_layout);
    ctx->IASetVertexBuffers(0, 1, &vbuf, &stride, &offset);
    ctx->VSSetShader(vertex_shader, nullptr, 0);
    ctx->RSSetViewports(1, &viewport);
    ctx->RSSetState(rasterizer);
    ctx->PSSetShader(pixel_shader, nullptr, 0);
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    ctx->Draw(3, 0);

    swapchain->Present(1, 0);
  }
}