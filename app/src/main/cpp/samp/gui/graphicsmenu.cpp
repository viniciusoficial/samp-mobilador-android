#include "../main.h"
#include "../settings.h"
#include "graphicsmenu.h"
#include "../vendor/imgui/imgui.h"
#include "../game/Mobile/MobileSettings/MobileSettings.h"

extern CSettings* pSettings;

namespace GraphicsMenu
{
namespace
{
    bool s_bVisible = false;
    bool s_bDragging = false;
    ImVec2 s_windowMin(0.0f, 0.0f);
    ImVec2 s_windowMax(0.0f, 0.0f);

    int ClampInt(int value, int lowest, int highest)
    {
        if (value < lowest) return lowest;
        if (value > highest) return highest;
        return value;
    }

    // Aplica sem gravar log a cada frame: durante o arrasto do slider isso
    // escreveria centenas de linhas em arquivo.
    void ApplyLive()
    {
        ApplyNexionGraphicsSettings(false);
    }

    void Persist()
    {
        if (pSettings)
            pSettings->SaveNexionMenuSettings();
    }
}

bool IsVisible()
{
    return s_bVisible;
}

void Toggle()
{
    s_bVisible = !s_bVisible;
    s_bDragging = false;

    if (!s_bVisible)
        Persist();
}

void Close()
{
    if (!s_bVisible)
        return;

    s_bVisible = false;
    s_bDragging = false;
    Persist();
}

bool HandleTouch(int type, int x, int y)
{
    if (!s_bVisible)
        return false;

    ImGuiIO& io = ImGui::GetIO();
    io.MousePos = ImVec2(static_cast<float>(x), static_cast<float>(y));

    const bool bInside = static_cast<float>(x) >= s_windowMin.x &&
                         static_cast<float>(x) <= s_windowMax.x &&
                         static_cast<float>(y) >= s_windowMin.y &&
                         static_cast<float>(y) <= s_windowMax.y;

    switch (type)
    {
        case 2: // push
            io.MouseDown[0] = true;
            s_bDragging = bInside;
            return bInside;

        case 3: // move
            return s_bDragging;

        case 1: // pop
            io.MouseDown[0] = false;
        {
            const bool bWasDragging = s_bDragging;
            s_bDragging = false;
            return bWasDragging;
        }

        default:
            break;
    }

    return false;
}

void Render()
{
    if (!s_bVisible || pSettings == nullptr)
        return;

    ImGuiIO& io = ImGui::GetIO();
    if (io.DisplaySize.x < 10.0f || io.DisplaySize.y < 10.0f)
        return;

    const ImVec2 display = io.DisplaySize;
    const ImVec2 windowPos(display.x * 0.06f, display.y * 0.08f);
    const ImVec2 windowSize(display.x * 0.52f, display.y * 0.74f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 16.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 34.0f);

    ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.90f);

    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin("Graficos - /menu", nullptr, flags))
    {
        s_windowMin = ImGui::GetWindowPos();
        const ImVec2 windowActual = ImGui::GetWindowSize();
        s_windowMax = ImVec2(s_windowMin.x + windowActual.x, s_windowMin.y + windowActual.y);

        stSettings& settings = pSettings->GetWrite();
        bool bChanged = false;

        char fpsText[64];
        snprintf(fpsText, sizeof(fpsText), "FPS: %.0f", io.Framerate);
        ImGui::TextUnformatted(fpsText);
        ImGui::Separator();

        // Resolucao: e' o ajuste que mais muda a nitidez e o FPS.
        int resolution = settings.iNexionRenderScale < 0 ? 100 : ClampInt(settings.iNexionRenderScale, 25, 100);
        if (ImGui::SliderInt("Resolucao (%)", &resolution, 25, 100))
        {
            settings.iNexionRenderScale = resolution;
            settings.iNexionMobileSettings[MS_Resolution] = -1; // o campo resumido manda
            bChanged = true;
        }

        int drawDistance = settings.iNexionDrawDistance < 0 ? 100 : ClampInt(settings.iNexionDrawDistance, 0, 100);
        if (ImGui::SliderInt("Distancia", &drawDistance, 0, 100))
        {
            settings.iNexionDrawDistance = drawDistance;
            settings.iNexionMobileSettings[MS_DrawDistance] = -1;
            bChanged = true;
        }

        const char* visualsNames[] = { "Baixo", "Medio", "Alto", "Muito alto" };
        int visuals = settings.iNexionVisualEffects < 0 ? 2 : ClampInt(settings.iNexionVisualEffects, 0, 3);
        if (ImGui::Combo("Efeitos visuais", &visuals, visualsNames, 4))
        {
            settings.iNexionVisualEffects = visuals;
            settings.iNexionMobileSettings[MS_Visuals] = -1;
            bChanged = true;
        }

        const char* shadowNames[] = { "Desligadas", "So o player", "Todos os peds" };
        int shadows = ClampInt(settings.iNexionMobileSettings[MS_Shadows] < 0 ? 1
                              : settings.iNexionMobileSettings[MS_Shadows], 0, 2);
        if (ImGui::Combo("Sombras", &shadows, shadowNames, 3))
        {
            settings.iNexionMobileSettings[MS_Shadows] = shadows;
            bChanged = true;
        }

        const char* reflectionNames[] = { "Desligados", "Baixos", "Altos" };
        int reflections = ClampInt(settings.iNexionMobileSettings[MS_CarReflections] < 0 ? 2
                                  : settings.iNexionMobileSettings[MS_CarReflections], 0, 2);
        if (ImGui::Combo("Reflexos dos carros", &reflections, reflectionNames, 3))
        {
            settings.iNexionMobileSettings[MS_CarReflections] = reflections;
            bChanged = true;
        }

        ImGui::Separator();

        if (ImGui::Button("Salvar", ImVec2(windowActual.x * 0.30f, 0.0f)))
            Persist();

        ImGui::SameLine();

        if (ImGui::Button("Padrao", ImVec2(windowActual.x * 0.30f, 0.0f)))
        {
            settings.iNexionRenderScale = 100;
            settings.iNexionDrawDistance = 100;
            settings.iNexionVisualEffects = 2;
            settings.iNexionMobileSettings[MS_Resolution] = -1;
            settings.iNexionMobileSettings[MS_DrawDistance] = -1;
            settings.iNexionMobileSettings[MS_Visuals] = -1;
            settings.iNexionMobileSettings[MS_Shadows] = 1;
            settings.iNexionMobileSettings[MS_CarReflections] = 2;
            bChanged = true;
        }

        ImGui::SameLine();

        if (ImGui::Button("Fechar", ImVec2(windowActual.x * 0.25f, 0.0f)))
            Close();

        if (bChanged)
            ApplyLive();
    }

    ImGui::End();
    ImGui::PopStyleVar(4);
}
}
