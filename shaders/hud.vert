#version 460 core
layout(location = 0) in vec2 aPos;   /* pixel coordinates, y counted from the top */
layout(location = 1) in vec4 aColor; /* premultiplied glyph ink: rgb = fg * ink, a = ink */

uniform vec2 uViewport;

out vec4 vColor;

void main() {
  vColor = aColor;
  gl_Position = vec4(aPos.x / uViewport.x * 2.0 - 1.0,
                     1.0 - aPos.y / uViewport.y * 2.0,
                     0.0, 1.0);
}
