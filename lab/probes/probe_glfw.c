#include <stdio.h>
#include "GLFW/glfw3.h"

int main(void) {
  printf("glfw header %d.%d.%d\n", GLFW_VERSION_MAJOR, GLFW_VERSION_MINOR, GLFW_VERSION_REVISION);
  if (!glfwInit()) { printf("init failed\n"); return 1; }
  int gm = 0, gn = 0, gr = 0;
  glfwGetVersion(&gm, &gn, &gr);
  printf("glfw runtime %d.%d.%d\n", gm, gn, gr);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
  glfwWindowHint(GLFW_SAMPLES, 4);
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  GLFWwindow* w = glfwCreateWindow(64, 64, "t", NULL, NULL);
  printf("window: %s\n", w ? "ok" : "failed");
  if (w) {
    glfwMakeContextCurrent(w);
    glfwDestroyWindow(w);
  }
  glfwTerminate();
  return w ? 0 : 1;
}
