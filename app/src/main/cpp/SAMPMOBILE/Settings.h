#pragma once

class Settings {
public:
	static void initialize();
	static void applyToGtaEngine(bool bInGameReady = false);
	static void applyDynamicCameraAndFreeCam();
	static void applyFpsLimitPatch();

	// server
	static char* host() { return m_host; }
	static int port() { return m_port; }
	static char* pass() { return m_pass; }
	static char* ip();

	// settings
	static char* nick() { return m_nick; }
	static const char* sampVersionName() { return m_sampVersionName; }
	static int sampNetgameVersion() { return m_sampNetgameVersion; }
	static bool systemkeyboard() { return m_systemkeyboard; }
	static bool timestamp() { return m_timestamp; }
	static void setTimestamp(bool v);
	static bool fastconnect() { return m_fastconnect; }
	static bool voice() { return m_voice; }
	static bool displayfps() { return m_displayfps; }
	static int fpslimit() { return m_fpslimit; }
	static void setFpsLimit(int v) { m_fpslimit = v; }
	static int chatstrings() { return m_chatstrings; }
	static void setChatstrings(int v) { m_chatstrings = v; }
	static float fontsize() { return m_fontsize; }
	static int fontSizeOffset() { return m_fontsizeoffset; }
	static void setFontSizeOffset(int v) { m_fontsizeoffset = v; }
	static float chatPosX() { return m_chatPosX; }
	static float chatPosY() { return m_chatPosY; }
	static float chatSizeX() { return m_chatSizeX; }
	static float chatSizeY() { return m_chatSizeY; }
	static char* charset() { return m_charset; }
	static const char* language() { return m_language; }
	static bool chineseLanguage() { return m_language[0] == 'z' && m_language[1] == 'h'; }
	static bool modloader() { return m_modloader; }
	static bool aml() { return m_aml; }
	static bool jpatch() { return m_jpatch; }
	static bool cleo() { return m_cleo; }
	static bool monet() { return m_monet; }

	// extended multiplayer & gameplay
	static bool headMove() { return m_headmove; }
	static void setHeadMove(bool v) { m_headmove = v; }
	static bool autoClimb() { return m_autoclimb; }
	static bool dlCustomModels() { return m_dlcustommodels; }

	// graphics & display (GTA:SA MobileSettings)
	static int visualFx() { return m_visualfx; }
	static int resolution() { return m_resolution; }
	static int drawDistance() { return m_drawdistance; }
	static int shadows() { return m_shadows; }
	static int carReflections() { return m_carreflections; }
	static int trafficMode() { return m_trafficmode; }
	static int brightness() { return m_brightness; }
	static int aspectRatio() { return m_aspectratio; }
	static bool frameLimiter() { return m_framelimiter; }

	// camera, controls & single-player GTA:SA options
	static int camSensX() { return m_camsensx; }
	static int camSensY() { return m_camsensy; }
	static int camSensFoot() { return m_camsensfoot; }
	static int camSensVeh() { return m_camsensveh; }
	static int camSensAim() { return m_camsensaim; }
	static bool freeCam() { return m_freecam; }
	static void setFreeCam(bool v) { m_freecam = v; }
	static bool firstPersonLook() { return m_firstpersonlook; }
	static bool invertLook() { return m_invertlook; }
	static int vehCamHeight() { return m_vehcamheight; }
	static int pedCamZoom() { return m_pedcamzoom; }
	static int targetingMode() { return m_targetingmode; }
	static int joystickSize() { return m_joysticksize; }
	static float joystickScale() { return static_cast<float>(m_joysticksize) / 100.0f; }
	static int analogSensitivity() { return m_analogsensitivity; }
	static int flickSensitivity() { return m_flicksensitivity; }
	static int shootThreshold() { return m_shootthreshold; }
	static int touchSteering() { return m_touchsteering; }
	static int touchLayout() { return m_touchlayout; }
	static bool bikeSteering() { return m_bikesteering; }
	static int accelerometer() { return m_accelerometer; }
	static bool vibration() { return m_vibration; }
	static bool joypadInvertX() { return m_joypadinvertx; }
	static bool joypadInvertY() { return m_joypadinverty; }
	static bool kmEnabled() { return m_kmenabled; }
	static bool openHudEditOnStart() { return m_openhudeditonstart; }
	static void setOpenHudEditOnStart(bool v) { m_openhudeditonstart = v; }

	// audio, radio & voice chat
	static int sfxVolume() { return m_sfxvolume; }
	static int musicVolume() { return m_musicvolume; }
	static bool radioAutoTune() { return m_radioautotune; }
	static int radioStation() { return m_radiostation; }
	static int voiceVolume() { return m_voicevolume; }
	static bool voiceMicEnable() { return m_voicemicenable; }
	static int voiceMicVolume() { return m_voicemicvolume; }
	static int voiceMicMode() { return m_voicemicmode; } // 0=PushToTalk, 1=Toggle
	static bool voiceBalancer() { return m_voicebalancer; }
	static bool voiceFilter() { return m_voicefilter; }
	static bool voiceSpeakerList() { return m_voicespeakerlist; }
	static int voiceBtnSize() { return m_voicebtnsize; }

	// HUD, fonts, chat & screenshot
	static bool showGtaHud() { return m_showgtahud; }
	static bool showRadar() { return m_showradar; }
	static bool radarTapMap() { return m_radartapmap; }
	static int crosshairStyle() { return m_crosshairstyle; }
	static bool showDeathWindow() { return m_showdeathwindow; }
	static bool showNameTags() { return m_shownametags; }
	static bool showNametagWeapon() { return m_shownametagweapon; }
	static int nametagSize() { return m_nametagsize; }
	static float nametagScale() { return static_cast<float>(m_nametagsize) / 100.0f; }
	static int textlabelSize() { return m_textlabelsize; }
	static float textlabelScale() { return static_cast<float>(m_textlabelsize) / 100.0f; }
	static int fontFamily() { return m_fontfamily; }
	static bool showButtonPanel() { return m_showbuttonpanel; }
	static bool showScreenshotBtn() { return m_showscreenshotbtn; }
	static bool gtaSubtitles() { return m_gtasubtitles; }
	static bool chatShadow() { return m_chatshadow; }
	static int screenshotMode() { return m_screenshotmode; }
	static void setScreenshotMode(int mode) { m_screenshotmode = mode; }
	static bool audioMsg() { return m_audiomsg; }
	static void setAudioMsg(bool v) { m_audiomsg = v; }
	static bool logUrls() { return m_logurls; }
	static void setLogUrls(bool v) { m_logurls = v; }
	static bool showNameTagStatus() { return m_shownametagstatus; }
	static void setShowNameTagStatus(bool v) { m_shownametagstatus = v; }

private:
	static bool m_initialized;

	// server
	static char m_host[64];
	static int m_port;
	static char m_pass[64];

	// settings
	static char m_nick[30];
	static char m_sampVersionName[16];
	static int m_sampNetgameVersion;
	static bool m_systemkeyboard;
	static bool m_timestamp;
	static bool m_fastconnect;
	static bool m_voice;
	static bool m_displayfps;
	static int m_fpslimit;
	static int m_chatstrings;
	static float m_fontsize;
	static float m_chatPosX;
	static float m_chatPosY;
	static float m_chatSizeX;
	static float m_chatSizeY;
	static char m_charset[16];
	static char m_language[8];
	static bool m_modloader;
	static bool m_aml;
	static bool m_jpatch;
	static bool m_cleo;
	static bool m_monet;

	static bool m_headmove;
	static bool m_autoclimb;
	static bool m_dlcustommodels;

	static int m_visualfx;
	static int m_resolution;
	static int m_drawdistance;
	static int m_shadows;
	static int m_carreflections;
	static int m_trafficmode;
	static int m_brightness;
	static int m_aspectratio;
	static bool m_framelimiter;

	static int m_camsensx;
	static int m_camsensy;
	static int m_camsensfoot;
	static int m_camsensveh;
	static int m_camsensaim;
	static bool m_freecam;
	static bool m_firstpersonlook;
	static bool m_invertlook;
	static int m_vehcamheight;
	static int m_pedcamzoom;
	static int m_targetingmode;
	static int m_joysticksize;
	static int m_analogsensitivity;
	static int m_flicksensitivity;
	static int m_shootthreshold;
	static int m_touchsteering;
	static int m_touchlayout;
	static bool m_bikesteering;
	static int m_accelerometer;
	static bool m_vibration;
	static bool m_joypadinvertx;
	static bool m_joypadinverty;
	static bool m_kmenabled;
	static bool m_openhudeditonstart;

	static int m_sfxvolume;
	static int m_musicvolume;
	static bool m_radioautotune;
	static int m_radiostation;
	static int m_voicevolume;
	static bool m_voicemicenable;
	static int m_voicemicvolume;
	static int m_voicemicmode;
	static bool m_voicebalancer;
	static bool m_voicefilter;
	static bool m_voicespeakerlist;
	static int m_voicebtnsize;

	static bool m_showgtahud;
	static bool m_showradar;
	static bool m_radartapmap;
	static int m_crosshairstyle;
	static bool m_showdeathwindow;
	static bool m_shownametags;
	static bool m_shownametagweapon;
	static int m_nametagsize;
	static int m_textlabelsize;
	static int m_fontfamily;
	static bool m_showbuttonpanel;
	static bool m_showscreenshotbtn;
	static bool m_gtasubtitles;
	static bool m_chatshadow;
	static int m_screenshotmode;
	static int m_fontsizeoffset;
	static bool m_audiomsg;
	static bool m_logurls;
	static bool m_shownametagstatus;
};
