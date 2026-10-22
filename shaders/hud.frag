#version 460 core
in vec4 vColor;
out vec4 frag;

void main() {
  /* Premultiplied ink blended over whatever was already in the framebuffer. */
  frag = vColor;
}
