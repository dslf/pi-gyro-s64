#version 460 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
layout(location = 2) in vec2 aUv;

uniform mat4 uVP;
uniform mat4 uModel;
uniform mat3 uNormal;

out vec3 vWorld;
out vec3 vNrm;
out vec2 vUv;

void main() {
  vec4 world = uModel * vec4(aPos, 1.0);
  vWorld = world.xyz;
  vNrm = normalize(uNormal * aNrm);
  vUv = aUv;
  gl_Position = uVP * world;
}
