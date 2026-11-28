#include "Settings.h"
#include "SettingsContract.h"
#include "Client.h"
#include "ClientSubsystems.h"
#include "Addr.h"
#include "Game/Engine/Symbols.h"
#include "Game/Engine/PatchRegistry.h"
#include "Game/Game.h"
#include "Game/PlayerPed.h"
#include "Game/TouchWidgets.h"
#include "Game/Util.h"
#include "Enums/WidgetIDs.h"

#include <arpa/inet.h>
#include <RakNet/SocketLayer.h>
#include <SA.h>
#include <SymUtils.h>
#include <spdlog/spdlog.h>
#include <rapidjson/document.h>
#include <rapidjson/filereadstream.h>
#include <rapidjson/filewritestream.h>
#include <rapidjson/prettywriter.h>
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <vector>

bool Settings::m_initialized = false;

// server
char Settings::m_host[64] = "127.0.0.1";
int Settings::m_port = 7777;
char Settings::m_pass[64] = "";

// settings
char Settings::m_nick[30] = "samp_player";
char Settings::m_sampVersionName[16] = "0.3.7";
int Settings::m_sampNetgameVersion = 0xFD9;
bool Settings::m_systemkeyboard = true;
bool Settings::m_timestamp = false;
bool Settings::m_fastconnect = false;
bool Settings::m_voice = false;
bool Settings::m_displayfps = false;
int Settings::m_fpslimit = 0; // Default 0 = Unlimited (无限制)
int Settings::m_chatstrings = 8;
float Settings::m_fontsize = 25.0f;
float Settings::m_chatPosX = 100.0f;
float Settings::m_chatPosY = 20.0f;
float Settings::m_chatSizeX = 360.0f;
float Settings::m_chatSizeY = 110.0f;
char Settings::m_charset[16] = "GBK";
char Settings::m_language[8] = "zh";
bool Settings::m_modloader = false;
bool Settings::m_aml = false;
bool Settings::m_jpatch = false;
bool Settings::m_cleo = false;
bool Settings::m_monet = false;

// extended multiplayer & gameplay
bool Settings::m_headmove = true;
bool Settings::m_autoclimb = true;
bool Settings::m_dlcustommodels = true;

// graphics & display (GTA:SA MobileSettings)
int Settings::m_visualfx = 2;        // High (0..3)
int Settings::m_resolution = 100;    // 100% (30..100)
int Settings::m_drawdistance = 70;   // 70% (10..100)
int Settings::m_shadows = 1;         // Classic (0..2)
int Settings::m_carreflections = 2;  // Medium (0..3)
int Settings::m_trafficmode = 2;     // Heavy (0..2)
int Settings::m_brightness = 60;     // 60% (0..100)
int Settings::m_aspectratio = 0;     // 0=Auto, 1=16:9, 2=20:9, 3=21:9, 4=16:10, 5=4:3
bool Settings::m_framelimiter = false;

// camera, controls & single-player GTA:SA options
int Settings::m_camsensx = 50;       // 1..100
int Settings::m_camsensy = 50;       // 1..100
int Settings::m_camsensfoot = 100;   // 20..200%
int Settings::m_camsensveh = 100;    // 20..200%
int Settings::m_camsensaim = 70;     // 20..200%
bool Settings::m_freecam = false;
bool Settings::m_firstpersonlook = true;
bool Settings::m_invertlook = false;
int Settings::m_vehcamheight = 1;    // High (0..1)
int Settings::m_pedcamzoom = 2;      // Normal (1..3)
int Settings::m_targetingmode = 0;   // Lock-On (0=LockOn, 1=FreeAim)
int Settings::m_joysticksize = 100;  // 100% (50..150)
int Settings::m_analogsensitivity = 50; // 1..100 -> mapped to 20..45
int Settings::m_flicksensitivity = 50;  // 1..100
int Settings::m_shootthreshold = 50;    // 1..100
int Settings::m_touchsteering = 0;   // Analog (0=Analog, 1=Buttons, 2=Flick)
int Settings::m_touchlayout = 0;     // Classic (0=Classic, 1=Adapted)
bool Settings::m_bikesteering = false;
int Settings::m_accelerometer = 0;   // Off (0..2)
bool Settings::m_vibration = true;
bool Settings::m_joypadinvertx = false;
bool Settings::m_joypadinverty = false;
bool Settings::m_kmenabled = true;
bool Settings::m_openhudeditonstart = false;

// audio, radio & voice chat
int Settings::m_sfxvolume = 80;      // 0..100
int Settings::m_musicvolume = 80;    // 0..100
bool Settings::m_radioautotune = true;
int Settings::m_radiostation = 0;    // 0..12
int Settings::m_voicevolume = 100;   // 0..100
bool Settings::m_voicemicenable = true;
int Settings::m_voicemicvolume = 75; // 0..100
int Settings::m_voicemicmode = 0;    // 0=PushToTalk, 1=Toggle
bool Settings::m_voicebalancer = false;
bool Settings::m_voicefilter = true;
bool Settings::m_voicespeakerlist = true;
int Settings::m_voicebtnsize = 100;  // 50..150%

// HUD, fonts, chat & screenshot
bool Settings::m_showgtahud = true;
bool Settings::m_showradar = true;
bool Settings::m_radartapmap = true;
int Settings::m_crosshairstyle = 0;  // 0=Default, 1=Cross, 2=Dot, 3=Cross+Dot, 4=Hidden
bool Settings::m_showdeathwindow = true;
bool Settings::m_shownametags = true;
bool Settings::m_shownametagweapon = false;
int Settings::m_nametagsize = 100;   // 50..150%
int Settings::m_textlabelsize = 100; // 50..150%
int Settings::m_fontfamily = 0;      // 0=chinese.ttf, 1=arial_bold.ttf, 2=custom.ttf
bool Settings::m_showbuttonpanel = true;
bool Settings::m_showscreenshotbtn = true;
bool Settings::m_gtasubtitles = true;
bool Settings::m_chatshadow = true;
int Settings::m_screenshotmode = 0;  // 0 = With HUD & Chat, 1 = Game Only
int Settings::m_fontsizeoffset = 0;  // -3..5 (/fontsize)
bool Settings::m_audiomsg = true;    // /audiomsg
bool Settings::m_logurls = false;    // /logurls
bool Settings::m_shownametagstatus = true; // /nametagstatus

char* Settings::ip()
{
	struct in_addr ipv4{};
	if (inet_pton(AF_INET, m_host, &ipv4) == 1) {
		return m_host;
	}
	char* resolved = (char*) SocketLayer::Instance()->DomainNameToIP(m_host);
	return resolved ? resolved : m_host;
}

void Settings::initialize()
{
	spdlog::info("Initializing settings...");

	if (m_initialized) {
		return;
	}

	if (Client::gameDir() == nullptr) {
		spdlog::error("Failed to initialize settings. (game storage null)");
		return;
	}

	char settings_json_path[512];
	snprintf(settings_json_path, sizeof(settings_json_path), "%sSAMP/settings.json", Client::gameDir());

	FILE* settings_json_file = fopen(settings_json_path, "rb");
	if (settings_json_file == nullptr) {
		spdlog::error("Failed to initialize settings. (settings file null)");
		return;
	}

	char buffer[0xFFFF];
	rapidjson::FileReadStream fileReadStream(settings_json_file, buffer, sizeof(buffer));

	rapidjson::Document settings_json;
	settings_json.ParseStream<0, rapidjson::UTF8<>, rapidjson::FileReadStream>(fileReadStream);
	fclose(settings_json_file);

	if (settings_json.HasParseError() || !settings_json.IsObject()
		|| !settings_json.HasMember("client") || !settings_json["client"].IsObject()) {
		spdlog::error("Failed to parse settings.json (parseError={}), keeping defaults",
			static_cast<int>(settings_json.GetParseError()));
		m_initialized = true;
		return;
	}

	const auto& clientObj = settings_json["client"];
	auto readStringFrom = [](const rapidjson::Value& obj, const char* key, char* dst, size_t dstSize) {
		if (obj.IsObject() && obj.HasMember(key) && obj[key].IsString()) {
			snprintf(dst, dstSize, "%s", obj[key].GetString());
		}
	};
	auto readBoolFrom = [](const rapidjson::Value& obj, const char* key, bool def) -> bool {
		return (obj.IsObject() && obj.HasMember(key) && obj[key].IsBool()) ? obj[key].GetBool() : def;
	};
	auto readIntFrom = [](const rapidjson::Value& obj, const char* key, int def, int minV, int maxV) -> int {
		if (obj.IsObject() && obj.HasMember(key) && obj[key].IsInt()) {
			int v = obj[key].GetInt();
			return std::max(minV, std::min(maxV, v));
		}
		return def;
	};
	auto readFloatFrom = [](const rapidjson::Value& obj, const char* key, float def, float minV, float maxV) -> float {
		if (obj.IsObject() && obj.HasMember(key) && obj[key].IsNumber()) {
			float v = obj[key].GetFloat();
			return std::max(minV, std::min(maxV, v));
		}
		return def;
	};

	// server
	if (clientObj.HasMember("server") && clientObj["server"].IsObject()) {
		const auto& srv = clientObj["server"];
		readStringFrom(srv, "host", m_host, sizeof(m_host));
		m_port = readIntFrom(srv, "port", 7777, 1, 65535);
		readStringFrom(srv, "password", m_pass, sizeof(m_pass));
	}

	// settings
	if (!clientObj.HasMember("settings") || !clientObj["settings"].IsObject()) {
		spdlog::warn("settings.json missing client.settings object, keeping defaults");
		m_initialized = true;
		return;
	}

	const auto& s = clientObj["settings"];
	readStringFrom(s, "nick_name", m_nick, sizeof(m_nick));
	{
		settings_contract::SampVersionSpec ver = settings_contract::kSampVersions[0];
		if (s.HasMember("samp_version")) {
			if (s["samp_version"].IsString()) {
				ver = settings_contract::parseSampVersion(s["samp_version"].GetString());
			}
			else if (s["samp_version"].IsInt()) {
				ver = settings_contract::parseSampVersionLegacyInt(s["samp_version"].GetInt());
			}
		}
		snprintf(m_sampVersionName, sizeof(m_sampVersionName), "%s", ver.id);
		m_sampNetgameVersion = ver.netgameVersion;
	}
	m_systemkeyboard = readBoolFrom(s, "system_keyboard", true);
	m_timestamp = readBoolFrom(s, "timestamp", false);
	m_fastconnect = readBoolFrom(s, "fast_connect", false);
	m_voice = readBoolFrom(s, "voice_chat", false);
	m_displayfps = readBoolFrom(s, "display_fps", false);
	m_fpslimit = readIntFrom(s, "fps_limit", 0, 0, 250);
	m_chatstrings = readIntFrom(s, "chat_strings", 8, 4, 24);
	m_fontsize = readFloatFrom(s, "font_size", 25.0f, 10.0f, 60.0f);
	m_chatPosX = static_cast<float>(readIntFrom(s, "chat_posx", 100, 0, 4096));
	m_chatPosY = static_cast<float>(readIntFrom(s, "chat_posy", 20, 0, 4096));
	m_chatSizeX = static_cast<float>(readIntFrom(s, "chat_sizex", 360, 64, 4096));
	m_chatSizeY = static_cast<float>(readIntFrom(s, "chat_sizey", 110, 32, 4096));
	readStringFrom(s, "charset", m_charset, sizeof(m_charset));
	readStringFrom(s, "app_language", m_language, sizeof(m_language));

	auto readBoolOpt = [&](const char* key, bool def) -> bool {
		return readBoolFrom(s, key, def);
	};
	auto readIntOpt = [&](const char* key, int def, int minV, int maxV) -> int {
		return readIntFrom(s, key, def, minV, maxV);
	};
	auto readEnumOpt = [&](const char* key, auto parseStrFn, auto parseIntFn, int def) -> int {
		if (!s.HasMember(key)) return def;
		if (s[key].IsString()) return parseStrFn(s[key].GetString());
		if (s[key].IsInt()) return parseIntFn(s[key].GetInt());
		return def;
	};

	m_modloader = readBoolOpt("modloader", false);
	m_aml = readBoolOpt("aml_scripts", false);
	m_jpatch = readBoolOpt("jpatch_enabled", false);
	m_cleo = readBoolOpt("cleo_scripts", false);
	m_monet = readBoolOpt("monet_scripts", false);

	m_headmove = readBoolOpt("head_move", true);
	m_autoclimb = readBoolOpt("auto_climb", true);
	m_dlcustommodels = readBoolOpt("dl_custom_models", true);

	m_visualfx = readIntOpt("gta_visual_fx", 2, 0, 3);
	m_resolution = readIntOpt("gta_resolution", 100, 30, 100);
	m_drawdistance = readIntOpt("gta_draw_distance", 70, 10, 100);
	m_shadows = readIntOpt("gta_shadows", 1, 0, 2);
	m_carreflections = readIntOpt("gta_car_reflections", 2, 0, 3);
	m_trafficmode = readIntOpt("gta_traffic_mode", 2, 0, 2);
	m_brightness = readIntOpt("gta_brightness", 60, 0, 100);
	m_aspectratio = readEnumOpt("aspect_ratio", settings_contract::parseAspectRatio, settings_contract::parseAspectRatioLegacyInt, 0);
	m_framelimiter = readBoolOpt("frame_limiter", false);

	m_camsensx = readIntOpt("cam_sens_x", 50, 1, 100);
	m_camsensy = readIntOpt("cam_sens_y", 50, 1, 100);
	m_camsensfoot = readIntOpt("cam_sens_foot", 100, 20, 200);
	m_camsensveh = readIntOpt("cam_sens_veh", 100, 20, 200);
	m_camsensaim = readIntOpt("cam_sens_aim", 70, 20, 200);
	m_freecam = readBoolOpt("free_cam", false);
	m_firstpersonlook = readBoolOpt("first_person_look", true);
	m_invertlook = readBoolOpt("invert_look", false);
	m_vehcamheight = readIntOpt("veh_cam_height", 1, 0, 1);
	m_pedcamzoom = readIntOpt("ped_cam_zoom", 2, 1, 3);
	m_targetingmode = readIntOpt("targeting_mode", 0, 0, 1);
	m_joysticksize = readIntOpt("joystick_size", 100, 50, 150);
	m_analogsensitivity = readIntOpt("analog_sensitivity", 50, 1, 100);
	m_flicksensitivity = readIntOpt("flick_sensitivity", 50, 1, 100);
	m_shootthreshold = readIntOpt("shoot_threshold", 50, 1, 100);
	m_touchsteering = readIntOpt("touch_steering", 0, 0, 2);
	m_touchlayout = readIntOpt("touch_layout", 0, 0, 1);
	m_bikesteering = readBoolOpt("bike_steering", false);
	m_accelerometer = readIntOpt("accelerometer", 0, 0, 2);
	m_vibration = readBoolOpt("vibration", true);
	m_joypadinvertx = readBoolOpt("joypad_invert_x", false);
	m_joypadinverty = readBoolOpt("joypad_invert_y", false);
	m_kmenabled = readBoolOpt("km_enabled", true);
	m_openhudeditonstart = readBoolOpt("open_hud_edit_on_start", false);

	m_sfxvolume = readIntOpt("sfx_volume", 80, 0, 100);
	m_musicvolume = readIntOpt("music_volume", 80, 0, 100);
	m_radioautotune = readBoolOpt("radio_autotune", true);
	m_radiostation = readIntOpt("radio_station", 0, 0, 12);
	m_voicevolume = readIntOpt("voice_volume", 100, 0, 100);
	m_voicemicenable = readBoolOpt("voice_mic_enable", true);
	m_voicemicvolume = readIntOpt("voice_mic_volume", 75, 0, 100);
	m_voicemicmode = readEnumOpt("voice_mic_mode", settings_contract::parseVoiceMicMode, settings_contract::parseVoiceMicModeLegacyInt, 0);
	m_voicebalancer = readBoolOpt("voice_balancer", false);
	m_voicefilter = readBoolOpt("voice_filter", true);
	m_voicespeakerlist = readBoolOpt("voice_speaker_list", true);
	m_voicebtnsize = readIntOpt("voice_btn_size", 100, 50, 150);

	m_showgtahud = readBoolOpt("show_gta_hud", true);
	m_showradar = readBoolOpt("show_radar", true);
	m_radartapmap = readBoolOpt("radar_tap_map", true);
	m_crosshairstyle = readEnumOpt("crosshair_style", settings_contract::parseCrosshairStyle, settings_contract::parseCrosshairStyleLegacyInt, 0);
	m_showdeathwindow = readBoolOpt("show_death_window", true);
	m_shownametags = readBoolOpt("show_nametags", true);
	m_shownametagweapon = readBoolOpt("show_nametag_weapon", false);
	m_nametagsize = readIntOpt("nametag_size", 100, 50, 150);
	m_textlabelsize = readIntOpt("textlabel_size", 100, 50, 150);
	m_fontfamily = readEnumOpt("font_family", settings_contract::parseFontFamily, settings_contract::parseFontFamilyLegacyInt, 0);
	m_showbuttonpanel = readBoolOpt("show_button_panel", true);
	m_showscreenshotbtn = readBoolOpt("show_screenshot_btn", true);
	m_gtasubtitles = readBoolOpt("gta_subtitles", true);
	m_chatshadow = readBoolOpt("chat_shadow", true);
	m_screenshotmode = readEnumOpt("screenshot_mode", settings_contract::parseScreenshotMode, settings_contract::parseScreenshotModeLegacyInt, 0);

	ClientSubsystems::onSettingsLoaded();

	spdlog::info("host {} port {}", m_host, m_port);
	spdlog::info("nick_name {} samp_version {} (0x{:X}) charset {}", m_nick, m_sampVersionName, m_sampNetgameVersion, m_charset);
	spdlog::info("mods: modloader={} aml={} jpatch={} cleo={} monet={}",
			m_modloader, m_aml, m_jpatch, m_cleo, m_monet);
	spdlog::info("gta: vfx={} res={} dd={} shd={} refl={} traf={} bri={} ar={} fps={} freeCam={}",
			m_visualfx, m_resolution, m_drawdistance, m_shadows, m_carreflections, m_trafficmode,
			m_brightness, m_aspectratio, m_fpslimit, m_freecam);

	m_initialized = true;
}

// Dual-ABI layout of MobileSettings::settings[37] in libGTASA.so v2.10:
// arm32 stride = 32B (value at +8, defaultValue at +12)
// arm64 stride = 40B (value at +16, defaultValue at +20)
struct GtaMobileSettingEntry {
	const char* gxtLabel;
	const char** gxtOptions;
	int32_t value;
	int32_t defaultValue;
	int32_t minValue;
	int32_t maxValue;
	uint8_t bSaveToFile;
	uint8_t pad[3];
	int32_t isSlider;
};
static_assert(sizeof(GtaMobileSettingEntry) == (VER_x32 ? 32 : 40), "GtaMobileSettingEntry ABI size mismatch");
static_assert(offsetof(GtaMobileSettingEntry, value) == (VER_x32 ? 8 : 16), "GtaMobileSettingEntry::value offset mismatch");
static_assert(offsetof(GtaMobileSettingEntry, defaultValue) == (VER_x32 ? 12 : 20), "GtaMobileSettingEntry::defaultValue offset mismatch");

void Settings::applyFpsLimitPatch()
{
	if (!g_saSym) return;
	uint8_t targetFps = (m_fpslimit <= 0) ? 250 : (uint8_t) std::max(15, std::min(250, m_fpslimit));
#if VER_x32
	patches::updateBytes("FpsLimit.patch1", "FPS cap immediate 1 (arm32)", g_saSym->Abs<uintptr_t>(addr::FpsLimit_patch1_arm32), &targetFps, 1);
	patches::updateBytes("FpsLimit.patch2", "FPS cap immediate 2 (arm32)", g_saSym->Abs<uintptr_t>(addr::FpsLimit_patch2_arm32), &targetFps, 1);
#else
	uint32_t movW9 = 0x52800009u | ((uint32_t) targetFps << 5);
	uint32_t movW8 = 0x52800008u | ((uint32_t) targetFps << 5);
	patches::updateBytes("FpsLimit.patch1", "FPS cap mov w9 (arm64)", g_saSym->Abs<uintptr_t>(addr::FpsLimit_patch1_arm64), &movW9, 4);
	patches::updateBytes("FpsLimit.patch2", "FPS cap mov w8 (arm64)", g_saSym->Abs<uintptr_t>(addr::FpsLimit_patch2_arm64), &movW8, 4);
	patches::updateBytes("FpsLimit.patch3", "FPS cap mov w9 #2 (arm64)", g_saSym->Abs<uintptr_t>(addr::FpsLimit_patch3_arm64), &movW9, 4);
#endif
	if (sa::RsGlobal) {
		sa::RsGlobal->maxFPS = targetFps;
	}
}

static GtaMobileSettingEntry* getLiveMobileSettings()
{
	return static_cast<GtaMobileSettingEntry*>(eng::live::mobileSettings());
}

void Settings::applyDynamicCameraAndFreeCam()
{
	if (!g_saSym) return;

	// 1. Free Camera (patch PEDCAM_SET[0..2].MaxBetaSpeed and CARCAM_SET[0..5].MaxBetaSpeed)
	if (auto* pedCamSet = eng::as<float*>(eng::Sym::PEDCAM_SET)) {
		for (int i = 0; i < 3; ++i) {
			pedCamSet[i * 15 + 9] = m_freecam ? 0.0f : 0.08f;
		}
	}
	if (auto* carCamSet = eng::as<float*>(eng::Sym::CARCAM_SET)) {
		for (int i = 0; i < 6; ++i) {
			carCamSet[i * 15 + 9] = m_freecam ? 0.0f : 0.06f;
			carCamSet[i * 15 + 12] = m_freecam ? 0.0f : 1.0f;
		}
	}
	if (m_freecam && GamePool_FindPlayerPed()) {
		sa::CCamera& theCam = sa::getTheCamera();
		for (int i = 0; i < 3; ++i) {
			theCam.m_aCams[i].BetaSpeed = 0.0f;
			theCam.m_aCams[i].m_bFixingBeta = false;
		}
	}

	// 2. State-specific Camera Sensitivity (On-Foot / In-Vehicle / Aiming)
	auto* entries = getLiveMobileSettings();
	if (!entries) return;

	int mult = m_camsensfoot;
	if (pGame && GamePool_FindPlayerPed() && pGame->FindPlayerPed()) {
		CPlayerPed* pPed = pGame->FindPlayerPed();
		if (pPed->m_ped) {
			uint8_t camMode = pPed->GetCameraMode();
			if (camMode == 53 || camMode == 7 || camMode == 8 || camMode == 51 || camMode == 55) {
				mult = m_camsensaim;
			}
			else if (pPed->IsInVehicle()) {
				mult = m_camsensveh;
			}
		}
	}

	int effX = std::max(1, std::min(100, (m_camsensx * mult) / 100));
	int effY = std::max(1, std::min(100, (m_camsensy * mult) / 100));

	entries[25].value = effX; // MOB_JYX
	entries[26].value = effY; // MOB_JYY
	entries[31].value = effX; // MOB_MSX
	entries[32].value = effY; // MOB_MSY
}

void Settings::applyToGtaEngine(bool bInGameReady)
{
	if (!g_saSym) return;

	applyFpsLimitPatch();

	auto* entries = getLiveMobileSettings();
	if (!entries) return;

	auto setIdx = [&](int idx, int32_t val) {
		entries[idx].value = val;
		entries[idx].defaultValue = val;
	};

	// Prevent SetRendererDefaults() from clobbering user choices when gta_sa.set is absent
	if (auto* pLoaded = eng::as<uint8_t*>(eng::Sym::MobileSettings_loaded)) {
		*pLoaded = 1;
	}

	setIdx(0, m_visualfx);                       // MOB_VS: Visual Effects (0..3)
	setIdx(1, m_resolution);                     // MOB_RES: Resolution % (0..100)
	setIdx(2, m_drawdistance);                   // MOB_DD: Draw Distance % (0..100)
	setIdx(3, m_touchsteering);                  // FEC_STE: Touch Steering (0=Analog,1=Buttons,2=Flick)
	setIdx(4, m_trafficmode);                    // MOB_TRA: Traffic Mode (0..2)
	setIdx(5, m_shadows);                        // MOB_SH: Shadows (0..2)
	setIdx(6, m_targetingmode);                  // MOB_TAR: Targeting Mode (0=LockOn,1=FreeAim)
	setIdx(7, m_carreflections);                 // CAR_REF: Car Reflections (0..3)
	setIdx(8, m_accelerometer);                  // MOB_ACC: Accelerometer (0..2)
	setIdx(9, m_touchlayout);                    // FEC_TOU: Touch Layout (0=Classic,1=Adapted)
	setIdx(10, m_vehcamheight);                  // MOB_CAM: Vehicle Camera Height (0=Low,1=High)
	setIdx(11, m_sfxvolume);                     // FEA_SFX: SFX Volume (0..100)
	setIdx(12, m_musicvolume);                   // FEA_MUS: Radio Volume (0..100)
	setIdx(13, m_radioautotune ? 1 : 0);         // FEA_ART: Radio Auto-Tune (0..1)
	setIdx(14, m_radiostation);                  // FEA_RSS: Radio Station (0..12)
	setIdx(15, m_invertlook ? 1 : 0);            // FEC_ILU: Invert Look (0..1)
	setIdx(16, m_brightness);                    // FED_BRI: Brightness (0..100)
	setIdx(18, m_bikesteering ? 1 : 0);          // MOB_BIK: Analog Bike Steering (0..1)
	setIdx(19, m_firstpersonlook ? 1 : 0);       // MOB_FP: First-Person Look (0..1)
	setIdx(21, m_flicksensitivity);              // MOB_FLK: Flick Steering Sensitivity (1..100)
	setIdx(22, m_shootthreshold);                // MOB_BTH: Shoot Button Threshold (1..100)
	int analogSteer = 20 + ((m_analogsensitivity - 1) * 25) / 99;
	setIdx(23, std::max(20, std::min(45, analogSteer))); // MOB_ANA: Analog Steering Sensitivity (20..45)
	setIdx(24, m_gtasubtitles ? 1 : 0);          // MOB_SUB: Subtitles (0..1)
	setIdx(25, m_camsensx);                      // MOB_JYX: Joypad X Sensitivity (0..100)
	setIdx(26, m_camsensy);                      // MOB_JYY: Joypad Y Sensitivity (0..100)
	setIdx(27, m_joypadinvertx ? 1 : 0);         // MOB_JIA: Joypad Invert X (0..1)
	setIdx(28, m_joypadinverty ? 1 : 0);         // MOB_JIB: Joypad Invert Y (0..1)
	setIdx(29, m_vibration ? 1 : 0);             // MOB_VIB: Vibration Feedback (0..1)
	setIdx(30, m_framelimiter ? 1 : 0);          // FEM_FRM: Frame Limiter (0..1)
	setIdx(31, m_camsensx);                      // MOB_MSX: Touch/Mouse X Sensitivity (1..100)
	setIdx(32, m_camsensy);                      // MOB_MSY: Touch/Mouse Y Sensitivity (1..100)
	setIdx(35, m_autoclimb ? 1 : 0);             // MOB_ATC: Auto Climb (0..1)

	applyDynamicCameraAndFreeCam();

	// Apply draw distance to CRenderer::ms_lodDistScale (matches Menu_SwitchOffToGame formula)
	if (auto* pLodDist = eng::as<float*>(eng::Sym::CRenderer_ms_lodDistScale)) {
		*pLodDist = 0.875f + ((float) m_drawdistance * 0.875f * 0.01f);
	}

	ClientSubsystems::onSettingsApplied();

	if (bInGameReady) {
		uintptr_t* widgets = GetTouchWidgets();
		if (widgets && widgets[sa::WidgetIDs::WIDGET_PED_MOVE] && widgets[sa::WidgetIDs::WIDGET_ATTACK]) {
			EnsureTouchWidgetsCreated();
			eng::fn::CTouchInterface_SetupLayoutObjects();
			eng::fn::CTouchInterface_SetupSteeringMode();
			EnsureTouchWidgetsCreated();
		}
		eng::fn::Menu_ApplyAudioSettings();
		sa::getTheCamera().m_nPedZoom = std::max(1, std::min(3, m_pedcamzoom));
	}
}

void Settings::setTimestamp(bool v)
{
	m_timestamp = v;

	const char* gameDir = Client::gameDir();
	if (!gameDir || !gameDir[0]) return;
	std::string base(gameDir);
	if (base.back() != '/') base.push_back('/');
	std::string path = base + "SAMP/settings.json";
	std::string tmpPath = path + ".tmp";

	FILE* fp = fopen(path.c_str(), "rb");
	if (!fp) return;
	std::vector<char> ioBuf(16384);
	rapidjson::FileReadStream is(fp, ioBuf.data(), ioBuf.size());
	rapidjson::Document doc;
	doc.ParseStream(is);
	fclose(fp);
	if (doc.HasParseError() || !doc.IsObject()) return;

	if (doc.HasMember("client") && doc["client"].IsObject()
			&& doc["client"].HasMember("settings") && doc["client"]["settings"].IsObject()) {
		auto& s = doc["client"]["settings"];
		if (s.HasMember("timestamp")) {
			s["timestamp"].SetBool(v);
		}
		else {
			s.AddMember("timestamp", v, doc.GetAllocator());
		}

		FILE* out = fopen(tmpPath.c_str(), "wb");
		if (out) {
			rapidjson::FileWriteStream os(out, ioBuf.data(), ioBuf.size());
			rapidjson::PrettyWriter<rapidjson::FileWriteStream> writer(os);
			doc.Accept(writer);
			const bool ok = (fflush(out) == 0);
			fclose(out);
			if (ok) {
				rename(tmpPath.c_str(), path.c_str());
			}
			else {
				remove(tmpPath.c_str());
			}
		}
	}
}
