#pragma once

class ImGuiRenderer;

namespace artwork {

// Renders a compact non-blocking ImGui progress overlay while 0.3.DL artwork files are downloading.
void DrawDownloadOverlay(ImGuiRenderer* renderer, float displayW, float displayH);

} // namespace artwork
