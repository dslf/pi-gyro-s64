// Core-only control: no GLFW/GL backends. DrawData must be non-empty if imgui.o works.
#include <cstdio>
#include "imgui.h"

int main(void) {
    setbuf(stdout, NULL);
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(800.0f, 450.0f);
    io.DeltaTime = 1.0f / 60.0f;
    io.Fonts->AddFontDefault();
    io.Fonts->GetTexDataAsRGBA32();
    ImGui::StyleColorsDark();

    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(8, 8), ImGuiCond_Always, ImVec2(0, 0));
    bool opened = ImGui::Begin("core-hud");
    ImGui::Text("hello 389024");
    ImGui::End();
    ImGui::Render();

    ImDrawData* dd = ImGui::GetDrawData();
    printf("core: begin=%d cmdLists=%d vtx=%d idx=%d display=%.0fx%.0f\n", opened ? 1 : 0,
           dd->CmdLists.Size, dd->TotalVtxCount, dd->TotalIdxCount, dd->DisplaySize.x, dd->DisplaySize.y);
    for (int i = 0; i < dd->CmdLists.Size; i++) {
        const ImDrawCmd* c0 = dd->CmdLists[i]->CmdBuffer.empty() ? NULL : &dd->CmdLists[i]->CmdBuffer[0];
        printf("  list %d vtx=%d cmds=%d clip=%.0f,%.0f..%.0f,%.0f elem=%p\n", i,
               dd->CmdLists[i]->VtxBuffer.Size, dd->CmdLists[i]->CmdBuffer.Size,
               c0 ? c0->ClipRect.x : -1, c0 ? c0->ClipRect.y : -1, c0 ? c0->ClipRect.z : -1,
               c0 ? c0->ClipRect.w : -1, c0 ? (void*)c0->UserCallbackData : NULL);
    }
    printf("font atlas: built=%d texList=%d drawDataTextures=%d\n", io.Fonts->TexIsBuilt ? 1 : 0,
           io.Fonts->TexList.Size, dd->Textures->Size);
    printf("core probe: %s\n", dd->TotalVtxCount > 0 ? "PASS" : "FAIL");
    ImGui::DestroyContext();
    return dd->TotalVtxCount > 0 ? 0 : 1;
}
