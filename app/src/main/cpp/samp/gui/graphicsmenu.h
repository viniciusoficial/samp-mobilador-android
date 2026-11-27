#pragma once

// Painel de graficos aberto pelo comando "/menu" do chat.
// As mudancas sao aplicadas no motor imediatamente (o mundo continua sendo
// desenhado atras do painel) e gravadas em SAMP/settings.ini ao fechar.
namespace GraphicsMenu
{
    void Toggle();
    void Close();
    bool IsVisible();

    // Converte o toque do Android (1 = pop, 2 = push, 3 = move) em mouse do
    // ImGui. Retorna true quando o evento foi consumido pelo painel.
    bool HandleTouch(int type, int x, int y);

    // Chamado dentro do frame do ImGui (UI::drawList).
    void Render();
}
