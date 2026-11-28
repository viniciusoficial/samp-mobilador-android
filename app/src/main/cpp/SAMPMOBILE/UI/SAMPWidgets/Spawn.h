#pragma once

#include "../Widgets/Layout.h"

class Button;

class Spawn : public Layout {
public:
	Spawn();

private:
	Button* m_buttonPrev;
	Button* m_buttonSpawn;
	Button* m_buttonNext;
};