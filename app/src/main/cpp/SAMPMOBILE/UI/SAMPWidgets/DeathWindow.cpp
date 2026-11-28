#include "DeathWindow.h"
#include "Scoreboard.h"
#include "../UI.h"
#include "../UISettings.h"
#include "../../Client.h"
#include "../../Game/Common.h"
#include "../../Game/Game.h"
#include "../../Net/NetGame.h"
#include <encoding.h>


/* DeathWindow */

DeathWindow::DeathWindow()
{
	m_pDeathWindow.clear();
}

void DeathWindow::record(const char* playername, unsigned int playercolor, const char* killername, unsigned int killercolor, uint8_t reason)
{
	DeathWindowStruct* pPlayerKill = new DeathWindowStruct;
	pPlayerKill->playerName = playername ? playername : "";
	pPlayerKill->playercolor = playercolor;
	pPlayerKill->killerName = killername ? killername : "";
	pPlayerKill->killercolor = killercolor;
	pPlayerKill->reason = reason;

	if (m_pDeathWindow.size() >= 5) {
		delete m_pDeathWindow.front();
		m_pDeathWindow.pop_front();
	}

	m_pDeathWindow.push_back(pPlayerKill);
}

static const char* FallbackWeaponTag(uint8_t reason)
{
	switch (reason) {
		case WEAPON_FIST: return "[Fist]";
		case WEAPON_BRASSKNUCKLE: return "[Knuckle]";
		case WEAPON_GOLFCLUB: return "[Golf]";
		case WEAPON_NITESTICK: return "[Stick]";
		case WEAPON_KNIFE: return "[Knife]";
		case WEAPON_BAT: return "[Bat]";
		case WEAPON_SHOVEL: return "[Shovel]";
		case WEAPON_POOLSTICK: return "[Pool]";
		case WEAPON_KATANA: return "[Katana]";
		case WEAPON_CHAINSAW: return "[Saw]";
		case WEAPON_GRENADE: return "[Nade]";
		case WEAPON_TEARGAS: return "[Gas]";
		case WEAPON_MOLOTOV: return "[Molotov]";
		case WEAPON_COLT45: return "[9mm]";
		case WEAPON_SILENCED: return "[SD9mm]";
		case WEAPON_DEAGLE: return "[Deagle]";
		case WEAPON_SHOTGUN: return "[Shotgun]";
		case WEAPON_SAWEDOFF: return "[Sawnoff]";
		case WEAPON_SHOTGSPA: return "[SPAZ]";
		case WEAPON_UZI: return "[Uzi]";
		case WEAPON_MP5: return "[MP5]";
		case WEAPON_AK47: return "[AK47]";
		case WEAPON_M4: return "[M4]";
		case WEAPON_TEC9: return "[Tec9]";
		case WEAPON_RIFLE: return "[Rifle]";
		case WEAPON_SNIPER: return "[Sniper]";
		case WEAPON_ROCKETLAUNCHER: return "[RPG]";
		case WEAPON_HEATSEEKER: return "[HS-RPG]";
		case WEAPON_FLAMETHROWER: return "[Flame]";
		case WEAPON_MINIGUN: return "[Minigun]";
		case WEAPON_SATCHEL: return "[Satchel]";
		case WEAPON_SPRAYCAN: return "[Spray]";
		case WEAPON_FIREEXTINGUISHER: return "[Exting]";
		case WEAPON_VEHICLE: return "[Car]";
		case WEAPON_DROWN: return "[Drown]";
		case WEAPON_HELIBLADES: return "[Heli]";
		case WEAPON_EXPLOSION: return "[Boom]";
		case WEAPON_COLLISION: return "[Fall]";
		case 200: return "[Join]";
		case 201: return "[Quit]";
		default: return "[Death]";
	}
}

void DeathWindow::draw(ImGuiRenderer* renderer)
{
	if (pUI && pUI->scoreboard() && pUI->scoreboard()->visible()) {
		return;
	}
	if (!m_pDeathWindow.empty()) {
		float fPosSprite = pUI->ScaleX(1480.0f);
		const float fontSize = UISettings::fontSize() / 2.0f;
		ImFont* weapFont = pUI->weapFont();

		ImVec2 vecPos;
		vecPos.y = pUI->ScaleY(420.0f);

		auto drawWeaponIcon = [&](float x, float y, uint8_t reason, uint32_t iconColor) {
			if (weapFont) {
				renderer->drawText(ImVec2(x, y), 0xFF000000, "G", false, fontSize, weapFont);
				renderer->drawText(ImVec2(x, y), iconColor, spriteIDForWeapon(reason), false, fontSize, weapFont);
			}
			else {
				renderer->drawText(ImVec2(x, y), iconColor, FallbackWeaponTag(reason), true, fontSize * 0.85f);
			}
		};

		for (auto& playerkill : m_pDeathWindow) {
			if (playerkill) {
				// Decode raw SA-MP 0xRRGGBBAA uniformly to ImColor(R, G, B)
				const ImColor playerCol = UI::fixcolor(playerkill->playercolor | 0xFF);
				const ImColor killerCol = UI::fixcolor(playerkill->killercolor | 0xFF);

				if (!playerkill->killerName.empty() && !playerkill->playerName.empty()) {
					const std::string killerUtf = Encoding::cp2utf(playerkill->killerName);
					const std::string playerUtf = Encoding::cp2utf(playerkill->playerName);
					vecPos.x = (fPosSprite - renderer->calculateTextSize(killerUtf, fontSize).x) - 10.0f;
					renderer->drawText(vecPos, killerCol, killerUtf, true, fontSize);

					drawWeaponIcon(fPosSprite, vecPos.y, playerkill->reason, 0xFFFFFFFF);

					const float iconW = weapFont ? (fontSize + 20.0f)
						: (renderer->calculateTextSize(FallbackWeaponTag(playerkill->reason), fontSize * 0.85f).x + 12.0f);
					vecPos.x = fPosSprite + iconW;
					renderer->drawText(vecPos, playerCol, playerUtf, true, fontSize);
				}
				else if (playerkill->killerName.empty() && !playerkill->playerName.empty()) {
					const std::string playerUtf = Encoding::cp2utf(playerkill->playerName);
					vecPos.x = (fPosSprite - renderer->calculateTextSize(playerUtf, fontSize).x) - 10.0f;
					renderer->drawText(vecPos, playerCol, playerUtf, true, fontSize);

					// PC SA-MP ICON_CONNECT = 200 (blue), ICON_DISCONNECT = 201 (red)
					uint32_t dwColor = 0xFFFFFFFF;
					if (playerkill->reason == 200) {
						dwColor = ImColor(0x11, 0x11, 0xAA);
					}
					else if (playerkill->reason == 201) {
						dwColor = ImColor(0xAA, 0x11, 0x11);
					}

					drawWeaponIcon(fPosSprite, vecPos.y, playerkill->reason, dwColor);
				}

				vecPos.y += fontSize + 5.0f;
			}
		}
	}
}

const char* DeathWindow::spriteIDForWeapon(uint8_t byteWeaponID)
{
	switch (byteWeaponID) {
		case WEAPON_FIST: return "%";
		case WEAPON_BRASSKNUCKLE: return "B";
		case WEAPON_GOLFCLUB: return ">";
		case WEAPON_NITESTICK: return "(";
		case WEAPON_KNIFE: return "C";
		case WEAPON_BAT: return "?";
		case WEAPON_SHOVEL: return "&";
		case WEAPON_POOLSTICK: return "\"";
		case WEAPON_KATANA: return "!";
		case WEAPON_CHAINSAW: return "1";
		case WEAPON_DILDO: return "E";
		case WEAPON_DILDO2: return "E";
		case WEAPON_VIBRATOR: return "E";
		case WEAPON_VIBRATOR2: return "E";
		case WEAPON_FLOWER: return "$";
		case WEAPON_CANE: return "#";
		case WEAPON_GRENADE: return "@";
		case WEAPON_TEARGAS: return "D";
		case WEAPON_COLT45: return "6";
		case WEAPON_SILENCED: return "2";
		case WEAPON_DEAGLE: return "3";
		case WEAPON_SHOTGUN: return "=";
		case WEAPON_SAWEDOFF: return "0";
		case WEAPON_SHOTGSPA: return "+";
		case WEAPON_UZI: return "I";
		case WEAPON_MP5: return "8";
		case WEAPON_AK47: return "H";
		case WEAPON_M4: return "5";
		case WEAPON_TEC9: return "7";
		case WEAPON_RIFLE: return ".";
		case WEAPON_SNIPER: return "A";
		case WEAPON_ROCKETLAUNCHER: return "4";
		case WEAPON_HEATSEEKER: return ")";
		case WEAPON_FLAMETHROWER: return "P";
		case WEAPON_MINIGUN: return "F";
		case WEAPON_SATCHEL: return "<";
		case WEAPON_BOMB: return ";";
		case WEAPON_SPRAYCAN: return "/";
		case WEAPON_FIREEXTINGUISHER: return ",";
		case WEAPON_PARACHUTE: return ":";
		case WEAPON_VEHICLE: return "L";
		case WEAPON_DROWN: return "J";
		case WEAPON_HELIBLADES: return "R";
		case WEAPON_EXPLOSION: return "Q";
		case WEAPON_COLLISION: return "K";
		case 200: return "N";
		case 201: return "N";
	}
	return "J";
}