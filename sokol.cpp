// cl /std:c++17 /nologo /Zi /Iinclude sokol.cpp

#define SOKOL_IMPL
#define SOKOL_GLCORE33
#define SOKOL_WIN32_FORCE_MAIN

#include <sokol_app.h>
#include <sokol_gfx.h>
#include <sokol_glue.h>
#include <sokol_log.h>

sg_buffer vbuf;
sg_pipeline pip;

void init() {
  sg_desc desc = {};
  desc.context = sapp_sgcontext();
  desc.logger.func = slog_func;
  sg_setup(&desc);

  struct Vertex {
    float position[3];
    float color[4];
  };

  vbuf = {};
  {
    Vertex vertices[] = {
        {{+0.0f, +0.5f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
        {{-0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
        {{+0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
    };

    sg_buffer_desc desc = {};
    desc.data = SG_RANGE(vertices);
    vbuf = sg_make_buffer(&desc);
  }

  sg_shader shd = {};
  {
    sg_shader_desc desc = {};

    desc.vs.source = R"(
      #version 330 core

      layout(location=0) in vec3 a_position;
      layout(location=1) in vec4 a_color;

      out vec4 v_color;

      void main() {
        gl_Position = vec4(a_position, 1.0);
        v_color = a_color;
      }
    )";

    desc.fs.source = R"(
      #version 330 core

      in vec4 v_color;
      out vec4 f_color;

      void main() {
        f_color = v_color;
      }
    )";

    shd = sg_make_shader(&desc);
  }

  pip = {};
  {
    sg_pipeline_desc desc = {};
    desc.shader = shd;
    desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT4;
    pip = sg_make_pipeline(&desc);
  }
}

void frame() {
  sg_pass_action pass_action = {};
  pass_action.colors[0].load_action = SG_LOADACTION_CLEAR;
  pass_action.colors[0].clear_value = {0.5f, 0.5f, 0.5f, 1.0f};
  sg_begin_default_pass(&pass_action, sapp_width(), sapp_height());

  sg_bindings bind = {};
  bind.vertex_buffers[0] = vbuf;

  sg_apply_pipeline(pip);
  sg_apply_bindings(&bind);
  sg_draw(0, 3, 1);

  sg_end_pass();
  sg_commit();
}

void cleanup() { sg_shutdown(); }

sapp_desc sokol_main(int argc, char **argv) {
  sapp_desc desc = {};
  desc.init_cb = init;
  desc.frame_cb = frame;
  desc.cleanup_cb = cleanup;
  desc.width = 800;
  desc.height = 600;
  desc.window_title = "Sokol";
  desc.logger.func = slog_func;
  return desc;
}