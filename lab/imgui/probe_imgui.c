/*
 * lab/imgui probe: measure a Dear ImGui (cimgui) text overlay as pixels.
 * Standalone: cimgui static lib + our GL loader + system GLFW. Product untouched.
 * Output numbers: ink coverage, ink/panel luminance, WCAG contrast, frame timing.
 */
#define CIMGUI_USE_GLFW
#define CIMGUI_USE_OPENGL3
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"
#include "cimgui_impl.h"
#include "gl.h"
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define W 800
#define H 450

static unsigned char* fb;   /* top-down RGBA, row 0 = top */

static double lum(unsigned r, unsigned g, unsigned b) {
    return (r * 30.0 + g * 59.0 + b * 11.0) / 100.0;
}

static double srgb(double v) {
    return v <= 0.03928 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
}

static double wcag(double a, double b) {
    double la = srgb(a / 255.0), lb = srgb(b / 255.0);
    if (la < lb) { double t = la; la = lb; lb = t; }
    return (la + 0.05) / (lb + 0.05);
}

static ImVec2 hud_pos, hud_size;
static int begin_ok = 0;

static bool demo_open = false;   /* enable with PROBE_DEMO=1 (control test for widgets) */
static int g_verbose = 0;   /* print diagnostics only on the measured frame */

static int draw_frame(void) {
    ImGui_ImplOpenGL3_NewFrame();
    igNewFrame();
    if (demo_open) igShowDemoWindow(&demo_open);
    igSetNextWindowPos((ImVec2){ 8.0f, 8.0f }, ImGuiCond_Always, (ImVec2){ 0.0f, 0.0f });
    float hud_w = getenv("PROBE_HUD_W") ? (float)atoi(getenv("PROBE_HUD_W")) : 0.0f;
    float hud_h = getenv("PROBE_HUD_H") ? (float)atoi(getenv("PROBE_HUD_H")) : 0.0f;
    if (hud_w > 0 && hud_h > 0)
        igSetNextWindowSize((ImVec2){ hud_w, hud_h }, ImGuiCond_Always);
    begin_ok = igBegin("hud", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
    if (!begin_ok && g_verbose) fprintf(stderr, "igBegin failed\n");
    igText("verts 389024  tris 129674  fps 61.4");
    igText("yaw 42.1 deg  pitch -18.7 deg  dist 4.12");
    igText("LMB drag orbit   wheel zoom   RMB pan   H hud   R reset   S screenshot");
    if (g_verbose)
        printf("frame: begin=%d drawListCmds=%d drawListVtx=%d windowPos=%.0f,%.0f size=%.0f,%.0f\n", begin_ok,
               igGetWindowDrawList()->CmdBuffer.Size, igGetWindowDrawList()->VtxBuffer.Size,
               igGetWindowPos().x, igGetWindowPos().y, igGetWindowSize().x, igGetWindowSize().y);
    hud_pos = igGetWindowPos();
    hud_size = igGetWindowSize();
    igEnd();
    igRender();
    if (g_verbose) printf("GL error before backend render: %u\n", (unsigned)gl_check_error());
    ImGui_ImplOpenGL3_RenderDrawData(igGetDrawData());
    if (g_verbose) printf("GL error after backend render: %u\n", (unsigned)gl_check_error());
    return 0;
}

int main(void) {
    setbuf(stdout, NULL);
    if (!glfwInit()) { fprintf(stderr, "glfwInit failed\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    GLFWwindow* win = glfwCreateWindow(W, H, "imgui probe", NULL, NULL);
    if (!win) { fprintf(stderr, "window failed\n"); glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win);

    char err[256] = { 0 };
    if (gl_load(glfwGetProcAddress, err, sizeof err)) { fprintf(stderr, "gl_load: %s\n", err); return 1; }
    printf("GL  : %s\n", gl_version_string(err, sizeof err));

    /* offscreen target so pixel numbers are deterministic */
    GLuint fbo, tex, depth;
    my_glGenFramebuffers(1, &fbo);
    my_glGenTextures(1, &tex);
    my_glGenRenderbuffers(1, &depth);
    my_glBindTexture(GL_TEXTURE_2D, tex);
    my_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    my_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    my_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    my_glBindRenderbuffer(GL_RENDERBUFFER, depth);
    my_glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, W, H);
    my_glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    my_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    my_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    if (my_glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "fbo incomplete\n"); return 1;
    }

    ImGuiContext* ctx = igCreateContext(NULL);
    printf("imgui ver: %s\n", igGetVersion());
    ImGuiStyle* st = igGetStyle();
    igStyleColorsDark(st);
    if (getenv("PROBE_DEMO")) demo_open = atoi(getenv("PROBE_DEMO")) != 0;
    st->FontSizeBase = 17.0f;
    float font_scale = getenv("PROBE_FONT_SCALE") ? (float)atof(getenv("PROBE_FONT_SCALE")) : 1.25f;
    st->FontScaleMain = font_scale;
    printf("font scale: %.2f\n", font_scale);
    st->Colors[ImGuiCol_WindowBg] = (ImVec4){ 0.10f, 0.12f, 0.16f, 0.92f };
    st->Colors[ImGuiCol_Text] = (ImVec4){ 0.92f, 0.96f, 1.00f, 1.00f };
    st->FramePadding = (ImVec2){ 6.0f, 3.0f };
    st->ItemSpacing = (ImVec2){ 6.0f, 2.0f };

    if (!ImGui_ImplGlfw_InitForOpenGL(win, true)) { fprintf(stderr, "glfw backend failed\n"); return 1; }
    if (!ImGui_ImplOpenGL3_Init("#version 460 core")) { fprintf(stderr, "gl backend failed\n"); return 1; }
    ImGuiIO* io = igGetIO_Nil();
    io->IniFilename = NULL;   /* no saved state: fresh window flags each run */
    ImFont* font = ImFontAtlas_AddFontDefault(io->Fonts, NULL);
    printf("font: %s\n", font ? "ok" : "NULL");
    printf("io: display %.0fx%.0f, defaultFont=%s\n", io->DisplaySize.x, io->DisplaySize.y,
           io->FontDefault ? "yes" : "no");
    printf("style: fontBase=%.1f scaleMain=%.2f\n", st->FontSizeBase, st->FontScaleMain);

    fb = malloc((size_t)W * H * 4);
    int use_fbo = getenv("PROBE_FBO") ? atoi(getenv("PROBE_FBO")) : 1;
    printf("target: %s\n", use_fbo ? "fbo" : "default framebuffer");
    GLuint target = use_fbo ? fbo : 0;

    my_glViewport(0, 0, W, H);
    my_glClearColor(0.16f, 0.18f, 0.22f, 1.0f);

    int fw = 0, fh = 0;
    glfwGetFramebufferSize(win, &fw, &fh);
    printf("glfw framebuffer: %dx%d\n", fw, fh);

    /* warm-up frames: with ImGuiBackendFlags_RendererHasTextures the font atlas is built
       at EndFrame and uploaded at the next backend NewFrame, so text appears from frame 2. */
    const int frames = 4;
    double t_first = glfwGetTime();
    double t_prev = t_first;
    for (int it = 0; it < frames; it++) {
        g_verbose = (it == frames - 1);
        my_glBindFramebuffer(GL_FRAMEBUFFER, target);
        my_glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        ImGui_ImplGlfw_NewFrame();
        draw_frame();
        if (!use_fbo) { glfwSwapBuffers(win); glfwPollEvents(); }
        double t_now = glfwGetTime();
        if (g_verbose) printf("frame time: %.2f ms (frame 1 took %.2f ms)\n",
                             (t_now - t_prev) * 1000.0, (t_prev - t_first) * 1000.0);
        t_prev = t_now;
    }
    printf("io display after NewFrame %.0fx%.0f\n", io->DisplaySize.x, io->DisplaySize.y);
    printf("markers: frame drawn (begin=%d)\n", begin_ok);
    ImDrawData* dd = igGetDrawData();
    printf("draw data: valid=%d cmdLists=%d vert=%d idx=%d fbScale=%.2f\n", dd->Valid, dd->CmdLists.Size,
           dd->TotalVtxCount, dd->TotalIdxCount, dd->FramebufferScale.x);
    printf("draw data display: pos %.0f,%.0f size %.0f,%.0f viewport=%s\n", dd->DisplayPos.x, dd->DisplayPos.y,
           dd->DisplaySize.x, dd->DisplaySize.y, dd->OwnerViewport ? "ok" : "NULL");
    printf("io: backendFlags=%d hasTextures=%d renderWindows=%d renderVtx=%d\n", io->BackendFlags,
           (io->BackendFlags & ImGuiBackendFlags_RendererHasTextures) ? 1 : 0,
           io->MetricsRenderWindows, io->MetricsRenderVertices);
    printf("atlas: built=%d texList=%d locked=%d hasTexBackend=%d fontDefault=%s\n", io->Fonts->TexIsBuilt ? 1 : 0,
           io->Fonts->TexList.Size, io->Fonts->Locked ? 1 : 0, io->Fonts->RendererHasTextures ? 1 : 0,
           io->FontDefault ? "yes" : "no");
    printf("draw data textures=%d\n", dd->Textures->Size);
    printf("GL error after render: %u\n", (unsigned)gl_check_error());
    my_glFinish();
    my_glBindFramebuffer(GL_FRAMEBUFFER, target);
    my_glReadBuffer(use_fbo ? GL_COLOR_ATTACHMENT0 : GL_FRONT);
    my_glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, fb);
    my_glBindFramebuffer(GL_FRAMEBUFFER, 0);

    /* flip to top-down (GL rows are bottom-up) */
    unsigned char* flip = malloc((size_t)W * H * 4);
    for (int y = 0; y < H; y++)
        memcpy(flip + (size_t)y * W * 4, fb + (size_t)(H - 1 - y) * W * 4, (size_t)W * 4);

    /* classify inside the hud window box (measured from ImGui, not guessed) */
    int x0 = (int)floor(hud_pos.x), y0 = (int)floor(hud_pos.y);
    int x1 = (int)ceil(hud_pos.x + hud_size.x), y1 = (int)ceil(hud_pos.y + hud_size.y);
    printf("hud rect: x %d..%d y %d..%d (%d x %d px)\n", x0, x1, y0, y1, x1 - x0, y1 - y0);

    /* fixed panel reference: WindowBg composited over the scene clear colour */
    double scene_lum = lum((unsigned)(0.16f * 255.0f), (unsigned)(0.18f * 255.0f), (unsigned)(0.22f * 255.0f));
    double bg_lum = lum((unsigned)(st->Colors[ImGuiCol_WindowBg].x * 255.0f),
                        (unsigned)(st->Colors[ImGuiCol_WindowBg].y * 255.0f),
                        (unsigned)(st->Colors[ImGuiCol_WindowBg].z * 255.0f));
    double bg_a = st->Colors[ImGuiCol_WindowBg].w;
    double panel_ref = bg_a * bg_lum + (1.0 - bg_a) * scene_lum;
    double ink_thresh = panel_ref + 25.0;
    printf("panel reference lum %.1f, ink threshold %.1f\n", panel_ref, ink_thresh);

    double panel_sum = 0, ink_sum = 0;
    long panel_n = 0, ink_n = 0, outside_n = 0;
    double ink_max_lum = 0, panel_min_lum = 1e9, panel_max_lum = 0;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            unsigned char* p = flip + ((size_t)y * W + x) * 4;
            double l = lum(p[0], p[1], p[2]);
            int inside = x >= x0 && x < x1 && y >= y0 && y < y1;
            if (!inside) { if (l > 200) outside_n++; continue; }
            if (l > ink_thresh) {          /* ink = clearly brighter than panel */
                ink_sum += l; ink_n++;
                if (l > ink_max_lum) ink_max_lum = l;
            } else {
                panel_sum += l; panel_n++;
                if (l < panel_min_lum) panel_min_lum = l;
                if (l > panel_max_lum) panel_max_lum = l;
            }
        }
    }
    double panel_lum = panel_n ? panel_sum / (double)panel_n : 0;
    double ink_lum = ink_n ? ink_sum / (double)ink_n : 0;
    double area = (double)(x1 - x0) * (y1 - y0);
    printf("panel mean lum %.1f (min %.1f max %.1f, n=%ld)\n", panel_lum, panel_min_lum, panel_max_lum, panel_n);
    printf("ink   mean lum %.1f (max %.1f, n=%ld, coverage %.2f%% of hud rect)\n",
           ink_lum, ink_max_lum, ink_n, area > 0 ? 100.0 * (double)ink_n / area : 0.0);
    printf("contrast ink/panel: %.2f:1 (WCAG), bright pixels outside hud=%ld\n",
           wcag(ink_lum, panel_lum), outside_n);

    /* stroke structure: must have short runs, not solid rows */
    long short_runs = 0, long_runs = 0;
    for (int y = y0; y < y1 && y < H; y++) {
        int run = 0;
        for (int x = x0; x < x1 && x < W; x++) {
            unsigned char* p = flip + ((size_t)y * W + x) * 4;
            if (lum(p[0], p[1], p[2]) > ink_thresh) run++;
            else { if (run > 0) { if (run <= 12) short_runs++; else long_runs++; } run = 0; }
        }
        if (run > 0) { if (run <= 12) short_runs++; else long_runs++; }
    }
    printf("runs: short(<=12px)=%ld long(>12px)=%ld ratio=%.2f\n", short_runs, long_runs,
           long_runs ? (double)short_runs / (double)long_runs : 99.0);

    /* ascii dump of the first text row */
    printf("first rows (top-down):\n");
    for (int y = y0; y < y0 + 22 && y < H; y++) {
        char line[121] = { 0 };
        for (int x = x0; x < x0 + 120 && x < x1; x++) {
            unsigned char* p = flip + ((size_t)y * W + x) * 4;
            line[x - x0] = lum(p[0], p[1], p[2]) > ink_thresh ? '#' : '.';
        }
        printf("%3d %s\n", y, line);
    }

    /* timing over 30 frames */
    printf("markers: timing start\n");
    fflush(stdout);
    double t0 = glfwGetTime();
    for (int i = 0; i < 30; i++) {
        my_glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        ImGui_ImplGlfw_NewFrame();
        draw_frame();
        my_glFinish();
    }
    double t1 = glfwGetTime();
    printf("hud+scene frame: %.3f ms/frame (30 frames)\n", (t1 - t0) * 1000.0 / 30.0);

    /* determinism: redraw, compare */
    unsigned char* again = malloc((size_t)W * H * 4);
    my_glBindFramebuffer(GL_FRAMEBUFFER, target);
    my_glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    ImGui_ImplGlfw_NewFrame();
    draw_frame();
    my_glFinish();
    my_glReadBuffer(use_fbo ? GL_COLOR_ATTACHMENT0 : GL_FRONT);
    my_glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, again);
    my_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    long diff = 0;
    for (size_t i = 0; i < (size_t)W * H * 4; i++) if (fb[i] != again[i]) diff++;
    printf("byte diff vs first frame: %ld (%.4f%% of buffer)\n", diff, 100.0 * (double)diff / ((double)W * H * 4));

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    igDestroyContext(ctx);
    glfwDestroyWindow(win);
    glfwTerminate();
    free(fb); free(flip); free(again);
    printf("probe done\n");
    return 0;
}
