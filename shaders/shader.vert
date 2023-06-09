#version 450

layout(location=0) in vec3 a_position;
layout(location=1) in vec4 a_color;

layout(location=0) out vec4 v_color;

void main() {
  gl_Position = vec4(a_position, 1.0);
  v_color = a_color;
}
