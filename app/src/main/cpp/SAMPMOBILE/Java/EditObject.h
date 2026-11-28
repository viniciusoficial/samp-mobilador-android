//
// Created by ALYN on 25-Jan-25.
//

#ifndef SA_MP_MOBILE_EDITOBJECT_H
#define SA_MP_MOBILE_EDITOBJECT_H

#include <jni.h>
#include <atomic>
#include <cstdint>

class EditObject {
public:
	enum eEditType {
		TYPE_NONE,
		TYPE_ATTACHED_OBJECT,
		TYPE_OBJECT
	};

	enum eResponse : uint32_t {
		EDIT_RESPONSE_CANCEL,
		EDIT_RESPONSE_FINAL,
		EDIT_RESPONSE_UPDATE
	};

	enum eOpKind : uint8_t {
		OP_CLICK = 1,
		OP_EXIT = 2,
		OP_SAVE = 3
	};

	static inline int m_id = -1;
	static inline eEditType m_type = TYPE_NONE;
	static inline std::atomic<bool> m_visible{false};
	static inline bool m_playerObj = false;
	static inline uint32_t m_lastUpdate = 0;

public:
	static void startEditAttachedObject(int slot);
	static void startEditObject(uint16_t objectId);
	static void showGui(bool show);
	static void cancelEdit();
	static void exitEdit();

	/**
	 * JNI callbacks run on the Java UI thread — they must NOT touch GTA
	 * entities or the shared ScriptCommand buffer directly. They enqueue
	 * ops here; processPending() drains them on the game thread from the
	 * CGame_Process hook.
	 */
	static void enqueueOp(int kind, int button, bool state);
	static void processPending();

private:
	struct PendingOp {
		uint8_t kind;
		int32_t button;
		uint8_t state;
		uint32_t sessionGen;
	};

	static constexpr uint32_t OP_CAPACITY = 32;
	static inline PendingOp s_ops[OP_CAPACITY] = {};
	static inline std::atomic<uint32_t> s_opPush{0};
	static inline std::atomic<uint32_t> s_opPop{0};
	static inline std::atomic<uint32_t> s_sessionGen{0};

	static void applyClick(int button, bool state);
	static void applyExit();
	static void applySave();
};

#endif //SA_MP_MOBILE_EDITOBJECT_H
