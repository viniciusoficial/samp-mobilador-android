#include "ScrollPanel.h"
#include "../ImGuiRenderer.h"

ScrollPanel::ScrollPanel()
{
	m_scroll = ImVec2(0.0f, 0.0f);
	m_scrollableX = true;
	m_scrollableY = true;
}

void ScrollPanel::setScrollX(float x)
{
	if (children().empty()) return;
	Widget* child = children()[0];

	m_scroll.x = std::clamp(x, 0.0f, 1.0f);
	const float maxX = std::max(0.0f, child->width() - width());
	const float maxY = std::max(0.0f, child->height() - height());
	ImVec2 deltaPos = ImVec2(-m_scroll.x * maxX, -m_scroll.y * maxY);

	child->setPosition(deltaPos);
}

void ScrollPanel::setScrollY(float y)
{
	if (children().empty()) return;
	Widget* child = children()[0];

	m_scroll.y = std::clamp(y, 0.0f, 1.0f);
	const float maxX = std::max(0.0f, child->width() - width());
	const float maxY = std::max(0.0f, child->height() - height());
	ImVec2 deltaPos = ImVec2(-m_scroll.x * maxX, -m_scroll.y * maxY);

	child->setPosition(deltaPos);
}

void ScrollPanel::performLayout()
{
	if (children().empty()) return;
	if (children().size() > 1) return;

	Widget* child = children()[0];

	child->performLayout();
	this->setSize(child->size());

	// Preserve current normalized scroll offset across relayouts (e.g. /timestamp)
	const float maxX = std::max(0.0f, child->width() - width());
	const float maxY = std::max(0.0f, child->height() - height());
	child->setPosition(ImVec2(-m_scroll.x * maxX, -m_scroll.y * maxY));
}

void ScrollPanel::draw(ImGuiRenderer* renderer)
{
	Widget::draw(renderer);
}

void ScrollPanel::touchMoveEvent(const ImVec2& delta)
{
	if (children().empty()) return;
	Widget* child = children()[0];

	const float maxX = std::max(0.0f, child->width() - width());
	const float maxY = std::max(0.0f, child->height() - height());
	ImVec2 deltaPos = child->position();

	if (m_scrollableX) {
		deltaPos.x += delta.x;
	}

	if (m_scrollableY) {
		deltaPos.y += delta.y;
	}

	deltaPos.x = std::clamp(deltaPos.x, -maxX, 0.0f);
	deltaPos.y = std::clamp(deltaPos.y, -maxY, 0.0f);

	m_scroll.x = (maxX > 0.0f) ? (-deltaPos.x / maxX) : 0.0f;
	m_scroll.y = (maxY > 0.0f) ? (-deltaPos.y / maxY) : 0.0f;

	child->setPosition(deltaPos);
}