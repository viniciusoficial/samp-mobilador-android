#include "ClientCmds.h"
#include "Client.h"
#include "Settings.h"
#include "L10n.h"
#include "UI/UI.h"
#include "UI/UISettings.h"
#include "UI/SAMPWidgets/Chat.h"
#include "UI/SAMPWidgets/DeathWindow.h"
#include "UI/SAMPWidgets/DebugLabels.h"
#include "UI/SAMPWidgets/NetStats.h"
#include "Java/Java.h"
#include "Game/Game.h"
#include "Game/Hooks.h"
#include "Game/PlayerPed.h"
#include "Game/Scripting.h"
#include "Game/Vehicle.h"
#include "Game/Util.h"
#include "Game/Engine/PatchRegistry.h"
#include "Net/NetGame.h"
#include "Net/NetCap.h"
#include "Net/LocalPlayer.h"
#include "Net/VehiclePool.h"
#include "Net/RpcTable.h"
#include "Net/Proto/Sync.h"
#include "Game/AudioStream.h"
#include "Game/CleoEngine.h"

#include <RakNet/RakClientInterface.h>
#include <RakNet/PacketEnumerations.h>
#include <RakNet/SAMP/SAMPRPC.h>
#include <SA.h>
#include <encoding.h>
#include <spdlog/spdlog.h>
#include <fstream>
#include <thread>
#include <cstdlib>


void cmdQuit(const std::string& params)
{
	(void) params;
	if (pUI) {
		pUI->chat()->addInfoMessage(L10N("-> Exiting game...", "-> 退出游戏..."));
	}
	Hooks::requestGracefulExit("/q");
}

void cmdRcon(const std::string& params)
{
	if (params.empty()) {
		return;
	}

	RakNet::BitStream bsCommand;
	proto::Writer writer(bsCommand);
	proto::RconCommandPacket pkt{};
	pkt.command = params;
	pkt.serialize(writer, proto::ProtoVersion::V037);
	pNetGame->GetRakClient()->Send(&bsCommand, HIGH_PRIORITY, RELIABLE, 0);
}

void cmdSave(const std::string& params)
{
	CPlayerPed* pPlayer = pGame->FindPlayerPed();
	FILE* fileOut;
	uint32_t dwVehicleID;
	float fZAngle;
	char szPath[0xFF];

	sprintf(szPath, "%sSAMP/savedpositions.txt", Client::gameDir());
	fileOut = fopen(szPath, "a");

	if (!fileOut) {
		if (pUI) {
			pUI->chat()->addInfoMessage(L10N("Save failed", "保存失败"));
		}

		return;
	}

	// incar savepos

	if (pPlayer->IsInVehicle()) {

		sa::CVehicle* pVehicle = pPlayer->GetGtaVehicle();

		dwVehicleID = GamePool_Vehicle_GetIndex(pVehicle);
		ScriptCommand(&get_car_z_angle, dwVehicleID, &fZAngle);

		fprintf(fileOut, "AddStaticVehicle(%u, %.4f, %.4f, %.4f, %.4f, %u, %u); // %s\n",
				pVehicle->m_nModelIndex,
				pVehicle->GetPosition().x,
				pVehicle->GetPosition().y,
				pVehicle->GetPosition().z,
				fZAngle, pVehicle->m_nPrimaryColor,
				pVehicle->m_nSecondaryColor,
				params.c_str());

		fclose(fileOut);

		if (pUI) {
			pUI->chat()->addInfoMessage(L10N("-> Successfully saved vehicle position", "-> 成功保存车辆位置"));
		}

		return;
	}

	// onfoot savepos

	sa::CPed* pActor = pPlayer->m_ped;
	ScriptCommand(&get_actor_z_angle, pPlayer->m_gtaId, &fZAngle);

	fprintf(fileOut, "AddPlayerClass(%u, %.4f, %.4f, %.4f, %.4f, 0, 0, 0, 0, 0, 0); // %s\n",
			pPlayer->GetModelIndex(),
			pActor->GetPosition().x,
			pActor->GetPosition().y,
			pActor->GetPosition().z,
			fZAngle,
			params.c_str());

	if (pUI) {
		pUI->chat()->addInfoMessage(L10N("-> Successfully saved onfoot position", "-> 成功保存步行位置"));
	}

	fclose(fileOut);
}

void cmdDL(const std::string& params)
{
	if (pUI && pUI->debuglabels()) {
		const bool next = !pUI->debuglabels()->vehicleLabelsVisible();
		pUI->debuglabels()->setVehicleLabelsVisible(next);
		pUI->chat()->addInfoMessage("-> DebugLabels %s", l10n::onOff(next));
	}
}

void cmdSaveRaw(const std::string& params)
{
	if (!pGame || !pGame->FindPlayerPed() || !Client::gameDir()) return;
	CPlayerPed* pPlayer = pGame->FindPlayerPed();
	float fZAngle = 0.0f;
	char szPath[512];

	if (pPlayer->IsInVehicle()) {
		sa::CVehicle* pVehicle = pPlayer->GetGtaVehicle();
		if (!pVehicle) return;
		snprintf(szPath, sizeof(szPath), "%sSAMP/rawvehicles.txt", Client::gameDir());
		FILE* fileOut = fopen(szPath, "a");
		if (!fileOut) {
			if (pUI) pUI->chat()->addDebugMessage("I can't open the rawvehicles.txt file for append.");
			return;
		}
		uint32_t dwVehicleID = GamePool_Vehicle_GetIndex(pVehicle);
		ScriptCommand(&get_car_z_angle, dwVehicleID, &fZAngle);
		fprintf(fileOut, "%u,%.4f,%.4f,%.4f,%.4f,%u,%u ; %s\n",
				pVehicle->m_nModelIndex,
				pVehicle->GetPosition().x,
				pVehicle->GetPosition().y,
				pVehicle->GetPosition().z,
				fZAngle,
				pVehicle->m_nPrimaryColor,
				pVehicle->m_nSecondaryColor,
				params.c_str());
		fclose(fileOut);
		if (pUI) pUI->chat()->addDebugMessage(L10N("-> InCar pos saved", "-> InCar pos 已保存"));
		return;
	}

	if (!pPlayer->m_ped) return;
	snprintf(szPath, sizeof(szPath), "%sSAMP/rawpositions.txt", Client::gameDir());
	FILE* fileOut = fopen(szPath, "a");
	if (!fileOut) {
		if (pUI) pUI->chat()->addDebugMessage("I can't open the rawpositions.txt file for append.");
		return;
	}
	ScriptCommand(&get_actor_z_angle, pPlayer->m_gtaId, &fZAngle);
	fprintf(fileOut, "%.4f,%.4f,%.4f,%.4f ; %s\n",
			pPlayer->m_ped->GetPosition().x,
			pPlayer->m_ped->GetPosition().y,
			pPlayer->m_ped->GetPosition().z,
			fZAngle,
			params.c_str());
	fclose(fileOut);
	if (pUI) pUI->chat()->addDebugMessage(L10N("-> OnFoot pos saved", "-> OnFoot 位置已保存"));
}

void cmdFontSize(const std::string& params)
{
	if (!pUI) return;
	if (params.empty()) {
		pUI->chat()->addDebugMessage(L10N("Valid fontsize: -3 to 5 (Current: %d)", "有效字体大小：-3 到 5（当前：%d）"), Settings::fontSizeOffset());
		return;
	}
	char* endPtr = nullptr;
	long size = std::strtol(params.c_str(), &endPtr, 10);
	if (endPtr == params.c_str() || size < -3 || size > 5) {
		pUI->chat()->addDebugMessage(L10N("Valid fontsize: -3 to 5", "有效字体大小：-3 到 5"));
		return;
	}
	UISettings::setFontSizeOffset(static_cast<int>(size));
	pUI->chat()->setItemSize(UISettings::chatItemSize());
	pUI->chat()->setFixedSize(UISettings::chatSize());
	pUI->chat()->performLayout();
	pUI->chat()->addInfoMessage(L10N("-> Chat FontSize set to %ld", "-> 聊天字体大小设置为 %ld"), size);
}

void cmdFpsLimit(const std::string& params)
{
	if (!pUI) return;
	if (params.empty()) {
		pUI->chat()->addInfoMessage(L10N("-> Frame Limiter: %d (valid amounts are 20-120, 0 = unlimited)", "-> 帧限制器：%d（有效数量为 20-120，0 = 无限制）"), Settings::fpslimit());
		return;
	}
	char* endPtr = nullptr;
	long value = std::strtol(params.c_str(), &endPtr, 10);
	if (endPtr == params.c_str() || (value != 0 && (value < 20 || value > 120))) {
		pUI->chat()->addInfoMessage(L10N("-> Frame Limiter: valid amounts are 20-120 (0 = unlimited)", "-> 帧限制器：有效数量为 20-120（0 = 无限制）"));
		return;
	}
	Settings::setFpsLimit(static_cast<int>(value));
	Settings::applyFpsLimitPatch();
	if (value == 0) {
		pUI->chat()->addInfoMessage(L10N("-> Frame Limiter: Unlimited", "-> 帧限制器：无限制"));
	}
	else {
		pUI->chat()->addInfoMessage(L10N("-> Frame Limiter: %ld", "-> 帧限制器：%ld"), value);
	}
}

void cmdInterior(const std::string& params)
{
	if (!pUI) return;
	uint32_t interior = 0;
	ScriptCommand(&get_active_interior, &interior);
	pUI->chat()->addDebugMessage(L10N("Current Interior: %u", "当前内饰：%u"), interior);
}

void cmdNameTagStatus(const std::string& params)
{
	const bool next = !Settings::showNameTagStatus();
	Settings::setShowNameTagStatus(next);
	if (pUI) {
		pUI->chat()->addInfoMessage("-> NameTag Status: %s", l10n::onOff(next));
	}
}

void cmdAudioMsg(const std::string& params)
{
	const bool next = !Settings::audioMsg();
	Settings::setAudioMsg(next);
	if (pUI) {
		pUI->chat()->addInfoMessage(L10N("Audio messages: %s", "音频消息：%s"), l10n::onOffTitle(next));
	}
}

void cmdLogUrls(const std::string& params)
{
	const bool next = !Settings::logUrls();
	Settings::setLogUrls(next);
	if (pUI) {
		pUI->chat()->addInfoMessage(L10N("URL messages: %s", "URL消息：%s"), l10n::onOffTitle(next));
	}
}

void cmdMem(const std::string& params)
{
	if (!pUI) return;
	const auto* pUsed = eng::as<uint32_t*>(eng::Sym::CStreaming_ms_memoryUsed);
	const auto* pAvail = eng::as<uint32_t*>(eng::Sym::CStreaming_ms_memoryAvailable);
	if (pUsed && pAvail) {
		pUI->chat()->addDebugMessage("Memory: %u (%.2f MB / %.2f MB)",
				*pUsed,
				static_cast<double>(*pUsed) / (1024.0 * 1024.0),
				static_cast<double>(*pAvail) / (1024.0 * 1024.0));
	}
	else {
		pUI->chat()->addDebugMessage(L10N("Memory: unavailable", "内存：不可用"));
	}
}

void cmdCameraTargetDebug(const std::string& params)
{
	if (pUI && pUI->debuglabels()) {
		const bool next = !pUI->debuglabels()->cameraTargetsVisible();
		pUI->debuglabels()->setCameraTargetsVisible(next);
		pUI->chat()->addInfoMessage("-> CameraTarget Debug: %s", l10n::onOff(next));
	}
}

void cmdTestDeathWindow(const std::string& params)
{
	if (!pUI || !pUI->deathwindow()) return;
	pUI->deathwindow()->record("Pooper333", 0xFFFFFFFF, "Pooper", 0xFFFFFFFF, 1);
	pUI->deathwindow()->record("Pooper", 0xFFFFFFFF, "Pooper", 0xFFFFFFFF, 5);
	pUI->deathwindow()->record("Pooper", 0xFFFFFFFF, "Pooper", 0xFFFFFFFF, 15);
	pUI->deathwindow()->record("Pooper", 0xFFFFFFFF, "Pooper", 0xFFFFFFFF, 24);
	pUI->deathwindow()->record("Pooper", 0xFFFFFFFF, nullptr, 0xFFFFFFFF, 255);
}

void cmdNetStats(const std::string& params)
{
	if (pUI && pUI->netstats()) {
		const bool next = !pUI->netstats()->clientStatsVisible();
		pUI->netstats()->setClientStatsVisible(next);
		pUI->chat()->addInfoMessage("-> Client NetStats: %s", l10n::onOff(next));
	}
}

void cmdSvrNetStats(const std::string& params)
{
	if (pUI && pUI->netstats()) {
		const bool next = !pUI->netstats()->serverStatsVisible();
		pUI->netstats()->setServerStatsVisible(next);
		pUI->chat()->addInfoMessage("-> Server NetStats: %s", l10n::onOff(next));
	}
}

void cmdDW(const std::string& params)
{
	if (pUI) {
		pUI->chat()->addInfoMessage("-> DeathWindow: %s", l10n::onOff(!pUI->deathwindow()->visible()));
		pUI->deathwindow()->setVisible(!pUI->deathwindow()->visible());
	}
}

void cmdClearMyChat(const std::string& params)
{
	if (!pUI) {
		return;
	}

	for (int i = 0; i < 25; i++) {
		pUI->chat()->addInfoMessage(" ");
	}
}

void cmdTimestamp(const std::string& params)
{
	if (pUI) {
		const bool next = !Chat::timestamp();
		Chat::setTimestamp(next);
		Settings::setTimestamp(next);
		pUI->chat()->performLayout();
		pUI->chat()->addInfoMessage("-> Timestamp %s", l10n::onOff(next));
	}
}

void cmdHeadMove(const std::string& params)
{
	Settings::setHeadMove(!Settings::headMove());
	if (pUI) {
		pUI->chat()->addInfoMessage("-> HeadMove: %s", l10n::onOff(Settings::headMove()));
	}
}

void cmdFreeCam(const std::string& params)
{
	Settings::setFreeCam(!Settings::freeCam());
	Settings::applyDynamicCameraAndFreeCam();
	if (pUI) {
		pUI->chat()->addInfoMessage(L10N("-> FreeCamera: %s", "-> 自由镜头：%s"), l10n::onOff(Settings::freeCam()));
	}
}

void cmdHudEdit(const std::string& params)
{
	Hooks::openAdjustableHud();
}

void cmdPageSize(const std::string& params)
{
	if (!pUI) return;
	if (params.empty()) {
		pUI->chat()->addInfoMessage(L10N("-> Usage: /pagesize [5-20] (Current: %d)", "-> 用法：/pagesize [5-20]（当前：%d）"), Settings::chatstrings());
		return;
	}
	int lines = std::atoi(params.c_str());
	if (lines < 5 || lines > 20) {
		pUI->chat()->addInfoMessage(L10N("-> PageSize must be between 5 and 20 lines", "-> PageSize 必须在 5 到 20 行之间"));
		return;
	}
	UISettings::setChatStrings(lines);
	pUI->chat()->setFixedSize(UISettings::chatSize());
	pUI->chat()->performLayout();
	pUI->chat()->addInfoMessage(L10N("-> Chat PageSize set to %d lines", "-> 聊天页面大小设置为 %d 行"), lines);
}

void cmdScreenshot(const std::string& params)
{
	int mode = -1;
	if (params == "game" || params == "clean" || params == "1") {
		mode = 1;
	}
	else if (params == "hud" || params == "all" || params == "0") {
		mode = 0;
	}
	Hooks::requestScreenshot(mode);
}

void cmdCmds(const std::string& params)
{
	if (pUI) {
		pUI->chat()->addInfoMessage(" ");
		pUI->chat()->addInfoMessage(L10N("Client Commands:", "客户端命令："));
		pUI->chat()->addInfoMessage(L10N("/quit (/q) -> Exit Game", "/quit (/q) -> 退出游戏"));
		pUI->chat()->addInfoMessage(L10N("/ss (/screenshot) [hud|game] -> Take Screenshot (or F8 key)", "/ss (/screenshot) [hud|game] -> 截屏（或 F8 键）"));
		pUI->chat()->addInfoMessage(L10N("/hudedit -> Open Touch Button Position Editor", "/hudedit -> 打开触摸按钮位置编辑器"));
		pUI->chat()->addInfoMessage(L10N("/freecam -> Toggle Free Camera (No Auto-Center)", "/freecam -> 切换自由相机（无自动居中）"));
		pUI->chat()->addInfoMessage(L10N("/headmove -> Toggle Character Head Rotation", "/headmove -> 切换角色头部旋转"));
		pUI->chat()->addInfoMessage(L10N("/pagesize [5-20] -> Change Chat Line Count", "/pagesize [5-20] -> 更改聊天行数"));
		pUI->chat()->addInfoMessage(L10N("/fontsize [-3..5] -> Change Chat Font Size", "/fontsize [-3..5] -> 更改聊天字体大小"));
		pUI->chat()->addInfoMessage(L10N("/fpslimit [20-120|0] -> Set Frame Rate Limit", "/fpslimit [20-120|0] -> 设置帧速率限制"));
		pUI->chat()->addInfoMessage(L10N("/timestamp -> Toggle Chat Timestamp", "/timestamp -> 切换聊天时间戳"));
		pUI->chat()->addInfoMessage(L10N("/nametagstatus -> Toggle AFK Hourglass Icon", "/nametagstatus -> 切换 AFK 沙漏图标"));
		pUI->chat()->addInfoMessage(L10N("/audiomsg -> Toggle Audio Stream Chat Messages", "/audiomsg -> 切换音频流聊天消息"));
		pUI->chat()->addInfoMessage(L10N("/logurls -> Toggle Logging Audio Stream URLs", "/logurls -> 切换记录音频流 URL"));
		pUI->chat()->addInfoMessage(L10N("/interior -> Show Current Interior ID", "/interior -> 显示当前内部 ID"));
		pUI->chat()->addInfoMessage(L10N("/mem -> Show GTA Streaming Memory Usage", "/mem -> 显示 GTA 流内存使用情况"));
		pUI->chat()->addInfoMessage(L10N("/clearmychat -> Clear Local Chat Window", "/clearmychat -> 清除本地聊天窗口"));
		pUI->chat()->addInfoMessage(L10N("/save [note] / /rs [note] -> Save Position (Script / Raw)", "/save [note] / /rs [note] -> 保存位置（脚本/原始）"));
		pUI->chat()->addInfoMessage("/dl / /ctd -> Toggle Vehicle / CameraTarget Debug Labels");
		pUI->chat()->addInfoMessage("/netstats / /svrnetstats -> Toggle Network Stats (F5 / F10)");
		pUI->chat()->addInfoMessage("/dw / /testdw -> Toggle / Test Death Window");
		pUI->chat()->addInfoMessage("/netcap on|off -> Record network packets to netcap/ (debug)");
		pUI->chat()->addInfoMessage("/rpcstats [all|reset] -> Show declarative RPC hit/fail counts");
		pUI->chat()->addInfoMessage("/session -> Dump current session state to log");
		pUI->chat()->addInfoMessage("/patches -> Dump engine patch & hook registry to log");
		pUI->chat()->addInfoMessage("/loglevel [debug|info] -> Switch client log verbosity");
		pUI->chat()->addInfoMessage("/rcon -> Send RCON Command (e.g. /rcon login [pass])");
		pUI->chat()->addInfoMessage(" ");
	}
}

void cmdJetpack(const std::string& params)
{
	if (pGame->FindPlayerPed()->IsInVehicle()) {
		pUI->chat()->addInfoMessage(L10N("-> You can't use jetpack while in vehicle", "-> 你不能在车内使用喷气背包"));
		return;
	}

	pGame->FindPlayerPed()->StartJetpack();
	pUI->chat()->addInfoMessage(L10N("-> Jetpack started", "-> 喷气背包启动"));
}

void cmdCleo(const std::string& params)
{
	if (!CleoEngine::isEnabled()) {
		if (pUI) pUI->chat()->addInfoMessage(L10N("-> CLEO is disabled (enable it in Launcher Settings / Mod Manager)", "-> CLEO 未开启（请在启动器设置/模组管理中启用 CLEO）"));
		return;
	}
	CleoEngine::toggleMenu();
}

void cmdNetCap(const std::string& params)
{
	if (!pUI) return;
	if (params == "on") {
		if (NetCap::start(Client::gameDir(), (uint32_t) Client::netgameVersion(), true)) {
			pUI->chat()->addInfoMessage("-> NetCap ON: %s", NetCap::currentFile().c_str());
			pUI->chat()->addInfoMessage("-> Restart the game to capture the join sequence as well");
		}
		else {
			pUI->chat()->addInfoMessage("-> NetCap: cannot create the capture file");
		}
	}
	else if (params == "off") {
		NetCap::stop(Client::gameDir(), true);
		pUI->chat()->addInfoMessage("-> NetCap OFF (%u records in the last file)", NetCap::recordCount());
	}
	else {
		pUI->chat()->addInfoMessage("-> Usage: /netcap on|off (now: %s, %u records)", NetCap::isOn() ? "ON" : "OFF", NetCap::recordCount());
	}
}

void cmdRpcStats(const std::string& params)
{
	if (!pUI) return;
	if (params == "reset") {
		RpcTable::resetStats();
		pUI->chat()->addInfoMessage("-> RpcStats reset");
		return;
	}
	const bool showAll = (params == "all");
	auto stats = RpcTable::snapshotStats();
	uint64_t totalHits = 0;
	uint64_t totalFails = 0;
	size_t activeCount = 0;
	for (const auto& s : stats) {
		totalHits += s.hits;
		totalFails += s.readFails;
		if (s.hits > 0 || s.readFails > 0) ++activeCount;
	}
	spdlog::info("[RPCSTATS] entries={} active={} totalHits={} totalReadFails={}",
		stats.size(), activeCount, totalHits, totalFails);
	pUI->chat()->addInfoMessage("-> RpcStats: %zu RPCs (%zu hit), hits=%llu, readFails=%llu",
		stats.size(), activeCount,
		static_cast<unsigned long long>(totalHits),
		static_cast<unsigned long long>(totalFails));

	int chatShown = 0;
	for (const auto& s : stats) {
		if (!showAll && s.hits == 0 && s.readFails == 0) continue;
		spdlog::info("[RPCSTATS] rpc {:3d} {:<24s} hits={} fail={}", s.id, s.name, s.hits, s.readFails);
		if (chatShown < 12) {
			pUI->chat()->addInfoMessage("   [%d] %s: hits=%llu fail=%llu", s.id, s.name,
				static_cast<unsigned long long>(s.hits),
				static_cast<unsigned long long>(s.readFails));
			++chatShown;
		}
	}
	if (static_cast<size_t>(chatShown) < (showAll ? stats.size() : activeCount)) {
		pUI->chat()->addInfoMessage("   ... full list written to client log");
	}
	if (auto logger = spdlog::default_logger()) {
		logger->flush();
	}
}

void cmdPatches(const std::string& params)
{
	if (!pUI) return;
	patches::dumpToLog();
	const auto s = patches::getSummary();
	pUI->chat()->addInfoMessage("-> Patches: total=%zu active=%zu failed=%zu (hooks=%zu, byte=%zu, data=%zu)",
		s.total, s.active, s.failed, s.hooks, s.bytePatches, s.dataPatches);
	pUI->chat()->addInfoMessage("   nopCalls=%llu restoreNopCalls=%llu (full table in client log)",
		static_cast<unsigned long long>(s.nopCalls),
		static_cast<unsigned long long>(s.restoreNopCalls));
}

void cmdSession(const std::string& params)
{
	if (!pUI || !pNetGame) return;
	const SessionState& s = pNetGame->session();
	s.dumpToLog();
	pUI->chat()->addInfoMessage("-> Session: %s (%s, lan=%d, lagComp=%d)",
		Encoding::cp2utf(s.hostName).c_str(),
		s.protoVersion == proto::ProtoVersion::V03DL ? "0.3.DL" : "0.3.7",
		s.lanMode ? 1 : 0, s.lagCompensationMode);
	pUI->chat()->addInfoMessage("   rates=%d/%d/%d(x%d) time=%02d:%02d weather=%d grav=%.4f cj=%d tagLOS=%d",
		s.onFootSendRate, s.inCarSendRate, s.firingSendRate, s.sendMultiplier,
		(int) s.settings.worldTime_Hour, (int) s.settings.worldTime_Minute,
		(int) s.settings.weather, s.settings.gravity,
		s.settings.useCJWalk ? 1 : 0, s.settings.nameTagLOS ? 1 : 0);
}

void cmdClassSelection(const std::string& params)
{
	if (!pNetGame || !pNetGame->GetPlayerPool() || !pNetGame->GetPlayerPool()->GetLocalPlayer()) {
		return;
	}
	CLocalPlayer* pLocal = pNetGame->GetPlayerPool()->GetLocalPlayer();
	if (!pLocal->WantsAnotherClass()) {
		pLocal->ReturnToClassSelection();
		if (pUI && pUI->chat()) {
			pUI->chat()->addInfoMessage(L10N("Returning to class selection after next death", "下次死亡后返回职业选择"));
		}
	}
}

void cmdLogLevel(const std::string& params)
{
	if (!pUI) return;
	if (params == "debug") {
		const bool saved = Client::setLogLevelDebug(true, true);
		pUI->chat()->addInfoMessage(saved
			? "-> LogLevel: DEBUG (saved across restarts)"
			: "-> LogLevel: DEBUG (runtime only; failed to write flag)");
	}
	else if (params == "info") {
		Client::setLogLevelDebug(false, true);
		pUI->chat()->addInfoMessage("-> LogLevel: INFO (saved across restarts)");
	}
	else {
		pUI->chat()->addInfoMessage("-> Usage: /loglevel debug|info (now: %s)",
			Client::isLogLevelDebug() ? "DEBUG" : "INFO");
	}
}

void ClientCmds::initialize()
{
	spdlog::info("Initializing client commands..");

	pUI->chat()->addClientCommand("cleo", cmdCleo);
	pUI->chat()->addClientCommand("patches", cmdPatches);
	pUI->chat()->addClientCommand("loglevel", cmdLogLevel);

	pUI->chat()->addClientCommand("ss", cmdScreenshot);
	pUI->chat()->addClientCommand("screenshot", cmdScreenshot);
	pUI->chat()->addClientCommand("hudedit", cmdHudEdit);
	pUI->chat()->addClientCommand("freecam", cmdFreeCam);
	pUI->chat()->addClientCommand("headmove", cmdHeadMove);
	pUI->chat()->addClientCommand("pagesize", cmdPageSize);
	pUI->chat()->addClientCommand("fontsize", cmdFontSize);
	pUI->chat()->addClientCommand("fpslimit", cmdFpsLimit);
	pUI->chat()->addClientCommand("interior", cmdInterior);
	pUI->chat()->addClientCommand("mem", cmdMem);
	pUI->chat()->addClientCommand("nametagstatus", cmdNameTagStatus);
	pUI->chat()->addClientCommand("audiomsg", cmdAudioMsg);
	pUI->chat()->addClientCommand("logurls", cmdLogUrls);
	pUI->chat()->addClientCommand("timestamp", cmdTimestamp);
	pUI->chat()->addClientCommand("clearmychat", cmdClearMyChat);
	pUI->chat()->addClientCommand("save", cmdSave);
	pUI->chat()->addClientCommand("rs", cmdSaveRaw);
	pUI->chat()->addClientCommand("clientcmds", cmdCmds);

	if (Client::offlineMode()) {
		pUI->chat()->addClientCommand("j", cmdJetpack);
		pUI->chat()->addClientCommand("q", cmdQuit);
		pUI->chat()->addClientCommand("quit", cmdQuit);
	}
	else {
		pUI->chat()->addClientCommand("q", cmdQuit);
		pUI->chat()->addClientCommand("quit", cmdQuit);
		pUI->chat()->addClientCommand("rcon", cmdRcon);
		pUI->chat()->addClientCommand("dl", cmdDL);
		pUI->chat()->addClientCommand("ctd", cmdCameraTargetDebug);
		pUI->chat()->addClientCommand("dw", cmdDW);
		pUI->chat()->addClientCommand("testdw", cmdTestDeathWindow);
		pUI->chat()->addClientCommand("netstats", cmdNetStats);
		pUI->chat()->addClientCommand("svrnetstats", cmdSvrNetStats);
		pUI->chat()->addClientCommand("netcap", cmdNetCap);
		pUI->chat()->addClientCommand("rpcstats", cmdRpcStats);
		pUI->chat()->addClientCommand("session", cmdSession);
		pUI->chat()->addClientCommand("class", cmdClassSelection);
		pUI->chat()->addClientCommand("f4", cmdClassSelection);
	}
}
