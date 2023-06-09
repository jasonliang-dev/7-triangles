// cl /std:c++17 /nologo /Zi /MD /Iinclude raylib.cpp lib/raylib.lib user32.lib shell32.lib gdi32.lib winmm.lib

#include <raylib.h>
#include <rlgl.h>

int main() {
  SetConfigFlags(FLAG_WINDOW_RESIZABLE);
  InitWindow(800, 600, "raylib + rlgl");

  while (!WindowShouldClose()) {
    BeginDrawing();
    ClearBackground({128, 128, 128, 255});

    rlMatrixMode(RL_PROJECTION);
    rlLoadIdentity();

    rlBegin(RL_TRIANGLES);
    rlColor4f(1.0f, 0.0f, 0.0f, 1.0f);
    rlVertex3f(+0.0f, +0.5f, 0.0f);
    rlColor4f(0.0f, 1.0f, 0.0f, 1.0f);
    rlVertex3f(-0.5f, -0.5f, 0.0f);
    rlColor4f(0.0f, 0.0f, 1.0f, 1.0f);
    rlVertex3f(+0.5f, -0.5f, 0.0f);
    rlEnd();

    EndDrawing();
  }
}