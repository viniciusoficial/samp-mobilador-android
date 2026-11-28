#pragma once

#include <cstdint>

namespace sa {
class CCamera;
struct CCam;
}

// Porte fiel do sistema de mouse do mobilador
// (app/src/main/cpp/samp/game/native_camera_mouse.cpp): mesma matematica, mesmos
// limites de pitch, mesmo fluxo pre/pos CCam::Process e mesmas opcoes.
//
// Thread-safety: a thread de UI do Android apenas enfileira deltas/botoes; a
// matematica de camera roda exclusivamente na thread do jogo, dentro do hook de
// CCam::Process (antes e depois do codigo original do engine).

void NativeCameraMouseEnqueue(float dx, float dy);
void NativeCameraMouseEnqueueWheel(int steps);
void NativeCameraMouseSetButtons(uint8_t buttonMask);
void NativeCameraMouseReset();
void NativeCameraMouseSetOptions(float maxDelta, float smoothing, float aimSensitivity, bool vehicleAutoCenter);
uint32_t NativeCameraMouseGetAppliedCount();

void NativeCameraMousePreProcess(sa::CCamera& camera, sa::CCam* cam);
void NativeCameraMousePostProcess(sa::CCamera& camera, sa::CCam* cam);

// Re-fixa a cada frame as teclas do pad que pertencem ao mouse. LocalPlayerKeys
// e zerado pelo GetKeys do engine a cada poll, exatamente como no mobilador,
// onde o pad.cpp consulta PcInputIsPressed() em toda leitura.
void NativeCameraMouseApplyKeys();

void InstallNativeCameraMouseHooks();
