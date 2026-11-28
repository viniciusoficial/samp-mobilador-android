#pragma once

#include <cstdio>
#include <ctime>
#include <functional>
#include <list>
#include <map>

#include "../Widgets/ListBox.h"
#include "../Widgets/Label.h"
#include "../UISettings.h"
#include "../../Settings.h"

class Chat : public ListBox {
public:
	Chat();
	~Chat();

	void addChatMessage(const std::string& message, const std::string& nick, const ImColor& nick_color);
	void addInfoMessage(const char* format, ...) __attribute__((format(printf, 2, 3)));
	void addDebugMessage(const char* format, ...) __attribute__((format(printf, 2, 3)));
	void addClientMessage(const std::string& message, const ImColor& color);

	void addClientCommand(const std::string& cmd_name, const std::function<void(const std::string&)>& callback);

	static void setTimestamp(bool timestamp) { m_timestamp = timestamp; }
	static bool timestamp() { return m_timestamp; }

	virtual void draw(ImGuiRenderer* renderer) override;

	virtual void activateEvent(bool active) override;
	virtual void touchPopEvent() override;
	virtual void keyboardEvent(const std::string& input) override;

private:
	void addMessage(const std::string& message, const ImColor& color = ImColor(1.0f, 1.0f, 1.0f));
	void writeChatLog(const std::string& timePrefix, const std::string& message);

	class MessageItem : public ListBoxItem {
	public:
		MessageItem(const std::string& message, const ImColor& color, const std::string& timePrefix)
			: m_rawText(message)
			, m_timePrefix(timePrefix)
		{
			const std::string initial = m_timestamp ? (m_timePrefix + m_rawText) : m_rawText;
			m_text = new Label(initial, color, Settings::chatShadow(), UISettings::fontSize() / 2);
			this->addChild(m_text);
		}

		void performLayout() override
		{
			const float desiredFont = UISettings::fontSize() / 2;
			if (m_text->fontSize() != desiredFont) {
				m_text->setFontSize(desiredFont);
			}
			const std::string desired = m_timestamp ? (m_timePrefix + m_rawText) : m_rawText;
			if (m_text->text() != desired) {
				m_text->setText(desired);
			}
			else {
				m_text->performLayout();
			}
			m_text->setPosition(ImVec2(0.0f, (height() - m_text->height()) / 2));
		}

	private:
		std::string m_rawText;
		std::string m_timePrefix;
		Label* m_text;
	};

private:
	std::map<std::string, std::function<void(const std::string&)>> m_commands;
	FILE* m_chatLogFile = nullptr;
	bool m_chatLogOpened = false;
	static bool m_timestamp;
};
