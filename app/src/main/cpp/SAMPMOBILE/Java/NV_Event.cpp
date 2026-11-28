#include <cstring>

#include "NV_Event.h"

/* NV_Event */

std::mutex NV_Event::g_EventMutex;
std::list<NVEvent*> NV_Event::g_pEvents;

void NV_Event::InsertNewest(NVEvent* ev)
{
	if (!ev) return;
	std::lock_guard<std::mutex> lock(g_EventMutex);

	constexpr size_t kMaxQueuedEvents = 256;
	if (g_pEvents.size() >= kMaxQueuedEvents) {
		// Drop the oldest queued move event first; fall back to oldest entry.
		auto victim = g_pEvents.end();
		for (auto it = g_pEvents.begin(); it != g_pEvents.end(); ++it) {
			NVEvent* e = *it;
			if (e && e->m_type == NV_EVENT_MULTITOUCH &&
				((static_cast<uint32_t>(e->m_data.m_multi.m_action) & NV_MULTITOUCH_ACTION_MASK) == NV_MULTITOUCH_MOVE)) {
				victim = it;
				break;
			}
		}
		if (victim == g_pEvents.end()) {
			victim = g_pEvents.begin();
		}
		delete *victim;
		g_pEvents.erase(victim);
	}

	NVEvent* pEvent = new NVEvent;
	memcpy(pEvent, ev, sizeof(NVEvent));

	g_pEvents.push_back(pEvent);
}

void NV_Event::GetNextEvent(NVEvent* ev)
{
	if (!ev) return;
	std::lock_guard<std::mutex> lock(g_EventMutex);

	while (!g_pEvents.empty()) {
		NVEvent* pPopped = g_pEvents.front();
		g_pEvents.pop_front();
		if (!pPopped) {
			continue;
		}
		memcpy(ev, pPopped, sizeof(NVEvent));
		delete pPopped;
		return;
	}

	ev->m_type = (NVEventType) 228;
}
