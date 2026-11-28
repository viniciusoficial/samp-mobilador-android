#pragma once

#include <cstdint>

// Unified cross-layer arbiter for focus/modal state, touch routing, GTA pointer release,
// radar tap suppression, and Back/ESC dismissal.
//
// Threading contract:
//   - Android UI thread: setJavaDialogVisible, setJavaKeyboardVisible, requestNativeBackDismiss
//   - GameMain thread:   publishGameMainState, processPendingBackDismiss, dismissTopNativeModal, isRadarSuppressed
//   - NVEvent thread:    shouldBypassExternalTouch, onTouchCancel, routeGtaTouch
//   - Any thread:        registerExternalTouchFilter
namespace InputArbiter {

using ExternalTouchFilterFn = bool (*)(int type, int num, int posX, int posY);
using ForwardGtaTouchFn = void (*)(int type, int num, int posX, int posY);

// Android UI thread (JNI) state updates
void setJavaDialogVisible(bool visible);
void setJavaKeyboardVisible(bool visible);
bool requestNativeBackDismiss();

// GameMain thread state publishing, Back/ESC consumption & radar suppression query
void publishGameMainState();
void processPendingBackDismiss();
void dismissTopNativeModal();
bool isRadarSuppressed();

// NVEvent thread touch routing & cancellation
bool shouldBypassExternalTouch();
void onTouchCancel();
void routeGtaTouch(int type, int num, int posX, int posY, ForwardGtaTouchFn forwardToGta);

// External touch filter registration (e.g. MonetLoader via SAMPMOBILE_RegisterTouchFilter)
void registerExternalTouchFilter(ExternalTouchFilterFn fn);

} // namespace InputArbiter
