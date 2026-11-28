#include "Spawn.h"
#include "Scoreboard.h"
#include "../UI.h"
#include "../UISettings.h"
#include "../Widgets/Button.h"
#include "../../Client.h"
#include "../../Game/Game.h"
#include "../../Net/NetGame.h"


Spawn::Spawn()
		: Layout(Orientation::HORIZONTAL)
{
	m_buttonPrev = new Button("<<", UISettings::fontSize() / 2);
	m_buttonSpawn = new Button("Spawn", UISettings::fontSize() / 2);
	m_buttonNext = new Button(">>", UISettings::fontSize() / 2);

	m_buttonPrev->setCallback([]() {
		if (pUI && pUI->scoreboard() && pUI->scoreboard()->visible()) return;
		if (pNetGame) pNetGame->SendPrevClass();
	});
	m_buttonSpawn->setCallback([]() {
		if (pUI && pUI->scoreboard() && pUI->scoreboard()->visible()) return;
		if (pNetGame) pNetGame->SendSpawn();
	});
	m_buttonNext->setCallback([]() {
		if (pUI && pUI->scoreboard() && pUI->scoreboard()->visible()) return;
		if (pNetGame) pNetGame->SendNextClass();
	});

	this->addChild(m_buttonPrev);
	this->addChild(m_buttonSpawn);
	this->addChild(m_buttonNext);
}