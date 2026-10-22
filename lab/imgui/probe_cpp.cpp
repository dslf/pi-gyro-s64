// Control probe in C++: imgui.h directly (same objects as libcimgui.a). If this renders,
// the failure above is a cimgui ABI mismatch, not the library itself.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <GLFW/glfw3.h>
#include "imgui.h"
extern "C" {
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
}

static const int W = 800, H = 450;

int main(void) {
    setbuf(stdout, NULL);
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* win = glfwCreateWindow(W, H, "imgui cpp probe", NULL, NULL);
    if (!win) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win);
    printf("GL: %s\n", (const char*)glGetString(GL_VERSION));

    ImGui::CreateContext();
    ImGuiStyle& style = ImGui::GetStyle();
    style.FontSizeBase = 17.0f;
    style.FontScaleMain = 1.25f;
    ImGui::StyleColorsDark();
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->AddFontDefault();
    if (!ImGui_ImplGlfw_InitForOpenGL(win, true)) { fprintf(stderr, "glfw backend failed\n"); return 1; }
    if (!ImGui_ImplOpenGL3_Init("#version 460 core")) { fprintf(stderr, "gl backend failed\n"); return 1; }

    glClearColor(0.16f, 0.18f, 0.22f, 1.0f);
    glViewport(0, 0, W, H);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    ImGui_ImplGlfw_NewFrame();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    printf("frame count=%d\n", ImGui::GetFrameCount());
    ImGui::SetNextWindowPos(ImVec2(8, 8), ImGuiCond_Always, ImVec2(0, 0));
    bool opened = ImGui::Begin("hud");
    printf("begin=%d\n", opened ? 1 : 0);
    printf("window draw list cmds=%d vtx=%d\n", (int)ImGui::GetWindowDrawList()->CmdBuffer.size(),
           (int)ImGui::GetWindowDrawList()->VtxBuffer.size());
    ImGui::Text("verts 389024  tris 129674  fps 61.4");
    ImGui::Text("yaw 42.1 deg  pitch -18.7 deg  dist 4.12");
    ImGui::Text("LMB drag orbit   wheel zoom   RMB pan   H hud   R reset   S screenshot");
    printf("after text cmds=%d vtx=%d\n", (int)ImGui::GetWindowDrawList()->CmdBuffer.size(),
           (int)ImGui::GetWindowDrawList()->VtxBuffer.size());
    ImGui::End();
    ImGui::Render();
    printf("draw data: cmdLists=%d vtx=%d idx=%d\n", ImGui::GetDrawData()->CmdLists.Size,
           ImGui::GetDrawData()->TotalVtxCount, ImGui::GetDrawData()->TotalIdxCount);
    for (int i = 0; i < ImGui::GetDrawData()->CmdLists.Size; i++)
        printf("  list %d: vtx=%d cmds=%d\n", i, ImGui::GetDrawData()->CmdLists[i]->VtxBuffer.Size,
               ImGui::GetDrawData()->CmdLists[i]->CmdBuffer.Size);
    printf("GL error before render: %u\n", (unsigned)glGetError());
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    printf("GL error after render: %u\n", (unsigned)glGetError());
    glfwSwapBuffers(win);
    glfwPollEvents();

    unsigned char* px = (unsigned char*)malloc((size_t)W * H * 4);
    glReadBuffer(GL_FRONT);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
    long ink = 0;
    for (int y = 0; y < 60; y++)
        for (int x = 0; x < 300; x++) {
            unsigned char* p = px + ((size_t)y * W + x) * 4;
            double l = (p[0] * 30 + p[1] * 59 + p[2] * 11) / 100.0;
            if (l > 120) ink++;
        }
    printf("bright pixels in top-left band (60x300): %ld\n", ink);
    printf("imgui cpp probe: PASS\n");

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    free(px);
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
