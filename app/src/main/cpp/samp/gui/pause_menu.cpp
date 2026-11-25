#include "pause_menu.h"

#include <algorithm>

#include "vendor/imgui/imgui.h"
#include "../main.h"
#include "../game/game.h"
#include "../game/Mobile/MobileSettings/MobileSettings.h"
#include "../game/Mobile/MobileMenu/MobileMenu.h"
#include "../game/pad.h"
#include "../vendor/armhook/patch.h"
#include "gui.h"
#include "samp_widgets/nexionsettingsmenu.h"

extern CGame* pGame;
extern UI* pUI;

namespace {
    bool g_open = false;
    int g_page = 0;
    NexionSettingsMenu g_nexionSettings;

    int ClampSetting(eMobileSettings id, int value)
    {
        auto& setting = CMobileSettings::ms_MobileSettings[id];
        if (setting.max >= setting.min)
            value = std::clamp(value, setting.min, setting.max);
        return value;
    }

    bool SettingSlider(const char* label, eMobileSettings id)
    {
        auto& setting = CMobileSettings::ms_MobileSettings[id];
        int value = setting.value;
        const int minValue = setting.min;
        const int maxValue = std::max(setting.max, minValue + 1);
        bool changed = ImGui::SliderInt(label, &value, minValue, maxValue);
        if (changed)
            setting.value = ClampSetting(id, value);
        return changed;
    }

    bool SettingCheck(const char* label, eMobileSettings id)
    {
        auto& setting = CMobileSettings::ms_MobileSettings[id];
        bool value = setting.value != 0;
        bool changed = ImGui::Checkbox(label, &value);
        if (changed)
            setting.value = value ? 1 : 0;
        return changed;
    }

    void OpenNativeMap()
    {
        if (gMobileMenu != nullptr) {
            gMobileMenu->DisplayingMap = true;
            gMobileMenu->isMapMode = true;
            CHook::CallFunction<void>("_ZN10MobileMenu12InitForPauseEv", gMobileMenu);
        }
        CHook::CallFunction<void>("_Z17Menu_MapInitPausev");
    }

    void Style()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 0.0f;
        style.ChildRounding = 0.0f;
        style.FrameRounding = 2.0f;
        style.PopupRounding = 2.0f;
        style.ScrollbarRounding = 2.0f;
        style.WindowBorderSize = 0.0f;
        style.FrameBorderSize = 0.0f;
        style.WindowPadding = ImVec2(18.0f, 16.0f);
        style.ItemSpacing = ImVec2(12.0f, 10.0f);
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.035f, 0.035f, 0.045f, 0.97f);
        style.Colors[ImGuiCol_ChildBg] = ImVec4(0.055f, 0.055f, 0.065f, 0.96f);
        style.Colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.12f, 0.14f, 1.0f);
        style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.18f, 0.20f, 1.0f);
        style.Colors[ImGuiCol_Button] = ImVec4(0.11f, 0.11f, 0.13f, 1.0f);
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.95f, 0.43f, 0.08f, 1.0f);
        style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.75f, 0.28f, 0.04f, 1.0f);
        style.Colors[ImGuiCol_Header] = ImVec4(0.95f, 0.43f, 0.08f, 1.0f);
        style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.95f, 0.43f, 0.08f, 1.0f);
        style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.60f, 0.14f, 1.0f);
    }
}

namespace PauseMenu {
    bool IsOpen() { return g_open; }

    void Open()
    {
        if (g_open) return;
        g_open = true;
        g_nexionSettings.show();
        if (pGame) pGame->EnableGameInput(false);
        PcInputRefreshNativeInterfaceState();
    }

    void Close()
    {
        if (!g_open) return;
        g_open = false;
        g_nexionSettings.hide();
        if (pGame) pGame->EnableGameInput(true);
        PcInputRefreshNativeInterfaceState();
    }

    void Toggle()
    {
        if (g_open) Close(); else Open();
    }

    void Render()
    {
        if (!g_open) return;

        if (pUI && g_nexionSettings.open()) {
            g_nexionSettings.draw(pUI->renderer());
            return;
        }

        Style();
        ImGuiIO& io = ImGui::GetIO();
        const ImVec2 display = io.DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(display.x * 0.08f, display.y * 0.08f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(display.x * 0.84f, display.y * 0.84f), ImGuiCond_Always);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
        if (!ImGui::Begin("PAUSE MENU##samp_pause_menu", nullptr, flags)) {
            ImGui::End();
            return;
        }

        ImGui::TextColored(ImVec4(0.95f, 0.43f, 0.08f, 1.0f), "SAN ANDREAS MULTIPLAYER");
        ImGui::Separator();
        ImGui::BeginChild("categories", ImVec2(display.x * 0.22f, -1.0f), true);
        const char* pages[] = {"Continuar", "Mapa", "Graficos", "Controles", "Interface", "Audio"};
        for (int i = 0; i < 6; ++i) {
            if (ImGui::Selectable(pages[i], g_page == i, 0, ImVec2(-1.0f, 42.0f)))
                g_page = i;
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("content", ImVec2(0.0f, -1.0f), true);

        if (g_page == 0) {
            ImGui::Text("Jogo pausado");
            ImGui::TextWrapped("O input do game esta bloqueado enquanto este menu esta aberto.");
            if (ImGui::Button("CONTINUAR", ImVec2(-1.0f, 50.0f))) Close();
            if (ImGui::Button("ABRIR MAPA", ImVec2(-1.0f, 50.0f))) {
                OpenNativeMap();
                g_page = 1;
            }
        } else if (g_page == 1) {
            ImGui::Text("MAPA");
            ImGui::TextWrapped("Mapa nativo inicializado pela libGame.so. O destino selecionado sera convertido em waypoint e rota pelos path nodes do jogo.");
            CHook::CallFunction<void, float>("_Z14Menu_MapUpdatef", 0.0f);
            if (ImGui::Button("ATUALIZAR MAPA", ImVec2(-1.0f, 46.0f)))
                OpenNativeMap();
            if (ImGui::Button("VOLTAR", ImVec2(-1.0f, 46.0f))) g_page = 0;
        } else if (g_page == 2) {
            ImGui::Text("GRAFICOS");
            SettingSlider("Qualidade visual", MS_Visuals);
            SettingSlider("Resolucao", MS_Resolution);
            SettingSlider("Distancia de renderizacao", MS_DrawDistance);
            SettingSlider("Sombras", MS_Shadows);
            SettingSlider("Brilho", MS_Brightness);
            SettingSlider("Limite de FPS", MS_FrameLimiter);
            SettingCheck("Reflexos dos veiculos", MS_CarReflections);
            SettingCheck("Trafego", MS_Traffic);
        } else if (g_page == 3) {
            ImGui::Text("CONTROLES E CAMERA");
            SettingSlider("Sensibilidade horizontal", MS_MouseSensitivityX);
            SettingSlider("Sensibilidade vertical", MS_MouseSensitivityY);
            SettingCheck("Inverter olhar", MS_InvertLook);
            SettingCheck("Vibracao", MS_VibrationFeedback);
            SettingCheck("Auto-climb", MS_AutoClimb);
        } else if (g_page == 4) {
            ImGui::Text("INTERFACE");
            SettingCheck("HUD", MS_HUD);
            ImGui::Text("Opcoes adicionais do cliente ficam persistidas no settings.ini.");
        } else if (g_page == 5) {
            ImGui::Text("AUDIO");
            SettingSlider("Efeitos sonoros", MS_SFXVolume);
            SettingSlider("Musica", MS_MusicVolume);
            SettingCheck("HRTF", MS_HRTF);
        }

        ImGui::EndChild();
        ImGui::End();
    }

    bool HandleTouch(int type, int x, int y)
    {
        if (!g_open || !g_nexionSettings.open()) return false;
        TouchType touch = type == 2 ? TouchType::push : (type == 3 ? TouchType::move : TouchType::pop);
        g_nexionSettings.touchEvent(ImVec2(static_cast<float>(x), static_cast<float>(y)), touch);
        return true;
    }
}

// O editor Nexion pode ser usado mesmo quando o HUD completo ainda não foi
// inicializado. A prévia é deliberadamente simples e não toca no estado do GTA.
void DrawNexionEditorHudPreview(ImDrawList* draw, const ImVec2& display, float scale, int)
{
    if (!draw) return;
    const ImVec2 center(display.x * 0.5f, display.y * 0.5f);
    const float size = std::max(18.0f, 34.0f * scale);
    draw->AddCircleFilled(ImVec2(center.x - size * 2.0f, center.y), size,
                          IM_COL32(51, 143, 221, 170));
    draw->AddCircleFilled(ImVec2(center.x + size * 2.0f, center.y), size,
                          IM_COL32(242, 74, 87, 170));
    draw->AddRect(ImVec2(center.x - size * 0.5f, center.y - size * 0.5f),
                  ImVec2(center.x + size * 0.5f, center.y + size * 0.5f),
                  IM_COL32(238, 238, 238, 180), 4.0f * scale, 0, 2.0f * scale);
}
