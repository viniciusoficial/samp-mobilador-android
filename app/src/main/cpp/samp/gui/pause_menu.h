#pragma once

namespace PauseMenu {
    bool IsOpen();
    void Toggle();
    void Open();
    void Close();
    void Render();
    bool HandleTouch(int type, int x, int y);
}
