// cl /std:c++17 /nologo /Zi /Iinclude sdl2-opengl.cpp lib/sdl2.lib lib/sdl2main.lib

#define SDL_MAIN_HANDLED
#define GLAD_GL_IMPLEMENTATION

#include <SDL2/SDL.h>
#include <glad/gl.h>

int main() {
  SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

  int width = 800, height = 600;
  SDL_Window *window = SDL_CreateWindow(
      "SDL2 + OpenGL", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width,
      height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);

  SDL_GL_CreateContext(window);
  gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress);

  struct Vertex {
    float position[3];
    float color[4];
  };

  GLuint vao = 0;
  {
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    Vertex vertices[] = {
        {{+0.0f, +0.5f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
        {{-0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
        {{+0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
    };

    GLuint vbo = 0;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void *)offsetof(Vertex, position));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void *)offsetof(Vertex, color));
  }

  GLuint program = glCreateProgram();
  {
    const char *vert_glsl = R"(
      #version 330 core

      layout(location=0) in vec3 a_position;
      layout(location=1) in vec4 a_color;

      out vec4 v_color;

      void main() {
        gl_Position = vec4(a_position, 1.0);
        v_color = a_color;
      }
    )";

    const char *frag_glsl = R"(
      #version 330 core

      in vec4 v_color;
      out vec4 f_color;

      void main() {
        f_color = v_color;
      }
    )";

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vert_glsl, 0);
    glCompileShader(vs);
    glAttachShader(program, vs);

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &frag_glsl, 0);
    glCompileShader(fs);
    glAttachShader(program, fs);

    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
  }

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

    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window, &width, &height);
    glViewport(0, 0, width, height);

    glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(program);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    SDL_GL_SwapWindow(window);
  }
}
