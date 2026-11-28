#pragma once

#include <cstdint>

namespace sa {
class CCamera;
struct CCam;
}

// Relative mouse input -> SA camera integration (physical/hardware mouse coming
// from Android generic motion events).
//
// Everything here is thread-safe: the Android UI thread only enqueues deltas and
// button states, while the camera math runs exclusively on the game thread inside
// the CCam::Process hook, right before and after the engine's own camera code.
namespace pcmouse {

// Android UI thread (JNI)
void addDelta(float dx, float dy);
void setButton(int button, bool pressed);
void addWheel(int steps);
void reset();

// Game thread
bool isActive();
void preProcess(sa::CCamera& camera, sa::CCam* cam);
void postProcess(sa::CCamera& camera, sa::CCam* cam);

void installHooks();

} // namespace pcmouse
