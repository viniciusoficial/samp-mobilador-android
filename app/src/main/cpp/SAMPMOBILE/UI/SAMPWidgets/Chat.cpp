#include "Chat.h"
#include "Keyboard.h"
#include "Scoreboard.h"
#include "../UI.h"
#include "../UISettings.h"
#include "../../Client.h"
#include "../../Game/Game.h"
#include "../../Game/Util.h"
#include "../../Net/NetGame.h"
#include "../../Java/Java.h"
#include <encoding.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cctype>
#include <cstdarg>


bool Chat::m_timestamp = false;

namespace {

std::string formatCurrentTimePrefix()
{
	time_t rawtime = std::time(nullptr);
	tm timeinfo{};
	localtime_r(&rawtime, &timeinfo);
	char buf[16];
	std::strftime(buf, sizeof(buf), "[%H:%M:%S] ", &timeinfo);
	return std::string(buf);
}

std::string stripColorCodes(const std::string& input)
{
	std::string out;
	out.reserve(input.size());
	for (size_t i = 0; i < input.size(); ++i) {
		if (input[i] == '{' && i + 7 < input.size() && input[i + 7] == '}') {
			bool isHex = true;
			for (size_t j = 1; j <= 6; ++j) {
				if (!std::isxdigit(static_cast<unsigned char>(input[i + j]))) {
					isHex = false;
					break;
				}
			}
			if (isHex) {
				i += 7;
				continue;
			}
		}
		out.push_back(input[i]);
	}
	return out;
}

} // namespace

Chat::Chat()
		: ListBox(), m_commands()
{
	m_timestamp = Settings::timestamp();
}

Chat::~Chat()
{
	if (m_chatLogFile) {
		fclose(m_chatLogFile);
		m_chatLogFile = nullptr;
	}
}

void Chat::addChatMessage(const std::string& message, const std::string& nick, const ImColor& nick_color)
{
	std::string full_message = nick + "{FFFFFF}: " + message;
	addMessage(full_message, UI::fixcolor(nick_color));
}

void Chat::addInfoMessage(const char* format, ...)
{
	if (!format) return;
	char tmp_buf[512];

	va_list args;
	va_start(args, format);
	vsnprintf(tmp_buf, sizeof(tmp_buf), format, args);
	va_end(args);

	addMessage(std::string(tmp_buf), ImColor(0x88, 0xFF, 0x62));
}

void Chat::addDebugMessage(const char* format, ...)
{
	if (!format) return;
	char tmp_buf[512];

	va_list args;
	va_start(args, format);
	vsnprintf(tmp_buf, sizeof(tmp_buf), format, args);
	va_end(args);

	addMessage(std::string(tmp_buf), ImColor(0xA9, 0xC4, 0xE4));
}

void Chat::addClientMessage(const std::string& message, const ImColor& color)
{
	addMessage(message, UI::fixcolor(color));
}

void Chat::writeChatLog(const std::string& timePrefix, const std::string& message)
{
	if (!m_chatLogOpened) {
		m_chatLogOpened = true;
		if (const char* dir = Client::gameDir(); dir && *dir) {
			char path[512];
			snprintf(path, sizeof(path), "%sSAMP/chatlog.txt", dir);
			m_chatLogFile = fopen(path, "w");
		}
	}
	if (!m_chatLogFile) return;

	const std::string plain = stripColorCodes(message);
	fprintf(m_chatLogFile, "%s%s\n", timePrefix.c_str(), plain.c_str());
	fflush(m_chatLogFile);
}

void Chat::addMessage(const std::string& message, const ImColor& color)
{
	const std::string timePrefix = formatCurrentTimePrefix();
	writeChatLog(timePrefix, message);

	if (this->itemsCount() > UISettings::chatMaxMessages()) {
		this->removeItem(0);
	}

	auto item = new MessageItem(message, color, timePrefix);
	this->addItem(item);
	this->setScrollY(1.0f);
}

void Chat::addClientCommand(const std::string& cmd_name, const std::function<void(const std::string&)>& callback)
{
	m_commands[cmd_name] = callback;
}

void Chat::draw(ImGuiRenderer* renderer)
{
	ListBox::draw(renderer);
}

void Chat::activateEvent(bool active)
{
	/*if (active) {
		this->setScrollable(true);
	}
	else {
		this->setScrollable(false);
	}*/
}

void Chat::touchPopEvent()
{
	if (pUI->scoreboard()->visible()) return;
	pUI->keyboard()->show(this);
}

void Chat::keyboardEvent(const std::string& input)
{
	if (input.empty()) return;

	// /mob abre as configuracoes de teclado/mouse (igual ao mobilador).
	{
		std::string lower = input;
		for (char& c : lower) {
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		if (lower == "/mob" || lower == "/mob " || lower.rfind("/mob ", 0) == 0) {
			if (g_java) {
				g_java->showKeyboardMouseSettings();
			}
			return;
		}
	}

	if (input[0] == '/') {
		std::string cmd_name;
		std::string cmd_param;

		size_t space_pos = input.find(' ');
		if (space_pos != std::string::npos) {
			cmd_name = input.substr(1, space_pos - 1);
			cmd_param = input.substr(space_pos + 1);
		}
		else {
			cmd_name = input.substr(1);
		}

		auto cmd = m_commands.find(cmd_name);
		if (cmd != m_commands.end()) {
			cmd->second(cmd_param);
		}
		else {
			if (pNetGame) {
				pNetGame->SendChatCommand(input.c_str());
			}
		}
	}
	else {
		if (pNetGame) {
			pNetGame->SendChatMessage(input.c_str());
		}
	}
}
