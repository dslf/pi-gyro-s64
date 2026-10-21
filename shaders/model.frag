#version 330 core
in vec3 vWorld;
in vec3 vNrm;
in vec2 vUv;

out vec4 fragColor;

uniform vec3 uCam;
uniform vec3 uLightDir;
uniform vec3 uLightCol;
uniform vec3 uAmbTop;
uniform vec3 uAmbBot;
uniform vec3 uColor;
uniform float uShine;
uniform float uSpecular;
uniform float uOverride;      /* > 0: ignore lighting and emit uOverrideCol */
uniform vec3  uOverrideCol;

void main() {
  if (uOverride > 0.0) {
    fragColor = vec4(uOverrideCol, 1.0);
    return;
  }
  vec3 n = normalize(vNrm);
  vec3 view = normalize(uCam - vWorld);
  vec3 l = normalize(uLightDir);

  float ndl  = max(dot(n, l), 0.0);
  vec3 sky   = mix(uAmbBot, uAmbTop, n.y * 0.5 + 0.5);
  vec3 halfv = normalize(l + view);
  float spec = pow(max(dot(n, halfv), 0.0), uShine) * uSpecular * ndl;

  vec3 col = uColor * (sky + uLightCol * ndl) + uLightCol * spec;
  fragColor = vec4(pow(clamp(col, 0.0, 1.0), vec3(1.0 / 2.2)), 1.0);
}
