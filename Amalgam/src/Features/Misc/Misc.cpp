#include "Misc.h"
#include "../Configs/Configs.h"

#include <array>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <filesystem>
#include <sstream>

#include "../Backtrack/Backtrack.h"
#include "../Ticks/Ticks.h"
#include "../Players/PlayerUtils.h"
#include "../Players/SteamProfileCache.h"
#include "../Aimbot/AutoRocketJump/AutoRocketJump.h"
#include "../AntiCheatCompatibility/AntiCheatCompatibility.h"
#include "../EnginePrediction/EnginePrediction.h"
#include "../ImGui/Render.h"
#include "../Binds/Binds.h"
#include "../NavBot/NavEngine/NavEngine.h"
#ifdef TEXTMODE
#include "NamedPipe/NamedPipe.h"
#endif

MAKE_SIGNATURE(Voice_IsRecording, "engine.dll", "80 3D ? ? ? ? ? 74 ? 80 3D ? ? ? ? ? 75", 0x0);
MAKE_SIGNATURE(ReportPlayerAccount, "client.dll", "48 89 5C 24 ? 57 48 83 EC ? 48 8B D9 8B FA 48 C1 E9", 0x0);

void CMisc::RunPre(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	EnsureChatUtilsDoc();
	NoiseSpam(pLocal);
	VoiceCommandSpam(pLocal);
	ChatSpam(pLocal);
	AutoDisguise(pLocal);
	AchievementSpam(pLocal);
	CallVoteSpam(pLocal);
	CheatsBypass();
	WeaponSway();
	AutoReport();
	AutoRetry(pLocal);

#ifdef TEXTMODE
	F::NamedPipe.Store(pLocal, true);
#endif
	AntiAFK(pLocal, pCmd);
	InstantRespawnMVM(pLocal);

	// flags + velocity before prediction (long/minijump transitions, pixel surf ride detection)
	m_iPrePredictionFlags = pLocal->m_fFlags();
	m_vPrePredictionVelocity = pLocal->m_vecVelocity();
	ExecBuyBot(pLocal, pCmd);

	if (!pLocal->IsAlive() || pLocal->IsAGhost() || pLocal->m_MoveType() != MOVETYPE_WALK || pLocal->IsSwimming()
		|| pLocal->IsTaunting() || pLocal->InCond(TF_COND_SHIELD_CHARGE))
		return;

	EdgebugDetect(pLocal);
	AutoJump(pLocal, pCmd);
	MiniJumpPre(pLocal, pCmd);
	EdgeJump(pLocal, pCmd);
	if (pLocal->InCond(TF_COND_HALLOWEEN_KART))
		return;

	JumpBug(pLocal, pCmd);
	AutoRevJump(pLocal, pCmd);
	AutoStrafe(pLocal, pCmd);
	AutoPeek(pLocal, pCmd);
	BreakJump(pLocal, pCmd);
}

void CMisc::RunPost(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	m_bDuckSpeedActive = false;
	AutoCrouchNavbot(pLocal, pCmd);

	if (!pLocal->IsAlive() || pLocal->IsAGhost() || pLocal->m_MoveType() != MOVETYPE_WALK || pLocal->IsSwimming()
		|| pLocal->InCond(TF_COND_SHIELD_CHARGE))
		return;

	if (pLocal->IsTaunting() || pLocal->InCond(TF_COND_HALLOWEEN_KART))
		TauntKartControl(pLocal, pCmd);
	else
	{
		F::EnginePrediction.End(pLocal, nullptr);
		AutoEdgebug(pLocal, pCmd);
		F::EnginePrediction.Start(pLocal, pCmd);
		if (!m_bEdgeBug)
		{
			EdgeJump(pLocal, pCmd, true);
			AutoPeek(pLocal, pCmd, true);
			FastMovement(pLocal, pCmd);
			LongJump(pLocal, pCmd);
			MiniJump(pLocal, pCmd);
			PixelSurfAssist(pLocal, pCmd);
			PixelSurf(pLocal, pCmd);
			AutoAlign(pLocal, pCmd);
			TextureBug(pLocal, pCmd);
			HeadSurf(pLocal, pCmd);
			WallClimb(pLocal, pCmd);
			AirStuck(pLocal, pCmd);
			PixelFinder(pLocal, pCmd);
			PixelSurfLine(pLocal, pCmd);
			BreakShootSound(pLocal, pCmd);
			MovementLock(pLocal, pCmd);
		}
	}

	Recorder(pLocal, pCmd);
}

void CMisc::RecorderToggleRecord(std::string sName)
{
	std::scoped_lock tLock(m_mRecorderMutex);

	if (m_bRecording)
	{
		m_bRecording = false;
		SaveRecording();
		return;
	}

	// strip characters that don't belong in a file name
	sName.erase(std::remove_if(sName.begin(), sName.end(), [](char c) { return std::string("\\/:*?\"<>|").find(c) != std::string::npos; }), sName.end());
	if (sName.empty())
	{
		int iCount = 0;
		for (const auto& sFile : GetRecordingNames())
		{
			if (sFile.rfind("Recording ", 0) == 0)
				iCount++;
		}
		sName = "Recording " + std::to_string(iCount + 1);
	}

	m_bPlaying = false;
	{
		std::scoped_lock tBindsLock(F::Binds.m_mMutex);
		F::Binds.m_vPlaybackActive.clear();
	}
	m_vRecording.clear();
	uRecorderTick = 0;
	m_sRecordingName = sName;
	m_bRecording = true;
}

void CMisc::RecorderTogglePlay()
{
	std::scoped_lock tLock(m_mRecorderMutex);

	if (m_bRecording)
	{
		m_bRecording = false;
		SaveRecording();
	}

	if (m_vRecording.empty() && !m_sRecordingName.empty())
		SelectRecording(m_sRecordingName);

	m_bPlaying = !m_bPlaying;
	uRecorderTick = 0;
	m_bRecorderAtPos = false;
	m_iRecorderSetupTimer = 128;
	if (!m_bPlaying)
	{
		std::scoped_lock tBindsLock(F::Binds.m_mMutex);
		F::Binds.m_vPlaybackActive.clear();
	}
}

void CMisc::RecorderReset()
{
	std::scoped_lock tLock(m_mRecorderMutex);

	m_bRecording = m_bPlaying = false;
	m_vRecording.clear();
	uRecorderTick = 0;
	m_bRecorderAtPos = false;
	{
		std::scoped_lock tBindsLock(F::Binds.m_mMutex);
		F::Binds.m_vPlaybackActive.clear();
	}
}

std::string CMisc::GetRecordingsFolder()
{
	const std::string sFolder = F::Configs.m_sCorePath + "recordings";
	std::error_code ec;
	std::filesystem::create_directories(sFolder, ec);

	// migrate the old single-slot recording into the library
	const std::string sOld = F::Configs.m_sCorePath + "Recording.json";
	if (std::filesystem::exists(sOld, ec) && !std::filesystem::exists(sFolder + "\\Recording.json", ec))
		std::filesystem::copy_file(sOld, sFolder + "\\Recording.json", ec);

	return sFolder;
}

std::vector<std::string> CMisc::GetRecordingNames(const std::string& sFilter)
{
	std::vector<std::string> vNames = {};

	std::string sLowerFilter = sFilter;
	std::transform(sLowerFilter.begin(), sLowerFilter.end(), sLowerFilter.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

	try
	{
		for (const auto& tFile : std::filesystem::directory_iterator(GetRecordingsFolder()))
		{
			if (!tFile.is_regular_file() || tFile.path().extension() != ".json")
				continue;

			std::string sName = tFile.path().stem().string();
			if (!sLowerFilter.empty())
			{
				std::string sLowerName = sName;
				std::transform(sLowerName.begin(), sLowerName.end(), sLowerName.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if (sLowerName.find(sLowerFilter) == std::string::npos)
					continue;
			}
			vNames.push_back(sName);
		}
		std::sort(vNames.begin(), vNames.end());
	}
	catch (...)
	{
	}

	return vNames;
}

bool CMisc::SelectRecording(const std::string& sName)
{
	if (sName.empty())
		return false;

	std::scoped_lock tLock(m_mRecorderMutex);

	RecorderReset();

	try
	{
		boost::property_tree::ptree tRead;
		boost::property_tree::read_json(GetRecordingsFolder() + "\\" + sName + ".json", tRead);
		if (auto tTicks = tRead.get_child_optional("Ticks"))
		{
			for (auto& [sKey, tEntry] : *tTicks)
			{
				RecorderTick_t tTick = {};
				tTick.m_vAngles = { tEntry.get<float>("Pitch", 0.f), tEntry.get<float>("Yaw", 0.f), tEntry.get<float>("Roll", 0.f) };
				tTick.m_flForwardMove = tEntry.get<float>("ForwardMove", 0.f);
				tTick.m_flSideMove = tEntry.get<float>("SideMove", 0.f);
				tTick.m_iButtons = tEntry.get<int>("Buttons", 0);
				tTick.m_vOrigin = { tEntry.get<float>("OriginX", 0.f), tEntry.get<float>("OriginY", 0.f), tEntry.get<float>("OriginZ", 0.f) };

				std::istringstream ssBinds(tEntry.get<std::string>("Binds", ""));
				std::string sBind;
				while (std::getline(ssBinds, sBind, ','))
					tTick.m_vBinds.push_back(sBind == "1");

				m_vRecording.push_back(tTick);
			}
		}
	}
	catch (...)
	{
		m_vRecording.clear();
		return false;
	}

	m_sRecordingName = sName;
	return true;
}

void CMisc::RecorderDelete(const std::string& sName)
{
	if (sName.empty())
		return;

	std::scoped_lock tLock(m_mRecorderMutex);

	std::error_code ec;
	std::filesystem::remove(GetRecordingsFolder() + "\\" + sName + ".json", ec);

	if (sName == m_sRecordingName)
	{
		RecorderReset();
		m_sRecordingName.clear();
	}
}

// walks the player towards a recorded position, ported from the lua movement recorder
static void RecorderWalkTo(CUserCmd* pCmd, CTFPlayer* pLocal, const Vec3& vDestination)
{
	const Vec3 vDiff = vDestination - pLocal->m_vecOrigin();
	const float flDist = vDiff.Length();
	if (flDist < 1.f)
		return;

	const float flYaw = Math::Deg2Rad(Math::VectorAngles(vDiff).y - pCmd->viewangles.y);
	float flForward = cosf(flYaw) * 450.f;
	float flSide = -sinf(flYaw) * 450.f;

	// slow down when closing in on the target
	const float flSpeed = pLocal->m_vecVelocity().Length();
	if (flDist < 10.f + flSpeed)
	{
		const float flScale = flDist / 100.f;
		flForward *= flScale;
		flSide *= flScale;
	}
	pCmd->forwardmove = flForward;
	pCmd->sidemove = flSide;
}

void CMisc::Recorder(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	std::scoped_lock tLock(m_mRecorderMutex);

	if (m_bRecording)
	{
		m_bRecorderAtPos = false;

		RecorderTick_t tTick = {};
		tTick.m_vAngles = pCmd->viewangles;
		tTick.m_vOrigin = pLocal->m_vecOrigin();
		tTick.m_flForwardMove = pCmd->forwardmove;
		tTick.m_flSideMove = pCmd->sidemove;
		tTick.m_iButtons = pCmd->buttons;
		{	// snapshot the cheat bind states so playback replays bound features
			std::scoped_lock tLock(F::Binds.m_mMutex);
			tTick.m_vBinds.resize(F::Binds.m_vBinds.size());
			for (size_t i = 0; i < F::Binds.m_vBinds.size(); i++)
				tTick.m_vBinds[i] = F::Binds.m_vBinds[i].m_bActive;
		}
		m_vRecording.push_back(tTick);
		return;
	}

	if (!m_bPlaying)
		return;

	if (G::OriginalCmd.forwardmove || G::OriginalCmd.sidemove)
		return; // manual movement input overrides playback

	if (m_vRecording.empty() || uRecorderTick >= m_vRecording.size())
	{
		if (Vars::Misc::Movement::RecorderRepeat.Value && !m_vRecording.empty())
		{
			uRecorderTick = 0;
			m_bRecorderAtPos = false;
		}
		else
		{
			m_bPlaying = false;
			F::Binds.m_vPlaybackActive.clear();
			return;
		}
	}

	const RecorderTick_t& tTick = m_vRecording[uRecorderTick];

	{	// the binds system picks this up next tick and replays this tick's bind states
		std::scoped_lock tLock(F::Binds.m_mMutex);
		F::Binds.m_vPlaybackActive = tTick.m_vBinds;
	}

	pCmd->viewangles = tTick.m_vAngles;
	pCmd->forwardmove = tTick.m_flForwardMove;
	pCmd->sidemove = tTick.m_flSideMove;
	pCmd->buttons = tTick.m_iButtons;
	if (Vars::Misc::Movement::RecorderViewAngles.Value)
		I::EngineClient->SetViewAngles(pCmd->viewangles);

	// catch up to the recorded position before advancing the tick, ported from the lua
	const float flDist = pLocal->m_vecOrigin().DistTo(tTick.m_vOrigin);
	const float flSpeed = std::clamp(pLocal->m_vecVelocity().Length(), 0.1f, 50.f);
	if (!m_bRecorderAtPos)
	{
		RecorderWalkTo(pCmd, pLocal, tTick.m_vOrigin);
		if (flDist > flSpeed)
		{
			m_iRecorderSetupTimer--;
			if (m_iRecorderSetupTimer < 1 && flSpeed < 5.f || m_iRecorderSetupTimer < 66 && flSpeed < 1.f)
			{
				m_bRecorderAtPos = true;
				m_iRecorderSetupTimer = 128;
			}
			return; // too far behind the recording, hold the tick
		}
	}
	else if (flDist < pLocal->m_vecVelocity().Length() + 50.f)
	{
		RecorderWalkTo(pCmd, pLocal, tTick.m_vOrigin);
		if (flSpeed < 1.f)
			m_bRecorderAtPos = true;
	}
	else
	{
		m_iRecorderSetupTimer = 128;
		m_bRecorderAtPos = false;
	}

	uRecorderTick++;
}

void CMisc::SaveRecording()
{
	std::scoped_lock tLock(m_mRecorderMutex);

	if (m_sRecordingName.empty() || m_vRecording.empty())
		return;

	try
	{
		boost::property_tree::ptree tTicks;
		for (const auto& tTick : m_vRecording)
		{
			boost::property_tree::ptree tEntry;
			tEntry.put("Pitch", tTick.m_vAngles.x);
			tEntry.put("Yaw", tTick.m_vAngles.y);
			tEntry.put("Roll", tTick.m_vAngles.z);
			tEntry.put("ForwardMove", tTick.m_flForwardMove);
			tEntry.put("SideMove", tTick.m_flSideMove);
			tEntry.put("Buttons", tTick.m_iButtons);
			tEntry.put("OriginX", tTick.m_vOrigin.x);
			tEntry.put("OriginY", tTick.m_vOrigin.y);
			tEntry.put("OriginZ", tTick.m_vOrigin.z);

			std::string sBinds;
			for (char cBind : tTick.m_vBinds)
			{
				if (!sBinds.empty())
					sBinds += ",";
				sBinds += cBind ? '1' : '0';
			}
			tEntry.put("Binds", sBinds);

			tTicks.push_back(std::make_pair("", tEntry));
		}

		boost::property_tree::ptree tWrite;
		tWrite.add_child("Ticks", tTicks);
		boost::property_tree::write_json(GetRecordingsFolder() + "\\" + m_sRecordingName + ".json", tWrite);
	}
	catch (...)
	{
	}
}

void CMisc::AutoCrouchNavbot(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!Vars::Misc::Movement::DuckSpeed.Value
		|| !Vars::Misc::Movement::NavBot::Enabled.Value
		|| !Vars::Misc::Movement::NavEngine::Enabled.Value)
		return;

	if (pLocal->IsSwimming() || F::NavEngine.IsUnstucking() || pCmd->buttons & IN_JUMP)
	{
		pCmd->buttons &= ~IN_DUCK;
		return;
	}

	pCmd->buttons |= IN_DUCK;
}



void CMisc::AutoJump(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!Vars::Misc::Movement::Bunnyhop.Value)
		return;

	if (auto pWeapon = H::Entities.GetWeapon(); pWeapon && pWeapon->GetWeaponID() == TF_WEAPON_GRAPPLINGHOOK && pWeapon->As<CTFGrapplingHook>()->m_hProjectile())
		return;

	static bool bStaticAttempted = false, bStaticValid = false;
	bool bLastAttempted = bStaticAttempted, bLastValid = bStaticValid;
	bool bCurrAttempted = bStaticAttempted = G::OriginalCmd.buttons & IN_JUMP, bCurrValid = bStaticValid = pLocal->m_hGroundEntity() && !pLocal->IsDucking();
	if (!bCurrValid || bCurrValid && G::LastUserCmd->buttons & IN_JUMP)
		pCmd->buttons &= ~IN_JUMP;

	static float flLastAttempt = 0.f;
	bool bPressed = bCurrAttempted && !bLastAttempted;
	bool bParachute = SDK::AttribHookValue(0, "parachute_attribute", pLocal) && !pLocal->InCond(TF_COND_PARACHUTE_ACTIVE);
	bool bAllow = !bParachute || G::OriginalCmd.buttons & IN_DUCK; // evil we don't want to manual
	bool bManual = bAllow && (bPressed || bParachute && I::GlobalVars->curtime < flLastAttempt + 0.1f);
	if (bPressed && !bCurrValid)
		flLastAttempt = I::GlobalVars->curtime;
	if (bManual)
		pCmd->buttons |= IN_JUMP;

	F::AntiCheatCompatibility.BunnyHop(pCmd, bCurrValid, bLastValid);
}

void CMisc::JumpBug(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!Vars::Misc::Movement::JumpBug.Value)
		return;

	// jumpbug needs a fall: duck through the air, then unduck and jump on the same tick we land
	if (pLocal->m_hGroundEntity())
		return;
	pCmd->buttons |= IN_DUCK;

	const float flVelocityZ = pLocal->m_vecVelocity().z;
	if (flVelocityZ >= 0.f) // only trigger when falling
		return;

	static auto sv_gravity = H::ConVars.FindVar("sv_gravity");
	const float flGravity = sv_gravity && sv_gravity->GetFloat() > 0.f ? sv_gravity->GetFloat() : 800.f;

	// unduck height plus safety margin, scaled with fall speed so slow falls stay consistent
	const float flUnduckHeight = 20.f * pLocal->m_flModelScale();
	const float flVelocityScale = std::min(std::max(1.f, -flVelocityZ / (flGravity * 0.5f)), 3.f);
	const float flTraceDistance = (flUnduckHeight + 60.f) * flVelocityScale;

	CGameTrace trace = {};
	CTraceFilterWorldAndPropsOnly filter = {};
	Vec3 vOrigin = pLocal->m_vecOrigin();
	SDK::TraceHull(vOrigin, vOrigin - Vec3(0, 0, flTraceDistance), pLocal->m_vecMins(), pLocal->m_vecMaxs(), pLocal->SolidMask(), &filter, &trace);
	if (!trace.DidHit())
		return;

	// only unduck and jump when the ground is within the window where unducking lands us standing this tick
	const float flHitDistance = trace.fraction * flTraceDistance;
	if (flHitDistance < flUnduckHeight)
		m_flJumpBugMissTime = I::GlobalVars->curtime; // fell past the execution window, feed the indicator
	if (flHitDistance < flUnduckHeight || flHitDistance > flUnduckHeight + 4.f)
		return;

	pCmd->buttons &= ~IN_DUCK;
	pCmd->buttons |= IN_JUMP;
	m_flJumpBugTime = I::GlobalVars->curtime;
	if (Vars::Misc::Movement::JumpBugSound.Value)
		I::EngineClient->ClientCmd_Unrestricted("play buttons/button14.wav");
}

void CMisc::EdgebugDetect(CTFPlayer* pLocal)
{
	const int iFlags = pLocal->m_fFlags();
	const Vec3 vVelocity = pLocal->m_vecVelocity();

	// real edgebug: we lost downward speed mid air without ever touching the ground (tick over tick)
	if (vVelocity.z > m_vEdgeBugPrevVel.z
		&& !(m_iEdgeBugPrevFlags & FL_ONGROUND) && !(iFlags & FL_ONGROUND)
		&& pLocal->m_MoveType() != MOVETYPE_LADDER && pLocal->m_MoveType() != MOVETYPE_NOCLIP && pLocal->m_MoveType() != MOVETYPE_OBSERVER)
	{
		m_flEdgeBugTime = I::GlobalVars->curtime;
		if (Vars::Misc::Movement::EdgeBugSound.Value)
			I::EngineClient->ClientCmd_Unrestricted("play ui/hint.wav");
	}

	m_vEdgeBugPrevVel = vVelocity;
	m_iEdgeBugPrevFlags = iFlags;
}

void CMisc::Draw(CTFPlayer* pLocal)
{
	if (!pLocal || !pLocal->IsAlive() || !I::EngineClient->IsInGame())
		return;

	// world-space drawing for the movement suite moved to IEngineVGui_Paint (DrawPixelSurf):
	// the vgui surface has no drawing context in the Present hook and errors out with
	// "Getting a dynamic mesh without resolving the previous one"

	// runs from the Present hook, so draw through imgui instead of the vgui surface (which has no drawing context here)
	auto DrawLuaText = [](const char* sText, const Color_t& tColor, const DragBox_t& tPos)
	{
		ImFont* pFont = F::Render.FontBold;
		if (!pFont)
			return;

		float flFontSize = pFont->LegacySize * 1.5f;
		ImVec2 vTextSize = pFont->CalcTextSizeA(flFontSize, FLT_MAX, 0.f, sText);
		ImVec2 vTextPos = { static_cast<float>(tPos.x) - vTextSize.x * 0.5f, static_cast<float>(tPos.y) };
		ImGui::GetBackgroundDrawList()->AddText(pFont, flFontSize, vTextPos, IM_COL32(tColor.r, tColor.g, tColor.b, tColor.a), sText);
	};

	if (Vars::Menu::Indicators.Value & Vars::Menu::IndicatorsEnum::Jumpbug && Vars::Misc::Movement::JumpBug.Value)
	{
		Color_t tColor = { 255, 255, 255, 255 };
		if (I::GlobalVars->curtime - m_flJumpBugMissTime < 0.5f)
			tColor = { 255, 0, 0, 255 };
		else if (I::GlobalVars->curtime - m_flJumpBugTime < 0.5f)
			tColor = { 0, 255, 0, 255 };
		DrawLuaText("JB", tColor, Vars::Menu::JumpbugDisplay.Value);
	}

	if (Vars::Menu::Indicators.Value & Vars::Menu::IndicatorsEnum::Edgebug && Vars::Misc::Movement::AutoEdgebug.Value)
	{
		Color_t tColor = { 255, 255, 255, 255 };
		if (I::GlobalVars->curtime - m_flEdgeBugTime < 0.5f)
			tColor = { 0, 255, 0, 255 };
		DrawLuaText("EB", tColor, Vars::Menu::EdgebugDisplay.Value);
	}
}

// ==== movement suite ====
#ifndef MVSUITE_PI
#define MVSUITE_PI 3.14159265358979323846f
#define MVSUPE_DEG2RAD(x) ((x) * (MVSUITE_PI / 180.f))
#define MVSUPE_RAD2DEG(x) ((x) * (180.f / MVSUITE_PI))
#endif
// Epsilon from the movement math (surface extension).
static constexpr float TB_EPSILON = 0.03125f;
// Maximum Simulate() calls per bruteforce search.
static constexpr int   TB_SIM_BUDGET = 24;

// The wall sweeps step through the same 16 fixed angles; precompute cos/sin once.
struct SweepDir_t { float c, s; };
constexpr float TB_SWEEP_STEP = (MVSUITE_PI * 2.f) / 16.f;
inline const SweepDir_t* TB_SweepDirs16()
{
	static SweepDir_t s_aDirs[16];
	static bool s_bInit = false;
	if (!s_bInit)
	{
		for (int k = 0; k < 16; k++)
		{
			const float a = float(k) * TB_SWEEP_STEP;
			s_aDirs[k] = { cosf(a), sinf(a) };
		}
		s_bInit = true;
	}
	return s_aDirs;
}


static bool TB_SteeringAwayFromWall(const CUserCmd* pCmd, const Vec3& vWallNormal,
	float flForwardMove, float flSideMove, float flMinDot = 0.3f)
{
	if (flForwardMove == 0.f && flSideMove == 0.f)
		return false; // no movement keys held -> not trying to leave

	Vec3 vForward, vRight;
	Math::AngleVectors(Vec3(0.f, pCmd->viewangles.y, 0.f), &vForward, &vRight, nullptr);

	Vec3 vWish = vForward * flForwardMove + vRight * flSideMove;
	vWish.z = 0.f;
	const float flWishLen = vWish.Length2D();
	if (flWishLen < 1.f)
		return false;
	vWish = vWish * (1.f / flWishLen);

	Vec3 vNormal = vWallNormal;
	vNormal.z = 0.f;
	const float flNormLen = vNormal.Length2D();
	if (flNormLen < 0.001f)
		return false;
	vNormal = vNormal * (1.f / flNormLen);

	// The wall normal points out of the wall toward the player, so a positive dot means the wish
	// direction heads outward. flMinDot ignores into-wall and parallel holds but catches a clear
	return vWish.Dot(vNormal) > flMinDot;
}


static float TB_HalfGravityPerTick()
{
    static auto sv_gravity = H::ConVars.FindVar("sv_gravity");
    const float flGravity = sv_gravity ? sv_gravity->GetFloat() : 800.f;
    return -(flGravity * I::GlobalVars->interval_per_tick * 0.5f);
}


static bool TB_IsPixelSurfVel(float vz, float flHalf, float tolerance = 2.f)
{
    return fabsf(vz - flHalf) < tolerance;
}


static bool TB_IsPositiveHalfGravity(float vz, float flHalf, float tolerance = 2.f)
{
    return fabsf(vz + flHalf) < tolerance;
}


float TB_WallSupportDistance(CTFPlayer* pLocal, const Vec3& vWallNormal)
{
    const Vec3 vMins = pLocal->m_vecMins();
    const Vec3 vMaxs = pLocal->m_vecMaxs();
    const Vec3 vHalf(std::max(fabsf(vMins.x), fabsf(vMaxs.x)),
        std::max(fabsf(vMins.y), fabsf(vMaxs.y)),
        std::max(fabsf(vMins.z), fabsf(vMaxs.z)));
    return fabsf(vWallNormal.x) * vHalf.x + fabsf(vWallNormal.y) * vHalf.y + fabsf(vWallNormal.z) * vHalf.z;
}


static bool TB_DetectWall(const Vec3& vStart, const Vec3& vMins, const Vec3& vMaxs,
                           float flStep, CGameTrace& outTrace, float& outAngle, float flCachedAngle = -1.f,
                           float flReach = 24.f)
{
    (void)flStep; // swept via fixed 16-angle table; kept for call-site compatibility
    CTraceFilterWorldAndPropsOnly filter;

    const auto TestDir = [&](float c, float s, CGameTrace& tr) -> bool
    {
        const Vec3 vEnd = vStart + Vec3(c * flReach, s * flReach, 0.f);
        SDK::TraceHull(vStart, vEnd, vMins, vMaxs, MASK_PLAYERSOLID, &filter, &tr);
        return tr.fraction < 1.f
            && fabsf(tr.plane.normal.z) <= 0.10f       // near-vertical only
            && !(tr.surface.flags & SURF_TRIGGER);     // skip walk-through brushes
    };

    // Fast path: re-test last tick's winning wall angle.
    if (flCachedAngle >= 0.f)
    {
        CGameTrace tr;
        if (TestDir(cosf(flCachedAngle), sinf(flCachedAngle), tr))
        {
            outTrace = tr;
            outAngle = flCachedAngle;
            return true;
        }
    }

    // Full sweep for the closest valid wall (precomputed 16-angle dir table).
    const SweepDir_t* pDirs = TB_SweepDirs16();
    float flBestFraction = 1.f;
    bool bHit = false;
    for (int k = 0; k < 16; k++)
    {
        CGameTrace trace;
        if (!TestDir(pDirs[k].c, pDirs[k].s, trace))
            continue;
        if (trace.fraction < flBestFraction)
        {
            flBestFraction = trace.fraction;
            outTrace = trace;
            outAngle = float(k) * TB_SWEEP_STEP;
            bHit = true;
        }
    }
    return bHit;
}


static bool TB_DetectCeiling(const Vec3& vPos, const Vec3& vMins, const Vec3& vMaxs,
                              float flReach, CGameTrace& traceOut)
{
    Vec3 vStart = vPos;
    Vec3 vEnd   = vPos;
    vEnd.z     += flReach;

    Ray_t ray;
    ray.Init(vStart, vEnd, vMins, vMaxs);
    CTraceFilterWorldAndPropsOnly filter;
    I::EngineTrace->TraceRay(ray, MASK_PLAYERSOLID, &filter, &traceOut);

    return traceOut.DidHit() && traceOut.plane.normal.z < -0.7f;
}


float TB_SupportDistance(CTFPlayer* pLocal, const CGameTrace& primary)
{
    const Vec3 vOrigin = pLocal->GetAbsOrigin();
    const Vec3& n = primary.plane.normal;
    const float flPlaneDist = vOrigin.x * n.x + vOrigin.y * n.y + vOrigin.z * n.z - primary.plane.dist;
    const Vec3 vWallNormal(n.x, n.y, 0.f);
    return fabsf(flPlaneDist) - TB_WallSupportDistance(pLocal, vWallNormal);
}


static bool TB_MeasureBrushFace(const Vec3& vOrigin, const Vec3& vMins, const Vec3& vMaxs,
                                const CGameTrace& primary, float flReach,
                                float& outBottomZ, float& outHeight)
{
    (void)vMins; // hull top (vMaxs.z) is what crosses brush bottoms; mins kept for call-site symmetry
    CTraceFilterWorldAndPropsOnly filter;
    const Vec3& n = primary.plane.normal;
    const float flIntoLen = sqrtf(n.x * n.x + n.y * n.y);
    if (flIntoLen < 0.01f)
        return false;
    const Vec3 vDir(-n.x / flIntoLen, -n.y / flIntoLen, 0.f); // horizontal unit dir into the wall
    const Vec3 vProbeMin(-2.f, -2.f, -1.f), vProbeMax(2.f, 2.f, 1.f); // thin probe (near a line)

    const float flLow  = vOrigin.z - 8.f;
    const float flHigh = vOrigin.z + vMaxs.z + 16.f;
    constexpr float flZStep = 4.f;
    constexpr int kMaxSamples = 48;

    struct Sample { float z; bool present; short props; float dist; };
    Sample aSamples[kMaxSamples];
    int nSamples = 0;
    for (float z = flLow; z <= flHigh && nSamples < kMaxSamples; z += flZStep)
    {
        const Vec3 vStart(vOrigin.x, vOrigin.y, z);
        const Vec3 vEnd = vStart + vDir * flReach;
        CGameTrace tr;
        SDK::TraceHull(vStart, vEnd, vProbeMin, vProbeMax, MASK_PLAYERSOLID, &filter, &tr);
        Sample& s = aSamples[nSamples++];
        s.z = z;
        s.present = tr.fraction < 1.f && fabsf(tr.plane.normal.z) <= 0.10f && !(tr.surface.flags & SURF_TRIGGER);
        s.props = s.present ? tr.surface.surfaceProps : (short)-1;
        s.dist  = s.present ? tr.plane.dist : 0.f;
    }
    if (nSamples < 2)
        return false;

    // Anchor on the present sample nearest the hull top: that is where the head crosses brush
    // bottoms as we fall, so it is the brush segment whose bottom edge actually matters.
    const float flHeadZ = vOrigin.z + vMaxs.z;
    int iAnchor = -1;
    float flBestDz = FLT_MAX;
    for (int i = 0; i < nSamples; i++)
    {
        if (!aSamples[i].present)
            continue;
        const float dz = fabsf(aSamples[i].z - flHeadZ);
        if (dz < flBestDz) { flBestDz = dz; iAnchor = i; }
    }
    if (iAnchor < 0)
        return false; // no wall present anywhere in the window

    // Grow the contiguous same-surface segment around the anchor (matching surfaceProps AND plane).
    const short refProps = aSamples[iAnchor].props;
    const float refDist  = aSamples[iAnchor].dist;
    const auto SameFace = [&](const Sample& s) -> bool
    {
        return s.present && s.props == refProps && fabsf(s.dist - refDist) <= 2.f;
    };
    int iBottom = iAnchor, iTop = iAnchor;
    while (iBottom - 1 >= 0 && SameFace(aSamples[iBottom - 1])) iBottom--;
    while (iTop + 1 < nSamples && SameFace(aSamples[iTop + 1])) iTop++;

    outBottomZ = aSamples[iBottom].z;
    outHeight  = aSamples[iTop].z - aSamples[iBottom].z + flZStep; // +step so the span includes the sample gaps
    return true;
}


static bool GridCheck(float a, float b)
{
	const int a1 = int(a), b1 = int(b);
	if (a1 != b1)
		return false;
	const int a2 = int((a - a1) * 100.f), b2 = int((b - b1) * 100.f);
	if (b < 0.f)
		return b2 == a2 || a2 + 1 == b2 || a2 + 2 == b2;
	return b2 == a2 || a2 == b2 - 1 || a2 == b2 - 2;
}


void CMisc::CorrectMovement(CUserCmd* pCmd, Vec3 vWishAngle, Vec3 vOldAngles)
{
	if (vOldAngles.x == vWishAngle.x && vOldAngles.y == vWishAngle.y && vOldAngles.z == vWishAngle.z)
		return;

	Vec3 vWishForward, vWishRight, vWishUp;
	Vec3 vCmdForward, vCmdRight, vCmdUp;

	Vec3 vMoveData(pCmd->forwardmove, pCmd->sidemove, pCmd->upmove);

	Math::AngleVectors(vWishAngle, &vWishForward, &vWishRight, &vWishUp);
	Math::AngleVectors(vOldAngles, &vCmdForward, &vCmdRight, &vCmdUp);

	// Normalize
	float flWishForwardLen = sqrtf(vWishForward.x * vWishForward.x + vWishForward.y * vWishForward.y);
	float flWishRightLen = sqrtf(vWishRight.x * vWishRight.x + vWishRight.y * vWishRight.y);
	float flWishUpLen = sqrtf(vWishUp.z * vWishUp.z);

	Vec3 vWishForwardNorm(vWishForward.x / flWishForwardLen, vWishForward.y / flWishForwardLen, 0.f);
	Vec3 vWishRightNorm(vWishRight.x / flWishRightLen, vWishRight.y / flWishRightLen, 0.f);
	Vec3 vWishUpNorm(0.f, 0.f, vWishUp.z / flWishUpLen);

	float flCmdForwardLen = sqrtf(vCmdForward.x * vCmdForward.x + vCmdForward.y * vCmdForward.y);
	float flCmdRightLen = sqrtf(vCmdRight.x * vCmdRight.x + vCmdRight.y * vCmdRight.y);
	float flCmdUpLen = sqrtf(vCmdUp.z * vCmdUp.z);

	Vec3 vCmdForwardNorm(vCmdForward.x / flCmdForwardLen, vCmdForward.y / flCmdForwardLen, 0.f);
	Vec3 vCmdRightNorm(vCmdRight.x / flCmdRightLen, vCmdRight.y / flCmdRightLen, 0.f);
	Vec3 vCmdUpNorm(0.f, 0.f, vCmdUp.z / flCmdUpLen);

	// Calculate corrected movement
	Vec3 vCorrectMove;
	vCorrectMove.x = vCmdForwardNorm.x * (vWishForwardNorm.x * vMoveData.x + vWishRightNorm.x * vMoveData.y) +
		vCmdForwardNorm.y * (vWishForwardNorm.y * vMoveData.x + vWishRightNorm.y * vMoveData.y);

	vCorrectMove.y = vCmdRightNorm.x * (vWishForwardNorm.x * vMoveData.x + vWishRightNorm.x * vMoveData.y) +
		vCmdRightNorm.y * (vWishForwardNorm.y * vMoveData.x + vWishRightNorm.y * vMoveData.y);

	vCorrectMove.z = vCmdUpNorm.z * vWishUpNorm.z * vMoveData.z;

	// Clamp
	vCorrectMove.x = std::clamp(vCorrectMove.x, -450.f, 450.f);
	vCorrectMove.y = std::clamp(vCorrectMove.y, -450.f, 450.f);
	vCorrectMove.z = std::clamp(vCorrectMove.z, -320.f, 320.f);

	pCmd->forwardmove = vCorrectMove.x;
	pCmd->sidemove = vCorrectMove.y;
	pCmd->upmove = vCorrectMove.z;
}


void CMisc::RestoreEntityToPredicted()
{
	I::Prediction->RestoreEntityToPredictedFrame(I::Prediction->m_nCommandsPredicted - 1);
}

void CMisc::LongJump(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	static int  longjump_tick = 0;
	static bool ljbool        = false;

	if (!Vars::Misc::Movement::LongJump.Value)
	{
		// Full reset on disable so a bind-toggle mid-air doesn't leave ljbool=true,
		// which would cause IN_DUCK to fire the next time the feature turns on.
		m_bLongJumpDetected = false;
		ljbool              = false;
		longjump_tick       = 0;
		return;
	}

	if (!pLocal || !pLocal->IsAlive())
		return;

	if (pLocal->m_MoveType() == MOVETYPE_LADDER || pLocal->m_MoveType() == MOVETYPE_NOCLIP)
		return;

	// Any ground-leave while LongJump is enabled fires the LJ: jump injected + duck window.
	if ((m_iPrePredictionFlags & FL_ONGROUND) && !(pLocal->m_fFlags() & FL_ONGROUND)
		&& m_iAssistJumpType == JT_NONE)
	{
		pCmd->buttons |= IN_JUMP;
		ljbool        = true;
		longjump_tick = I::GlobalVars->tickcount + 2;
	}

	if (ljbool)
	{
		m_bLongJumpDetected = true;

		// Hold duck for 2 ticks after leaving ground.
		if (I::GlobalVars->tickcount < longjump_tick)
			pCmd->buttons |= IN_DUCK;

		// Reset once the duck window expires.
		if (I::GlobalVars->tickcount >= longjump_tick)
		{
			ljbool              = false;
			m_bLongJumpDetected = false;
		}
	}
	else
	{
		m_bLongJumpDetected = false;
	}
}


void CMisc::MiniJump(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!Vars::Misc::Movement::MiniJump.Value)
	{
		// Full reset on disable so a bind-toggle mid-air doesn't leave
		// m_bMiniJumpShouldDuck=true, which would cause perpetual IN_DUCK next enable.
		m_bMiniJumpDetected   = false;
		m_bMiniJumpShouldDuck = false;
		m_iMiniJumpPendingUntil = 0;
		return;
	}

	const bool bGroundPre = m_iPrePredictionFlags & FL_ONGROUND;
	const bool bGroundPost = pLocal->m_fFlags() & FL_ONGROUND;

	// Clear hold-duck the moment we touch ground again.
	if (bGroundPre && bGroundPost)
		m_bMiniJumpShouldDuck = false;

	// Pre-landing unduck: release our held duck while still airborne and about to land (an
	// in-air unduck is instant) so we touch down STANDING. Landing ducked is what made chains
	if (m_bMiniJumpShouldDuck && !bGroundPost && pLocal->m_vecVelocity().z < 0.f)
	{
		const float flLookahead = -pLocal->m_vecVelocity().z * I::GlobalVars->interval_per_tick * 5.f + 16.f;
		CTraceFilterWorldAndPropsOnly filter;
		CGameTrace trace;
		SDK::TraceHull(pLocal->m_vecOrigin(), pLocal->m_vecOrigin() - Vec3(0.f, 0.f, flLookahead),
			pLocal->m_vecMins(), pLocal->m_vecMaxs(), MASK_PLAYERSOLID, &filter, &trace);
		if (trace.fraction < 1.f)
			m_bMiniJumpShouldDuck = false; // ground ~5 ticks away - stand up now
	}

	// jump -> minijump repair: a jump pressed while we're still grounded AFTER prediction means
	// the engine ate the press - it refuses to jump while FL_DUCKING is set or the unduck
	if (Vars::Misc::Movement::MiniJumpQueue.Value && bGroundPre && bGroundPost && (pCmd->buttons & IN_JUMP))
	{
		m_iMiniJumpPendingUntil = I::GlobalVars->tickcount + 40;
		pCmd->buttons &= ~IN_JUMP;
	}

	// Detect a real jump takeoff.
	const bool bJumpingOff = pLocal->m_vecVelocity().z > 100.f || (pCmd->buttons & IN_JUMP);
	if (bGroundPre && !bGroundPost && bJumpingOff && m_iAssistJumpType == JT_NONE)
	{
		pCmd->buttons |= IN_DUCK;
		m_bMiniJumpDetected = true;

		if (Vars::Misc::Movement::MiniJumpHoldDuck.Value)
			m_bMiniJumpShouldDuck = true;
	}
	else
	{
		m_bMiniJumpDetected = false;
	}

	if (m_bMiniJumpShouldDuck)
		pCmd->buttons |= IN_DUCK;

	if (pCmd->buttons & IN_JUMP)
		m_iMiniJumpLastJumpCmdTick = I::GlobalVars->tickcount;
}


void CMisc::MiniJumpPre(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!Vars::Misc::Movement::MiniJump.Value || !Vars::Misc::Movement::MiniJumpQueue.Value)
	{
		m_iMiniJumpPendingUntil = 0;
		return;
	}

	const bool bGround = pLocal->m_fFlags() & FL_ONGROUND;

	// Stale-clock guard: tickcount restarts on a new server/map, so tick references stored on the
	// previous connection sit far in the "future" - the bhop delay below would then read every
	const int iNow = I::GlobalVars->tickcount;
	if (m_iMiniJumpLandTick > iNow)
		m_iMiniJumpLandTick = 0;
	if (m_iMiniJumpLastJumpCmdTick > iNow)
		m_iMiniJumpLastJumpCmdTick = 0;
	if (m_iMiniJumpPendingUntil > iNow + 60)
		m_iMiniJumpPendingUntil = 0;

	// Track the landing tick for the bhop delay below.
	if (bGround && !m_bMiniJumpPrevGround)
		m_iMiniJumpLandTick = I::GlobalVars->tickcount;
	m_bMiniJumpPrevGround = bGround;

	if (!bGround)
	{
		if (pCmd->buttons & IN_JUMP)
		{
			m_iMiniJumpPendingUntil = I::GlobalVars->tickcount + 40; // plenty to land + unduck
			pCmd->buttons &= ~IN_JUMP;
		}
		return;
	}

	// Bhop delay: a jump on the landing tick (a bhop-injected one, or space held/spammed) fires
	// before the previous hop's duck state has settled and the mini comes out broken. Hold EVERY
	constexpr int iBhopDelayTicks = 2;
	if (I::GlobalVars->tickcount - m_iMiniJumpLandTick < iBhopDelayTicks)
	{
		if (pCmd->buttons & IN_JUMP)
		{
			m_iMiniJumpPendingUntil = I::GlobalVars->tickcount + 40;
			pCmd->buttons &= ~IN_JUMP;
		}
		return;
	}

	if (!m_iMiniJumpPendingUntil)
		return;

	if (I::GlobalVars->tickcount > m_iMiniJumpPendingUntil)
	{
		m_iMiniJumpPendingUntil = 0; // engine never came unblocked in time - drop the press
		return;
	}

	const bool bDuckBlocked = (pLocal->m_fFlags() & FL_DUCKING) || pLocal->m_bDucking() || pLocal->m_flDuckJumpTime() > 0.f;
	const bool bReleaseBlocked = I::GlobalVars->tickcount - m_iMiniJumpLastJumpCmdTick <= 1;
	if (bDuckBlocked || bReleaseBlocked)
	{
		pCmd->buttons &= ~IN_JUMP; // still waiting: keep the press parked
		return;
	}

	pCmd->buttons |= IN_JUMP;
	m_iMiniJumpPendingUntil = 0;
}


void CMisc::AutoAlign(CTFPlayer* pLocal, CUserCmd* pCmd)
{

	if (!Vars::Misc::Movement::AutoAlign.Value)
	{
		m_bWallDetected = false;
		return;
	}

	if (pLocal->m_fFlags() & FL_ONGROUND)
		return;

	if (pLocal->m_MoveType() == MOVETYPE_LADDER || pLocal->m_MoveType() == MOVETYPE_NOCLIP)
	{
		m_bWallDetected = false;
		return;
	}

	float flMaxRadius = MVSUITE_PI * 2.f;
	float flStep = flMaxRadius / 16.f;
	Vec3 vStartPos = pLocal->GetAbsOrigin();
	Vec3 vMins = pLocal->m_vecMins();
	Vec3 vMaxs = pLocal->m_vecMaxs();

	CTraceFilterWorldAndPropsOnly filter;
	CGameTrace trace;
	m_bWallDetected = false;

	// Find wall around player
	for (float a = m_flAutoAlignStartCircle; a < flMaxRadius; a += flStep)
	{
		Vec3 vEndPos;
		vEndPos.x = cosf(a) + vStartPos.x;
		vEndPos.y = sinf(a) + vStartPos.y;
		vEndPos.z = vStartPos.z;

		SDK::TraceHull(vStartPos, vEndPos, vMins, vMaxs, MASK_PLAYERSOLID, &filter, &trace);

		// Upright, flat brush wall only - reject ramps (normal.z != 0), passable trigger volumes and
		// displacements (a disp can be locally near-vertical but never holds a pixel surf).
		if (trace.fraction != 1.f && trace.plane.normal.z == 0.f
			&& !(trace.surface.flags & SURF_TRIGGER) && !trace.IsDispSurface())
		{
			m_bWallDetected = true;
			m_flAutoAlignStartCircle = a;
			break;
		}
	}

	if (!m_bWallDetected)
	{
		m_flAutoAlignStartCircle = 0.f;
		return;
	}

	// Seamless leave: if the player is actively steering off the wall, don't force their movement
	// back into it - hand control straight back this tick. Without this AutoAlign re-grabs the wall
	if (TB_SteeringAwayFromWall(pCmd, trace.plane.normal, pCmd->forwardmove, pCmd->sidemove))
	{
		m_bWallDetected = false;
		return;
	}

	// Calculate movement direction towards wall
	Vec3 vNormalPlane = Vec3(trace.plane.normal.x * -0.005f, trace.plane.normal.y * -0.005f, 0.f);
	Vec3 vWallAngle = Math::VectorAngles(vNormalPlane);
	Math::ClampAngles(vWallAngle);

	float flRotation = MVSUPE_DEG2RAD(vWallAngle.y - pCmd->viewangles.y);
	float flCosRot = cosf(flRotation);
	float flSinRot = sinf(flRotation);

	// Try to find movement that maintains pixel surf velocity.
	static auto sv_gravity = H::ConVars.FindVar("sv_gravity");
	const float flSurfZ = (sv_gravity ? sv_gravity->GetFloat() : 800.f) * I::GlobalVars->interval_per_tick * 0.5f;

	// Pin the prediction frame (same fix as TextureBug/SimJumpReach): a bare
	const int nBasePredicted = I::Prediction->m_nCommandsPredicted;
	const auto RestoreOriginal = [&]()
	{
		I::Prediction->RestoreEntityToPredictedFrame(nBasePredicted - 1);
	};

	// The hold search used to sweep ONLY the pure into-wall direction, discarding any along-wall
	// input - once pinned you couldn't adjust your position on the surf, just hope or bail
	const float flUserFwd = pCmd->forwardmove;
	const float flUserSide = pCmd->sidemove;

	Vec3 vFwdDir, vRightDir;
	Math::AngleVectors(Vec3(0.f, pCmd->viewangles.y, 0.f), &vFwdDir, &vRightDir, nullptr);
	Vec3 vWishWorld = vFwdDir * flUserFwd + vRightDir * flUserSide;
	vWishWorld.z = 0.f;

	Vec3 vInto = Vec3(-trace.plane.normal.x, -trace.plane.normal.y, 0.f);
	vInto.Normalize();
	const Vec3 vTangentDir = Vec3(trace.plane.normal.y, -trace.plane.normal.x, 0.f);
	const float flTangentInput = std::clamp(vWishWorld.Dot(vTangentDir), -450.f, 450.f);

	bool bDetect = false;
	for (int iPass = 0; iPass < 2 && !bDetect; iPass++)
	{
		const float flTangent = iPass == 0 ? flTangentInput : 0.f;
		if (iPass == 0 && fabsf(flTangent) < 10.f)
			continue; // no meaningful along-wall input - just run the pure hold pass

		for (float flMultiplier = 0.f; flMultiplier < 100.f; flMultiplier += 10.f)
		{
			RestoreOriginal();

			// Candidate move in world space: press into the wall + the player's lateral slide
			// (mult 0 + tangent = drifting along the surf without pressing in, also valid).
			const Vec3 vMove = vInto * flMultiplier + vTangentDir * flTangent;
			const float flMag = vMove.Length2D();
			if (flMag < 0.01f)
			{
				pCmd->forwardmove = 0.f;
				pCmd->sidemove = 0.f;
			}
			else
			{
				Vec3 vMoveAngle = Math::VectorAngles(vMove);
				const float flMoveRot = MVSUPE_DEG2RAD(vMoveAngle.y - pCmd->viewangles.y);
				pCmd->forwardmove = cosf(flMoveRot) * flMag;
				pCmd->sidemove = -sinf(flMoveRot) * flMag;
			}

			F::EnginePrediction.Simulate(pLocal, pCmd);

			if (fabsf(pLocal->m_vecVelocity().z + flSurfZ) < 1.5f) // tick-correct pixel surf velocity
			{
				bDetect = true;
				break;
			}
		}
	}

	// Always hand the entity back un-simulated: leaving it one predicted tick ahead poisoned
	// every live-velocity read in the features that run after us (TextureBug's locked-band
	RestoreOriginal();

	if (!bDetect)
	{
		// Default small movement towards wall
		pCmd->forwardmove = flCosRot * 10.f;
		pCmd->sidemove = -flSinRot * 10.f;
	}
}


void CMisc::PixelSurf(CTFPlayer* pLocal, CUserCmd* pCmd)
{

	m_bPixelSurfingNow = false; // recomputed each tick from live velocity (drives the "ps" indicator)

	// Half-gravity per tick (negative once we add it to vz) - the velocity the engine pins you
	// to while riding a pixel surf.
	static auto sv_gravity = H::ConVars.FindVar("sv_gravity");
	const float flHalfGrav = (sv_gravity ? sv_gravity->GetFloat() : 800.f) * I::GlobalVars->interval_per_tick * 0.5f;
	constexpr float flSurfTol = 1.5f;

	// Ride detection FIRST, before any feature gate: the "ps" indicator lights when you are
	// ACTUALLY riding a surf, not when the auto-catcher merely decides to crouch, and it works
	if (pLocal->IsAlive() && pLocal->m_MoveType() != MOVETYPE_NOCLIP && pLocal->m_MoveType() != MOVETYPE_LADDER)
	{
		const bool bSurfVel = fabsf(m_vPrePredictionVelocity.z + flHalfGrav) < flSurfTol
			&& (!(m_iPrePredictionFlags & FL_ONGROUND) || m_bShouldPixelSurf);
		m_bPixelSurfingNow = bSurfVel && m_bPrevSurfVel;
		m_bPrevSurfVel = bSurfVel;
	}
	else
		m_bPrevSurfVel = false;

	if (!Vars::Misc::Movement::PixelSurf.Value)
	{
		m_bShouldPixelSurf = false;
		return;
	}

	if (!pLocal->IsAlive())
	{
		m_iPixelSurfTicks = 0;
		m_bShouldPixelSurf = false;
		return;
	}

	if (pLocal->m_MoveType() == MOVETYPE_NOCLIP || pLocal->m_MoveType() == MOVETYPE_LADDER)
		return;

	if (pLocal->m_fFlags() & FL_ONGROUND)
	{
		m_bShouldPixelSurf = false;
		return;
	}

	// NOTE: do NOT gate on m_bWallDetected here. That flag is only set by
	// AutoAlign (and only when AutoAlign is enabled), so requiring it meant

	if (!m_bShouldPixelSurf)
	{
		// Pixel surfs only catch on perfectly-upright, flat brush walls. The duck prediction
		// below can momentarily hit the surf z-velocity next to a displacement (a disp is built
		{
			constexpr float flWallRange = 10.f;          // max distance from the hull to count as "next to a wall"
			constexpr float flStep = (MVSUITE_PI * 2.f) / 16.f;  // sweep resolution
			constexpr float flUprightEps = 0.0001f;        // brush wall faces are exactly vertical (normal.z == 0)

			const Vec3 vStartPos = pLocal->GetAbsOrigin();
			const Vec3 vMins = pLocal->m_vecMins();
			const Vec3 vMaxs = pLocal->m_vecMaxs();
			CTraceFilterWorldAndPropsOnly filter;

			bool bUprightWall = false;
			for (float a = 0.f; a < MVSUITE_PI * 2.f && !bUprightWall; a += flStep)
			{
				Vec3 vEndPos = vStartPos;
				vEndPos.x += cosf(a) * flWallRange;
				vEndPos.y += sinf(a) * flWallRange;

				CGameTrace trace;
				SDK::TraceHull(vStartPos, vEndPos, vMins, vMaxs, MASK_PLAYERSOLID, &filter, &trace);

				if (trace.fraction >= 1.f || (trace.surface.flags & SURF_TRIGGER))
					continue;
				if (trace.IsDispSurface())
					continue; // displacements aren't flat walls - pixel surfs don't hold on them
				if (fabsf(trace.plane.normal.z) < flUprightEps)
					bUprightWall = true;
			}

			if (!bUprightWall)
				return; // no flat upright wall to surf - don't crouch

			// Reject inclines (ramps / stairs). A real wall pixel-surf pins you to the SIDE of an
			// upright face with nothing solid under your feet - you fall slowly alongside it. A ramp
			{
				constexpr float flFloorRange = 18.f;   // step height - stairs/ramp underfoot, but ignores distant real ground
				constexpr float flUprightEps = 0.0001f; // a vertical face below (wall continuing down) is fine - not a floor

				const Vec3 vDownStart = pLocal->GetAbsOrigin();
				const Vec3 vDownEnd = vDownStart - Vec3(0.f, 0.f, flFloorRange);
				CTraceFilterWorldAndPropsOnly downFilter;
				CGameTrace downTrace;
				SDK::TraceHull(vDownStart, vDownEnd, pLocal->m_vecMins(), pLocal->m_vecMaxs(), MASK_PLAYERSOLID, &downFilter, &downTrace);

				if (downTrace.fraction < 1.f && !(downTrace.surface.flags & SURF_TRIGGER)
					&& fabsf(downTrace.plane.normal.z) > flUprightEps)
					return; // floor/ramp/step beneath - incline, not a wall pixel surf
			}
		}

		int iBackupButtons = pCmd->buttons;
		bool bFoundPixelSurf = false;
		
		// Try standing and ducking to find pixel surf
		for (int i = 0; i < 2; i++)
		{
			F::Misc.RestoreEntityToPredicted();
			
			if (i == 0)
				pCmd->buttons &= ~IN_DUCK;
			else
				pCmd->buttons |= IN_DUCK;

			// Predict forward to find pixel surf
			for (int z = 0; z < 8; z++)
			{
				F::EnginePrediction.Simulate(pLocal, pCmd);
				
				if (pLocal->m_fFlags() & FL_ONGROUND)
					break;

				float flZVelo = pLocal->m_vecVelocity().z;
				m_bShouldPixelSurf = fabsf(flZVelo + flHalfGrav) < flSurfTol; // tick-rate-correct (was exact -6.25, never matched on TF2)
				
				if (m_bShouldPixelSurf && i == 0)
				{
					// Found pixel surf while standing - don't need to duck
					m_bShouldPixelSurf = false;
					pCmd->buttons = iBackupButtons;
					F::Misc.RestoreEntityToPredicted();
					return;
				}
				
				if (m_bShouldPixelSurf)
				{
					m_iPixelSurfTicks = I::GlobalVars->tickcount + z + 16;
					bFoundPixelSurf = true;
					// Keep the duck button that we set above (i == 1)
					break;
				}
			}
			
			if (bFoundPixelSurf)
				break;
		}
		
		// Only restore buttons if we didn't find pixel surf
		if (!bFoundPixelSurf)
		{
			pCmd->buttons = iBackupButtons;
		}
		
		F::Misc.RestoreEntityToPredicted();
	}
	else
	{
		pCmd->buttons |= IN_DUCK;

		// staying pinned must not depend on continuously holding a
		// key into the wall - releasing everything keeps you on the surf, and only actively
		if (pCmd->forwardmove == 0.f && pCmd->sidemove == 0.f)
		{
			constexpr float flHoldStep = (MVSUITE_PI * 2.f) / 16.f;
			const Vec3 vHoldStart = pLocal->GetAbsOrigin();
			CTraceFilterWorldAndPropsOnly holdFilter;
			for (float a = 0.f; a < MVSUITE_PI * 2.f; a += flHoldStep)
			{
				const Vec3 vHoldEnd = vHoldStart + Vec3(cosf(a) * 5.f, sinf(a) * 5.f, 0.f);
				CGameTrace holdTrace;
				SDK::TraceHull(vHoldStart, vHoldEnd, pLocal->m_vecMins(), pLocal->m_vecMaxs(), MASK_PLAYERSOLID, &holdFilter, &holdTrace);
				if (holdTrace.fraction >= 1.f || fabsf(holdTrace.plane.normal.z) > 0.1f
					|| (holdTrace.surface.flags & SURF_TRIGGER))
					continue;

				Vec3 vIntoAngle = Math::VectorAngles(Vec3(-holdTrace.plane.normal.x, -holdTrace.plane.normal.y, 0.f));
				Math::ClampAngles(vIntoAngle);
				const float flHoldRot = MVSUPE_DEG2RAD(vIntoAngle.y - pCmd->viewangles.y);
				pCmd->forwardmove = cosf(flHoldRot) * 10.f;
				pCmd->sidemove = -sinf(flHoldRot) * 10.f;
				break;
			}
		}

		if (I::GlobalVars->tickcount > m_iPixelSurfTicks)
		{
			if (fabsf(pLocal->m_vecVelocity().z + flHalfGrav) >= flSurfTol) // surf lost (was exact -6.25, never matched on TF2)
				m_bShouldPixelSurf = false;
		}
	}
}


void CMisc::TB_RefreshWallCache(CTFPlayer* pLocal)
{
    if (m_TB_WallCache.valid)
        return;
 
    const Vec3 vPos  = pLocal->GetAbsOrigin();
    const Vec3 vMins = pLocal->m_vecMins();
    const Vec3 vMaxs = pLocal->m_vecMaxs();
    constexpr float flStep = (MVSUITE_PI * 2.f) / 16.f;
    const float flScanStart = (m_flTBCachedWallAngle >= 0.f) ? m_flTBCachedWallAngle : 0.f;
 
    CGameTrace trace;
    float foundAngle = 0.f;
    m_TB_WallCache.valid = TB_DetectWall(vPos, vMins, vMaxs, flStep, trace, foundAngle, flScanStart);
    if (m_TB_WallCache.valid)
    {
        m_TB_WallCache.trace      = trace;
        m_TB_WallCache.foundAngle = foundAngle;
        m_flTBCachedWallAngle     = foundAngle;
    }
    else
    {
        m_flTBCachedWallAngle = -1.f;
    }
}


void CMisc::TextureBug(CTFPlayer* pLocal, CUserCmd* pCmd)
{

	const bool caught_last_tick = m_bTextureBugHit; // last tick's result (fast-path + coast gate)
	m_bTextureBugHit = false;
	m_bTBOwnsCmd = false;
	// Nothing rides a persistent lock any more - clear stale lock state so nothing downstream thinks
	// we are mid-ride.
	m_bTBLocked = false; m_bTBArrestLock = false; m_iTBPendingCatchTicks = 0;

	// consecutive hit ticks - the coast gate: only established rides may coast through misses
	static int ride_ticks = 0;
	ride_ticks = caught_last_tick ? ride_ticks + 1 : 0;

	// ride fast path: last committed plain catch (world yaw + forward + duck), retested first
	static float fast_yaw  = 0.f;
	static float fast_fwd  = 0.f;
	static bool  fast_duck = false;

	// duck state of the last commit - flips only when the current state loses the pin (flapping the
	// hull every few ticks made rides jitter and randomly crouch/uncrouch).
	static bool  ride_duck = false;

	static bool  duck_extension_pending      = false;
	static bool  duck_extension_holding      = false;
	static int   duck_extension_wait_ticks   = 0;
	static float duck_extension_yaw          = 0.f;
	static float duck_extension_forward_move = 0.f;
	static float duck_extension_side_move    = 0.f;
	static int   duck_extension_stand_buttons= 0;
	static int   duck_extension_duck_buttons = 0;

	// ride hold (coyote time): last catch move in the wall frame, replayed when the wall scan drops
	// out at a brush seam so the ride carries across to the next surface.
	static int   ride_hold_ticks_left    = 0;
	static float ride_hold_yaw           = 0.f;
	static float ride_hold_forward_move  = 0.f;
	static float ride_hold_side_move     = 0.f;
	static int   ride_hold_buttons       = 0;

	// edge stop: brake/hold at the end of a surf instead of sliding off
	static bool  edge_braking = false;
	if (!caught_last_tick) edge_braking = false;

	// creep lock: the seam look-ahead saw a pin k ticks out on the current creep move - replay it
	// verbatim until then instead of re-ranking every tick.
	static int   creep_lock_ticks   = 0;
	static float creep_lock_yaw     = 0.f;
	static float creep_lock_fwd     = 0.f;
	static int   creep_lock_buttons = 0;
	if (caught_last_tick) creep_lock_ticks = 0; // riding - the lock is approach-only

	const auto reset_duck_extension = [&]() noexcept
	{
		duck_extension_pending = false;
		duck_extension_holding = false;
		duck_extension_wait_ticks = 0;
		duck_extension_yaw = 0.f;
		duck_extension_forward_move = 0.f;
		duck_extension_side_move = 0.f;
		duck_extension_stand_buttons = 0;
		duck_extension_duck_buttons = 0;
	};

	// Set the hit flag and arm the choke-tick option on a real catch so the choke sliders keep working.
	const auto SetHit = [&](bool b)
	{
		m_bTextureBugHit = b;
		if (b && Vars::Misc::Movement::TextureBugChokeTick.Value && m_iTBChokeTicksLeft <= 0)
			m_iTBChokeTicksLeft = std::clamp(Vars::Misc::Movement::TextureBugChokeTicks.Value, 1, 14);
	};

	if (!Vars::Misc::Movement::TextureBug.Value)
		{ reset_duck_extension(); ride_hold_ticks_left = 0; creep_lock_ticks = 0; return; }
	if (!pLocal || !pLocal->IsAlive())
		{ reset_duck_extension(); ride_hold_ticks_left = 0; creep_lock_ticks = 0; return; }

	const int iMoveType = pLocal->m_MoveType();
	if (iMoveType == MOVETYPE_LADDER || iMoveType == MOVETYPE_NOCLIP
		|| iMoveType == MOVETYPE_FLY || iMoveType == MOVETYPE_OBSERVER)
		{ reset_duck_extension(); ride_hold_ticks_left = 0; creep_lock_ticks = 0; return; }

	const bool auto_crouch = Vars::Misc::Movement::TextureBugAutoCrouch.Value;
	const bool edge_stop   = Vars::Misc::Movement::TextureBugEdgeStop.Value;
	const int  hold_ticks  = std::clamp(Vars::Misc::Movement::TextureBugHoldTicks.Value, 0, 16);

	const int   original_buttons      = pCmd->buttons;
	const float original_forward_move = pCmd->forwardmove;
	const float original_side_move    = pCmd->sidemove;
	const bool  local_on_ground       = (pLocal->m_fFlags() & FL_ONGROUND) != 0;
	const bool  user_holding_jump     = (original_buttons & IN_JUMP) != 0;
	const bool  user_has_manual_movement_input = fabsf(original_forward_move) > 0.01f || fabsf(original_side_move) > 0.01f;

	if (local_on_ground && !user_holding_jump)
		{ reset_duck_extension(); ride_hold_ticks_left = 0; creep_lock_ticks = 0; return; }

	// half-gravity pin velocity (the "landed on a surface this tick" z-vel), cached for the sweep
	const float flTargetVz  = F::EnginePrediction.GetTargetPredictZVelocity();
	const float k_catch_eps = Vars::Misc::Movement::TextureBugCatchEps.Value;
	// a catch pins vz to -half; the +half sign is a "snapped onto a top surface" hit (tighter band)
	const auto is_pinned = [&](float vz, float eps) noexcept -> bool
	{
		return fabsf(vz - flTargetVz) <= eps || fabsf(vz + flTargetVz) <= std::min(eps, 0.35f);
	};

	// hard Simulate budget - the sweep can fan out to hundreds of sims in the worst tick
	int tb_sims = 0;
	constexpr int k_tb_sim_cap = 384;
	const auto sim      = [&](CUserCmd* c) { ++tb_sims; F::EnginePrediction.Simulate(pLocal, c); };
	const auto sims_left = [&]() noexcept { return k_tb_sim_cap - tb_sims; };

	// move expressed in the WALL frame -> the player command frame (cmd forward/side)
	const auto wall_to_player_fwd = [&](float wall_yaw_deg, float fwd, float side) noexcept -> float
	{
		const float delta = MVSUPE_DEG2RAD(Math::NormalizeAngle(wall_yaw_deg - pCmd->viewangles.y));
		return fwd * cosf(delta) + side * sinf(delta);
	};
	const auto wall_to_player_side = [&](float wall_yaw_deg, float fwd, float side) noexcept -> float
	{
		const float delta = MVSUPE_DEG2RAD(Math::NormalizeAngle(wall_yaw_deg - pCmd->viewangles.y));
		return -fwd * sinf(delta) + side * cosf(delta);
	};

	// arm the ride hold with the move that caught this tick (also the single place the ride duck
	// state is updated - every commit path passes through here)
	const auto arm_ride_hold = [&](float yaw, float fwd, float side, int buttons)
	{
		ride_duck = (buttons & IN_DUCK) != 0;
		if (hold_ticks <= 0) { ride_hold_ticks_left = 0; return; }
		ride_hold_ticks_left   = hold_ticks;
		ride_hold_yaw          = yaw;
		ride_hold_forward_move = fwd;
		ride_hold_side_move    = side;
		ride_hold_buttons      = buttons;
	};

	const Vec3 vLiveVelocity = pLocal->m_vecVelocity();

	// HeadSurf runs after us, so m_bHeadSurfHit still holds last tick's result here. If an
	// established head surf is still pinned (+half) it re-locks this tick - a wall commit would
	if (m_bHeadSurfHit && fabsf(vLiveVelocity.z + flTargetVz) <= k_catch_eps)
	{
		reset_duck_extension();
		return;
	}

	constexpr float k_pi   = 3.14159265358979323846f;
	const float max_radius = k_pi * 2.f;
	const float step       = max_radius / 16.f;
	const Vec3  start_pos  = pLocal->GetAbsOrigin();
	const Vec3  mins       = pLocal->m_vecMins();
	const Vec3  maxs       = pLocal->m_vecMaxs();
	static float start_circle = 0.f;
	bool wall_detected = false;
	CGameTrace primary;

	// per-Z line-trace scan across 16 angles for the closest near-vertical, player-facing wall.
	// hull_only = probe mode: steering only needs the yaw, so the hull normal is enough and we skip
	const auto detect_wall = [&](const Vec3& sp, const Vec3& mn, const Vec3& mx, float scan_start,
	                             CGameTrace& out_trace, float& out_angle, bool hull_only = false) -> bool
	{
		const float extra_reach          = Vars::Misc::Movement::TextureBugReach.Value;
		const float z_step               = Vars::Misc::Movement::TextureBugScanStep.Value > 0.5f ? Vars::Misc::Movement::TextureBugScanStep.Value : 1.f;
		constexpr float normal_z_epsilon = 0.05f;
		const float half_x = std::max(fabsf(mn.x), fabsf(mx.x));
		const float half_y = std::max(fabsf(mn.y), fabsf(mx.y));
		CTraceFilterWorldAndPropsOnly fil;

		for (float a = scan_start; a < scan_start + max_radius; a += step)
		{
			const float angle = fmodf(a, max_radius);
			const float dir_x = cosf(angle);
			const float dir_y = sinf(angle);
			const float ax = std::max(fabsf(dir_x), 1e-6f);
			const float ay = std::max(fabsf(dir_y), 1e-6f);
			const float box_reach  = std::min(half_x / ax, half_y / ay);
			const float ray_length = box_reach + extra_reach;
			const float z_start = sp.z + mn.z;
			const float z_end   = sp.z + mx.z;

			// hull prefilter: one sweep answers "anything at this angle?" before the ~40 per-Z lines
			CGameTrace pre;
			Ray_t pray; pray.Init(sp, Vec3(sp.x + dir_x * ray_length, sp.y + dir_y * ray_length, sp.z), mn, mx);
			I::EngineTrace->TraceRay(pray, MASK_PLAYERSOLID, &fil, &pre);
			if (!pre.DidHit())
				continue;

			if (hull_only)
			{
				if (fabsf(pre.plane.normal.z) <= normal_z_epsilon)
				{
					const float pre_facing = pre.plane.normal.x * dir_x + pre.plane.normal.y * dir_y;
					if (pre_facing <= -0.1f) { out_trace = pre; out_angle = a; return true; }
				}
				continue;
			}

			CGameTrace best_trace; best_trace.fraction = 1.f;
			bool hit_at_this_angle = false;
			for (float z = z_start; z <= z_end; z += z_step)
			{
				const Vec3 ray_start(sp.x, sp.y, z);
				const Vec3 ray_end(sp.x + dir_x * ray_length, sp.y + dir_y * ray_length, z);
				Ray_t ray; ray.Init(ray_start, ray_end, Vec3(0.f, 0.f, 0.f), Vec3(0.f, 0.f, 0.f));
				CGameTrace tr;
				I::EngineTrace->TraceRay(ray, MASK_PLAYERSOLID, &fil, &tr);
				if (tr.fraction >= 1.f) continue;
				if (fabsf(tr.plane.normal.z) > normal_z_epsilon) continue;
				const float facing = tr.plane.normal.x * dir_x + tr.plane.normal.y * dir_y;
				if (facing > -0.1f) continue;
				hit_at_this_angle = true;
				if (tr.fraction < best_trace.fraction) best_trace = tr;
			}
			if (hit_at_this_angle) { out_trace = best_trace; out_angle = a; return true; }

			// short wall that slipped between the scan lines: the line grid missed but the hull hit a
			// near-vertical facing wall - take it.
			if (fabsf(pre.plane.normal.z) <= normal_z_epsilon)
			{
				const float pre_facing = pre.plane.normal.x * dir_x + pre.plane.normal.y * dir_y;
				if (pre_facing <= -0.1f) { out_trace = pre; out_angle = a; return true; }
			}
		}
		return false;
	};

	float found_angle = 0.f;
	if (detect_wall(start_pos, mins, maxs, start_circle, primary, found_angle))
	{
		wall_detected = true;
		start_circle  = found_angle;
	}
	if (!wall_detected)
	{
		start_circle = 0.f;
		reset_duck_extension();
		creep_lock_ticks = 0; // the locked move is wall-frame - wall gone, lock is dead
		// ride hold: scan gap at a brush seam - replay the last catch move so the ride carries over.
		if (ride_hold_ticks_left > 0)
		{
			--ride_hold_ticks_left;
			SetHit(true);
			pCmd->forwardmove = wall_to_player_fwd(ride_hold_yaw, ride_hold_forward_move, ride_hold_side_move);
			pCmd->sidemove    = wall_to_player_side(ride_hold_yaw, ride_hold_forward_move, ride_hold_side_move);
			pCmd->buttons     = ride_hold_buttons;
		}
		// live pin but no wall in the scan grid (flush overlap / geometry the angle grid misses):
		// a catch is happening right now, so hold it instead of dropping the cmd. Try the user's own
		else if (is_pinned(vLiveVelocity.z, k_catch_eps))
		{
			const int nBaseNW = I::Prediction->m_nCommandsPredicted;
			bool held = false;
			for (int i = 0; i < 2 && !held; ++i)
			{
				if (i == 0 && !user_has_manual_movement_input) continue;
				I::Prediction->RestoreEntityToPredictedFrame(nBaseNW - 1);
				pLocal->SetAbsOrigin(start_pos);
				pLocal->m_vecOrigin()   = start_pos;
				pLocal->m_vecVelocity() = vLiveVelocity;
				CUserCmd test = *pCmd;
				if (i == 1) { test.forwardmove = 0.f; test.sidemove = 0.f; }
				sim(&test);
				if (is_pinned(pLocal->m_vecVelocity().z, k_catch_eps))
				{
					SetHit(true);
					pCmd->forwardmove = test.forwardmove;
					pCmd->sidemove    = test.sidemove;
					held = true;
				}
			}
			I::Prediction->RestoreEntityToPredictedFrame(nBaseNW - 1);
		}
		// look-ahead: wall on the fall path but not beside us yet -> steer now so we arrive flush.
		// Only when hands-off and edge stop is on (this is a manufactured approach, the grabby part).
		else if (!local_on_ground && !user_has_manual_movement_input && edge_stop)
		{
			const float dt = I::GlobalVars->interval_per_tick;
			// probe at a fixed fall DISTANCE (not a fixed tick count - a fixed count overshoots small
			// ledges at high fall speed). Cycle three distances so tiny ledges and fast falls both get
			const float fall_speed = std::max(-vLiveVelocity.z, 50.f);
			const float per_tick   = fall_speed * dt;
			const int   di         = pCmd->command_number % 3;
			const float dist = di == 0 ? 12.f
			                 : (di == 1 ? std::max(26.f, per_tick * 6.f)
			                            : std::max(45.f, per_tick * 11.f));
			float t = dist / fall_speed;
			t = dist / (fall_speed + 400.f * t);
			t = std::min(std::max(t / dt, 2.f), 12.f) * dt;
			Vec3 probe = start_pos + vLiveVelocity * t;
			probe.z -= 400.f * t * t; // 0.5 * g * t^2
			CGameTrace ahead; float ahead_angle = 0.f;
			if (detect_wall(probe, mins, maxs, 0.f, ahead, ahead_angle, true))
			{
				Vec3 ahead_wall = Math::VectorAngles(ahead.plane.normal * -1.f);
				Math::ClampAngles(ahead_wall);
				pCmd->forwardmove = wall_to_player_fwd(ahead_wall.y, 450.f, 0.f);
				pCmd->sidemove    = wall_to_player_side(ahead_wall.y, 450.f, 0.f);
				m_bTBOwnsCmd = true;
			}
		}
		return;
	}

	// pin the base prediction frame; every probe restores to it (Simulate drifts m_nCommandsPredicted
	// forward, so a bare RestoreEntityToPredicted would creep). RAII guard restores on every exit.
	const int nBasePredicted = I::Prediction->m_nCommandsPredicted;
	const auto RestoreOriginal = [&]() { I::Prediction->RestoreEntityToPredictedFrame(nBasePredicted - 1); };
	struct StateGuard { int base; ~StateGuard() { I::Prediction->RestoreEntityToPredictedFrame(base - 1); } } prediction_state_guard{ nBasePredicted };

	const auto apply_predicted_state = [&]() noexcept
	{
		pLocal->SetAbsOrigin(start_pos);
		pLocal->m_vecOrigin()   = start_pos;
		pLocal->m_vecVelocity() = vLiveVelocity;
	};

	const Vec3 approach_normal = primary.plane.normal * -1.f;
	Vec3 angle_wall = Math::VectorAngles(approach_normal);
	Math::ClampAngles(angle_wall);

	// z of the wall face for the support line.
	float support_z = primary.endpos.z;
	{
		CTraceFilterWorldAndPropsOnly zfil;
		const Vec3 into = primary.plane.normal * -1.f * 32.f;
		float best_fr = 2.f; // > 1 so the primary z wins only by actually hitting
		const float z_cands[6] = { primary.endpos.z,
		                           start_pos.z + mins.z + 2.f,  start_pos.z + mins.z + 20.f,
		                           start_pos.z + mins.z + 41.f, start_pos.z + mins.z + 62.f,
		                           start_pos.z + maxs.z - 2.f };
		for (float zc : z_cands)
		{
			Ray_t ray; ray.Init(Vec3(start_pos.x, start_pos.y, zc),
			                    Vec3(start_pos.x + into.x, start_pos.y + into.y, zc),
			                    Vec3(0.f, 0.f, 0.f), Vec3(0.f, 0.f, 0.f));
			CGameTrace tr;
			I::EngineTrace->TraceRay(ray, MASK_ALL, &zfil, &tr);
			if (tr.fraction < 1.f && tr.fraction < best_fr && fabsf(tr.plane.normal.z) <= 0.05f)
				{ best_fr = tr.fraction; support_z = zc; }
		}
	}

	// support gap: a line trace along -normal minus the hull's projected extent
	const auto get_support_gap = [&]() -> float
	{
		const Vec3 minusplane = primary.plane.normal * -1.f * 32.f;
		const Vec3 origin = pLocal->GetAbsOrigin();
		Ray_t ray; ray.Init(Vec3(origin.x, origin.y, support_z),
		                    Vec3(origin.x + minusplane.x, origin.y + minusplane.y, support_z),
		                    Vec3(0.f, 0.f, 0.f), Vec3(0.f, 0.f, 0.f));
		CTraceFilterWorldAndPropsOnly wlft;
		CGameTrace trace_support;
		I::EngineTrace->TraceRay(ray, MASK_ALL, &wlft, &trace_support);
		const Vec3 wall_normal(primary.plane.normal.x, primary.plane.normal.y, 0.f);
		return trace_support.fraction * 32.f - TB_WallSupportDistance(pLocal, wall_normal);
	};

	const float live_gap = get_support_gap(); // gap before any sim (entity is still live here)

	float gap               = 1.f;
	float best_forward_move = pCmd->forwardmove;
	float best_side_move    = pCmd->sidemove;
	float best_yaw          = angle_wall.y;
	int   best_buttos       = pCmd->buttons;
	int   best_hit_streak   = 0;
	int   best_sustain      = -1; // sim look-ahead streak of the best pin so far (the ranker)
	bool  best_is_duck_extension = false;
	int   best_stand_buttons = pCmd->buttons;
	int   best_duck_buttons  = pCmd->buttons | IN_DUCK;
	const bool manual_duck_held = (original_buttons & IN_DUCK) != 0;

	const float vel_yaw = MVSUPE_RAD2DEG(atan2f(vLiveVelocity.y, vLiveVelocity.x));
	const float rel     = Math::NormalizeAngle(vel_yaw - angle_wall.y);
	const float sign    = (rel >= 0.f) ? -1.f : 1.f;

	static constexpr float k_forward_move_logged_values[] = { 0.125f, 0.875f, 0.750f, 0.5f, 1.f, 2.125f, 2.875f, 5.f, 15.f, 30.f };
	constexpr float k_duck_extension_gap_tolerance = 0.05f;
	constexpr int   k_duck_extension_delay_ticks       = 0;

	// swap a move that predicts an air-stuck (2D speed < 1) for its reverse if that keeps more speed
	const auto fix_air_stuck_move = [&]() noexcept
	{
		const float inward_fwd  = pCmd->forwardmove;
		const float inward_side = pCmd->sidemove;
		if (fabsf(inward_fwd) < 0.01f && fabsf(inward_side) < 0.01f)
			return;

		RestoreOriginal();
		apply_predicted_state();

		CUserCmd test_cmd = *pCmd;
		sim(&test_cmd);
		const float inward_speed = pLocal->m_vecVelocity().Length2D();
		if (inward_speed >= 1.f) { RestoreOriginal(); apply_predicted_state(); return; }

		RestoreOriginal();
		apply_predicted_state();
		test_cmd = *pCmd;
		test_cmd.forwardmove = -inward_fwd;
		test_cmd.sidemove    = -inward_side;
		sim(&test_cmd);
		const float outward_speed = pLocal->m_vecVelocity().Length2D();
		// a committed pin has to survive the swap - reversing a stationary hold's wish can drift past
		// the eps band and drop the surf.
		const bool outward_pin = is_pinned(pLocal->m_vecVelocity().z, k_catch_eps);
		RestoreOriginal();
		apply_predicted_state();
		if (outward_speed > inward_speed + 0.01f && (!m_bTextureBugHit || outward_pin))
		{
			pCmd->forwardmove = -inward_fwd;
			pCmd->sidemove    = -inward_side;
		}
	};

	// edge stop: the surf ends along the ride direction -> brake so we stop on the brush instead of
	// sliding off. Brake rate = airaccel 10 * wishcap 30 = 300 u/s^2, so stop dist = v^2/600. Runs
	const auto apply_edge_stop = [&]()
	{
		if (!edge_stop)
			return;
		// near-zero speed the sweep can miss the pin (sims < 1 u/s are rejected) - a live pin plus the
		// brake state keeps the governor owning the cmd so the hold does not drift off the edge.
		const bool live_pinned = is_pinned(vLiveVelocity.z, k_catch_eps);
		if (!m_bTextureBugHit && !(edge_braking && live_pinned))
			return;

		const auto wall_at = [&](const Vec3& p) -> bool
		{
			const Vec3 into_wall = primary.plane.normal * -32.f;
			CTraceFilterWorldAndPropsOnly efil;
			Ray_t ray; ray.Init(Vec3(p.x, p.y, support_z),
			                    Vec3(p.x + into_wall.x, p.y + into_wall.y, support_z),
			                    Vec3(0.f, 0.f, 0.f), Vec3(0.f, 0.f, 0.f));
			CGameTrace tr;
			I::EngineTrace->TraceRay(ray, MASK_PLAYERSOLID, &efil, &tr);
			return tr.fraction < 1.f && fabsf(tr.plane.normal.z) <= 0.05f;
		};

		const auto move_keeps_pin = [&](float fwd, float side) -> bool
		{
			CUserCmd test = *pCmd;
			test.forwardmove = fwd;
			test.sidemove    = side;
			RestoreOriginal(); apply_predicted_state();
			sim(&test);
			const bool pinned = is_pinned(pLocal->m_vecVelocity().z, k_catch_eps);
			RestoreOriginal(); apply_predicted_state();
			return pinned;
		};

		const float speed_2d = vLiveVelocity.Length2D();

		if (speed_2d < 1.f)
		{
			// stopped at the edge: hold with a zero wish while the pin lasts
			if (!edge_braking)
				return;
			if (!move_keeps_pin(0.f, 0.f))
				return; // zero wish drops the pin -> the picked ride move stands
			pCmd->forwardmove = 0.f;
			pCmd->sidemove    = 0.f;
			SetHit(true);
			return;
		}

		const Vec3 ride_dir(vLiveVelocity.x / speed_2d, vLiveVelocity.y / speed_2d, 0.f);
		const float stop_dist = speed_2d * speed_2d / 600.f + speed_2d * I::GlobalVars->interval_per_tick * 3.f + 2.f;
		if (wall_at(start_pos + ride_dir * stop_dist))
		{
			edge_braking = false; // the surf continues past the stopping distance
			return;
		}

		// the edge is inside the stopping distance -> full brake against horizontal velocity
		const float brake_yaw  = MVSUPE_RAD2DEG(atan2f(-vLiveVelocity.y, -vLiveVelocity.x));
		const float brake_fwd  = wall_to_player_fwd(brake_yaw, 450.f, 0.f);
		const float brake_side = wall_to_player_side(brake_yaw, 450.f, 0.f);
		if (!move_keeps_pin(brake_fwd, brake_side))
			return; // the brake drops the pin -> the ride move stands
		edge_braking = true;
		pCmd->forwardmove = brake_fwd;
		pCmd->sidemove    = brake_side;
		SetHit(true);
	};

	const auto simulate_variant = [&](float wall_yaw, float forward_move, bool force_duck,
	                                  float& out_gap, int& out_hit_streak, int& out_buttons, bool& out_is_headbounce,
	                                  bool allow_retry) -> bool
	{
		// reject defaults FIRST - an early return must not leave the caller's 0.0 in out_gap (a 0.0
		// wins the no-catch ranking and fakes the duck near-miss gate = a dead move gets committed).
		out_gap = FLT_MAX;
		out_hit_streak = 0;
		out_is_headbounce = false;

		RestoreOriginal();
		apply_predicted_state();

		pCmd->buttons = original_buttons;
		if (force_duck) pCmd->buttons |= IN_DUCK;
		pCmd->forwardmove = wall_to_player_fwd(wall_yaw, forward_move, 0.f);
		pCmd->sidemove    = wall_to_player_side(wall_yaw, forward_move, 0.f);

		const float pre_vel_z = pLocal->m_vecVelocity().z;

		sim(pCmd);

		float post_vel_z = pLocal->m_vecVelocity().z;
		const float predicted_speed_2d = pLocal->m_vecVelocity().Length2D();
		bool pinned = is_pinned(post_vel_z, k_catch_eps);
		// the dead-wedge guard only applies to NON-pins: a pinned stationary hold is a valid catch
		// (edge stop holds exactly this state).
		if (!pinned && predicted_speed_2d < 1.f)
			return false;
		out_gap = get_support_gap();
		bool delayed = false;
		if (allow_retry && !pinned && pre_vel_z < -1.0f)
		{
			// fast fall: the pin often lands one tick late - but retest only when the eps band is
			// reachable next tick, otherwise a blind retry doubles every failing sweep sim.
			const Vec3 v_post = pLocal->m_vecVelocity();
			const float v_in_post = std::max(0.f, v_post.x * approach_normal.x + v_post.y * approach_normal.y);
			if (out_gap <= 0.03125f + (v_in_post + 30.f) * I::GlobalVars->interval_per_tick)
			{
				sim(pCmd);
				post_vel_z = pLocal->m_vecVelocity().z;
				pinned = is_pinned(post_vel_z, k_catch_eps);
				delayed = pinned;
			}
		}

		if (pinned)
		{
			// first pin wins - ride depth comes from the fast path + ride hold, not a per-variant loop
			out_hit_streak = 1;
			// were rising and got pinned tight = the engine snapped us onto a top surface
			if (!delayed && pre_vel_z > 0.0f && F::EnginePrediction.IsTargetPredictZVelocity(post_vel_z, 0.1f))
				out_is_headbounce = true;
		}
		out_buttons = pCmd->buttons;
		return true;
	};

	// duck-extension replay owns the cmd - the old approach ran the full sweep first and threw it away
	if (!manual_duck_held && (duck_extension_holding || duck_extension_pending))
	{
		const bool started_falling_from_stand = !F::EnginePrediction.IsTargetPredictZVelocity(vLiveVelocity.z, k_catch_eps) && vLiveVelocity.z < -1.0f;
		SetHit(true);
		pCmd->forwardmove = wall_to_player_fwd(duck_extension_yaw, duck_extension_forward_move, duck_extension_side_move);
		pCmd->sidemove    = wall_to_player_side(duck_extension_yaw, duck_extension_forward_move, duck_extension_side_move);
		if (duck_extension_holding)
		{
			pCmd->buttons = started_falling_from_stand ? duck_extension_stand_buttons : duck_extension_duck_buttons;
			fix_air_stuck_move();
			arm_ride_hold(duck_extension_yaw, duck_extension_forward_move, duck_extension_side_move, pCmd->buttons);
			if (started_falling_from_stand) reset_duck_extension();
		}
		else
		{
			pCmd->buttons = duck_extension_stand_buttons;
			fix_air_stuck_move();
			arm_ride_hold(duck_extension_yaw, duck_extension_forward_move, duck_extension_side_move, pCmd->buttons);
			if (duck_extension_wait_ticks > 0) --duck_extension_wait_ticks;
			else { duck_extension_pending = false; duck_extension_holding = true; }
		}
		apply_edge_stop();
		return;
	}

	// live stick: the live vz is already pinned = a texture bug is in progress right now, however it
	// started (own jump, any combo, no combo). Hold it: the user's cmd if it sustains, else the tiny
	{
		const bool live_pin_now = F::EnginePrediction.IsTargetPredictZVelocity(vLiveVelocity.z, k_catch_eps)
		                       || fabsf(vLiveVelocity.z + flTargetVz) <= k_catch_eps;
		if (live_pin_now)
		{
			struct StickTry { float fwd; int buttons; bool user_cmd; };
			StickTry tries[5]; int n_tries = 0;
			// mid-ride: keep the current duck state on every try. A duck flip is a fresh-catch thing
			// only; flapping the hull mid-surf jittered rides.
			const bool riding = caught_last_tick;
			const int  duck_keep = (riding && ride_duck && auto_crouch && !manual_duck_held) ? IN_DUCK : 0;
			if (user_has_manual_movement_input)
				tries[n_tries++] = { 0.f, original_buttons | duck_keep, true };
			if (duck_keep)
				tries[n_tries++] = { 0.1f, original_buttons | IN_DUCK, false };
			tries[n_tries++] = { 0.1f, original_buttons, false };
			if (!duck_keep && !riding && !manual_duck_held && auto_crouch)
				tries[n_tries++] = { 0.1f, original_buttons | IN_DUCK, false };
			tries[n_tries++] = { 0.f, original_buttons | duck_keep, false };
			for (int i = 0; i < n_tries; ++i)
			{
				RestoreOriginal();
				apply_predicted_state();
				CUserCmd test = *pCmd;
				test.buttons = tries[i].buttons;
				if (!tries[i].user_cmd)
				{
					test.forwardmove = wall_to_player_fwd(angle_wall.y, tries[i].fwd, 0.f);
					test.sidemove    = wall_to_player_side(angle_wall.y, tries[i].fwd, 0.f);
				}
				sim(&test);
				const bool pin = is_pinned(pLocal->m_vecVelocity().z, k_catch_eps);
				RestoreOriginal();
				apply_predicted_state();
				if (!pin) continue;
				SetHit(true);
				reset_duck_extension();
				pCmd->forwardmove = test.forwardmove;
				pCmd->sidemove    = test.sidemove;
				pCmd->buttons     = test.buttons;
				if (tries[i].user_cmd)
				{
					fast_fwd = 0.f; // the user owns this ride
					arm_ride_hold(pCmd->viewangles.y, original_forward_move, original_side_move, test.buttons);
				}
				else
				{
					if (tries[i].fwd > 0.f)
					{
						fast_yaw  = angle_wall.y;
						fast_fwd  = tries[i].fwd;
						fast_duck = (test.buttons & IN_DUCK) != 0;
					}
					arm_ride_hold(angle_wall.y, tries[i].fwd, 0.f, test.buttons);
				}
				apply_edge_stop();
				return;
			}
		}
	}

	// natural hit: the user's own cmd already pins -> keep it, no bruteforce. Manual input only -
	// hands-off pins go to the sweep, which verifies durability; a 1-tick natural fluke here poisons
	if (!caught_last_tick && user_has_manual_movement_input)
	{
		RestoreOriginal();
		apply_predicted_state();
		CUserCmd natural = *pCmd;
		sim(&natural);
		const bool nat_pin = is_pinned(pLocal->m_vecVelocity().z, k_catch_eps);
		RestoreOriginal();
		apply_predicted_state();
		if (nat_pin)
		{
			SetHit(true);
			reset_duck_extension();
			fast_fwd = 0.f; // the user owns this ride - don't chase a stale sweep move next tick
			arm_ride_hold(pCmd->viewangles.y, original_forward_move, original_side_move, original_buttons);
			apply_edge_stop();
			return;
		}
	}

	// ride fast path: the last winning move still pins -> commit it and skip the sweep. As the band
	// drifts mid-ride: exact retest first, then a 2-tick retest (delayed pin), then small yaw nudges.
	if (fast_fwd > 0.f && caught_last_tick && !manual_duck_held)
	{
		static constexpr float k_fp_nudges[] = { 0.f, 0.f, -0.5f, 0.5f, -1.f, 1.f };
		const int n_fp_slots = (ride_ticks >= 2) ? 6 : 2; // nudge-chase only real rides
		for (int ni = 0; ni < n_fp_slots; ++ni)
		{
			const bool fp_retry = (ni == 1); // slot 1 = same yaw, 2-tick retest
			float fp_gap = 0.f; int fp_streak = 0; int fp_buttons = original_buttons; bool fp_hb = false;
			simulate_variant(fast_yaw + k_fp_nudges[ni], fast_fwd, fast_duck,
			                 fp_gap, fp_streak, fp_buttons, fp_hb, fp_retry);
			if (fp_streak > 0)
			{
				fast_yaw += k_fp_nudges[ni];
				SetHit(true);
				pCmd->forwardmove = wall_to_player_fwd(fast_yaw, fast_fwd, 0.f);
				pCmd->sidemove    = wall_to_player_side(fast_yaw, fast_fwd, 0.f);
				pCmd->buttons     = fp_buttons;
				fix_air_stuck_move();
				arm_ride_hold(fast_yaw, fast_fwd, 0.f, pCmd->buttons);
				apply_edge_stop();
				return;
			}
		}
	}

	// (mul x ang) grid, deduped by the effective wish against the wall: inward = mul*cos(ang) drives
	// the band stepping, along = mul*sin(ang) drives ride accel. Low-mul rows are near-identical
	struct TbVariant { float ang, mul; };
	static const auto build_variants = [](TbVariant* out, int cap, float ang_start, float ang_end, float ang_step) -> int
	{
		int n = 0, n_seen = 0;
		long long seen[96];
		for (float mul : k_forward_move_logged_values)
			for (float ang = ang_start; ang < ang_end && n < cap; ang += ang_step)
			{
				const float r = MVSUPE_DEG2RAD(ang);
				const long long key = (long long)floorf(mul * cosf(r) / 0.03f) * 4096
				                    + (long long)floorf(mul * sinf(r) / 0.25f);
				bool dup = false;
				for (int i = 0; i < n_seen; i++) if (seen[i] == key) { dup = true; break; }
				if (dup) continue;
				if (n_seen < 96) seen[n_seen++] = key;
				out[n++] = { ang, mul };
			}
		return n;
	};
	static TbVariant s_main_vars[96], s_wig_vars[48];
	static const int s_n_main = build_variants(s_main_vars, 96, 85.999f, 90.f, 0.5f);
	static const int s_n_wig  = build_variants(s_wig_vars,  48, 90.5f, 94.f, 1.f);

	// streak-ranked picker: one sim per variant, but a pinning move is scored by how many consecutive
	// ticks it re-clips the seam in sim (each caught tick ends vz = -half, so sustain = a move whose
	static constexpr int k_streak_depth = 12; // sustain look-ahead ticks (tuning lever)
	static constexpr int k_early_accept = 12; // a full-depth streak stops the sweep - this is the ride
	float last_eval_gap = FLT_MAX;
	const auto eval_variant = [&](float current_yaw, float mul, bool force_duck, bool allow_retry) -> bool
	{
		float p = FLT_MAX; int streak = 0; int btn = original_buttons; bool hb = false;
		const bool ok = simulate_variant(current_yaw, mul, force_duck, p, streak, btn, hb, allow_retry);
		last_eval_gap = ok ? p : FLT_MAX;
		if (streak > 0)
		{
			// count consecutive sustain ticks (state + pCmd continue from the simulate)
			int sustain = 0;
			for (int t = 0; t < k_streak_depth && sims_left() > 0; ++t)
			{
				sim(pCmd);
				if (!is_pinned(pLocal->m_vecVelocity().z, k_catch_eps))
					break;
				++sustain;
			}
			// rank: longer sustain wins; equal sustain -> tighter gap
			if (sustain > best_sustain || (sustain == best_sustain && p < gap))
			{
				best_sustain       = sustain;
				gap                = p;
				best_hit_streak    = 1;
				best_forward_move  = mul;
				best_side_move     = 0.f;
				best_yaw           = current_yaw;
				best_buttos        = btn;
				best_stand_buttons = btn & ~IN_DUCK;
				best_duck_buttons  = btn | IN_DUCK;
			}
			return sustain >= k_early_accept; // fully sustaining -> abort the sweep
		}
		if (ok && !force_duck && best_hit_streak == 0 && p < gap)
		{
			gap                = p;
			best_forward_move  = mul;
			best_side_move     = 0.f;
			best_yaw           = current_yaw;
			best_buttos        = btn;
			best_stand_buttons = btn;
			best_duck_buttons  = btn | IN_DUCK;
		}
		return false;
	};

	// a catch needs the perpendicular speed < eps; the sweep pins within 2 sim ticks max, so gate on
	// the closable distance. Closing speed = existing velocity into the wall (uncapped) + accel cap 30.
	const float v_toward_wall = std::max(0.f, vLiveVelocity.x * approach_normal.x + vLiveVelocity.y * approach_normal.y);
	const bool sweep_viable = live_gap <= 1.0f + (v_toward_wall + 30.f) * 2.f * I::GlobalVars->interval_per_tick;
	bool winner = false;

	if (sweep_viable)
	{
		// pass 1: stand sweep, 1 sim each; pool the closest misses for the retry pass. Fast fall = the
		// pin nearly always lands one tick late (band crossed mid-tick), so pool more there.
		const int n_pool = (vLiveVelocity.z < -400.f) ? 12 : 8;
		int retry_idx[12]; float retry_gap[12]; int n_retry = 0;
		for (int vi = 0; vi < s_n_main && !winner && sims_left() > 0; ++vi)
		{
			winner = eval_variant(angle_wall.y + s_main_vars[vi].ang * sign, s_main_vars[vi].mul, false, false);
			if (!winner && last_eval_gap < FLT_MAX)
			{
				if (n_retry < n_pool || last_eval_gap < retry_gap[n_pool - 1])
				{
					int j = (n_retry < n_pool) ? n_retry++ : n_pool - 1;
					while (j > 0 && retry_gap[j - 1] > last_eval_gap)
						{ retry_gap[j] = retry_gap[j - 1]; retry_idx[j] = retry_idx[j - 1]; --j; }
					retry_gap[j] = last_eval_gap; retry_idx[j] = vi;
				}
			}
		}
		if (best_hit_streak > 0) winner = true;

		// pass 2: fast-fall pins land one tick late - 2-tick retest, closest misses only
		if (!winner && vLiveVelocity.z < -1.f)
			for (int i = 0; i < n_retry && !winner && sims_left() > 0; ++i)
				winner = eval_variant(angle_wall.y + s_main_vars[retry_idx[i]].ang * sign,
				                      s_main_vars[retry_idx[i]].mul, false, true);

		// pass 3: a rising headbounce needs duck (the stand hull can't snap onto the top side).
		// mid-ride only when already riding ducked - no stand-ride duck flips.
		if (!winner && !manual_duck_held && auto_crouch && vLiveVelocity.z > 0.f
			&& (!caught_last_tick || ride_duck))
			for (int vi = 0; vi < s_n_main && !winner && sims_left() > 0; ++vi)
				winner = eval_variant(angle_wall.y + s_main_vars[vi].ang * sign, s_main_vars[vi].mul, true, false);

		// pass 4: wiggle - flush at exactly eps never catches; a tiny outward drift re-enters the band.
		if (!winner && live_gap < 0.5f)
			for (int vi = 0; vi < s_n_wig && !winner && sims_left() > 0; ++vi)
				winner = eval_variant(angle_wall.y + s_wig_vars[vi].ang * sign, s_wig_vars[vi].mul, false,
				                      vLiveVelocity.z < -1.f);

		// pass 4.5: falling duck probe - short ledges: the duck hull (62 vs 82) shrinks player height
		// in the catch equation, and an air FinishDuck shifts the origin +20u instantly = a different
		if (!winner && !manual_duck_held && auto_crouch && vLiveVelocity.z < -1.f
			&& !caught_last_tick
			&& live_gap < 0.75f)
		{
			if (gap < 0.5f)
				winner = eval_variant(best_yaw, best_forward_move, true, true);
			for (int i = 0; i < n_retry && i < 4 && !winner && sims_left() > 0; ++i)
				winner = eval_variant(angle_wall.y + s_main_vars[retry_idx[i]].ang * sign,
				                      s_main_vars[retry_idx[i]].mul, true, true);
		}

		if (best_hit_streak > 0) winner = true;

		// pass 5: one duck probe of the stand winner -> a duck extension when duck also pins at about
		// the same gap. Fresh catch only: mid-ride re-plans re-ran the pending->holding choreography.
		if (winner && ride_ticks == 0 && !manual_duck_held && auto_crouch && !(best_buttos & IN_DUCK))
		{
			const float win_gap = gap;
			float dp = FLT_MAX; int ds = 0; int db = original_buttons; bool dh = false;
			simulate_variant(best_yaw, best_forward_move, true, dp, ds, db, dh, false);
			if (ds > 0 && dp <= win_gap + k_duck_extension_gap_tolerance)
			{
				best_is_duck_extension = true;
				best_duck_buttons = db;
			}
		}
	}

	// pin coast: a mid-ride sweep miss (band drift / seam) - replay the last catch move for up to the
	// hold ticks; the pin usually re-lands within 1-2 ticks and dropping to creep killed the ride.
	if (!winner && caught_last_tick && ride_ticks >= 2 && ride_hold_ticks_left > 0)
	{
		--ride_hold_ticks_left;
		SetHit(true);
		reset_duck_extension();
		pCmd->forwardmove = wall_to_player_fwd(ride_hold_yaw, ride_hold_forward_move, ride_hold_side_move);
		pCmd->sidemove    = wall_to_player_side(ride_hold_yaw, ride_hold_forward_move, ride_hold_side_move);
		pCmd->buttons     = ride_hold_buttons;
		apply_edge_stop();
		return;
	}

	if (manual_duck_held)
	{
		SetHit(best_hit_streak > 0);
		reset_duck_extension();
	}

	if (best_is_duck_extension && !manual_duck_held)
	{
		SetHit(best_hit_streak > 0);

		const bool plan_changed = fabsf(duck_extension_yaw - best_yaw) > 0.001f ||
		                          fabsf(duck_extension_forward_move - best_forward_move) > 0.001f ||
		                          fabsf(duck_extension_side_move - best_side_move) > 0.001f ||
		                          duck_extension_stand_buttons != best_stand_buttons || duck_extension_duck_buttons != best_duck_buttons;

		if (plan_changed || (!duck_extension_pending && !duck_extension_holding))
		{
			duck_extension_pending       = true;
			duck_extension_holding       = false;
			duck_extension_wait_ticks    = k_duck_extension_delay_ticks;
			duck_extension_yaw           = best_yaw;
			duck_extension_forward_move  = best_forward_move;
			duck_extension_side_move     = best_side_move;
			duck_extension_stand_buttons = best_stand_buttons;
			duck_extension_duck_buttons  = best_duck_buttons;
		}

		pCmd->forwardmove = wall_to_player_fwd(best_yaw, best_forward_move, best_side_move);
		pCmd->sidemove    = wall_to_player_side(best_yaw, best_forward_move, best_side_move);
		pCmd->buttons     = best_stand_buttons;
		fix_air_stuck_move();
		if (m_bTextureBugHit)
			arm_ride_hold(best_yaw, best_forward_move, best_side_move, pCmd->buttons);
		apply_edge_stop();
		return;
	}

	reset_duck_extension();
	SetHit(best_hit_streak > 0);

	// seam look-ahead: the sweep only sees 1-2 ticks, but falling fast the seam is often 3-7 ticks
	if (!m_bTextureBugHit && creep_lock_ticks == 0 && vLiveVelocity.z < -1.f
		&& live_gap < 0.5f && gap < 1.f && sims_left() >= 8)
	{
		RestoreOriginal();
		apply_predicted_state();
		CUserCmd look = *pCmd;
		look.buttons     = best_buttos;
		look.forwardmove = wall_to_player_fwd(best_yaw, best_forward_move, 0.f);
		look.sidemove    = wall_to_player_side(best_yaw, best_forward_move, 0.f);
		for (int k = 1; k <= 7 && sims_left() > 0; ++k)
		{
			sim(&look);
			if (is_pinned(pLocal->m_vecVelocity().z, k_catch_eps))
			{
				creep_lock_ticks   = k;
				creep_lock_yaw     = best_yaw;
				creep_lock_fwd     = best_forward_move;
				creep_lock_buttons = best_buttos;
				break;
			}
		}
		RestoreOriginal();
		apply_predicted_state();
	}
	if (!m_bTextureBugHit && creep_lock_ticks > 0)
	{
		--creep_lock_ticks;
		pCmd->forwardmove = wall_to_player_fwd(creep_lock_yaw, creep_lock_fwd, 0.f);
		pCmd->sidemove    = wall_to_player_side(creep_lock_yaw, creep_lock_fwd, 0.f);
		pCmd->buttons     = creep_lock_buttons;
		m_bTBOwnsCmd = true;
		return;
	}

	// no catch + user strafing + still far -> hand the cmd back, the user owns the approach. A NEAR
	// wall creep stays owned even against input: it is the multi-tick band setup (inside eps + tiny
	if (!m_bTextureBugHit && user_has_manual_movement_input && live_gap > 0.75f)
	{
		pCmd->forwardmove = original_forward_move;
		pCmd->sidemove    = original_side_move;
		pCmd->buttons     = original_buttons;
		return;
	}

	// no catch + still gapped + hands off -> drive straight at the wall (a near-tangent creep is too
	if (!m_bTextureBugHit && live_gap > 1.0f && !user_has_manual_movement_input && edge_stop)
	{
		const float drive_fwd = std::min(std::max(live_gap * 18.f, 120.f), 450.f);
		pCmd->forwardmove = wall_to_player_fwd(angle_wall.y, drive_fwd, 0.f);
		pCmd->sidemove    = wall_to_player_side(angle_wall.y, drive_fwd, 0.f);
		pCmd->buttons     = original_buttons;
		if (!manual_duck_held)
			pCmd->buttons &= ~IN_DUCK; // the catch was computed standing
		m_bTBOwnsCmd = true;
		return;
	}

	pCmd->forwardmove = wall_to_player_fwd(best_yaw, best_forward_move, best_side_move);
	pCmd->sidemove    = wall_to_player_side(best_yaw, best_forward_move, best_side_move);
	pCmd->buttons     = best_buttos;
	m_bTBOwnsCmd = true; // creep = the multi-tick band setup, AirStuck must not stomp it
	fix_air_stuck_move();
	if (m_bTextureBugHit)
	{
		arm_ride_hold(best_yaw, best_forward_move, best_side_move, pCmd->buttons);
		fast_yaw  = best_yaw;
		fast_fwd  = best_forward_move;
		fast_duck = (best_buttos & IN_DUCK) != 0;
	}
	apply_edge_stop();
}


void CMisc::HeadSurf(CTFPlayer* pLocal, CUserCmd* pCmd)
{

    m_bHeadSurfHit = false;

    if (!Vars::Misc::Movement::HeadSurf.Value || !pLocal || !pLocal->IsAlive())
    {
        m_bHSLocked = false;
        m_flHSCachedForward = -1.f;
        return;
    }

    // TextureBug caught a wall surf this tick - its move owns the cmd, don't fight it.
    if (m_bTextureBugHit)
    {
        m_bHSLocked = false;
        return;
    }

    const float flHalfGrav = TB_HalfGravityPerTick();

    if (pLocal->m_fFlags() & FL_ONGROUND)
    {
        m_flHSCachedForward = -1.f;
        m_bHSSurfingNow     = false;
        m_bHSLocked         = false;
        return;
    }

    const int iMoveType = pLocal->m_MoveType();
    if (iMoveType == MOVETYPE_LADDER || iMoveType == MOVETYPE_NOCLIP
        || iMoveType == MOVETYPE_FLY || iMoveType == MOVETYPE_OBSERVER)
        return;

    const Vec3 vStartPos = pLocal->GetAbsOrigin();
    const Vec3 vMins     = pLocal->m_vecMins();
    const Vec3 vMaxs     = pLocal->m_vecMaxs();
    const float flReach  = m_bHSSurfingNow ? 10.f : 6.f;

    CGameTrace ceilTrace;
    if (!TB_DetectCeiling(vStartPos, vMins, vMaxs, flReach, ceilTrace))
    {
        m_flHSCachedForward = -1.f;
        m_bHSSurfingNow     = false;
        m_bHSLocked         = false;
        m_iHSColdSkip       = 0;
        return;
    }

    {
        const float flVZ    = pLocal->m_vecVelocity().z;
        const bool  bSurfVel = TB_IsPositiveHalfGravity(flVZ, flHalfGrav, 1.5f);
        m_bHSSurfingNow  = bSurfVel && m_bHSPrevSurfVel;
        m_bHSPrevSurfVel = bSurfVel;
    }

    const Vec3  vOriginalView     = pCmd->viewangles;
    const float flOriginalForward = pCmd->forwardmove;
    const float flOriginalSide    = pCmd->sidemove;
    const int   iOriginalButtons  = pCmd->buttons;

    // Locked: riding the head surf. Same zero-Simulate model as TextureBug - the engine pins the
    // real vz to +halfgravity while the surf holds, so test that and replay the move verbatim
    if (m_bHSLocked)
    {
        if (TB_IsPositiveHalfGravity(pLocal->m_vecVelocity().z, flHalfGrav, 3.f))
        {
            pCmd->viewangles  = vOriginalView;
            pCmd->forwardmove = m_flHSLockedForward;
            pCmd->sidemove    = m_flHSLockedSide;
            if (m_bHSLockedDuck) pCmd->buttons |= IN_DUCK;
            else                 pCmd->buttons &= ~IN_DUCK;
            m_bHeadSurfHit  = true;
            m_bHSSurfingNow = true;
            return;
        }
        m_bHSLocked = false; // slipped - fall through and re-catch this tick
    }

    Vec3 vCeilAngle = vOriginalView;
    {
        const Vec3 vNxy(ceilTrace.plane.normal.x, ceilTrace.plane.normal.y, 0.f);
        if (vNxy.Length2D() > 0.05f)
        {
            Vec3 vInto = vNxy * -1.f;
            vCeilAngle = Math::VectorAngles(vInto);
            Math::ClampAngles(vCeilAngle);
        }
    }

    // Pin the prediction frame (same fix as TextureBug/SimJumpReach): the bare
    // RestoreEntityToPredicted() drifts one slot forward per Simulate, so every candidate
    const int nBasePredicted = I::Prediction->m_nCommandsPredicted;
    const auto RestoreOriginal = [&]()
    {
        I::Prediction->RestoreEntityToPredictedFrame(nBasePredicted - 1);
    };

    constexpr int HS_SIM_BUDGET = 80;
    int iSimsUsed = 0;
    const auto TryConfig = [&](float flForward, float flAngleOffset, bool bDuck) -> bool
    {
        if (iSimsUsed >= HS_SIM_BUDGET) return false;
        iSimsUsed++;
        RestoreOriginal();
        Vec3 vWishAngle  = vOriginalView;
        vWishAngle.y     = vCeilAngle.y + flAngleOffset;
        pCmd->viewangles = vOriginalView;
        pCmd->buttons    = iOriginalButtons;
        if (bDuck) pCmd->buttons |= IN_DUCK;
        else       pCmd->buttons &= ~IN_DUCK;
        pCmd->forwardmove = flForward;
        pCmd->sidemove    = 0.f;
        CorrectMovement(pCmd, vWishAngle, vOriginalView);
        F::EnginePrediction.Simulate(pLocal, pCmd);
        // 0.5 tolerance to match TextureBug - the old 0.2 was tighter than the catch
        // velocity's own tick-to-tick jitter, so real catches were being rejected.
        return TB_IsPositiveHalfGravity(pLocal->m_vecVelocity().z, flHalfGrav, 0.5f);
    };

    const auto Commit = [&](float flCfgForward, float flCfgOffset, bool bDuck)
    {
        RestoreOriginal();                // pCmd already holds the winning silent move
        pCmd->viewangles        = vOriginalView;
        m_flHSCachedForward     = flCfgForward;
        m_flHSCachedAngleOffset = flCfgOffset;
        m_bHSCachedDuck         = bDuck;
        m_bHSLocked             = true;
        m_flHSLockedForward     = pCmd->forwardmove;
        m_flHSLockedSide        = pCmd->sidemove;
        m_bHSLockedDuck         = bDuck;
        m_bHeadSurfHit          = true;
        m_bHSSurfingNow         = true;
        m_iHSColdSkip           = 0;
    };

    // Fast path: last tick's winning config (1 Simulate).
    if (m_flHSCachedForward >= 0.f
        && TryConfig(m_flHSCachedForward, m_flHSCachedAngleOffset, m_bHSCachedDuck))
    {
        Commit(m_flHSCachedForward, m_flHSCachedAngleOffset, m_bHSCachedDuck);
        return;
    }

    // Throttle: a ceiling the whole sweep already missed stays un-surfable for a while; only
    // the cheap fast path above runs while cooling down (mirrors TextureBug's cold-skip).
    if (m_iHSColdSkip > 0)
    {
        m_iHSColdSkip--;
        RestoreOriginal();
        pCmd->viewangles  = vOriginalView;
        pCmd->forwardmove = flOriginalForward;
        pCmd->sidemove    = flOriginalSide;
        pCmd->buttons     = iOriginalButtons;
        return;
    }

    const float flVelYaw  = MVSUPE_RAD2DEG(atan2f(pLocal->m_vecVelocity().y, pLocal->m_vecVelocity().x));
    const float flRelYaw  = Math::NormalizeAngle(flVelYaw - vCeilAngle.y);
    const float flSign    = (flRelYaw >= 0.f) ? -1.f : 1.f;

    static constexpr float k_hs_fwd[] = { 0.f, 0.1f, 0.25f, 0.5f, 0.75f, 1.f, 1.5f, 2.125f,
                                           2.875f, 5.f, 10.f, 20.f, 30.f };
    constexpr float flCoarseStep = 5.f, flFineStep = 1.f;

    for (float fwd : k_hs_fwd)
    {
        if (iSimsUsed >= HS_SIM_BUDGET) break;
        for (int iDuck = 0; iDuck <= 1 && iSimsUsed < HS_SIM_BUDGET; iDuck++)
        {
            const bool bDuck = (iDuck == 1);
            for (float ang = 0.f; ang < 90.f && iSimsUsed < HS_SIM_BUDGET; ang += flCoarseStep)
            {
                if (!TryConfig(fwd, ang * flSign, bDuck))
                    continue;

                // Coarse hit - refine around it for the steadiest offset, else keep the coarse one.
                for (float d = flFineStep; d <= flCoarseStep; d += flFineStep)
                {
                    for (int s = 0; s <= 1; s++)
                    {
                        const float fa = ang + (s ? d : -d);
                        if (fa < 0.f) continue;
                        if (TryConfig(fwd, fa * flSign, bDuck))
                        {
                            Commit(fwd, fa * flSign, bDuck);
                            return;
                        }
                    }
                }
                // Refinement found nothing better - re-derive the coarse hit and take it.
                if (TryConfig(fwd, ang * flSign, bDuck))
                {
                    Commit(fwd, ang * flSign, bDuck);
                    return;
                }
            }
        }
    }

    // Full miss: hand the player's movement back and cool down.
    RestoreOriginal();
    pCmd->viewangles    = vOriginalView;
    pCmd->forwardmove   = flOriginalForward;
    pCmd->sidemove      = flOriginalSide;
    pCmd->buttons       = iOriginalButtons;
    m_flHSCachedForward = -1.f;
    m_iHSColdSkip       = 4;
}


void CMisc::WallClimb(CTFPlayer* pLocal, CUserCmd* pCmd)
{

    m_bWallClimbHit = false;
 
    if (!Vars::Misc::Movement::WallClimb.Value)
    {
        m_flWallClimbCachedForward = -1.f;
        return;
    }
 
    if (!pLocal || !pLocal->IsAlive())
        return;

    // A texture-bug / head-surf catch owns this tick's move - grounding through it would drop the surf.
    if (m_bTextureBugHit || m_bHeadSurfHit)
        return;

    if (pLocal->m_fFlags() & FL_ONGROUND)
        return;

    const int iMoveType = pLocal->m_MoveType();
    if (iMoveType == MOVETYPE_LADDER || iMoveType == MOVETYPE_NOCLIP
        || iMoveType == MOVETYPE_FLY || iMoveType == MOVETYPE_OBSERVER)
        return;

    // Reuse the shared wall scan — free if TextureBug already ran it.
    TB_RefreshWallCache(pLocal);
    if (!m_TB_WallCache.valid)
    {
        m_flWallClimbCachedForward = -1.f;
        return;
    }
 
    const CGameTrace& primary = m_TB_WallCache.trace;
    const Vec3 vIntoWall      = primary.plane.normal * -1.f;
    Vec3 vWallAngle           = Math::VectorAngles(vIntoWall);
    Math::ClampAngles(vWallAngle);
 
    const Vec3  vOriginalView     = pCmd->viewangles;
    const float flOriginalForward = pCmd->forwardmove;
    const float flOriginalSide    = pCmd->sidemove;
    const int   iOriginalButtons  = pCmd->buttons;
 
    // Pin the prediction frame (same fix as TextureBug/SimJumpReach): a bare
    // RestoreEntityToPredicted() drifts one slot per Simulate, corrupting every sweep
    const int nBasePredicted = I::Prediction->m_nCommandsPredicted;
    const auto RestoreOriginal = [&]()
    {
        I::Prediction->RestoreEntityToPredictedFrame(nBasePredicted - 1);
    };

    int iSims = 0;
    const auto TryConfig = [&](float flForward, float flAngleOffset) -> bool
    {
        if (iSims >= TB_SIM_BUDGET) return false;
        iSims++;
        RestoreOriginal();
        Vec3 vWishAngle   = vOriginalView;
        vWishAngle.y      = vWallAngle.y + flAngleOffset;
        pCmd->viewangles  = vOriginalView;
        pCmd->buttons     = iOriginalButtons;
        pCmd->forwardmove = flForward;
        pCmd->sidemove    = 0.f;
        CorrectMovement(pCmd, vWishAngle, vOriginalView);
        F::EnginePrediction.Simulate(pLocal, pCmd);
        return (pLocal->m_fFlags() & FL_ONGROUND) != 0;
    };
 
    // Fast path.
    if (m_flWallClimbCachedForward >= 0.f
        && TryConfig(m_flWallClimbCachedForward, m_flWallClimbCachedAngleOffset))
    {
        RestoreOriginal();
        m_bWallClimbHit = true;
        return;
    }
 
    static constexpr float k_fwd[] = { 0.f, 0.25f, 0.5f, 0.75f, 1.f, 2.125f, 2.875f, 5.f, 15.f, 30.f };
    const Vec3  vVel     = pLocal->m_vecVelocity();
    const float flVelYaw = MVSUPE_RAD2DEG(atan2f(vVel.y, vVel.x));
    const float flRelYaw = Math::NormalizeAngle(flVelYaw - vWallAngle.y);
    const float flSign   = (flRelYaw >= 0.f) ? -1.f : 1.f;
 
    bool  bFound      = false;
    float flBestForward = flOriginalForward, flBestSide = flOriginalSide;
    int   iBestButtons  = iOriginalButtons;
 
    for (float fwd : k_fwd)
    {
        if (iSims >= TB_SIM_BUDGET || bFound) break;
        for (float ang = 0.f; ang < 45.f && iSims < TB_SIM_BUDGET; ang += 2.f)
        {
            if (TryConfig(fwd, ang * flSign))
            {
                bFound      = true;
                flBestForward = pCmd->forwardmove;
                flBestSide    = pCmd->sidemove;
                iBestButtons  = pCmd->buttons;
                m_flWallClimbCachedForward     = fwd;
                m_flWallClimbCachedAngleOffset = ang * flSign;
                m_bWallClimbHit                = true;
                break;
            }
        }
    }
 
    RestoreOriginal();
    pCmd->viewangles = vOriginalView;

    if (bFound)
    {
        pCmd->forwardmove = flBestForward;
        pCmd->sidemove    = flBestSide;
        pCmd->buttons     = iBestButtons;
    }
    else
    {
        pCmd->forwardmove = flOriginalForward;
        pCmd->sidemove    = flOriginalSide;
        pCmd->buttons     = iOriginalButtons;
    }
}


bool CMisc::ConsumeTextureBugChoke()
{
	if (m_iTBChokeTicksLeft <= 0)
		return false;
	m_iTBChokeTicksLeft--;
	return true;
}


void CMisc::AirStuckCore(CTFPlayer* pLocal, CUserCmd* pCmd, bool bEnabled, bool bSlantedOnly,
                         bool& bHitFlag, AirStuckState_t& tState)
{

	bHitFlag = false;

	const auto reset_state = [&]() { tState = AirStuckState_t{}; };

	if (!bEnabled)                         { reset_state(); return; }
	if (!pLocal || !pLocal->IsAlive())     { reset_state(); return; }

	const int iMoveType = pLocal->m_MoveType();
	if (iMoveType == MOVETYPE_LADDER || iMoveType == MOVETYPE_NOCLIP
		|| iMoveType == MOVETYPE_FLY || iMoveType == MOVETYPE_OBSERVER)
		{ reset_state(); return; }

	// Don't fight a TB/HeadSurf catch, or a TB creep that owns the cmd this tick.
	if (m_bTextureBugHit || m_bHeadSurfHit || m_bTBOwnsCmd) { reset_state(); return; }

	if (pLocal->m_fFlags() & FL_ONGROUND)  { reset_state(); return; }

	const Vec3 vOriginalView  = pCmd->viewangles;
	const Vec3 live_velocity  = pLocal->m_vecVelocity(); // backup_data.m_velocity
	const float step          = (3.14159265358979323846f * 2.f) / 16.f; // TB_DetectWall flStep (unused by it, kept for call sig)
	const Vec3  start_pos     = pLocal->GetAbsOrigin();
	const Vec3  mins          = pLocal->m_vecMins();
	const Vec3  maxs          = pLocal->m_vecMaxs();
	const int   cur_tick      = I::GlobalVars->tickcount;
	// -- Aggressiveness / perf levers --------------------------------------------------------------
	// All sliders now (Misc>Movement). Defaults match the old bumped constants.
	const float k_catch_eps    = Vars::Misc::Movement::AirStuckCatchEps.Value;  // raise = sticks more / easier
	const float k_detect_reach = Vars::Misc::Movement::AirStuckReach.Value;     // hull reach: engage walls this far out
	const float k_flush_tol    = Vars::Misc::Movement::AirStuckFlushTol.Value;  // gap > this -> drift only; <= -> catch sweep
	const float k_drift_gain   = Vars::Misc::Movement::AirStuckDriftGain.Value; // drift fwd = gap * gain (raise = faster drive-in)
	const int   k_sim_budget   = Vars::Misc::Movement::AirStuckSimBudget.Value; // hard cap on Simulate()s in the catch sweep
	// Detection now uses TB_DetectWall (the texture-bug path): ONE hull trace per angle (16 cold), and a
	// cached-angle fast path -> ~1 trace steady state, instead of the old per-Z line scan (~16x41 traces/tick)

	// Re-express a wall-frame move (forward into the wall, side along it) onto pCmd in the player's
	// real view frame so the engine produces that world wishdir without snapping the aim.
	const auto apply_move = [&](float wall_yaw, float fwd, float side)
	{
		Vec3 vWishAngle  = vOriginalView;
		vWishAngle.y     = wall_yaw;
		pCmd->viewangles = vOriginalView;
		pCmd->forwardmove = fwd;
		pCmd->sidemove    = side;
		CorrectMovement(pCmd, vWishAngle, vOriginalView);
	};

	// -- detect wall (TB_DetectWall: ONE hull trace/angle, cached-angle fast path) ------------------
	// This is the texture-bug detection path we target. Steady state is ~1 trace (re-test last
	CGameTrace primary{};
	float found_angle = tState.cached_wall_angle;
	bool wall_detected = TB_DetectWall(start_pos, mins, maxs, step, primary, found_angle,
	                                   tState.cached_wall_angle, k_detect_reach);

	// wall_stuck variant (bSlantedOnly): only slanted/diagonal vertical faces qualify - reject axis-aligned.
	const auto is_straight = [](float v) -> bool { return v == -1.f || v == 0.f || v == 1.f; };
	if (wall_detected && bSlantedOnly
		&& is_straight(primary.plane.normal.x) && is_straight(primary.plane.normal.y))
		wall_detected = false;

	if (!wall_detected)
	{
		tState.cached_wall_angle = -1.f;
		tState.start_circle      = 0.f;
		tState.stuck_hold_ticks  = 0;
		return; // not engaged - leave pCmd untouched
	}
	tState.cached_wall_angle = found_angle;

	const Vec3 approach_normal = primary.plane.normal * -1.f;
	Vec3 angle_wall = Math::VectorAngles(approach_normal);
	Math::ClampAngles(angle_wall);

	// Pin the base prediction frame; every probe restores to it (Simulate drifts m_nCommandsPredicted
	// forward, so a bare restore would creep). RAII guard restores on every exit path.
	const int nBasePredicted = I::Prediction->m_nCommandsPredicted;
	const auto RestoreOriginal = [&]() { I::Prediction->RestoreEntityToPredictedFrame(nBasePredicted - 1); };
	struct StateGuard { int base; ~StateGuard() { I::Prediction->RestoreEntityToPredictedFrame(base - 1); } } prediction_state_guard{ nBasePredicted };

	const auto apply_predicted_state = [&]() noexcept
	{
		pLocal->SetAbsOrigin(start_pos);
		pLocal->m_vecOrigin()   = start_pos;
		pLocal->m_vecVelocity() = live_velocity;
	};

	// the reference get_align: support-trace distance minus the hull's projected support distance.
	// > 0 == still a gap to the wall (approach); <= 0 == flush.
	const auto get_align = [&]() -> float
	{
		const Vec3 minusplane = primary.plane.normal * -1.f * 32.f;
		const Vec3 origin = pLocal->GetAbsOrigin();
		Ray_t ray2; ray2.Init(Vec3(origin.x, origin.y, primary.endpos.z),
		                      Vec3(origin.x + minusplane.x, origin.y + minusplane.y, primary.endpos.z),
		                      Vec3(0.f, 0.f, 0.f), Vec3(0.f, 0.f, 0.f)); // line trace
		CTraceFilterWorldAndPropsOnly wlft;
		CGameTrace trace_support;
		I::EngineTrace->TraceRay(ray2, MASK_ALL, &wlft, &trace_support);
		const Vec3 wall_normal(primary.plane.normal.x, primary.plane.normal.y, 0.f);
		return trace_support.fraction * 32.f - TB_WallSupportDistance(pLocal, wall_normal);
	};

	// Already stuck (live vz pinned to the catch target): hold last tick's winning move. Checked FIRST,
	// before any approach logic, so a successful stick is never interrupted by a re-approach.
	if (F::EnginePrediction.IsTargetPredictZVelocity(live_velocity.z, k_catch_eps))
	{
		bHitFlag = true;
		tState.stuck_hold_ticks++;
		const float hold_yaw  = (tState.stuck_last_yaw  != FLT_MAX) ? tState.stuck_last_yaw  : angle_wall.y;
		const float hold_move = (tState.stuck_last_move != FLT_MAX) ? tState.stuck_last_move : 5.f;
		const float hold_side = tState.stuck_last_side;
		apply_move(hold_yaw, hold_move, hold_side);
		return;
	}

	tState.stuck_hold_ticks = 0;

	// -- approach: still gapped -> can't catch un-flush, so just drive in HARD (0 Simulates) ---------
	// One cheap drive tick closes the gap (high gain so a fast fall reaches the wall before slipping past
	const float align = get_align();
	if (align > k_flush_tol)
	{
		const float drift_fwd = fminf(fmaxf(align * k_drift_gain, 120.f), 650.f); // [bumped min 60->120, cap 450->650: faster approach]
		apply_move(angle_wall.y, drift_fwd, 0.f);
		pCmd->buttons &= ~IN_DUCK;
		return;
	}

	// -- near flush: budgeted, coarse near-tangent catch sweep (runs EVERY tick = instant) ----------
	const float side_sign = (cur_tick % 2 == 0) ? 1.f : -1.f;

	constexpr float k_yaw_offsets[]   = { 88.0f, 88.5f, 89.0f, 89.5f, 89.9f }; // near-tangent (small |v_perp|)
	constexpr float k_forward_moves[] = { 2.f, 10.f, 60.f, 250.f, 650.f };     // tiny..slam [bumped for speed]

	int   sims_used      = 0;
	bool  found_catch    = false;
	bool  have_fallback  = false;
	float best_yaw       = angle_wall.y;
	float best_forward   = 2.f;
	float best_align_pred = FLT_MAX;

	for (const float rel_ang : k_yaw_offsets)
	{
		if (found_catch || sims_used >= k_sim_budget) break;
		for (const float fwd : k_forward_moves)
		{
			if (sims_used >= k_sim_budget) break;

			RestoreOriginal();
			apply_predicted_state();

			const float cand_yaw = Math::NormalizeAngle(angle_wall.y + rel_ang * side_sign);
			apply_move(cand_yaw, fwd, 0.f);
			pCmd->buttons &= ~IN_DUCK; // catch is computed standing (no duck-extension here)

			F::EnginePrediction.Simulate(pLocal, pCmd); sims_used++;

			if (F::EnginePrediction.IsTargetPredictZVelocity(pLocal->m_vecVelocity().z, k_catch_eps))
			{
				// Confirm: a real catch keeps pinning vz a second predicted tick (same cmd, no restore).
				F::EnginePrediction.Simulate(pLocal, pCmd); sims_used++;
				if (F::EnginePrediction.IsTargetPredictZVelocity(pLocal->m_vecVelocity().z, k_catch_eps))
				{
					best_yaw     = cand_yaw;
					best_forward = fwd;
					found_catch  = true;
					break;
				}
				continue; // single-tick blip, not a real catch
			}

			// No catch: rank by post-move flushness so the committed fallback hugs (and drives the gap shut).
			const float predicted_align = get_align();
			if (!have_fallback || predicted_align < best_align_pred)
			{
				best_align_pred = predicted_align;
				best_yaw        = cand_yaw;
				best_forward    = fwd;
				have_fallback   = true;
			}
		}
	}

	RestoreOriginal();

	// Commit the best move (the catching one if found, else the most-flush hug). Reality must match the
	// standing prediction, so clear the crouch here too.
	apply_move(best_yaw, best_forward, 0.f);
	pCmd->buttons &= ~IN_DUCK;

	if (found_catch)
	{
		bHitFlag               = true;
		tState.stuck_last_yaw  = best_yaw;
		tState.stuck_last_move = best_forward;
		tState.stuck_last_side = 0.f;
	}
}


void CMisc::AirStuck(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	// One toggle, all walls (bSlantedOnly=false). Flip to true for the wall_stuck variant (skip
	// axis-aligned faces) if a slanted-only mode is ever wanted again.
	AirStuckCore(pLocal, pCmd, Vars::Misc::Movement::AirStuck.Value, /*bSlantedOnly*/ false,
	             m_bAirStuckHit, m_AirStuckState);
}


void CMisc::PixelFinder(CTFPlayer* pLocal, CUserCmd* pCmd)
{

	const bool bHeld = Vars::Misc::Movement::PixelFinder.Value;
	const bool bRising = bHeld && !m_bPixelFinderHeld;
	const bool bFalling = !bHeld && m_bPixelFinderHeld;
	m_bPixelFinderHeld = bHeld;

	if (!bHeld && !bFalling)
		return;

	// Trace where we're aiming to find the wall + its normal.
	Vec3 vForward; Math::AngleVectors(pCmd->viewangles, &vForward);
	const Vec3 vEye = pLocal->GetEyePosition();

	CTraceFilterWorldAndPropsOnly filter;
	CGameTrace trace;
	SDK::Trace(vEye, vEye + vForward * 6000.f, MASK_PLAYERSOLID, &filter, &trace);
	const Vec3 vHit = trace.endpos;

	if (bRising)
	{
		// First press: clear old results, anchor the start of the scan column. Ignore walls you
		// can't actually interact with (trigger brushes) or empty aims, same as Auto Align.
		m_vPixelSurfPoints.clear();
		m_bPixelFinderFirst = false;
		if (trace.fraction >= 1.f || (trace.surface.flags & SURF_TRIGGER))
			return;
		m_vPixelFinderStart = vHit;
		m_vPixelFinderEnd = vHit;
		m_vPixelFinderWallNormal = trace.plane.normal;
		m_bPixelFinderFirst = true;
		return;
	}

	if (bHeld)
	{
		// While held: stretch the scan column to the current aim height.
		if (m_bPixelFinderFirst)
			m_vPixelFinderEnd.z = vHit.z;
		return;
	}

	// Release: brute-force the wall column for pixel-surf catch positions.
	if (!m_bPixelFinderFirst)
		return;
	m_bPixelFinderFirst = false;

	Vec3 vWallNormal = m_vPixelFinderWallNormal;
	vWallNormal.z = 0.f;
	if (vWallNormal.Length() < 0.1f)
		return;
	vWallNormal.Normalize();

	// Tick-rate-correct pixel-surf velocity: while riding a surf the engine pins vertical velocity
	// to -halfgravity-per-tick.
	static auto sv_gravity = H::ConVars.FindVar("sv_gravity");
	const float flHalfGrav = (sv_gravity ? sv_gravity->GetFloat() : 800.f) * I::GlobalVars->interval_per_tick * 0.5f;
	constexpr float flSurfTol = 1.5f;
	// Lateral hull offset that makes the probe actually CONTACT the lip. the reference working CSGO finder

	const float flMargin = Vars::Misc::Movement::PixelFinderScanMargin.Value;
	const float flLow = std::min(m_vPixelFinderStart.z, m_vPixelFinderEnd.z) - flMargin;
	const float flHigh = std::max(m_vPixelFinderStart.z, m_vPixelFinderEnd.z) + flMargin;
	const int iSteps = std::clamp(int(flHigh - flLow), 0, 600);

	const int iBackupButtons = pCmd->buttons;
	const float flBackupForward = pCmd->forwardmove;
	const float flBackupSide = pCmd->sidemove;
	const Vec3 vMins = pLocal->m_vecMins();
	const Vec3 vMaxs = pLocal->m_vecMaxs();
	const std::string sMap = I::EngineClient->GetLevelName() ? I::EngineClient->GetLevelName() : "";

	// Wall tangent (horizontal, perpendicular to the scan normal) for the lateral band sweep.
	Vec3 vTangent = Vec3(vWallNormal.y, -vWallNormal.x, 0.f);
	if (vTangent.Length() > 0.1f)
		vTangent.Normalize();

	// A pixel-surf lip occupies a narrow XY, so probing only the single aimed column means you have
	// to luck your crosshair onto it - that is the "scan 10 times before it finds one" problem.
	constexpr float flBandHalf = 16.f; // +/- units swept along the wall tangent at each height
	constexpr float flBandStep = 4.f;  // lateral sample spacing within the band

	// One probe per height. the reference finder (the accurate one) does NOT drop a falling probe and hope
	auto TryPin = [&](const Vec3& vTestOrigin, const Vec3& vN, bool bDuck, float& flPinZ) -> bool
	{
		const Vec3 vInto = Vec3(-vN.x, -vN.y, 0.f);
		Vec3 vIntoAngle = Math::VectorAngles(vInto);
		Math::ClampAngles(vIntoAngle);
		const float flRot = MVSUPE_DEG2RAD(vIntoAngle.y - pCmd->viewangles.y);
		const float flCos = cosf(flRot), flSin = sinf(flRot);

		auto Probe = [&](float flFeetZ, float flSeedVz, int iTicks) -> bool
		{
			F::Misc.RestoreEntityToPredicted();

			const Vec3 vAt = Vec3(vTestOrigin.x, vTestOrigin.y, flFeetZ);
			pLocal->SetAbsOrigin(vAt);
			pLocal->m_vecOrigin() = vAt;
			pLocal->m_fFlags() &= ~FL_ONGROUND; // airborne, on (or falling onto) the lip

			// Light into-wall press only - a big into-wall speed pins the hull flat against the
			// main wall and free-falls past the lip.
			pLocal->m_vecVelocity() = vInto * 10.f + Vec3(0.f, 0.f, flSeedVz);

			pCmd->buttons = iBackupButtons & ~IN_JUMP;
			if (bDuck)
				pCmd->buttons |= IN_DUCK;
			else
				pCmd->buttons &= ~IN_DUCK;
			pCmd->forwardmove = flCos * 10.f;
			pCmd->sidemove = -flSin * 10.f;

			for (int t = 0; t < iTicks; t++)
			{
				F::EnginePrediction.Simulate(pLocal, pCmd);
				pCmd->buttons &= ~IN_JUMP; // never let the probe launch upward

				if (fabsf(pLocal->m_vecVelocity().z + flHalfGrav) < flSurfTol)
				{
					flPinZ = pLocal->m_vecOrigin().z; // the engine's own settled catch height
					return true;
				}
			}
			return false;
		};

		// Exact placement at the reference catch offset (1 tick is decisive), then the fallback drop
		// sweeping the rest of this 1u step (-8 vz = 0.12u per tick x 10 ticks covers ~1.2u).
		const float flT = truncf(vTestOrigin.z);
		const float flExact = vTestOrigin.z >= 0.f ? flT + 0.0287018f : flT - 0.972092f;
		if (Probe(flExact, -flHalfGrav, 2))
			return true;
		return Probe(flExact + 0.9f, -8.f, 10);
	};

	for (int i = 0; i <= iSteps; i++)
	{
		const float flZ = flLow + float(i);
		bool bFoundThisHeight = false;

		// Sweep the lateral band at this height. First spot that pins wins - the run-dedup below
		// already collapses the column of adjacent catches into one marker, so one per height is plenty.
		for (float flLat = -flBandHalf; flLat <= flBandHalf + 0.01f && !bFoundThisHeight; flLat += flBandStep)
		{
			// Re-trace the wall at THIS height and lateral offset instead of reusing the first-aim XY
			// for the whole column. A pixel-surf lip protrudes at its own height only, so probing per
			const Vec3 vProbeMid = Vec3(m_vPixelFinderStart.x, m_vPixelFinderStart.y, flZ) + vTangent * flLat;
			CGameTrace wallTrace;
			SDK::Trace(vProbeMid + vWallNormal * 12.f, vProbeMid - vWallNormal * 12.f, MASK_PLAYERSOLID, &filter, &wallTrace);

			Vec3 vN, vWallXY;
			bool bDispWall = false;
			if (wallTrace.fraction < 1.f && fabsf(wallTrace.plane.normal.z) <= 0.7f
				&& Vec3(wallTrace.plane.normal.x, wallTrace.plane.normal.y, 0.f).Length2D() > 0.1f)
			{
				vN = Vec3(wallTrace.plane.normal.x, wallTrace.plane.normal.y, 0.f);
				vN.Normalize();
				vWallXY = wallTrace.endpos;
				bDispWall = wallTrace.IsDispSurface();
			}
			else
			{
				// No clean re-acquire at this height (floor clutter, props, displacement noise in
				// the ray's path). Don't throw the height away - fall back to the ANCHORED wall
				vN = vWallNormal;
				vWallXY = vProbeMid;
			}

			// Feet origin: hull pressed up against the surface we just found here, at the AABB
			// support distance for THIS column's normal (face contact on axis-aligned walls, corner
			const float flSupport = fabsf(vMaxs.x) * fabsf(vN.x) + fabsf(vMaxs.y) * fabsf(vN.y);
			const float flGap = flSupport + (bDispWall ? 0.001f : -0.022f);
			const Vec3 vTestOrigin = Vec3(vWallXY.x, vWallXY.y, flZ) + vN * flGap;

			// Reject offsets where the hull spawns embedded in geometry - a real surf catches a lip
			// with the hull in open air; an embedded hull only produces junk clip velocities.
			const Vec3 vDuckMaxs = Vec3(vMaxs.x, vMaxs.y, 62.f);
			CGameTrace solidTrace;
			SDK::TraceHull(vTestOrigin, vTestOrigin, vMins, vDuckMaxs, MASK_PLAYERSOLID, &filter, &solidTrace);
			if (solidTrace.startsolid || solidTrace.allsolid)
				continue;

			// Test standing, then ducked (most TF2 pixel surfs are ducked).
			for (int iDuck = 0; iDuck < 2 && !bFoundThisHeight; iDuck++)
			{
				float flPinZ = flZ;
				if (TryPin(vTestOrigin, vN, iDuck != 0, flPinZ))
				{
					// Store the spot ON the wall surface (wall XY, not the hull feet origin that
					// sits ~half a hull-width off the wall) at the height the hull actually pinned -
					const Vec3 vWallPoint = Vec3(vWallXY.x, vWallXY.y, flPinZ);
					m_vPixelSurfPoints.push_back({ vWallPoint, sMap });
					bFoundThisHeight = true;
				}
			}
		}
	}

	// Restore everything we touched during the scan.
	pCmd->buttons = iBackupButtons;
	pCmd->forwardmove = flBackupForward;
	pCmd->sidemove = flBackupSide;
	F::Misc.RestoreEntityToPredicted();

	// The scan walks the column 1u at a time, so a single surfable lip yields a run of adjacent
	if (m_vPixelSurfPoints.size() > 1)
	{
		std::vector<PixelSurfPoint_t> vDeduped;
		vDeduped.push_back(m_vPixelSurfPoints.front());
		float flPrevZ = m_vPixelSurfPoints.front().m_vPos.z;

		for (size_t i = 1; i < m_vPixelSurfPoints.size(); i++)
		{
			const Vec3& vPos = m_vPixelSurfPoints[i].m_vPos;
			if (fabsf(vPos.z - flPrevZ) > 1.5f)
				vDeduped.push_back(m_vPixelSurfPoints[i]);
			flPrevZ = vPos.z;
		}
		m_vPixelSurfPoints = std::move(vDeduped);
	}
}


void CMisc::PixelSurfLine(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	// Recording this tick? Feature on, alive, and actually riding a surf (PixelSurf set this earlier
	// in RunPost; it's the same signal the "ps" indicator uses, so it works with the auto-catcher off).
	const bool bSurfing = Vars::Misc::Movement::PixelSurfLine.Value
		&& pLocal && pLocal->IsAlive() && m_bPixelSurfingNow;

	if (bSurfing)
	{
		if (!m_bWasInSurfLine)
			m_vSurfLineCurrent.clear(); // new ride - start a fresh path
		m_bWasInSurfLine = true;
		m_iSurfLineGrace = 8; // tolerate a few non-surf ticks (detection blip) before closing the path

		// On-wall position: project the player origin onto the nearest near-vertical wall so the line
		// sits on the surface (a quick radial scan, no prediction); fall back to the origin if no wall
		const Vec3 vOrigin = pLocal->m_vecOrigin();
		Vec3 vPoint = vOrigin;
		{
			CTraceFilterWorldAndPropsOnly filter;
			constexpr int   iDirs = 16;
			constexpr float flProbe = 64.f;
			constexpr float flTwoPi = 6.28318530717958647692f;
			float flBest = 1.f;
			for (int i = 0; i < iDirs; i++)
			{
				const float flAng = float(i) / float(iDirs) * flTwoPi;
				const Vec3 vDir = Vec3(cosf(flAng), sinf(flAng), 0.f);
				CGameTrace t;
				SDK::Trace(vOrigin, vOrigin + vDir * flProbe, MASK_PLAYERSOLID, &filter, &t);
				if (t.fraction >= 1.f || t.fraction >= flBest)
					continue;
				if (fabsf(t.plane.normal.z) > 0.35f) // floor/ceiling, not a wall
					continue;
				flBest = t.fraction;
				vPoint = t.endpos;
			}
		}

		// Append, spaced out so a near-stationary pin doesn't pile up thousands of points.
		if (m_vSurfLineCurrent.empty() || m_vSurfLineCurrent.back().DistTo(vPoint) >= 4.f)
			m_vSurfLineCurrent.push_back(vPoint);
		return;
	}

	// Not surfing this tick. A brief blip in detection shouldn't end the path - ride it out for a few
	// ticks; if the surf resumes, the same path continues. Only when the grace runs out is the path
	if (m_bWasInSurfLine && m_iSurfLineGrace > 0)
	{
		m_iSurfLineGrace--;
		return;
	}

	// Grace expired: if we had a path open, commit the line we traced.
	if (m_bWasInSurfLine)
	{
		m_bWasInSurfLine = false;
		if (m_vSurfLineCurrent.size() >= 2)
		{
			const std::string sMap = I::EngineClient->GetLevelName() ? I::EngineClient->GetLevelName() : "";

			// Skip if this path basically retraces one already saved here (re-surfing the same line).
			// Sample every point of the new path against each saved segment; if most of the new points
			bool bDup = false;
			constexpr float flNearDist = 32.f;   // a new point this close to a saved point = overlapping
			constexpr float flDupRatio = 0.6f;    // this fraction of the path overlapping = duplicate
			for (const auto& seg : m_vSurfLineSegments)
			{
				if (seg.m_sMap != sMap)
					continue;
				int iOverlap = 0;
				for (const auto& np : m_vSurfLineCurrent)
				{
					for (const auto& p : seg.m_vPoints)
						if (p.DistTo(np) < flNearDist) { iOverlap++; break; }
				}
				if (iOverlap >= int(m_vSurfLineCurrent.size() * flDupRatio))
				{
					bDup = true;
					break;
				}
			}

			if (!bDup)
			{
				SurfLineSegment_t seg;
				seg.m_sMap = sMap;
				seg.m_vPoints = m_vSurfLineCurrent;
				m_vSurfLineSegments.push_back(std::move(seg));
				constexpr size_t iMaxSegments = 64; // bound memory: drop the oldest strip past the cap
				if (m_vSurfLineSegments.size() > iMaxSegments)
					m_vSurfLineSegments.erase(m_vSurfLineSegments.begin());
			}
		}
		m_vSurfLineCurrent.clear();
	}
}


float CMisc::SimJumpReach(CTFPlayer* pLocal, CUserCmd* pCmd, int iJumpType, float flAtFlatDist)
{

	const bool bMini = iJumpType == JT_MINI || iJumpType == JT_MINIDUCK;
	const bool bLong = iJumpType == JT_LONG || iJumpType == JT_LONGDUCK;
	const bool bHold = iJumpType == JT_CROUCH || iJumpType == JT_MINIDUCK || iJumpType == JT_LONGDUCK;

	// flAtFlatDist <= 0: apex feet-Z gain. > 0: the feet-Z still left once the arc has travelled that far
	// out (you arrive over a far target already descending, so the apex over-states reach).
	const bool bAtDist = flAtFlatDist > 0.f;

	const int iBtnBackup = pCmd->buttons;
	const float flFwdBackup = pCmd->forwardmove;
	const float flSideBackup = pCmd->sidemove;

	// Pin the base frame before Simulate: each Simulate advances m_nCommandsPredicted, so a bare restore
	// drifts and back-to-back candidate sims corrupt each other. Pinning keeps every sim independent.
	const int nBasePredicted = I::Prediction->m_nCommandsPredicted;
	I::Prediction->RestoreEntityToPredictedFrame(nBasePredicted - 1);
	const Vec3 vStart = pLocal->m_vecOrigin();
	const float flStartZ = vStart.z;
	float flPeakZ = flStartZ;

	// At-distance tracking: Z sampled at exactly flAtFlatDist of horizontal displacement, found by
	// interpolating across the tick where displacement first crosses that distance.
	float flAtDistZ = -FLT_MAX;
	float flPrevDisp = 0.f, flPrevZ = flStartZ;

	for (int t = 0; t < 50; t++)
	{
		pCmd->buttons = iBtnBackup & ~(IN_JUMP | IN_DUCK);
		if (t == 0)
			pCmd->buttons |= IN_JUMP; // launch on the first tick, same as the commit path

		// Real TF2 timings (user-measured apexes told the story: mini 28 / mini+duck 48 / crouch
		// 72 - the held-duck variants differ, so the mini's duck must ride ON the launch cmd).
		bool bDuck = false;
		if ((bMini || bLong) && t == 0)
			bDuck = true;             // mini/long: duck pressed WITH the jump
		if (bLong && t >= 1 && t <= 2)
			bDuck = true;             // long: keep it two more cmds
		if (bHold && t >= 1)
			bDuck = true;             // crouch/+duck: hold from the first airborne tick to landing
		if (bDuck)
			pCmd->buttons |= IN_DUCK;

		F::EnginePrediction.Simulate(pLocal, pCmd); // user's own forward/side carry the arc

		const Vec3 vNow = pLocal->m_vecOrigin();
		const float flZ = vNow.z;
		if (flZ > flPeakZ)
			flPeakZ = flZ;

		// Sample the arc's Z at the target's horizontal distance the first time we reach it.
		if (bAtDist && flAtDistZ == -FLT_MAX)
		{
			const float flDisp = Vec3(vNow.x - vStart.x, vNow.y - vStart.y, 0.f).Length2D();
			if (flDisp >= flAtFlatDist)
			{
				const float flSpan = flDisp - flPrevDisp;
				const float flFrac = flSpan > 0.01f ? (flAtFlatDist - flPrevDisp) / flSpan : 1.f;
				flAtDistZ = flPrevZ + (flZ - flPrevZ) * flFrac;
			}
			flPrevDisp = flDisp;
			flPrevZ = flZ;
		}

		if (t > 2 && pLocal->m_vecVelocity().z < 0.f && flZ <= flStartZ)
			break; // back down to launch height - arc finished
	}

	pCmd->buttons = iBtnBackup;
	pCmd->forwardmove = flFwdBackup;
	pCmd->sidemove = flSideBackup;
	// Restore back to the same pinned frame, not m_nCommandsPredicted - 1 (which has
	// drifted forward by however many Simulate calls the loop took).
	I::Prediction->RestoreEntityToPredictedFrame(nBasePredicted - 1);

	// Calibrated against real measured landings (chudhook\Debug\jumpstats.csv vs jumpsim.csv):
	// the raw per-tick simulated apex reads ~4u higher than the height the player actually gains
	constexpr float flSimOvershoot = 4.f;

	float flGain;
	if (bAtDist)
		// Height available where the target actually is. If the arc landed before ever travelling
		// that far horizontally, the target is out of this jump's range -> 0 (won't be picked).
		flGain = (flAtDistZ == -FLT_MAX) ? 0.f : (flAtDistZ - flStartZ);
	else
		flGain = flPeakZ - flStartZ; // apex

	const float flReach = flGain - flSimOvershoot;
	return flReach > 0.f ? flReach : 0.f;
}


CMisc::JumpCatchResult_t CMisc::SimJumpCatch(CTFPlayer* pLocal, CUserCmd* pCmd, int iJumpType, const Vec3& vTarget, float flAtFlatDist)
{
	JumpCatchResult_t tResult;

	const bool bMini = iJumpType == JT_MINI || iJumpType == JT_MINIDUCK;
	const bool bLong = iJumpType == JT_LONG || iJumpType == JT_LONGDUCK;
	// mini+duck does NOT hold its duck here: live, the +20u at the catch comes from the descent
	// duck that the auto PixelSurf owns (you only minijump, and don't land crouched on a miss).
	const bool bHold = iJumpType == JT_CROUCH || iJumpType == JT_LONGDUCK;

	static auto sv_gravity = H::ConVars.FindVar("sv_gravity");
	const float flHalfGrav = (sv_gravity ? sv_gravity->GetFloat() : 800.f) * I::GlobalVars->interval_per_tick * 0.5f;
	constexpr float flSurfTol = 1.5f; // same pin tolerance PixelSurf uses
	constexpr float flZNear = 1.f;    // pin must happen AT our point - tight, so a neighbouring
	                                  // pixel on the same wall can't pass as a hit

	const int iBtnBackup = pCmd->buttons;
	const float flFwdBackup = pCmd->forwardmove;
	const float flSideBackup = pCmd->sidemove;

	// Pin the base prediction frame (see SimJumpReach for why a bare RestoreEntityToPredicted
	// drifts when several sims run back-to-back).
	const int nBasePredicted = I::Prediction->m_nCommandsPredicted;
	I::Prediction->RestoreEntityToPredictedFrame(nBasePredicted - 1);

	// Approach phase: when the sim starts airborne - descending from
	// the previous hop, running off a bump, mid bhop chain - it does NOT pretend a jump can fire
	constexpr int iMaxApproach = 40;
	int iLaunch = -1;          // sim tick the jump was pressed on
	Vec3 vLaunch = pLocal->m_vecOrigin();
	float flPeakZ = -FLT_MAX;  // tracked post-launch only

	// At-distance tracking, same interpolation as SimJumpReach: the height the arc still has
	float flAtDistZ = -FLT_MAX;
	float flPrevDisp = 0.f, flPrevZ = 0.f;

	// Analytic grid catch. Once the rising arc is past
	const float flGravPerTick2 = (sv_gravity ? sv_gravity->GetFloat() : 800.f)
		* I::GlobalVars->interval_per_tick * I::GlobalVars->interval_per_tick;
	// mini+duck rides the mini's grid: its +20 at the catch is PixelSurf's in-air descent duck,
	// an exact constant feet shift (standing hull 82 - duck hull 62), so its grid simply checks
	const float flGridDuckShift = iJumpType == JT_MINIDUCK ? 20.f : 0.f;
	// LONG releases its launch duck on t3 - a measurement before the resulting origin shift has
	// settled would bake +-20 into the delta and poison the whole extrapolation.
	const int iMinStable = bLong ? 5 : 3;
	int iRiseStable = 0;       // consecutive clean rising ticks
	bool bGridDone = false;
	float flGridPrevZ = 0.f;   // feet Z after the previous simulated tick

	bool bPrevPin = false;
	int iGroundTicks = 0; // consecutive grounded sim ticks (single bump contacts don't end the arc)
	for (int t = 0; t < iMaxApproach + 60; t++)
	{
		pCmd->buttons = iBtnBackup & ~(IN_JUMP | IN_DUCK);

		if (iLaunch < 0)
		{
			const bool bGroundedNow = pLocal->m_fFlags() & FL_ONGROUND;
			if (iJumpType == JT_AIRDASH || bGroundedNow)
			{
				iLaunch = t;
				vLaunch = pLocal->m_vecOrigin();
				flPeakZ = vLaunch.z;
				flPrevZ = vLaunch.z;
				pCmd->buttons |= IN_JUMP; // launch this cmd, same as the commit path
			}
			else if (t >= iMaxApproach)
				break; // never found ground to launch from inside the window
		}

		// Live choreography, real TF2 timings (see SimJumpReach), indexed from the LAUNCH tick:
		// mini/long press their duck WITH the jump (that's what cuts the apex), long keeps it 2
		const int iSince = iLaunch >= 0 ? t - iLaunch : -1;
		bool bDuck = false;
		if ((bMini || bLong) && iSince == 0)
			bDuck = true;
		if (bLong && iSince >= 1 && iSince <= 2)
			bDuck = true;
		if (bHold && iSince >= 1)
			bDuck = true;
		if (iSince >= 1 && pLocal->m_vecVelocity().z < 0.f)
			bDuck = true; // descending - PixelSurf will be holding the catch duck by now
		if (bDuck)
			pCmd->buttons |= IN_DUCK;

		const float flPreVz = pLocal->m_vecVelocity().z;
		const bool bPreDucked = pLocal->m_fFlags() & FL_DUCKING;

		F::EnginePrediction.Simulate(pLocal, pCmd); // user's own forward/side carry the arc into the wall

		if (iLaunch < 0)
			continue; // still approaching - the hop doesn't exist yet, nothing below applies

		const Vec3 vNow = pLocal->m_vecOrigin();
		const float flZ = vNow.z;
		const float flVz = pLocal->m_vecVelocity().z;
		const bool bAir = !(pLocal->m_fFlags() & FL_ONGROUND);
		if (flZ > flPeakZ)
			flPeakZ = flZ;

		// Sample the arc's Z at the target's horizontal distance the first time we reach it
		// (distance measured from the launch point, where the committed hop actually starts).
		if (flAtFlatDist > 0.f && flAtDistZ == -FLT_MAX)
		{
			const float flDisp = Vec3(vNow.x - vLaunch.x, vNow.y - vLaunch.y, 0.f).Length2D();
			if (flDisp >= flAtFlatDist)
			{
				const float flSpan = flDisp - flPrevDisp;
				const float flFrac = flSpan > 0.01f ? (flAtFlatDist - flPrevDisp) / flSpan : 1.f;
				flAtDistZ = flPrevZ + (flZ - flPrevZ) * flFrac;
			}
			flPrevDisp = flDisp;
			flPrevZ = flZ;
		}

		// Grid measurement: the first tick that is (a) past the launch choreography's duck
		if (!bGridDone && iSince >= 1)
		{
			const bool bPostDucked = pLocal->m_fFlags() & FL_DUCKING;
			const float flD = flGridPrevZ - flZ; // negative while rising (references' iCalcilate)
			const bool bClean = iSince >= iMinStable && flPreVz > 0.f && flVz > 0.f
				&& bPreDucked == bPostDucked && flD < 0.f && flD > -4.5f;
			if (!bClean)
				iRiseStable = 0;
			else if (++iRiseStable > 2)
			{
				bGridDone = true;
				// Anchor at the PRE-tick Z and replay the measured step as k=0 (exactly the
				// references): each later step then grows by gravity*dt^2 and every extrapolated
				float flGridZ = flGridPrevZ;
				for (int k = 0; k < 512; k++)
				{
					const float flStep = flD + flGravPerTick2 * float(k);
					if (flStep > 0.f) // apex passed - these are the landable descending ticks
					{
						if (GridCheck(flGridZ + flGridDuckShift, vTarget.z)
							|| (iJumpType == JT_AIRDASH && GridCheck(flGridZ + 20.f, vTarget.z)))
						{
							tResult.m_bGridCatch = true;
							break;
						}
						if (flGridZ < vTarget.z - 150.f)
							break; // fell far past the point - no tick aligned
					}
					flGridZ -= flStep;
				}
			}
		}
		flGridPrevZ = flZ;

		// Track how close the ducked descent got to the point's height, plus how fast it was
		// moving and how far from the point it still was (picker's slow-pass fallback + readout).
		if (bAir && flVz < 0.f)
		{
			const float flGap = fabsf(flZ - vTarget.z);
			if (flGap < tResult.m_flGap)
			{
				tResult.m_flGap = flGap;
				tResult.m_flGapVz = flVz;
				tResult.m_flGapFlat = Vec3(vTarget.x - vNow.x, vTarget.y - vNow.y, 0.f).Length2D();
			}
		}

		// The pin: free fall changes vz by a full gravity tick (~12), so a single tick can read
		const bool bPin = bAir && fabsf(flVz + flHalfGrav) < flSurfTol;
		if (bPin && fabsf(flZ - vTarget.z) < flZNear)
		{
			tResult.m_bGraze = true;
			if (bPrevPin)
			{
				tResult.m_bCatch = true;
				break;
			}
		}
		bPrevPin = bPin;

		// Landing check with displacement tolerance: launching from (or flying over) bumpy dirt,
		// the arc can clip a bump for a single grounded tick while still genuinely airborne -
		iGroundTicks = bAir ? 0 : iGroundTicks + 1;
		if (iSince > 2 && iGroundTicks >= 2)
			break; // landed without catching
		if (flVz < 0.f && flZ < vTarget.z - 30.f)
			break; // fell well past the point - no tick caught it
	}

	pCmd->buttons = iBtnBackup;
	pCmd->forwardmove = flFwdBackup;
	pCmd->sidemove = flSideBackup;
	I::Prediction->RestoreEntityToPredictedFrame(nBasePredicted - 1);

	tResult.m_iLaunchDelay = iLaunch;
	tResult.m_flLaunchZ = vLaunch.z;
	tResult.m_flApexGain = iLaunch >= 0 ? flPeakZ - vLaunch.z : 0.f;
	// If the arc never travelled that far horizontally, the point is out of this jump's range.
	tResult.m_flAtDistGain = (flAtDistZ == -FLT_MAX) ? 0.f : (flAtDistZ - vLaunch.z);
	return tResult;
}


void CMisc::PixelSurfAssist(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	m_bPixelSurfAssistActive = false;

	// Finished-hop bookkeeping must run UNCONDITIONALLY, before any early return. It used to sit
	// behind the key/radius gates below, so firing one assist jump and releasing the key mid-air
	if (m_iAssistJumpType != JT_NONE)
	{
		const int iNow = I::GlobalVars->tickcount;
		if (iNow < m_iAssistJumpStartTick
			|| ((m_iPrePredictionFlags & FL_ONGROUND) && iNow > m_iAssistJumpStartTick)
			// Catching the surf finishes the hop too. A pinned player is never FL_ONGROUND, so
			// without this the steer lock ran forever once you landed ON the point - still
			|| m_bPixelSurfingNow)
			m_iAssistJumpType = JT_NONE;
	}
	// Pending-launch sanity, same unconditional spot (the iteration-5 lesson: every tickcount
	if (m_iAssistPendingType != JT_NONE)
	{
		const int iNow = I::GlobalVars->tickcount;
		if (iNow > m_iAssistPendingUntil || m_iAssistPendingUntil > iNow + 120
			|| m_bPixelSurfingNow || !Vars::Misc::Movement::PixelSurfAssist.Value)
			m_iAssistPendingType = JT_NONE;
	}

	const std::string sMap = I::EngineClient->GetLevelName() ? I::EngineClient->GetLevelName() : "";
	const float flSnap = Vars::Misc::Movement::PixelSurfAssistSnapDist.Value;

	// Pull this map's saved targets from disk on the first run / whenever the map changes.
	if (sMap != m_sAssistPointsMap)
		LoadAssistPoints(sMap);

	// Where the crosshair hits the world (shared by set-point / delete-point).
	auto AimHit = [&](Vec3& vOut) -> bool
	{
		Vec3 vForward; Math::AngleVectors(pCmd->viewangles, &vForward);
		const Vec3 vEye = pLocal->GetEyePosition();
		CTraceFilterWorldAndPropsOnly filter;
		CGameTrace trace;
		SDK::Trace(vEye, vEye + vForward * 4000.f, MASK_PLAYERSOLID, &filter, &trace);
		vOut = trace.endpos;
		return trace.fraction < 1.f;
	};

	// Set-point: a target can only be placed on a pixel-surf spot the finder found - snap to the
	// nearest finder point on this map within the snap distance. Works any time.
	const bool bSetHeld = Vars::Misc::Movement::PixelSurfAssistSetPoint.Value;
	if (bSetHeld && !m_bAssistSetHeld)
	{
		Vec3 vHit;
		if (AimHit(vHit))
		{
			const PixelSurfPoint_t* pSnap = nullptr;
			float flBestSnap = flSnap;
			for (const auto& p : m_vPixelSurfPoints)
			{
				if (p.m_sMap != sMap)
					continue;
				const float flDist = vHit.DistTo(p.m_vPos);
				if (flDist < flBestSnap)
				{
					flBestSnap = flDist;
					pSnap = &p;
				}
			}
			if (pSnap)
			{
				m_vAssistPoints.push_back({ pSnap->m_vPos, sMap });
				WriteAssistPoints(sMap);
			}
		}
	}
	m_bAssistSetHeld = bSetHeld;

	// Delete-point: remove the saved target on this map nearest to where you're aiming.
	const bool bDelHeld = Vars::Misc::Movement::PixelSurfAssistDeletePoint.Value;
	if (bDelHeld && !m_bAssistDeleteHeld)
	{
		Vec3 vHit;
		if (AimHit(vHit))
		{
			int iErase = -1;
			float flBestDel = flSnap;
			for (int i = 0; i < int(m_vAssistPoints.size()); i++)
			{
				if (m_vAssistPoints[i].m_sMap != sMap)
					continue;
				const float flDist = vHit.DistTo(m_vAssistPoints[i].m_vPos);
				if (flDist < flBestDel)
				{
					flBestDel = flDist;
					iErase = i;
				}
			}
			if (iErase >= 0)
			{
				m_vAssistPoints.erase(m_vAssistPoints.begin() + iErase);
				WriteAssistPoints(sMap);
			}
		}
	}
	m_bAssistDeleteHeld = bDelHeld;

	if (!Vars::Misc::Movement::PixelSurfAssist.Value || m_vAssistPoints.empty())
		return;

	// Nearest ACTIVE saved point on this map inside its engage radius (per-point radius from the
	// ENTER menu wins over the global slider when set).
	const Vec3 vOrigin = pLocal->m_vecOrigin();
	const PixelSurfPoint_t* pPoint = nullptr;
	float flBestDist = 1e9f;
	for (const auto& p : m_vAssistPoints)
	{
		if (p.m_sMap != sMap || !p.m_bActive)
			continue;
		const float flRadius = p.m_flRadius > 0.f ? p.m_flRadius : Vars::Misc::Movement::PixelSurfAssistRadius.Value;
		const float flDist = vOrigin.DistTo(p.m_vPos);
		if (flDist > flRadius || flDist >= flBestDist)
			continue;
		flBestDist = flDist;
		pPoint = &p;
	}
	if (!pPoint)
		return;
	const Vec3* pTarget = &pPoint->m_vPos;

	m_bPixelSurfAssistActive = true;

	// Geometry to the target. Per request the assist does NOT steer your movement - it only
	// jumps; horizontal aiming/strafing toward the point is left entirely to you.
	const Vec3 vDelta = *pTarget - vOrigin;
	const float flFlat = Vec3(vDelta.x, vDelta.y, 0.f).Length2D();

	// --- Jump onto it: pick the cheapest jump that reaches the target, then drive its duck. ---
	// (Jump-type ids JT_* are the shared CMisc::EJumpType enum, declared in Misc.h.)

	// Real current ground contact is the pre-prediction flag (post-prediction already reflects
	// the cmd we're building); this matches how MiniJump/LongJump detect leaving the ground.
	const bool bOnGround = m_iPrePredictionFlags & FL_ONGROUND;

	// ---- Pending-launch service. ----
	// A pick armed mid-air fires on the REAL ground contact: the sim already validated the whole
	if (m_iAssistPendingType != JT_NONE && m_iAssistJumpType == JT_NONE && bOnGround)
	{
		const int iPick = m_iAssistPendingType;
		m_iAssistPendingType = JT_NONE;

		m_iAssistJumpType = iPick;
		m_iAssistJumpStartTick = I::GlobalVars->tickcount;
		pCmd->buttons |= IN_JUMP;
		const bool bLaunchDuck = iPick == JT_MINI || iPick == JT_MINIDUCK || iPick == JT_LONG || iPick == JT_LONGDUCK;
		if (bLaunchDuck)
			pCmd->buttons |= IN_DUCK;
		else
			pCmd->buttons &= ~IN_DUCK;
		// Steer lock stays as captured at ARM time - the sim flew that wish through the whole
		// approach, so recapturing here (possibly mid keyboard adjustment) would break the match.

		m_tAssistJumpBox.m_iJumpType = iPick;
		m_tAssistJumpBox.m_flTargetZ = pTarget->z;
		m_tAssistJumpBox.m_flCurTime = I::GlobalVars->curtime;
	}

	// The picker runs on the ground (launch now) AND while descending mid-air (plan the hop off
	// the predicted landing - this is where the previous jump's carried speed/duck state shapes
	const bool bDescending = !bOnGround && m_vPrePredictionVelocity.z < 0.f;

	if ((bOnGround || bDescending) && m_iAssistJumpType == JT_NONE && m_iAssistPendingType == JT_NONE)
	{
		// The jump box is NOT cleared here any more: hard-clearing on every picker re-entry meant

		// The pick is no longer "cheapest jump whose reach clears the height" - clearing is not
		// catching. Each type's whole arc is run through the real movement code from this exact
		struct Cand_t { int m_iType; JumpCatchResult_t m_tRes; };
		Cand_t aCands[] = // readout order: mini, regular, mini+duck, crouch, long, long+duck
		{
			{ JT_MINI,     {} },
			{ JT_REGULAR,  {} },
			{ JT_MINIDUCK, {} },
			{ JT_CROUCH,   {} },
			{ JT_LONG,     {} },
			{ JT_LONGDUCK, {} },
		};

		// Per-point jump-type allowance from the ENTER menu (bit k = aCands[k] allowed).
		auto Allowed = [&](int k) -> bool { return pPoint->m_iTypeMask & (1 << k); };

		// No jump gains ~90u even held-duck; skip the six full-arc sims when the point is
		// hopelessly above us (or we're outside the sane flat range the gate below enforces).
		const bool bWorthSimming = vDelta.z < 90.f && flFlat < 450.f;
		if (bWorthSimming)
			for (int k = 0; k < 6; k++)
				if (Allowed(k))
				{
					aCands[k].m_tRes = SimJumpCatch(pLocal, pCmd, aCands[k].m_iType, *pTarget, flFlat);
					// All six share the same approach (same held wish, no jump until contact) -
					// if this one never found ground inside the window, none of them will.
					if (aCands[k].m_tRes.m_iLaunchDelay < 0)
						break;
				}

		// Three tiers, best first. Pin-only firing turned out too strict in-game (the canned sim
		static constexpr int aPriority[] = { JT_REGULAR, JT_MINI, JT_LONG, JT_MINIDUCK, JT_CROUCH, JT_LONGDUCK };
		int iPick = JT_NONE;

		// Effective max height this jump can actually deliver, hoisted here so the grid-catch floor
		// below and the clears tier further down share one definition. flApexGain is the REAL
		constexpr float flSimHighBias = 4.f;  // sim reads ~4u high of real landings (calibrated)
		constexpr float flDuckShift = 20.f;   // VEC_HULL_MAX.z 82 - VEC_DUCK_HULL_MAX.z 62
		auto EffApex = [&](const Cand_t& c) -> float
		{
			float fl = c.m_tRes.m_flApexGain - flSimHighBias;
			// mini+duck's catch height comes from PixelSurf's descent duck, which its
			// (deliberately duck-free) sim apex doesn't carry - the descent samples do.
			if (c.m_iType == JT_MINIDUCK)
				fl += flDuckShift;
			return fl;
		};

		// Grid catch is only honest when the REAL arc actually reaches the point. The grid is pure
		auto GridCatchOk = [&](const Cand_t& c) -> bool
		{
			if (!c.m_tRes.m_bGridCatch)
				return false;
			const float flNeedK = pTarget->z - c.m_tRes.m_flLaunchZ;
			return EffApex(c) >= flNeedK - flSimHighBias;
		};

		// Tier 0 - the criterion, exact and strafe-proof: some future descending tick
		// of this launch lands in the point's sub-unit catch window (analytic grid). From a given
		for (int iWant : aPriority)
		{
			for (const auto& c : aCands)
			{
				if (c.m_iType == iWant && GridCatchOk(c))
				{
					iPick = iWant;
					break;
				}
			}
			if (iPick != JT_NONE)
				break;
		}

		for (int iPass = 0; iPass < 2 && iPick == JT_NONE; iPass++) // pass 0 = catch, pass 1 = graze
		{
			for (int iWant : aPriority)
			{
				for (const auto& c : aCands)
				{
					if (c.m_iType == iWant && (iPass == 0 ? c.m_tRes.m_bCatch : c.m_tRes.m_bGraze))
					{
						iPick = iWant;
						break;
					}
				}
				if (iPick != JT_NONE)
					break;
			}
		}

		// Displacement ground gets extra slack everywhere below: the launch height fraction
		// shifts with every step on dirt, so strict windows rarely line up there. (Trace reaches
		CGameTrace groundTrace;
		CTraceFilterWorldAndPropsOnly groundFilter;
		SDK::Trace(vOrigin, vOrigin - Vec3(0.f, 0.f, 80.f), MASK_PLAYERSOLID, &groundFilter, &groundTrace);
		const bool bDispGround = groundTrace.fraction < 1.f && groundTrace.IsDispSurface();

		// Tier-3 eligibility, shared with the readout's state column.
		const float flMaxOver = bDispGround ? 18.f : 10.f; // flSimHighBias/flDuckShift/EffApex hoisted above tier 0
		auto ClearsOk = [&](const Cand_t& c) -> bool
		{
			if (c.m_tRes.m_iLaunchDelay < 0)
				return false;
			const float flNeedK = pTarget->z - c.m_tRes.m_flLaunchZ; // need from the LAUNCH, not from here
			if (c.m_tRes.m_flAtDistGain - flSimHighBias < flNeedK + 1.f)
				return false; // doesn't clear the height where the point actually is
			const float flOver = EffApex(c) - flNeedK;
			return flOver >= 0.f && flOver <= flMaxOver;
		};

		if (iPick == JT_NONE)
		{
			float flBestOver = 1e9f;
			for (int k = 0; k < 6; k++)
			{
				const auto& c = aCands[k];
				if (!Allowed(k) || !ClearsOk(c))
					continue;
				const float flOver = EffApex(c) - (pTarget->z - c.m_tRes.m_flLaunchZ);
				if (flOver < flBestOver)
				{
					flBestOver = flOver;
					iPick = c.m_iType;
				}
			}
		}

		if (iPick != JT_NONE)
		{
			// Capture the commit-tick wish in WORLD space for the steer lock. SimJumpCatch held
			// this exact movement for the whole simulated approach + arc - if the player's keys
			Vec3 vFwd, vRight;
			Math::AngleVectors(Vec3(0.f, pCmd->viewangles.y, 0.f), &vFwd, &vRight, nullptr);
			Vec3 vWish = vFwd * pCmd->forwardmove + vRight * pCmd->sidemove;
			vWish.z = 0.f;
			m_flAssistWishMag = vWish.Length2D();
			if (m_flAssistWishMag > 1.f)
				m_vAssistWishDir = vWish * (1.f / m_flAssistWishMag);
			else
				m_flAssistWishMag = 0.f;

			const int iDelay = [&]{ for (const auto& c : aCands) if (c.m_iType == iPick) return c.m_tRes.m_iLaunchDelay; return 0; }();
			if (bOnGround && iDelay <= 0)
			{
				// Grounded with a zero-delay plan: fire now (the classic path).
				m_iAssistJumpType = iPick;
				m_iAssistJumpStartTick = I::GlobalVars->tickcount;
				pCmd->buttons |= IN_JUMP;
				// Mini/long variants press their duck WITH the jump - a fresh duck on the launch
				// cmd is taken (only an ALREADY-held duck, FL_DUCKING, eats the jump) and is what
				const bool bLaunchDuck = iPick == JT_MINI || iPick == JT_MINIDUCK || iPick == JT_LONG || iPick == JT_LONGDUCK;
				if (bLaunchDuck)
					pCmd->buttons |= IN_DUCK;
				else
					pCmd->buttons &= ~IN_DUCK;

				// Stamp the jump-box pop-up: which jump we just fired + the target's Z height.
				m_tAssistJumpBox.m_iJumpType = iPick;
				m_tAssistJumpBox.m_flTargetZ = pTarget->z;
				m_tAssistJumpBox.m_flCurTime = I::GlobalVars->curtime;
			}
			else
			{
				// Mid-air (or the launch sits a few ticks out): arm the pending launch. The hop
				m_iAssistPendingType = iPick;
				m_iAssistPendingUntil = I::GlobalVars->tickcount + iDelay + 8;
			}
		}
	}

	// Scout double jump: while airborne with a dash still in pocket, it can be fired at exactly
	// the height whose arc lands on the point - "jump from the perfect height". Re-evaluated

	// Dash budget, engine-faithful (CTFPlayer::CanAirDash, tf_player_shared.cpp): 1 by default,
	// 2 with the Atomizer's air_dash_count attribute ON THE ACTIVE WEAPON (the triple jump), 5
	auto CanAirDashNow = [&]() -> bool
	{
		if (pLocal->m_iClass() != TF_CLASS_SCOUT)
			return false;
		if (pLocal->InCond(TF_COND_HALLOWEEN_SPEED_BOOST))
			return true;
		if (pLocal->InCond(TF_COND_SODAPOPPER_HYPE))
			return pLocal->m_iAirDash() < 5;
		int iDashCount = 1;
		if (auto pWeapon = pLocal->m_hActiveWeapon()->As<CTFWeaponBase>())
			iDashCount = int(SDK::AttribHookValue(float(iDashCount), "air_dash_count", pWeapon));
		return pLocal->m_iAirDash() < iDashCount;
	};
	// An assist-fired dash that has provably missed - descending again with the feet already
	const bool bDashRetry = m_iAssistJumpType == JT_AIRDASH
		&& m_vPrePredictionVelocity.z < 0.f && vOrigin.z < pTarget->z;

	if (!bOnGround && (m_iAssistJumpType == JT_NONE || bDashRetry) && m_iAssistPendingType == JT_NONE
		&& CanAirDashNow()
		&& (pPoint->m_iTypeMask & (1 << 6))
		&& vDelta.z < 90.f && flFlat < 450.f)
	{
		const JumpCatchResult_t tDash = SimJumpCatch(pLocal, pCmd, JT_AIRDASH, *pTarget, flFlat);
		const float flDashOver = (tDash.m_flApexGain - 4.f) - vDelta.z; // same sim-high bias as grounded

		// Fast falls drop more than 8u per tick, so a fixed window gets skipped clean between
		// per-tick evaluations - widen it by the current per-tick fall so the perfect-height band
		const float flFallPerTick = std::max(0.f, -pLocal->m_vecVelocity().z) * I::GlobalVars->interval_per_tick;
		// Rising and still under the point: this is the moment to dash. Below the surf the apex
		const bool bRisingBelow = pLocal->m_vecVelocity().z > 0.f && vOrigin.z < pTarget->z;
		const bool bHeightWindow = flDashOver >= 0.5f && flDashOver <= std::max(8.f, flFallPerTick + 2.f)
			// Column clearance is only a VETO when the sim actually measured it.
			&& (bRisingBelow || tDash.m_flAtDistGain <= 0.f || tDash.m_flAtDistGain - 4.f >= vDelta.z + 1.f);

		// Same overhang/below-the-surf floor the grounded grid catch now uses: the dash's analytic
		// grid is free-flight math, so under a ledge it extrapolates past where the real hull bonked
		const bool bDashGridReaches = (tDash.m_flApexGain - 4.f) + 20.f >= vDelta.z;

		if ((tDash.m_bGridCatch && bDashGridReaches) || tDash.m_bCatch || tDash.m_bGraze || bHeightWindow)
		{
			m_iAssistJumpType = JT_AIRDASH;
			m_iAssistJumpStartTick = I::GlobalVars->tickcount;
			pCmd->buttons |= IN_JUMP;
			pCmd->buttons &= ~IN_DUCK; // launch the dash with the same clean buttons the sim flew

			// Steer lock capture, same as the grounded commit - but ONLY when the sim actually
			if (tDash.m_bCatch || tDash.m_bGraze)
			{
				Vec3 vFwd, vRight;
				Math::AngleVectors(Vec3(0.f, pCmd->viewangles.y, 0.f), &vFwd, &vRight, nullptr);
				Vec3 vWish = vFwd * pCmd->forwardmove + vRight * pCmd->sidemove;
				vWish.z = 0.f;
				m_flAssistWishMag = vWish.Length2D();
				if (m_flAssistWishMag > 1.f)
					m_vAssistWishDir = vWish * (1.f / m_flAssistWishMag);
				else
					m_flAssistWishMag = 0.f;
			}
			else
				m_flAssistWishMag = 0.f; // window-fired: your strafe, your path

			m_tAssistJumpBox.m_iJumpType = JT_AIRDASH;
			m_tAssistJumpBox.m_flTargetZ = pTarget->z;
			m_tAssistJumpBox.m_flCurTime = I::GlobalVars->curtime;
		}
	}

	// Post-launch duck choreography: reproduce live EXACTLY what SimJumpCatch validated the pick
	if ((m_iAssistJumpType != JT_NONE || m_iAssistPendingType != JT_NONE) && !bOnGround)
	{
		if (m_iAssistJumpType != JT_NONE)
		{
			const int iSinceLaunch = I::GlobalVars->tickcount - m_iAssistJumpStartTick;
			const bool bLong = m_iAssistJumpType == JT_LONG || m_iAssistJumpType == JT_LONGDUCK;
			const bool bHold = m_iAssistJumpType == JT_CROUCH || m_iAssistJumpType == JT_LONGDUCK;

			if (bHold || (bLong && iSinceLaunch >= 1 && iSinceLaunch <= 2))
				pCmd->buttons |= IN_DUCK;
		}

		// Steer lock: replay the commit-tick wish (the reference replays its recorded sim cmds the same
		// way). World-space, so turning the mouse mid-hop doesn't bend the path - the wish stays
		if (Vars::Misc::Movement::PixelSurfAssistSteer.Value && m_flAssistWishMag > 0.f)
		{
			Vec3 vWishAngle = Math::VectorAngles(m_vAssistWishDir);
			const float flRot = MVSUPE_DEG2RAD(vWishAngle.y - pCmd->viewangles.y);
			pCmd->forwardmove = cosf(flRot) * m_flAssistWishMag;
			pCmd->sidemove = -sinf(flRot) * m_flAssistWishMag;
		}
	}
}


void CMisc::DrawPixelSurf(CTFPlayer* pLocal)
{
	if (!pLocal || !pLocal->IsAlive() || !I::EngineClient->IsInGame())
	{
		m_iAssistPointHover = -1; // never let a stale hover open the ENTER menu from a dead frame
		return;
	}

	const std::string sMap = I::EngineClient->GetLevelName() ? I::EngineClient->GetLevelName() : "";

	// Distance fade: points (and the finder line) past the assist engage radius + 200u stop
	// rendering, fading out over the last 100u so they don't pop. Distance is from the local origin.
	const Vec3 vLocalOrigin = pLocal->m_vecOrigin();
	const float flFadeCutoff = Vars::Misc::Movement::PixelSurfAssistRadius.Value + 200.f;
	constexpr float flFadeRange = 100.f;
	auto FadeAlpha = [&](const Vec3& vWorld) -> float
	{
		const float flD = vLocalOrigin.DistTo(vWorld);
		if (flD >= flFadeCutoff)
			return 0.f;
		return std::clamp((flFadeCutoff - flD) / flFadeRange, 0.f, 1.f);
	};

	// Pixel finder: every found surf spot on this map, plus the aim line while scanning.
	// The point under the crosshair grows + recolours (so you can see which one you're aiming at)
	if (Vars::Misc::Movement::PixelFinder.Value || !m_vPixelSurfPoints.empty())
	{
		const Color_t tPoint = Color_t(150, 150, 150, 255);     // unselected = gray
		const Color_t tHover = Vars::Menu::Theme::Accent.Value; // selected (crosshair) = accent

		const float flCenterX = H::Draw.m_nScreenW / 2.f;
		const float flCenterY = H::Draw.m_nScreenH / 2.f;
		const float flHoverRadius = H::Draw.Scale(16, Scale_Round); // crosshair pickup radius (px)
		const float flNormalSize = H::Draw.Scale(4, Scale_Round);
		const float flHoverSize = H::Draw.Scale(8, Scale_Round);
		const float flLerp = std::clamp(I::GlobalVars->frametime * 12.f, 0.f, 1.f);

		// Which on-screen point is the crosshair over? (nearest to screen centre within radius)
		int iHover = -1;
		float flHoverBest = flHoverRadius;
		Vec3 vScreen;
		for (int i = 0; i < int(m_vPixelSurfPoints.size()); i++)
		{
			if (m_vPixelSurfPoints[i].m_sMap != sMap)
				continue;
			if (!SDK::W2S(m_vPixelSurfPoints[i].m_vPos, vScreen))
				continue;
			const float flDx = vScreen.x - flCenterX, flDy = vScreen.y - flCenterY;
			const float flDist = sqrtf(flDx * flDx + flDy * flDy);
			if (flDist < flHoverBest)
			{
				flHoverBest = flDist;
				iHover = i;
			}
		}

		// Delete the hovered point on a fresh delete-key press.
		const bool bDel = Vars::Misc::Movement::PixelSurfAssistDeletePoint.Value;
		if (bDel && !m_bPixelDeleteHeldDraw && iHover >= 0)
		{
			m_vPixelSurfPoints.erase(m_vPixelSurfPoints.begin() + iHover);
			iHover = -1;
		}
		m_bPixelDeleteHeldDraw = bDel;

		for (int i = 0; i < int(m_vPixelSurfPoints.size()); i++)
		{
			auto& p = m_vPixelSurfPoints[i];
			if (p.m_sMap != sMap)
				continue;
			if (!SDK::W2S(p.m_vPos, vScreen))
				continue;

			const float flFade = FadeAlpha(p.m_vPos);
			if (flFade <= 0.f)
				continue;

			const bool bHover = (i == iHover);
			const float flTarget = bHover ? flHoverSize : flNormalSize;
			p.m_flSize += (flTarget - p.m_flSize) * flLerp; // smooth grow/shrink (and pop-in from 0)

			const Color_t tCol = bHover ? tHover : tPoint;
			H::Draw.FillCircle(int(vScreen.x), int(vScreen.y), p.m_flSize * 0.6f, 32, Color_t(tCol.r, tCol.g, tCol.b, int(90 * flFade)));
			H::Draw.LineCircle(int(vScreen.x), int(vScreen.y), p.m_flSize, 48, Color_t(tCol.r, tCol.g, tCol.b, int(tCol.a * flFade)));
		}

		if (Vars::Misc::Movement::PixelFinder.Value && m_bPixelFinderFirst)
		{
			Vec3 vA, vB;
			const float flFade = FadeAlpha(m_vPixelFinderEnd);
			if (flFade > 0.f && SDK::W2S(m_vPixelFinderStart, vA) && SDK::W2S(m_vPixelFinderEnd, vB))
				H::Draw.Line(int(vA.x), int(vA.y), int(vB.x), int(vB.y), Color_t(tPoint.r, tPoint.g, tPoint.b, int(tPoint.a * flFade)));
		}
	}

	// Pixel surf line: the path of the surf in progress (live) plus every saved surfable strip on this
	// map, drawn as a connected line in the configured colour. The saved strip nearest the crosshair
	if (Vars::Misc::Movement::PixelSurfLine.Value)
	{
		const Color_t tLine = Vars::Misc::Movement::PixelSurfLineColor.Value;
		// Surf lines get their own far fade - the assist-point cutoff (radius+200, ~500u) was clipping
		constexpr float flSurfFadeCutoff = 4096.f;
		constexpr float flSurfFadeRange = 256.f;
		auto SurfFade = [&](const Vec3& vWorld) -> float
		{
			const float flD = vLocalOrigin.DistTo(vWorld);
			if (flD >= flSurfFadeCutoff)
				return 0.f;
			return std::clamp((flSurfFadeCutoff - flD) / flSurfFadeRange, 0.f, 1.f);
		};
		const float flCenterX = H::Draw.m_nScreenW / 2.f;
		const float flCenterY = H::Draw.m_nScreenH / 2.f;
		const float flHoverRadius = H::Draw.Scale(20, Scale_Round); // crosshair pickup radius (px)

		// Which strip is the crosshair over? (its nearest on-screen point within the pickup radius)
		int iHoverSeg = -1;
		float flHoverBest = flHoverRadius;
		Vec3 vScreenPt;
		for (int s = 0; s < int(m_vSurfLineSegments.size()); s++)
		{
			if (m_vSurfLineSegments[s].m_sMap != sMap)
				continue;
			for (const auto& p : m_vSurfLineSegments[s].m_vPoints)
			{
				if (!SDK::W2S(p, vScreenPt))
					continue;
				const float flDx = vScreenPt.x - flCenterX, flDy = vScreenPt.y - flCenterY;
				const float flDist = sqrtf(flDx * flDx + flDy * flDy);
				if (flDist < flHoverBest)
				{
					flHoverBest = flDist;
					iHoverSeg = s;
				}
			}
		}

		// Delete the hovered strip on a fresh delete-key press (own edge so it doesn't race the finder's).
		const bool bDel = Vars::Misc::Movement::PixelSurfAssistDeletePoint.Value;
		if (bDel && !m_bSurfLineDeleteHeldDraw && iHoverSeg >= 0)
		{
			m_vSurfLineSegments.erase(m_vSurfLineSegments.begin() + iHoverSeg);
			iHoverSeg = -1;
		}
		m_bSurfLineDeleteHeldDraw = bDel;

		for (int s = 0; s < int(m_vSurfLineSegments.size()); s++)
		{
			const auto& seg = m_vSurfLineSegments[s];
			if (seg.m_sMap != sMap || seg.m_vPoints.size() < 2)
				continue;
			const Color_t tCol = tLine; // always configured colour - no hover highlight (delete still works)
			Vec3 vA, vB;
			for (size_t i = 1; i < seg.m_vPoints.size(); i++)
			{
				const float flFade = SurfFade(seg.m_vPoints[i]);
				if (flFade <= 0.f)
					continue;
				if (SDK::W2S(seg.m_vPoints[i - 1], vA) && SDK::W2S(seg.m_vPoints[i], vB))
					H::Draw.Line(int(vA.x), int(vA.y), int(vB.x), int(vB.y), Color_t(tCol.r, tCol.g, tCol.b, int(tCol.a * flFade)));
			}
		}

		// The surf in progress: draw the path traced so far this ride (it becomes a saved strip on landing).
		if (m_vSurfLineCurrent.size() >= 2)
		{
			Vec3 vA, vB;
			for (size_t i = 1; i < m_vSurfLineCurrent.size(); i++)
			{
				const float flFade = SurfFade(m_vSurfLineCurrent[i]);
				if (flFade <= 0.f)
					continue;
				if (SDK::W2S(m_vSurfLineCurrent[i - 1], vA) && SDK::W2S(m_vSurfLineCurrent[i], vB))
					H::Draw.Line(int(vA.x), int(vA.y), int(vB.x), int(vB.y), Color_t(tLine.r, tLine.g, tLine.b, int(tLine.a * flFade)));
			}
		}
	}

	// Pixel surf assist: saved target points on this map. The active target (nearest ACTIVE point
	m_iAssistPointHover = -1;
	if (Vars::Misc::Movement::PixelSurfAssistRender.Value && !m_vAssistPoints.empty())
	{
		const Color_t tSelected = Vars::Menu::Theme::Accent.Value;
		const Color_t tUnselected = Color_t(150, 150, 150, 255);
		const Color_t tInactive = Color_t(90, 90, 90, 255);

		const float flCenterX = H::Draw.m_nScreenW / 2.f;
		const float flCenterY = H::Draw.m_nScreenH / 2.f;
		const float flHoverRadius = H::Draw.Scale(16, Scale_Round); // crosshair pickup radius (px)

		// Which saved point is the active target? Same rules as the picker: active points only,
		// per-point radius (from the ENTER menu) beating the global slider when set.
		int iActive = -1;
		float flActiveBest = 1e9f;
		for (int i = 0; i < int(m_vAssistPoints.size()); i++)
		{
			const auto& p = m_vAssistPoints[i];
			if (p.m_sMap != sMap || !p.m_bActive)
				continue;
			const float flRadius = p.m_flRadius > 0.f ? p.m_flRadius : Vars::Misc::Movement::PixelSurfAssistRadius.Value;
			const float flDist = vLocalOrigin.DistTo(p.m_vPos);
			if (flDist > flRadius || flDist >= flActiveBest)
				continue;
			flActiveBest = flDist;
			iActive = i;
		}

		// First pass: find the hovered point (nearest to the crosshair inside the pickup radius).
		float flHoverBest = flHoverRadius;
		Vec3 vScreen;
		for (int i = 0; i < int(m_vAssistPoints.size()); i++)
		{
			const auto& p = m_vAssistPoints[i];
			if (p.m_sMap != sMap || FadeAlpha(p.m_vPos) <= 0.f || !SDK::W2S(p.m_vPos, vScreen))
				continue;
			const float flScrDist = sqrtf(powf(vScreen.x - flCenterX, 2.f) + powf(vScreen.y - flCenterY, 2.f));
			if (flScrDist < flHoverBest)
			{
				flHoverBest = flScrDist;
				m_iAssistPointHover = i;
			}
		}

		for (int i = 0; i < int(m_vAssistPoints.size()); i++)
		{
			const auto& p = m_vAssistPoints[i];
			if (p.m_sMap != sMap)
				continue;
			const float flFade = FadeAlpha(p.m_vPos);
			if (flFade <= 0.f)
				continue;
			if (SDK::W2S(p.m_vPos, vScreen))
			{
				const Color_t& tBase = !p.m_bActive ? tInactive : (i == iActive) ? tSelected : tUnselected;
				const Color_t tCol = Color_t(tBase.r, tBase.g, tBase.b, int(tBase.a * flFade));
				const bool bBig = i == m_iAssistPointHover || i == m_iAssistPointMenu;
				H::Draw.LineCircle(int(vScreen.x), int(vScreen.y), bBig ? 11.f : 7.f, 48, tCol);
			}
		}
	}

	// Jump-box pop-up is drawn in the ImGui pass (CMenu::DrawPixelSurfAssistBox) so it can share the
	// watermark's exact font + rounded background. See GetAssistJumpBox() for the text/fade/expiry.
}


bool CMisc::GetAssistJumpBox(std::string& sTextOut, float& flFadeOut)
{
	if (!Vars::Misc::Movement::PixelSurfAssistJumpBox.Value || m_tAssistJumpBox.m_iJumpType == JT_NONE)
		return false;

	constexpr float flShow = 3.f; // seconds the box stays up after the jump (timer is the ONLY expiry)
	const float flAge = I::GlobalVars->curtime - m_tAssistJumpBox.m_flCurTime;
	if (flAge >= flShow)
	{
		m_tAssistJumpBox.m_iJumpType = JT_NONE; // expired - stop drawing
		return false;
	}
	if (flAge < 0.f)
		return false;

	static const char* aszNames[] =
	{
		"",                  // JT_NONE
		"regular jump",      // JT_REGULAR
		"crouch jump",       // JT_CROUCH
		"minijump",          // JT_MINI
		"minijump (duck)",   // JT_MINIDUCK
		"longjump",          // JT_LONG
		"longjump (duck)",   // JT_LONGDUCK
		"double jump",       // JT_AIRDASH (scout)
	};
	const int iType = m_tAssistJumpBox.m_iJumpType;
	const char* szName = (iType >= JT_REGULAR && iType <= JT_AIRDASH) ? aszNames[iType] : "jump";

	sTextOut = std::format("{} to {:.3f}", szName, m_tAssistJumpBox.m_flTargetZ);
	flFadeOut = std::clamp((flShow - flAge) / 0.4f, 0.f, 1.f); // fade the last ~0.4s
	return true;
}


static std::string PointsDir()
{
	std::string sDir = F::Configs.m_sConfigPath + "Points\\";
	try { if (!std::filesystem::exists(sDir)) std::filesystem::create_directories(sDir); } catch (...) {}
	return sDir;
}
static std::string PointsFile(const std::string& sMap)
{
	// Map names can contain '/' (e.g. workshop/x.ugc123) - flatten to a safe filename.
	std::string sSafe = sMap.empty() ? "unknown" : sMap;
	for (auto& c : sSafe) if (c == '/' || c == '\\' || c == ':') c = '_';
	return PointsDir() + sSafe + ".txt";
}

void CMisc::LoadAssistPoints(const std::string& sMap)
{
	// Drop this map's points from the combined list, then reload them from disk.
	std::erase_if(m_vAssistPoints, [&](const PixelSurfPoint_t& p) { return p.m_sMap == sMap; });
	m_sAssistPointsMap = sMap;

	std::ifstream f(PointsFile(sMap));
	if (!f.is_open())
		return;

	// Line format: "x y z [active radius typemask]" - the per-point settings are optional so
	// files written before the on-point ENTER menu existed still load with defaults.
	std::string sLine;
	while (std::getline(f, sLine))
	{
		std::istringstream ss(sLine);
		PixelSurfPoint_t p;
		if (!(ss >> p.m_vPos.x >> p.m_vPos.y >> p.m_vPos.z))
			continue;
		int iActive = 1;
		if (ss >> iActive >> p.m_flRadius >> p.m_iTypeMask)
		{
			p.m_bActive = iActive != 0;
			if (p.m_iTypeMask == 63)
				p.m_iTypeMask = 127; // pre-air-dash default ("all types") gains the new dash bit
		}
		p.m_sMap = sMap;
		m_vAssistPoints.push_back(p);
	}
}


bool CMisc::WriteAssistPoints(const std::string& sMap)
{
	std::ofstream f(PointsFile(sMap), std::ios::trunc);
	if (!f.is_open())
		return false;

	for (const auto& p : m_vAssistPoints)
	{
		if (p.m_sMap != sMap)
			continue;
		f << p.m_vPos.x << ' ' << p.m_vPos.y << ' ' << p.m_vPos.z << ' '
			<< (p.m_bActive ? 1 : 0) << ' ' << p.m_flRadius << ' ' << p.m_iTypeMask << '\n';
	}
	return bool(f);
}


void CMisc::AutoEdgebug(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!Vars::Misc::Movement::AutoEdgebug.Value || pLocal->OnSolid())
	{
		m_bEdgeBug = false;
		m_iEdgeBugMoveStage = EBStageEnum::Normal;
		m_iEdgeBugTicksLeft = m_iEdgeBugTicksTotal = 0;
		m_flEdgeBugStartYaw = m_flEdgeBugYawDelta = 0.f;
		m_vEdgeBugMove.Zero();
		m_vEdgebugPath.clear();
		return;
	}

	auto fRunStrafe = [&]()-> void
		{
			if (m_iEdgeBugTicksLeft < m_iEdgeBugTicksTotal
				&& m_vEdgebugPath.size() 
				&& m_vEdgebugPath.front().DistTo2D(pLocal->m_vecOrigin()) > 30.f)
			{
				m_bEdgeBug = false;
				m_iEdgeBugMoveStage = EBStageEnum::Normal;
				m_iEdgeBugTicksLeft = m_iEdgeBugTicksTotal = 0;
				m_flEdgeBugStartYaw = m_flEdgeBugYawDelta = 0.f;
				m_vEdgeBugMove.Zero();
				m_vEdgebugPath.clear();
				return;
			}
			if (m_flEdgeBugYawDelta)
			{
				if (G::Attacking == 1)
				{
					m_bEdgeBug = false;
					m_iEdgeBugMoveStage = EBStageEnum::Normal;
					m_iEdgeBugTicksLeft = m_iEdgeBugTicksTotal = 0;
					m_flEdgeBugStartYaw = m_flEdgeBugYawDelta = 0.f;
					m_vEdgeBugMove.Zero();
					m_vEdgebugPath.clear();
					return;
				}
				pCmd->viewangles.y = Math::NormalizeAngle(m_flEdgeBugStartYaw + m_flEdgeBugYawDelta * (1 + m_iEdgeBugTicksTotal - m_iEdgeBugTicksLeft));
				if (Vars::Misc::Movement::AutoEdgebug.Value != Vars::Misc::Movement::AutoEdgebugEnum::StrafeSilent)
					I::EngineClient->SetViewAngles(pCmd->viewangles);
			}
			pCmd->buttons = m_bEdgeBugCrouch ? (pCmd->buttons | IN_DUCK) : (pCmd->buttons & ~IN_DUCK);
			pCmd->forwardmove = m_vEdgeBugMove.x;
			pCmd->sidemove = m_vEdgeBugMove.y;
			m_iEdgeBugTicksLeft--;
		};

	if (m_bEdgeBug)
	{
		if (!m_iEdgeBugTicksLeft)
		{
			m_bEdgeBug = false;
			m_iEdgeBugMoveStage = EBStageEnum::Normal;
			m_iEdgeBugTicksTotal = 0;
			m_flEdgeBugStartYaw = m_flEdgeBugYawDelta = 0.f;
			m_vEdgeBugMove.Zero();
		}
		else
		{
			fRunStrafe();
			if (m_vEdgebugPath.size())
			{
				if (Vars::Colors::EdgebugPath.Value.a)
					G::PathStorage.emplace_back(m_vEdgebugPath, I::GlobalVars->curtime + TICK_INTERVAL, Vars::Colors::EdgebugPath.Value, Vars::Visuals::Path::StyleEnum::Line);
				m_vEdgebugPath.erase(m_vEdgebugPath.begin());
			}
			if (!m_bEdgeBugRepredict)
				return;
		}
	}

	size_t iSize = pLocal->GetIntermediateDataSize();
	auto pDataMap = pLocal->GetPredDescMap();
	if (!pDataMap)
		return;

	const int iLocalIdx = pLocal->entindex();
	if (!m_pEdgebugBackup || m_uEdgebugBackupSize < iSize)
	{
		if (m_pEdgebugBackup)
			I::MemAlloc->Free(m_pEdgebugBackup);
		m_pEdgebugBackup = reinterpret_cast<byte*>(I::MemAlloc->Alloc(iSize));
		m_uEdgebugBackupSize = iSize;
	}
	byte* pOriginalData = m_pEdgebugBackup;
	{
		CPredictionCopy copy = { PC_EVERYTHING, pOriginalData, PC_DATA_PACKED, pLocal, PC_DATA_NORMAL };
		copy.TransferData("EdgebugStore", iLocalIdx, pDataMap);
	}

	const bool bOldIsFirstPrediction = I::Prediction->m_bFirstTimePredicted;
	const bool bOldInPrediction = I::Prediction->m_bInPrediction;
	const float flOldFrametime = I::GlobalVars->frametime;
	const float flOldCurtime = I::GlobalVars->curtime;

	if (m_iEdgeBugTicksUntilLand)
	{
		// disable random if we are about to land, its not going to change anything anyway
		if (m_iEdgeBugMoveStage > EBStageEnum::NormalInverted
			&& TICKS_TO_TIME(m_iEdgeBugTicksUntilLand) < 0.4f)
			m_iEdgeBugMoveStage -= 2;

		m_iEdgeBugTicksUntilLand--;
	}

	const bool bNegateDir = Vars::Misc::Movement::AutoEdgebugTryNegativeDir.Value && m_iEdgeBugMoveStage % 2 != 0;
	const float flDirMult = bNegateDir ? -1.f : 1.f;

	float flForwardmoveMult = 1.f, flSidemoveMult = flDirMult;
	if (Vars::Misc::Movement::AutoEdgebugTryRandomMove.Value 
		&& m_iEdgeBugMoveStage > EBStageEnum::NormalInverted)
	{
		flForwardmoveMult = SDK::RandomFloat(-0.2f, 4.f);
		flSidemoveMult *= SDK::RandomFloat(0.3f, 2.f);
	}

	float flCurrentDirDelta = 0.f;
	{
		float flForward = pCmd->forwardmove, flSide = pCmd->sidemove;
		Vec3 vForward, vRight; Math::AngleVectors(pCmd->viewangles, &vForward, &vRight, nullptr);
		vForward.Normalize2D(), vRight.Normalize2D();
		Vec3 vWishDir = Math::VectorAngles({ vForward.x * flForward + vRight.x * flSide, vForward.y * flForward + vRight.y * flSide, 0.f });
		Vec3 vCurDir = Math::VectorAngles(pLocal->m_vecVelocity());
		flCurrentDirDelta = Math::NormalizeAngle(vWishDir.y - vCurDir.y);
	}

	float flYawDelta = 30.f;
	bool bShouldStrafe = Vars::Misc::Movement::AutoEdgebug.Value > Vars::Misc::Movement::AutoEdgebugEnum::Legit && G::Attacking != 1;
	if (bShouldStrafe && abs(flCurrentDirDelta) > 1.f)
		flYawDelta = std::clamp(flCurrentDirDelta, -45.f, 45.f);
	flYawDelta *= flDirMult;

	const float flStartYaw = pCmd->viewangles.y;
	if (bShouldStrafe && abs(Math::NormalizeAngle(flStartYaw + flYawDelta) - flStartYaw) > Vars::Misc::Movement::AutoEdgebugStrafeMaxDelta.Value)
		bShouldStrafe = false;

	static auto sv_gravity = H::ConVars.FindVar("sv_gravity");
	const float flFallPerTick = round(TICKS_TO_TIME(-sv_gravity->GetFloat()));

	const int iMaxTicks = TIME_TO_TICKS(1.5f);
	int iMaxStages = bShouldStrafe ? 4 : 2;

	bool bSuccess = false;
	std::vector<Vector>& vPath = m_vEdgebugScratchPath;
	vPath.reserve(iMaxTicks + 2);
	CMoveData moveData = {};
	const int iStrafeSamples = Vars::Misc::Movement::AutoEdgebugStrafeSamples.Value;
	for (int iStage = 0; iStage < iMaxStages; iStage++)
	{
		float flMaxYawDelta = abs(flYawDelta);
		float flYawDeltaAdd = flYawDelta /= iStrafeSamples;
		bool bEnd = false, bStrafe = iStage >= 2, bCrouch = iStage % 2 == 0;

		I::MoveHelper->SetHost(pLocal);
		while (!bEnd)
		{
			// restoring a prediction copy is ~5 times faster than calling RestoreEntityToPredictedFrame every time (plus its not even correct to use it here)
			CPredictionCopy copy = { PC_EVERYTHING, pLocal, PC_DATA_NORMAL, pOriginalData, PC_DATA_PACKED};
			copy.TransferData("EdgebugReset", iLocalIdx, pDataMap);

			vPath.clear();
			vPath.push_back(pLocal->m_vecOrigin());
			bEnd = !bStrafe || abs(flYawDelta) >= flMaxYawDelta;

			G::DummyCmd = *pCmd;
			G::DummyCmd.buttons = bCrouch ? (G::DummyCmd.buttons | IN_DUCK) : (G::DummyCmd.buttons & ~IN_DUCK);

			if (bStrafe)
			{
				if (!G::DummyCmd.forwardmove)
					G::DummyCmd.forwardmove = 30.f; // makes it detect wallbugs (will probably make it a separate feature later)
				if (!G::DummyCmd.sidemove)
					G::DummyCmd.sidemove = 450.f;
			}
			else
				G::DummyCmd.forwardmove = G::DummyCmd.sidemove = 0.f;

			G::DummyCmd.forwardmove = std::clamp(G::DummyCmd.forwardmove * flForwardmoveMult, -450.f, 450.f);
			G::DummyCmd.sidemove = std::clamp(G::DummyCmd.sidemove * flSidemoveMult, -450.f, 450.f);;
			
			pLocal->m_pCurrentCommand() = &G::DummyCmd;
			I::Prediction->m_bFirstTimePredicted = false;
			I::Prediction->m_bInPrediction = true;
			I::GlobalVars->frametime = I::Prediction->m_bEnginePaused ? 0.f : TICK_INTERVAL;
			I::GlobalVars->curtime = TICKS_TO_TIME(pLocal->m_nTickBase());

			CMoveData& moveData = m_tEdgebugMoveData;
			Vector vOriginalVelocity;
			for (int iTick = 1; iTick <= iMaxTicks; iTick++)
			{
				Vector vPreviousVelocity = pLocal->m_vecVelocity();
				bool bWasOnSolid = pLocal->OnSolid();
				if (bWasOnSolid && iStage == 0)
					m_iEdgeBugTicksUntilLand = iTick - 1;

				if (bStrafe)
				{
					G::DummyCmd.viewangles.y = Math::NormalizeAngle(pCmd->viewangles.y + flYawDelta * iTick);
					if (abs(G::DummyCmd.viewangles.y - flStartYaw) > Vars::Misc::Movement::AutoEdgebugStrafeMaxDelta.Value)
						break;
				}

				I::Prediction->SetLocalViewAngles(G::DummyCmd.viewangles);
				I::Prediction->SetupMove(pLocal, &G::DummyCmd, I::MoveHelper, &moveData);
				I::GameMovement->ProcessMovement(pLocal, &moveData); // dont mind the sudden water splashing sounds, its just your ghost copy drowning in another realm
				I::Prediction->FinishMove(pLocal, pCmd, &moveData);
				vPath.push_back(pLocal->m_vecOrigin());

				if (pLocal->m_nWaterLevel() > 0)
					break; // simmed into water: edgebugs need solid contact, nothing to find below the surface and water sims are expensive

				if (vPreviousVelocity.z >= 0.f || bWasOnSolid)
					break;

				if (iTick == 1)
				{
					// fix for non-local servers
					vOriginalVelocity = pLocal->m_vecVelocity();
					continue;
				}

				if (vPreviousVelocity.z > vOriginalVelocity.z)
				{
					float flExpectedFallSpeed = vPreviousVelocity.z + flFallPerTick;
					float flFallSpeed = round(pLocal->m_vecVelocity().z);

					if (flExpectedFallSpeed == flFallSpeed)
					{
						m_iEdgeBugTicksTotal = m_iEdgeBugTicksLeft = iTick;
						m_bEdgeBugCrouch = bCrouch;
						if (bStrafe)
						{
							m_flEdgeBugStartYaw = flStartYaw;
							m_flEdgeBugYawDelta = flYawDelta;
							m_vEdgeBugMove = { moveData.m_flForwardMove, moveData.m_flSideMove };
						}
							m_vEdgebugPath = vPath;
							m_bEdgeBug = bEnd = bSuccess = true; // sound and indicator are fed by EdgebugDetect when the bug actually lands
						fRunStrafe();
						m_bEdgeBugRepredict = !m_bEdgeBug;
					}
					break;
				}
			}
			I::Prediction->m_bFirstTimePredicted = bOldIsFirstPrediction;
			I::Prediction->m_bInPrediction = bOldInPrediction;
			I::GlobalVars->frametime = flOldFrametime;
			I::GlobalVars->curtime = flOldCurtime;

			flYawDelta += flYawDeltaAdd;
		}
		I::MoveHelper->SetHost(nullptr);
		pLocal->m_pCurrentCommand() = nullptr;

		if (bSuccess)
			break;
	}
	const int iMaxMode = Vars::Misc::Movement::AutoEdgebugTryRandomMove.Value ? EBStageEnum::RandomInverted : EBStageEnum::NormalInverted;
	m_iEdgeBugMoveStage = m_iEdgeBugMoveStage < iMaxMode ? m_iEdgeBugMoveStage + 1 : EBStageEnum::Normal;

	CPredictionCopy copy = { PC_EVERYTHING, pLocal, PC_DATA_NORMAL, pOriginalData, PC_DATA_PACKED };
	copy.TransferData("EdgebugReset", iLocalIdx, pDataMap);
	G::DummyCmd = {};
}

void CMisc::AutoStrafe(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!Vars::Misc::Movement::AutoStrafe.Value || pLocal->m_hGroundEntity() || !(pLocal->m_afButtonLast() & IN_JUMP) && (pCmd->buttons & IN_JUMP))
		return;

	switch (Vars::Misc::Movement::AutoStrafe.Value)
	{
	case Vars::Misc::Movement::AutoStrafeEnum::Legit:
	{
		static auto cl_sidespeed = H::ConVars.FindVar("cl_sidespeed");
		const float flSideSpeed = cl_sidespeed->GetFloat();

		if (pCmd->mousedx)
		{
			pCmd->forwardmove = 0.f;
			pCmd->sidemove = pCmd->mousedx > 0 ? flSideSpeed : -flSideSpeed;
		}
		break;
	}
	case Vars::Misc::Movement::AutoStrafeEnum::Directional:
	{
		// credits: KGB
		if (!(pCmd->buttons & (IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT)))
			break;

		float flForward = pCmd->forwardmove, flSide = pCmd->sidemove;
		Vec3 vForward, vRight; Math::AngleVectors(pCmd->viewangles, &vForward, &vRight, nullptr);
		vForward.Normalize2D(), vRight.Normalize2D();

		Vec3 vWishDir = Math::VectorAngles({ vForward.x * flForward + vRight.x * flSide, vForward.y * flForward + vRight.y * flSide, 0.f });
		Vec3 vCurDir = Math::VectorAngles(pLocal->m_vecVelocity());
		float flDirDelta = Math::NormalizeAngle(vWishDir.y - vCurDir.y);
		if (fabsf(flDirDelta) > Vars::Misc::Movement::AutoStrafeMaxDelta.Value)
			break;

		float flTurnScale = Math::RemapVal(Vars::Misc::Movement::AutoStrafeTurnScale.Value, 0.f, 1.f, 0.9f, 1.f);
		float flRotation = Math::Deg2Rad((flDirDelta > 0.f ? -90.f : 90.f) + flDirDelta * flTurnScale);
		float flCosRot = cosf(flRotation), flSinRot = sinf(flRotation);

		pCmd->forwardmove = flCosRot * flForward - flSinRot * flSide;
		pCmd->sidemove = flSinRot * flForward + flCosRot * flSide;
	}
	}
}

void CMisc::AutoFaNJump(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	static bool bDidJump = false, bCanJump = false, bShouldRun = false;
	static int iTicksOnSolid = Vars::Misc::Movement::AutoFaNJumpOnSolidTicks.Value;

	bool bOnSolid = pLocal->OnSolid();
	if (!bOnSolid)
	{
		iTicksOnSolid = 0;
		if (pLocal->InCond(TF_COND_STUNNED))
			bCanJump = false;
	}
	else iTicksOnSolid += bCanJump = true;
	
	if (bDidJump)
	{
		if (iTicksOnSolid >= Vars::Misc::Movement::AutoFaNJumpOnSolidTicks.Value)
		{
			bDidJump = false;
			bShouldRun = false;
		}
		return;
	}

	if (!Vars::Misc::Movement::AutoFaNJump.Value || !bCanJump
		|| G::Attacking == 1 || !G::CanPrimaryAttack
		|| !pLocal->IsAlive() || pLocal->IsAGhost()
		|| pLocal->m_bScattergunJump()
		|| pLocal->m_MoveType() != MOVETYPE_WALK || pLocal->IsSwimming() 
		|| pLocal->IsTaunting() || pLocal->InCond(TF_COND_HALLOWEEN_KART))
		return;

	if (!pWeapon || pWeapon->m_iClip1() <= 0)
		return;

	int iDefIndex = pWeapon->m_iItemDefinitionIndex();
	if (iDefIndex != Scout_m_ForceANature && iDefIndex != Scout_m_FestiveForceANature)
		return;

	const Vector vVelocity = pLocal->m_vecVelocity();

	// If we dont have enough speed we wont be able to climb anything.
	// From my experience 150.f is enough to climb the grate dropdowns on 2fort bases
	if (vVelocity.Length2D() <= 150.f)
		return;

	Vector vAngles;
	float flScale = 0.f;
	if (vVelocity.Length2D() > 300.f && !bOnSolid)
	{
		flScale = Math::RemapVal(vVelocity.Length2D(), 300.f, 450.f, 0.f, 1.f);

		CGameTrace wallTrace = {};
		CTraceFilterWorldAndPropsOnly filter(pLocal);
		Vector vTrace = pLocal->GetCenter(), vMins =  pLocal->m_vecMins() * 3, vMaxs = pLocal->m_vecMaxs() * 3;
		vMins.z = -1;
		SDK::TraceHull(vTrace, vTrace, vMins, vMaxs, MASK_PLAYERSOLID_BRUSHONLY, &filter, &wallTrace);
		if (!wallTrace.DidHit())
		{
			// Preserve speed if we are not attempting to climb
			vAngles = pCmd->viewangles;
			vAngles.x = 89.f; 
		}
	}

	if (vAngles.x == 0.f)
	{
		Math::VectorAngles(vVelocity, vAngles);
		
		// 2fort sewers to bridge jump pitch angle if we have enough speed and not on solid
		vAngles.x = 37.f * (1 + 0.5f * flScale); 
		vAngles.z = 0;
	}

	if (!bShouldRun)
	{
		if (!Vars::Misc::Movement::AutoFaNJumpCheckCeiling.Value || vVelocity.z <= -300.f)
			bShouldRun = true;
		else
		{
			CGameTrace ceilingTrace = {};
			CTraceFilterWorldAndPropsOnly filter(pLocal);
			Vector vStart = pLocal->m_vecOrigin(); vStart += Vector(0, 0, pLocal->m_vecMaxs().z);
			Vector vEnd = vStart + Vector(0, 0, pLocal->m_vecMaxs().z * 1.25);

			// Increase hull size because if we change the pitch the trajectory also changes
			float flSizeScale = 1 + 0.75f * flScale;
			SDK::TraceHull(vStart, vEnd, pLocal->m_vecMins() * flSizeScale, pLocal->m_vecMaxs() * flSizeScale, MASK_PLAYERSOLID_BRUSHONLY, &filter, &ceilingTrace);
			if (!ceilingTrace.DidHit())
				bShouldRun = true;
		}
	}

	if (bShouldRun)
	{
		if (bOnSolid)
			pCmd->buttons |= IN_JUMP;

		pCmd->buttons |= IN_ATTACK;
		G::Attacking = true;
		bDidJump = true;

		pCmd->viewangles = vAngles;
		G::SilentAngles = true;
	}
}

void CMisc::AutoRevJump(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!Vars::Misc::Movement::AutoRevJump.Value || !pLocal->m_hGroundEntity())
		return;

	if (auto pWeapon = H::Entities.GetWeapon(); !pWeapon || pWeapon->GetWeaponID() != TF_WEAPON_MINIGUN || pWeapon->As<CTFMinigun>()->m_iWeaponState() != AC_STATE_IDLE)
		return;

	if (!(pCmd->buttons & IN_ATTACK2) || G::LastUserCmd->buttons & IN_ATTACK2)
		return;

	pCmd->buttons |= IN_JUMP;
}

void CMisc::MovementLock(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	static bool bLock = false;

	if (!Vars::Misc::Movement::MovementLock.Value || pLocal->InCond(TF_COND_HALLOWEEN_KART))
	{
		bLock = false;
		return;
	}

	static Vec3 vMove = {}, vView = {};
	if (!bLock)
	{
		bLock = true;
		vMove = { pCmd->forwardmove, pCmd->sidemove, pCmd->upmove };
		vView = pCmd->viewangles;
	}

	pCmd->forwardmove = vMove.x, pCmd->sidemove = vMove.y, pCmd->upmove = vMove.z;
	SDK::FixMovement(pCmd, vView, pCmd->viewangles);
}

void CMisc::BreakJump(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!Vars::Misc::Movement::BreakJump.Value || F::AutoRocketJump.IsRunning())
		return;

	static bool bStaticJump = false;
	const bool bLastJump = bStaticJump;
	const bool bCurrJump = bStaticJump = pCmd->buttons & IN_JUMP;

	static int iTickSinceGrounded = -1;
	if (pLocal->m_hGroundEntity().Get())
		iTickSinceGrounded = -1;
	iTickSinceGrounded++;

	switch (iTickSinceGrounded)
	{
	case 0:
		if (bLastJump || !bCurrJump || pLocal->IsDucking())
			return;
		break;
	case 1:
		break;
	default:
		return;
	}

	pCmd->buttons |= IN_DUCK;
}

void CMisc::BreakShootSound(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	static int iOriginalWeaponSlot = -1;
	auto pWeapon = H::Entities.GetWeapon();
	if (!Vars::Misc::Exploits::BreakShootSound.Value || F::Ticks.m_bDoubletap || pLocal->m_iClass() != TF_CLASS_SOLDIER || !pWeapon)
		return;

	static bool bLastWasInAttack = false;
	static int iLastWeaponSelect = -1;
	const int iCurrSlot = pWeapon->GetSlot();
	if (pCmd->weaponselect && pCmd->weaponselect != iLastWeaponSelect)
		iOriginalWeaponSlot = -1;

	auto pSwap = iCurrSlot == SLOT_SECONDARY ? pLocal->GetWeaponFromSlot(SLOT_PRIMARY) : iCurrSlot == SLOT_PRIMARY ? pLocal->GetWeaponFromSlot(SLOT_SECONDARY) : pLocal->GetWeaponFromSlot(iOriginalWeaponSlot);
	if (pSwap && pSwap->CanBeSelected() && !pCmd->weaponselect)
	{
		if (bLastWasInAttack)
		{
			if (iOriginalWeaponSlot < 0)
			{
				iOriginalWeaponSlot = iCurrSlot;
				pCmd->weaponselect = pSwap->entindex();
				iLastWeaponSelect = pCmd->weaponselect;
			}
		}
		else
		{
			if (iOriginalWeaponSlot == iCurrSlot && G::CanPrimaryAttack)
				iOriginalWeaponSlot = -1;
			else if (iOriginalWeaponSlot >= 0 && iOriginalWeaponSlot != iCurrSlot)
			{
				pCmd->weaponselect = pSwap->entindex();
				iLastWeaponSelect = pCmd->weaponselect;
			}
		}
	}

	bLastWasInAttack = G::Attacking == 1 && G::CanPrimaryAttack;
}

void CMisc::AntiAFK(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	static Timer tTimer = {};
	m_bAntiAFK = false;
	static auto mp_idledealmethod = H::ConVars.FindVar("mp_idledealmethod");
	static auto mp_idlemaxtime = H::ConVars.FindVar("mp_idlemaxtime");
	const int iIdleMethod = mp_idledealmethod->GetInt();
	const float flMaxIdleTime = mp_idlemaxtime->GetFloat();
	static bool bForce = false;

	// Just in case there's a connection problem
	auto pNetChan = I::EngineClient->GetNetChannelInfo();
	bool bTimingOut = pNetChan && pNetChan->IsTimingOut();
	if (bTimingOut)
		bForce = true;

	if (pCmd->buttons & (IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT) || !pLocal->IsAlive())
	{
		tTimer.Update();
		bForce = false;
	}
	else if (Vars::Misc::Automation::AntiAFK.Value && iIdleMethod && (tTimer.Check(flMaxIdleTime * 60.f - 10.f) || (!bTimingOut && bForce))) // trigger 10 seconds before kick
	{
		pCmd->buttons |= I::GlobalVars->tickcount % 2 ? IN_FORWARD : IN_BACK;
		tTimer.Update();
		bForce = false;
		m_bAntiAFK = true;
	}
}

void CMisc::InstantRespawnMVM(CTFPlayer* pLocal)
{
	if (!Vars::Misc::MannVsMachine::InstantRespawn.Value || pLocal->IsAlive())
		return;

	KeyValues* kv = new KeyValues("MVM_Revive_Response");
	kv->SetBool("accepted", true);
	I::EngineClient->ServerCmdKeyValues(kv);
}

void CMisc::NoiseSpam(CTFPlayer* pLocal)
{
	if (!Vars::Misc::Automation::NoiseSpam.Value || pLocal->m_bUsingActionSlot())
		return;

	static float flLastSpamTime = 0.0f;
	float flCurrentTime = SDK::PlatFloatTime();
	if (flCurrentTime - flLastSpamTime < 0.2f)
		return;

	flLastSpamTime = flCurrentTime;
	I::EngineClient->ServerCmdKeyValues(new KeyValues("use_action_slot_item_server"));
}

void CMisc::AutoDisguise(CTFPlayer* pLocal)
{
	if (!Vars::Misc::Automation::AutoDisguise.Value || !pLocal->IsAlive() || !pLocal->IsInValidTeam() || pLocal->m_iClass() != TF_CLASS_SPY)
		return;

	if (pLocal->InCond(TF_COND_DISGUISING) || pLocal->InCond(TF_COND_DISGUISED) || pLocal->InCond(TF_COND_DISGUISE_WEARINGOFF))
		return;

	static Timer tDisguiseTimer{};
	if (!tDisguiseTimer.Run(0.75f))
		return;

	auto pResource = H::Entities.GetResource();
	if (!pResource)
		return;

	const int iEnemyTeam = pLocal->m_iTeamNum() == TF_TEAM_RED ? TF_TEAM_BLUE : TF_TEAM_RED;
	std::array<int, TF_CLASS_COUNT> aClassCounts = {};
	int iTotalClasses = 0;

	for (int i = 1; i <= I::EngineClient->GetMaxClients(); ++i)
	{
		if (!pResource->m_bValid(i) || !pResource->m_bConnected(i) || pResource->m_iTeam(i) != iEnemyTeam)
			continue;

		const int iEnemyClass = pResource->m_iPlayerClass(i);
		if (iEnemyClass <= TF_CLASS_UNDEFINED || iEnemyClass >= TF_CLASS_COUNT || iEnemyClass == TF_CLASS_HEAVY)
			continue;

		++aClassCounts[iEnemyClass];
		++iTotalClasses;
	}

	if (!iTotalClasses)
		return;

	int iClass = TF_CLASS_UNDEFINED;
	int iRandomClass = SDK::RandomInt(1, iTotalClasses);
	for (int i = TF_CLASS_SCOUT; i < TF_CLASS_COUNT; ++i)
	{
		if (iRandomClass > aClassCounts[i])
		{
			iRandomClass -= aClassCounts[i];
			continue;
		}

		iClass = i;
		break;
	}

	if (iClass == TF_CLASS_UNDEFINED)
		return;

	I::EngineClient->ClientCmd_Unrestricted(std::format("disguise {} -1", iClass).c_str());
}

void CMisc::CallVoteSpam(CTFPlayer* pLocal)
{
	if (!Vars::Misc::Automation::CallVoteSpam.Value || !m_tCallVoteSpamTimer.Run(1.0f))
		return;

	std::vector<std::string> vVoteOptions = {
		"callvote changelevel cp_badlands",
		"callvote changelevel cp_granary",
		"callvote changelevel cp_well",
		"callvote changelevel cp_5gorge",
		"callvote changelevel cp_freight_final1",
		"callvote changelevel cp_yukon_final",
		"callvote changelevel cp_gravelpit",
		"callvote changelevel cp_dustbowl",
		"callvote changelevel cp_egypt_final",
		"callvote changelevel cp_junction_final",
		"callvote changelevel cp_steel",
		"callvote changelevel ctf_2fort",
		"callvote changelevel ctf_well",
		"callvote changelevel ctf_sawmill",
		"callvote changelevel ctf_turbine",
		"callvote changelevel ctf_doublecross",
		"callvote changelevel pl_badwater",
		"callvote changelevel pl_goldrush",
		"callvote changelevel pl_dustbowl",
		"callvote changelevel pl_upward",
		"callvote changelevel pl_thundermountain",
		"callvote changelevel koth_harvest_final",
		"callvote changelevel koth_nucleus",
		"callvote changelevel koth_sawmill",
		"callvote changelevel koth_viaduct",
		"callvote changelevel cp_5gorge",
		"callvote changelevel cp_dustbowl",
		"callvote changelevel ctf_2fort",
		"callvote changelevel ctf_doublecross",
		"callvote changelevel ctf_turbine",
		"callvote changelevel koth_brazil",
		"callvote changelevel pl_badwater",
		"callvote changelevel pl_pheonix",
		"callvote changelevel plr_bananabay",
		"callvote changelevel plr_hightower",
		"callvote scrambleteams"
	};

	int iRandomIndex = SDK::RandomInt(0, static_cast<int>(vVoteOptions.size()) - 1);
	std::string strSelectedVote = vVoteOptions[iRandomIndex];

	I::ClientState->SendStringCmd(strSelectedVote.c_str());
}

void CMisc::CheatsBypass()
{
	static bool bCheatSet = false;
	static auto sv_cheats = H::ConVars.FindVar("sv_cheats");
	const bool bShouldBypass = Vars::Misc::Exploits::CheatsBypass.Value;
	if (bShouldBypass)
		bCheatSet = sv_cheats->m_nValue = 1;
	else if (bCheatSet)
		bCheatSet = false;
}

void CMisc::WeaponSway()
{
	static auto cl_wpn_sway_interp = H::ConVars.FindVar("cl_wpn_sway_interp");
	static auto cl_wpn_sway_scale = H::ConVars.FindVar("cl_wpn_sway_scale");

	bool bSway = Vars::Visuals::Viewmodel::SwayInterp.Value || Vars::Visuals::Viewmodel::SwayScale.Value;
	cl_wpn_sway_interp->SetValue(bSway ? Vars::Visuals::Viewmodel::SwayInterp.Value : 0.f);
	cl_wpn_sway_scale->SetValue(bSway ? Vars::Visuals::Viewmodel::SwayScale.Value : 0.f);
}

void CMisc::TauntKartControl(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (Vars::Misc::Automation::TauntControl.Value && pLocal->IsTaunting() && pLocal->m_bAllowMoveDuringTaunt())
	{
		if (pLocal->m_bTauntForceMoveForward())
		{
			if (pCmd->buttons & IN_BACK)
				pCmd->viewangles.x = 91.f;
			else if (!(pCmd->buttons & IN_FORWARD))
				pCmd->viewangles.x = 90.f;
		}
		if (pCmd->buttons & IN_MOVELEFT)
			pCmd->sidemove = pCmd->viewangles.x == 90.f ? -450.f : -pLocal->m_flTauntForceMoveForwardSpeed();
		else if (pCmd->buttons & IN_MOVERIGHT)
			pCmd->sidemove = pCmd->viewangles.x == 90.f ? 450.f : pLocal->m_flTauntForceMoveForwardSpeed();
	}
	else if (Vars::Misc::Automation::KartControl.Value && pLocal->InCond(TF_COND_HALLOWEEN_KART))
	{
		bool bChoke = I::ClientState->chokedcommands < 3 && F::Ticks.CanChoke(true);
		float flForward = fabsf(pCmd->forwardmove), flSide = pCmd->sidemove * (!bChoke ? 0.f : pCmd->forwardmove < 0.f ? -1 : 1);

		Vec3 vForward, vRight; Math::AngleVectors(pCmd->viewangles, &vForward, &vRight, nullptr);
		vForward.Normalize2D(), vRight.Normalize2D();

		pCmd->viewangles.x = 90.f;
		G::SilentAngles = true;

		if (!(pCmd->buttons & (IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT)))
			return;

		if (pCmd->forwardmove < 0.f)
			pCmd->viewangles.x = 91.f;
		else if (pCmd->forwardmove > 0.f || flSide)
			pCmd->viewangles.x = 10.f;
		pCmd->forwardmove = 0.f;

		if (!flForward && !flSide)
			return;

		pCmd->forwardmove = 450.f;
		if (flSide)
		{
			Vec3 vWishDir = Math::VectorAngles({ vForward.x * flForward + vRight.x * flSide, vForward.y * flForward + vRight.y * flSide, 0.f });
			pCmd->viewangles.y = vWishDir.y;
			G::PSilentAngles = true;
		}
	}
}

void CMisc::FastMovement(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!pLocal->m_hGroundEntity() || pLocal->InCond(TF_COND_HALLOWEEN_KART))
		return;

	const float flSpeed = pLocal->m_vecVelocity().Length2D();
	const int flMaxSpeed = std::min(pLocal->m_flMaxspeed() * 0.9f, 520.f) - 10.f;
	const int iRun = !pCmd->forwardmove && !pCmd->sidemove ? 0 : flSpeed < flMaxSpeed ? 1 : 2;

	switch (iRun)
	{
	case 0:
	{
		if (!Vars::Misc::Movement::FastStop.Value || !flSpeed)
			return;

		Vec3 vDirection = pLocal->m_vecVelocity().ToAngle();
		vDirection.y = pCmd->viewangles.y - vDirection.y;
		Vec3 vNegatedDirection = vDirection.FromAngle() * -flSpeed;
		pCmd->forwardmove = vNegatedDirection.x;
		pCmd->sidemove = vNegatedDirection.y;

		break;
	}
	case 1:
	{
		const bool bDucking = pLocal->IsDucking() || (pCmd->buttons & IN_DUCK);
		if ((bDucking ? !Vars::Misc::Movement::DuckSpeed.Value : !Vars::Misc::Movement::FastAccelerate.Value)
			|| F::AntiCheatCompatibility.Active()
			|| G::Attacking == 1 || F::Ticks.m_bDoubletap || F::Ticks.m_bRecharge)
			return;

		if (!(pCmd->buttons & (IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT))
			&& (!bDucking || (!pCmd->forwardmove && !pCmd->sidemove)))
			return;

		bool bChoke = !I::ClientState->chokedcommands && F::Ticks.CanChoke(true);
		if (!bChoke)
			return;

		if (bDucking)
		{
			pCmd->forwardmove *= -1.f;
			pCmd->sidemove *= -1.f;
			pCmd->viewangles.x = 91.f;
		}

		Vec3 vMove = { pCmd->forwardmove, pCmd->sidemove, 0.f };
		Vec3 vAngMoveReverse = Math::VectorAngles(-vMove);
		pCmd->forwardmove = -vMove.Length();
		pCmd->sidemove = 0.f;
		pCmd->viewangles.y = fmodf(pCmd->viewangles.y - vAngMoveReverse.y, 360.f);
		pCmd->viewangles.z = 270.f;
		G::PSilentAngles = true;
		m_bDuckSpeedActive = bDucking;

		break;
	}
	}
}

void CMisc::AutoPeek(CTFPlayer* pLocal, CUserCmd* pCmd, bool bPost)
{
	static bool bReturning = false;

	if (!bPost)
	{
		if (Vars::AutoPeek::Enabled.Value)
		{
			Vec3 vLocalPos = pLocal->m_vecOrigin();

			if (bReturning)
			{
				if (vLocalPos.DistTo2D(m_vPeekReturnPos) < 8.f)
				{
					bReturning = false;
					return;
				}

				SDK::WalkTo(pCmd, pLocal, m_vPeekReturnPos);
				pCmd->buttons &= ~IN_JUMP;
			}
			else if (!pLocal->m_hGroundEntity())
				m_bPeekPlaced = false;

			if (!m_bPeekPlaced)
			{
				m_vPeekReturnPos = vLocalPos;
				m_bPeekPlaced = true;
			}
			else
			{
				static Timer tTimer = {};
				if (tTimer.Run(0.7f))
					H::Particles.DispatchParticleEffect("ping_circle", m_vPeekReturnPos, {});
			}
		}
		else
			m_bPeekPlaced = bReturning = false;
	}
	else if (G::Attacking && m_bPeekPlaced)
		bReturning = true;
}

void CMisc::EdgeJump(CTFPlayer* pLocal, CUserCmd* pCmd, bool bPost)
{
	if (!Vars::Misc::Movement::EdgeJump.Value)
		return;

	static bool bStaticGround = false;
	if (!bPost)
		bStaticGround = pLocal->m_hGroundEntity();
	else if (bStaticGround && !pLocal->m_hGroundEntity())
		pCmd->buttons |= IN_JUMP;
}

void CMisc::Event(IGameEvent* pEvent, uint32_t uHash)
{
	switch (uHash)
	{
	case FNV1A::Hash32Const("client_disconnect"):
	case FNV1A::Hash32Const("client_beginconnect"):
	case FNV1A::Hash32Const("game_newmap"):
		m_vChatSpamLines.clear();
		m_vKillSayLines.clear();
		m_iCurrentChatSpamIndex = 0;
		m_bAutoBalanceTeamChangePending = false;
		ResetBuyBot();

		m_bEdgeBug = m_bEdgeBugCrouch = false;
		m_iEdgeBugMoveStage = EBStageEnum::Normal;
		m_iEdgeBugTicksLeft = m_iEdgeBugTicksTotal = 0;
		m_flEdgeBugStartYaw = m_flEdgeBugYawDelta = 0.f;
		m_vEdgeBugMove.Zero();
		m_vEdgebugPath.clear();
		break;
	case FNV1A::Hash32Const("player_spawn"):
	{
		if (I::EngineClient->GetPlayerForUserID(pEvent->GetInt("userid")) != I::EngineClient->GetLocalPlayer())
			return;

		m_bPeekPlaced = false;

		m_bEdgeBug = m_bEdgeBugCrouch = false;
		m_iEdgeBugMoveStage = EBStageEnum::Normal;
		m_iEdgeBugTicksLeft = m_iEdgeBugTicksTotal = 0;
		m_flEdgeBugStartYaw = m_flEdgeBugYawDelta = 0.f;
		m_vEdgeBugMove.Zero();
		m_vEdgebugPath.clear();
		break;
	case FNV1A::Hash32Const("player_death"):
	{
		const int iLocalPlayer = I::EngineClient->GetLocalPlayer();
		const int iAttacker = I::EngineClient->GetPlayerForUserID(pEvent->GetInt("attacker"));
		const int iVictim = I::EngineClient->GetPlayerForUserID(pEvent->GetInt("userid"));

		if (iVictim == iLocalPlayer
			&& m_bAutoBalanceTeamChangePending
			&& Vars::Misc::Automation::AntiAutobalance.Value)
		{
			auto pResource = H::Entities.GetResource();
			if (pResource)
			{
				int iRedPlayers = 0, iBluePlayers = 0;
				for (int n = 1; n <= I::EngineClient->GetMaxClients(); n++)
				{
					if (!pResource->m_bValid(n) || !pResource->m_bConnected(n))
						continue;

					switch (pResource->m_iTeam(n))
					{
					case TF_TEAM_RED: iRedPlayers++; break;
					case TF_TEAM_BLUE: iBluePlayers++; break;
					}
				}

				const int iLocalTeam = pResource->m_iTeam(iLocalPlayer);
				if (iLocalTeam == TF_TEAM_RED || iLocalTeam == TF_TEAM_BLUE)
				{
					const int iOurPlayers = iLocalTeam == TF_TEAM_RED ? iRedPlayers : iBluePlayers;
					const int iEnemyPlayers = iLocalTeam == TF_TEAM_RED ? iBluePlayers : iRedPlayers;
					if (iOurPlayers - iEnemyPlayers > 2)
						I::EngineClient->ClientCmd_Unrestricted("retry");
				}
			}
		}

		if (iAttacker == iLocalPlayer && iAttacker != iVictim)
		{
			player_info_t pi;
			if (I::EngineClient->GetPlayerInfo(iVictim, &pi))
				m_sLastKilledName = pi.name;

			DoKillSay(iVictim);
		}

		if (!Vars::Misc::Automation::AutoTaunt.Value)
			break;

		if (iAttacker != iLocalPlayer || iAttacker == iVictim)
			break;

		const int iChance = std::clamp(Vars::Misc::Automation::AutoTauntChance.Value, 0, 100);
		if (!iChance)
			break;

		const auto pLocal = H::Entities.GetLocal();
		if (!pLocal || !pLocal->IsAlive())
			break;

		if (pLocal->IsTaunting() || pLocal->InCond(TF_COND_HALLOWEEN_KART))
			break;

		if (SDK::RandomInt(1, 100) > iChance)
			break;

		if (!Vars::Misc::Automation::AutoTauntSlot.Value)
		{
weapon_taunt:
			I::EngineClient->ClientCmd_Unrestricted("taunt");
			break;
		}

		// Check if we actually have a taunt at that slot
		{
			auto pInventoryManager = I::TFInventoryManager();
			if (!pInventoryManager)
				goto weapon_taunt;

			auto pLocalInventory = pInventoryManager->GetLocalInventory();
			if (!pLocalInventory)
				goto weapon_taunt;

			auto pEconItemView = pLocalInventory->GetItemInLoadout(pLocal->m_iClass(), Vars::Misc::Automation::AutoTauntSlot.Value + 10);
			if (!pEconItemView)
				goto weapon_taunt;

			auto pItemDefinition = pEconItemView->GetStaticData();
			if (!pItemDefinition || !pItemDefinition->m_pTauntData())
				goto weapon_taunt;
		}

		I::EngineClient->ClientCmd_Unrestricted(std::format("taunt {}", Vars::Misc::Automation::AutoTauntSlot.Value).c_str());
		break;
	}
	case FNV1A::Hash32Const("vote_maps_changed"):
		if (Vars::Misc::Automation::AutoVoteMap.Value)
		{
			I::EngineClient->ClientCmd_Unrestricted(std::format("next_map_vote {}", Vars::Misc::Automation::AutoVoteMapOption.Value).c_str());
		}
		break;
	}
	}
}

int CMisc::AntiBackstab(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!Vars::Misc::Automation::AntiBackstab.Value || !G::SendPacket || G::Attacking == 1 || !pLocal || pLocal->m_MoveType() != MOVETYPE_WALK || pLocal->InCond(TF_COND_HALLOWEEN_KART))
		return 0;

	std::vector<std::pair<Vec3, CBaseEntity*>> vTargets = {};
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		if (pPlayer->IsDormant() || !pPlayer->IsAlive() || pPlayer->IsAGhost() || pPlayer->InCond(TF_COND_STEALTHED))
			continue;

		auto pWeapon = pPlayer->m_hActiveWeapon()->As<CTFWeaponBase>();
		if (!pWeapon
			|| pWeapon->GetWeaponID() != TF_WEAPON_KNIFE
			&& !(G::PrimaryWeaponType == EWeaponType::MELEE && SDK::AttribHookValue(0, "crit_from_behind", pWeapon) > 0)
			&& !(pWeapon->GetWeaponID() == TF_WEAPON_FLAMETHROWER && SDK::AttribHookValue(0, "set_flamethrower_back_crit", pWeapon) == 1)
			|| F::PlayerUtils.IsIgnored(pPlayer->entindex()))
			continue;

		Vec3 vLocalPos = pLocal->GetCenter();
		Vec3 vTargetPos1 = pPlayer->GetCenter();
		Vec3 vTargetPos2 = vTargetPos1 + pPlayer->m_vecVelocity() * F::Backtrack.GetReal();
		float flDistance = std::max(std::max(SDK::MaxSpeed(pPlayer), SDK::MaxSpeed(pLocal)), pPlayer->m_vecVelocity().Length());
		float flDistanceSqr = powf(flDistance, 2);
		if ((vLocalPos.DistToSqr(vTargetPos1) > flDistanceSqr || !SDK::VisPosWorld(pLocal, pPlayer, vLocalPos, vTargetPos1))
			&& (vLocalPos.DistToSqr(vTargetPos2) > flDistanceSqr || !SDK::VisPosWorld(pLocal, pPlayer, vLocalPos, vTargetPos2)))
			continue;

		vTargets.emplace_back(vTargetPos2, pEntity);
	}
	if (vTargets.empty())
		return 0;

	std::sort(vTargets.begin(), vTargets.end(), [&](const auto& a, const auto& b) -> bool
	{
		return pLocal->GetCenter().DistToSqr(a.first) < pLocal->GetCenter().DistToSqr(b.first);
	});

	auto& pTargetPos = vTargets.front();
	switch (Vars::Misc::Automation::AntiBackstab.Value)
	{
	case Vars::Misc::Automation::AntiBackstabEnum::Yaw:
	{
		Vec3 vAngleTo = Math::CalcAngle(pLocal->m_vecOrigin(), pTargetPos.first);
		vAngleTo.x = pCmd->viewangles.x;
		SDK::FixMovement(pCmd, vAngleTo);
		pCmd->viewangles = vAngleTo;

		return 1;
	}
	case Vars::Misc::Automation::AntiBackstabEnum::Pitch:
	case Vars::Misc::Automation::AntiBackstabEnum::Fake:
	{
		bool bCheater = F::PlayerUtils.HasTag(pTargetPos.second->entindex(), F::PlayerUtils.TagToIndex(CHEATER_TAG));
		// if the closest spy is a cheater, assume auto stab is being used, otherwise don't do anything if target is in front
		if (!bCheater)
		{
			auto fTargetIsBehind = [&]()
			{
				const float flCompDist = PLAYER_ORIGIN_COMPRESSION / 2;
				const float flSqCompDist = 0.0884f;

				Vec3 vToTarget = (pLocal->m_vecOrigin() - pTargetPos.first).To2D();
				const float flDist = vToTarget.Normalize();
				if (flDist < flSqCompDist)
					return true;

				const float flExtra = 2.f * flCompDist / flDist; // account for origin compression
				float flPosVsTargetViewMinDot = 0.f - 0.0031f - flExtra;

				Vec3 vTargetForward; Math::AngleVectors(pCmd->viewangles, &vTargetForward);
				vTargetForward.Normalize2D();

				const float flPosVsTargetViewDot = vToTarget.Dot(vTargetForward); // Behind?

				return flPosVsTargetViewDot > flPosVsTargetViewMinDot;
			};

			if (!fTargetIsBehind())
				return 0;
		}

		if (!bCheater || Vars::Misc::Automation::AntiBackstab.Value == Vars::Misc::Automation::AntiBackstabEnum::Pitch)
		{
			pCmd->forwardmove *= -1;
			pCmd->viewangles.x = 269.f;
		}
		else
			pCmd->viewangles.x = 271.f;
		// may slip up some auto backstabs depending on mode, though we are still able to be stabbed

		return 2;
	}
	}

	return 0;
}

void CMisc::PingReducer()
{
	static Timer tTimer = {};
	if (!tTimer.Run(0.1f))
		return;

	auto pNetChan = reinterpret_cast<CNetChannel*>(I::EngineClient->GetNetChannelInfo());
	auto pResource = H::Entities.GetResource();
	if (!pNetChan || !pResource)
		return;

	static auto cl_cmdrate = H::ConVars.FindVar("cl_cmdrate");
	const int iCmdRate = cl_cmdrate->GetInt();
	const int Ping = pResource->m_iPing(I::EngineClient->GetLocalPlayer());
	const int iTarget = Vars::Misc::Exploits::PingReducer.Value && (Ping > Vars::Misc::Exploits::PingTarget.Value) ? -1 : iCmdRate;

	NET_SetConVar cmd("cl_cmdrate", std::to_string(m_iWishCmdrate = iTarget).c_str());
	pNetChan->SendNetMsg(cmd);
}

void CMisc::UnlockAchievements()
{
	const auto pAchievementMgr = I::EngineClient->GetAchievementMgr();
	if (pAchievementMgr)
	{
		I::SteamUserStats->RequestCurrentStats();
		for (int i = 0; i < pAchievementMgr->GetAchievementCount(); i++)
			pAchievementMgr->AwardAchievement(pAchievementMgr->GetAchievementByIndex(i)->GetAchievementID());
		I::SteamUserStats->StoreStats();
		I::SteamUserStats->RequestCurrentStats();
	}
}

void CMisc::UnlockItemAchievements()
{
	const auto pAchievementMgr = I::EngineClient->GetAchievementMgr();
	if (pAchievementMgr)
	{
		const std::unordered_set<int> vItemAchievementIDs(
			Vars::Misc::Automation::GetItemAchievementIDs().begin(),
			Vars::Misc::Automation::GetItemAchievementIDs().end());

		I::SteamUserStats->RequestCurrentStats();
		for (int i = 0; i < pAchievementMgr->GetAchievementCount(); i++)
		{
			if (const auto pAchievement = pAchievementMgr->GetAchievementByIndex(i))
			{
				const int iAchievementID = pAchievement->GetAchievementID();
				if (vItemAchievementIDs.contains(iAchievementID))
					pAchievementMgr->AwardAchievement(iAchievementID);
			}
		}
		I::SteamUserStats->StoreStats();
		I::SteamUserStats->RequestCurrentStats();
	}
}

void CMisc::LockAchievements()
{
	const auto pAchievementMgr = I::EngineClient->GetAchievementMgr();
	if (pAchievementMgr)
	{
		I::SteamUserStats->RequestCurrentStats();
		for (int i = 0; i < pAchievementMgr->GetAchievementCount(); i++)
			I::SteamUserStats->ClearAchievement(pAchievementMgr->GetAchievementByIndex(i)->GetName());
		I::SteamUserStats->StoreStats();
		I::SteamUserStats->RequestCurrentStats();
	}
}

void CMisc::LockItemAchievements()
{
	const auto pAchievementMgr = I::EngineClient->GetAchievementMgr();
	if (pAchievementMgr)
	{
		const std::unordered_set<int> vItemAchievementIDs(
			Vars::Misc::Automation::GetItemAchievementIDs().begin(),
			Vars::Misc::Automation::GetItemAchievementIDs().end());

		I::SteamUserStats->RequestCurrentStats();
		for (int i = 0; i < pAchievementMgr->GetAchievementCount(); i++)
		{
			if (const auto pAchievement = pAchievementMgr->GetAchievementByIndex(i))
			{
				if (vItemAchievementIDs.contains(pAchievement->GetAchievementID()))
					I::SteamUserStats->ClearAchievement(pAchievement->GetName());
			}
		}
		I::SteamUserStats->StoreStats();
		I::SteamUserStats->RequestCurrentStats();
	}
}

void CMisc::AchievementSpam(CTFPlayer* pLocal)
{
	if (!Vars::Misc::Automation::AchievementSpam.Value || !pLocal || !pLocal->IsAlive())
	{
		m_eAchievementSpamState = AchievementSpamState::IDLE;
		return;
	}

	const auto pAchievementMgr = reinterpret_cast<IAchievementMgr * (*)()>(U::Memory.GetVirtual(I::EngineClient, 114))();
	if (!pAchievementMgr)
	{
		m_eAchievementSpamState = AchievementSpamState::IDLE;
		return;
	}

	switch (m_eAchievementSpamState)
	{
	case AchievementSpamState::IDLE:
	{
		if (!m_tAchievementSpamTimer.Run(20.0f))
			return;

		int specificAchievementID = Vars::Misc::Automation::AchievementSpamID.Value;

		IAchievement* pAchievement = nullptr;
		for (int i = 0; i < pAchievementMgr->GetAchievementCount(); i++)
		{
			IAchievement* pCurrentAchievement = pAchievementMgr->GetAchievementByIndex(i);
			if (pCurrentAchievement && pCurrentAchievement->GetAchievementID() == specificAchievementID)
			{
				pAchievement = pCurrentAchievement;
				break;
			}
		}

		if (!pAchievement || !pAchievement->GetName())
		{
			pAchievement = pAchievementMgr->GetAchievementByIndex(0);
			if (!pAchievement || !pAchievement->GetName())
				return;

			specificAchievementID = pAchievement->GetAchievementID();
			Vars::Misc::Automation::AchievementSpamID.Value = specificAchievementID;
		}

		m_iAchievementSpamID = specificAchievementID;
		m_sAchievementSpamName = pAchievement->GetName();
		m_eAchievementSpamState = AchievementSpamState::CLEARING;
		break;
	}
	case AchievementSpamState::CLEARING:
	{
		I::SteamUserStats->RequestCurrentStats();
		I::SteamUserStats->ClearAchievement(m_sAchievementSpamName.c_str());
		I::SteamUserStats->StoreStats();

		m_tAchievementDelayTimer.Update();
		m_eAchievementSpamState = AchievementSpamState::WAITING;
		break;
	}
	case AchievementSpamState::WAITING:
	{
		if (!m_tAchievementDelayTimer.Run(0.1f))
			return;

		m_eAchievementSpamState = AchievementSpamState::AWARDING;
		break;
	}
	case AchievementSpamState::AWARDING:
	{
		I::SteamUserStats->RequestCurrentStats();
		pAchievementMgr->AwardAchievement(m_iAchievementSpamID);
		I::SteamUserStats->StoreStats();

		m_eAchievementSpamState = AchievementSpamState::IDLE;
		break;
	}
	}
}

void CMisc::VoiceCommandSpam(CTFPlayer* pLocal)
{
	if (!Vars::Misc::Automation::VoiceCommandSpam.Value || !pLocal->IsAlive())
		return;

	static float flLastVoiceTime = 0.0f;
	float flCurrentTime = SDK::PlatFloatTime();
	if (flCurrentTime - flLastVoiceTime >= 6.5f) // 6500ms in seconds
	{
		flLastVoiceTime = flCurrentTime;

		switch (Vars::Misc::Automation::VoiceCommandSpam.Value)
		{
		case Vars::Misc::Automation::VoiceCommandSpamEnum::Random:
		{
			int iMenu = SDK::RandomInt(0, 2);
			int iCommand = SDK::RandomInt(0, 8);
			std::string sCmd = "voicemenu " + std::to_string(iMenu) + " " + std::to_string(iCommand);
			I::EngineClient->ClientCmd_Unrestricted(sCmd.c_str());
		}
		break;
		case Vars::Misc::Automation::VoiceCommandSpamEnum::Medic:
			I::EngineClient->ClientCmd_Unrestricted("voicemenu 0 0");
			break;
		case Vars::Misc::Automation::VoiceCommandSpamEnum::Thanks:
			I::EngineClient->ClientCmd_Unrestricted("voicemenu 0 1");
			break;
		case Vars::Misc::Automation::VoiceCommandSpamEnum::NiceShot:
			I::EngineClient->ClientCmd_Unrestricted("voicemenu 2 6");
			break;
		case Vars::Misc::Automation::VoiceCommandSpamEnum::Cheers:
			I::EngineClient->ClientCmd_Unrestricted("voicemenu 2 2");
			break;
		case Vars::Misc::Automation::VoiceCommandSpamEnum::Jeers:
			I::EngineClient->ClientCmd_Unrestricted("voicemenu 2 3");
			break;
		case Vars::Misc::Automation::VoiceCommandSpamEnum::GoGoGo:
			I::EngineClient->ClientCmd_Unrestricted("voicemenu 0 2");
			break;
		case Vars::Misc::Automation::VoiceCommandSpamEnum::MoveUp:
			I::EngineClient->ClientCmd_Unrestricted("voicemenu 0 3");
			break;
		case Vars::Misc::Automation::VoiceCommandSpamEnum::GoLeft:
			I::EngineClient->ClientCmd_Unrestricted("voicemenu 0 4");
			break;
		case Vars::Misc::Automation::VoiceCommandSpamEnum::GoRight:
			I::EngineClient->ClientCmd_Unrestricted("voicemenu 0 5");
			break;
		case Vars::Misc::Automation::VoiceCommandSpamEnum::Yes:
			I::EngineClient->ClientCmd_Unrestricted("voicemenu 0 6");
			break;
		case Vars::Misc::Automation::VoiceCommandSpamEnum::No:
			I::EngineClient->ClientCmd_Unrestricted("voicemenu 0 7");
			break;
		case Vars::Misc::Automation::VoiceCommandSpamEnum::Incoming:
			I::EngineClient->ClientCmd_Unrestricted("voicemenu 1 0");
			break;
		case Vars::Misc::Automation::VoiceCommandSpamEnum::Spy:
			I::EngineClient->ClientCmd_Unrestricted("voicemenu 1 1");
			break;
		case Vars::Misc::Automation::VoiceCommandSpamEnum::Sentry:
			I::EngineClient->ClientCmd_Unrestricted("voicemenu 1 2");
			break;
		case Vars::Misc::Automation::VoiceCommandSpamEnum::NeedTeleporter:
			I::EngineClient->ClientCmd_Unrestricted("voicemenu 1 3");
			break;
		case Vars::Misc::Automation::VoiceCommandSpamEnum::Pootis:
			I::EngineClient->ClientCmd_Unrestricted("voicemenu 1 4");
			break;
		case Vars::Misc::Automation::VoiceCommandSpamEnum::NeedSentry:
			I::EngineClient->ClientCmd_Unrestricted("voicemenu 1 5");
			break;
		case Vars::Misc::Automation::VoiceCommandSpamEnum::ActivateCharge:
			I::EngineClient->ClientCmd_Unrestricted("voicemenu 1 6");
			break;
		case Vars::Misc::Automation::VoiceCommandSpamEnum::Help:
			I::EngineClient->ClientCmd_Unrestricted("voicemenu 2 0");
			break;
		case Vars::Misc::Automation::VoiceCommandSpamEnum::BattleCry:
			I::EngineClient->ClientCmd_Unrestricted("voicemenu 2 1");
			break;
		case Vars::Misc::Automation::VoiceCommandSpamEnum::GoodJob:
			I::EngineClient->ClientCmd_Unrestricted("voicemenu 2 7");
			break;
		}
	}
}

void CMisc::AutoReport()
{
	if (!Vars::Misc::Automation::AutoReport.Value)
		return;

	static Timer tReportTimer{};
	if (!tReportTimer.Run(5.0f))
		return;

	int iLocalIdx = I::EngineClient->GetLocalPlayer();
	for (int i = 1; i <= I::EngineClient->GetMaxClients(); i++)
	{
		if (i == iLocalIdx)
			continue;

		if (auto uSteamId = F::PlayerUtils.GetAccountID(i))
		{
			if (H::Entities.IsFriend(i) ||
				H::Entities.InParty(i) ||
				F::PlayerUtils.IsIgnored(i) ||
				F::PlayerUtils.HasTag(i, F::PlayerUtils.TagToIndex(IGNORED_TAG)) ||
				F::PlayerUtils.HasTag(i, F::PlayerUtils.TagToIndex(FRIEND_TAG)))
				continue;

			uint64_t uSteamID64 = ((uint64_t)1 << 56) | ((uint64_t)1 << 52) | ((uint64_t)1 << 32) | uSteamId;
			S::ReportPlayerAccount.Call<bool>(uSteamID64, 1);
		}
	}
}

void CMisc::AutoRetry(CTFPlayer* pLocal)
{
	if (!Vars::Misc::Automation::AutoRetry.Value || !pLocal || !pLocal->IsAlive())
		return;

	const int max_health = pLocal->GetMaxHealth();
	const float health_percent = max_health ? static_cast<float>(pLocal->m_iHealth()) / max_health * 100.f : 100.f;
	if (health_percent >= Vars::Misc::Automation::AutoRetryHealth.Value)
		return;

	static Timer tRetryTimer{};
	if (!tRetryTimer.Run(5.0f))
		return;

	I::EngineClient->ClientCmd_Unrestricted("retry");
}


void CMisc::ChatSpam(CTFPlayer* pLocal)
{
	auto ResetChatTimer = [&]()
		{
			m_tChatSpamTimer.Update();
		};

	if (!Vars::Misc::Automation::ChatSpam::Enable.Value)
	{
		ResetChatTimer();
		return;
	}

	if (!pLocal->IsAlive() || !pLocal->IsInValidTeam() || pLocal->m_iClass() == TF_CLASS_UNDEFINED)
	{
		ResetChatTimer();
		return;
	}

	static Timer tReloadTimer{};
	auto EnsureChatLinesLoaded = [&]() -> bool
		{
			size_t uSize = m_vChatSpamLines.size();
			if (uSize > 0 && !tReloadTimer.Run(5.0f))
				return true;

			static const char* szDefaultContent =
				"This is a default message from cat_chatspam.txt\n"
				"Edit this file unibox/cat_chatspam.txt\n"
				"Each line will be sent as a separate message\n"
				"[unibox] Chat Spam is working!\n"
				"Put your chat spam lines in this file\n";

			if (LoadLines("cat_chatspam.txt", m_vChatSpamLines, szDefaultContent))
			{
				// Reset index if number of lines changed
				if (uSize != m_vChatSpamLines.size())
					m_iCurrentChatSpamIndex = 0;
				return true;
			}

			m_vChatSpamLines = {
				"Put your chat spam lines in unibox/cat_chatspam.txt",
				"ChatSpam is running but couldn't find unibox/cat_chatspam.txt",
				"[unibox] Chat Spam is working!"
			};
			m_iCurrentChatSpamIndex = 0;
			return false;
		};

	if (!EnsureChatLinesLoaded())
		return;

	float flSpamInterval = Vars::Misc::Automation::ChatSpam::Interval.Value;
	if (flSpamInterval < 0.2f)
		flSpamInterval = 0.2f;

	if (!m_tChatSpamTimer.Run(flSpamInterval))
		return;

	auto FetchNextChatLine = [&]() -> std::string
		{
			const size_t uLineCount = m_vChatSpamLines.size();
			if (!uLineCount)
				return {};

			if (Vars::Misc::Automation::ChatSpam::Randomize.Value)
			{
				int iMax = static_cast<int>(uLineCount) - 1;
				if (iMax < 0)
					iMax = 0;
				const int iRandomIndex = SDK::RandomInt(0, iMax);
				if (iRandomIndex >= 0 && iRandomIndex < static_cast<int>(uLineCount))
					return m_vChatSpamLines[iRandomIndex];
				return "[ChatSpam]";
			}

			if (m_iCurrentChatSpamIndex < 0 || m_iCurrentChatSpamIndex >= static_cast<int>(uLineCount))
				m_iCurrentChatSpamIndex = 0;

			const std::string& sLine = m_vChatSpamLines[m_iCurrentChatSpamIndex];
			m_iCurrentChatSpamIndex = (m_iCurrentChatSpamIndex + 1) % static_cast<int>(uLineCount);
			return sLine;
		};

	std::string sChatLine = FetchNextChatLine();
	if (sChatLine.empty())
		return;

	sChatLine = ReplaceTags(sChatLine);

	if (sChatLine.length() > 150)
		sChatLine.resize(150);

	std::string sChatCommand;
	if (Vars::Misc::Automation::ChatSpam::TeamChat.Value)
		sChatCommand = "say_team \"" + sChatLine + "\"";
	else
		sChatCommand = "say \"" + sChatLine + "\"";

	SDK::Output("ChatSpam", std::format("Sending: {}", sChatCommand).c_str(), {}, OUTPUT_CONSOLE | OUTPUT_DEBUG);
	I::EngineClient->ClientCmd_Unrestricted(sChatCommand.c_str());
}

void CMisc::EnsureChatUtilsDoc()
{
	static bool bWritten = false;
	if (bWritten)
		return;
	bWritten = true;

	const std::string sPath = F::Configs.m_sConfigPath + "chatutils_readme.txt";
	if (std::filesystem::exists(sPath))
		return;

	static const char* szDoc =
		"================================================================\n"
		"  ChatUtils Documentation\n"
		"================================================================\n"
		"\n"
		"All chat-related text features read plain .txt files from this\n"
		"cfg folder. Each file is auto-generated with examples the first\n"
		"time the corresponding feature runs. Lines starting with // and\n"
		"empty lines are ignored. Edit, save, and the file is reloaded\n"
		"automatically (within ~5 seconds).\n"
		"\n"
		"----------------------------------------------------------------\n"
		"  Files\n"
		"----------------------------------------------------------------\n"
		"\n"
		"  cat_chatspam.txt   Lines sent on a timer by Chat Spam.\n"
		"                     One message per line.\n"
		"\n"
		"  killsay.txt        Lines sent when YOU kill another player.\n"
		"                     One message per line, picked at random.\n"
		"\n"
		"  votekick.txt       Replies fired when a vote kick starts.\n"
		"                     Prefix each line with `F1:` or `F2:`.\n"
		"                       F1: used when a friend/you call the vote\n"
		"                       F2: used when someone votes on a friend/you\n"
		"\n"
		"  autoreply.txt      Trigger-based replies in public chat.\n"
		"                     Format:  trigger1, trigger2 : reply1, reply2\n"
		"                     Triggers are matched case-insensitively as\n"
		"                     substrings; one reply is chosen at random.\n"
		"\n"
		"----------------------------------------------------------------\n"
		"  Tags\n"
		"----------------------------------------------------------------\n"
		"\n"
		"  {killer}        Your in-game name (KillSay).\n"
		"  {victim}        Name of the player you just killed (KillSay).\n"
		"  {target}        Vote-kick target / autoreply sender.\n"
		"  {triggername}   Same thing as {target}.\n"
		"  {initiator}     Player who called the current vote.\n"
		"  {enemyteam}     `RED` or `BLU` - the opposite of your team.\n"
		"  {friendlyteam}  `RED` or `BLU` - your team.\n"
		"  {lastkilled}    Name of the last player you killed. (Global)\n"
		"  {highestscore}  Name of the player with the highest score.\n"
		"  {random}        Random non-bot player on the server.\n"
		"  {friend}        Random player tagged FRIEND.\n"
		"  {ignored}       Random player tagged IGNORED.\n"
		"\n"
		"Tags only resolve when the relevant data is available - e.g.\n"
		"{killer}/{victim} are empty outside KillSay, {target} is empty\n"
		"outside vote replies / auto-replies.\n"
		"\n"
		"----------------------------------------------------------------\n"
		"  PS\n"
		"----------------------------------------------------------------\n"
		"\n"
		"  - Messages are truncated to 150 characters after tag expansion.\n"
		"  - Kill Say honours the Chance slider in the menu.\n"
		"  - Use Team Chat toggles in the menu to switch say -> say_team.\n"
		"\n"
		"================================================================\n";

	std::ofstream file(sPath);
	if (file.good())
		file << szDoc;
}

void CMisc::DoKillSay(int iVictim)
{
	if (!Vars::Misc::Automation::KillSay::Enable.Value)
		return;

	const int iChance = std::clamp(Vars::Misc::Automation::KillSay::Chance.Value, 0, 100);
	if (!iChance || SDK::RandomInt(1, 100) > iChance)
		return;

	static Timer tReloadTimer{};
	if (m_vKillSayLines.empty() || tReloadTimer.Run(5.0f))
	{
		static const char* szDefaultContent =
			"// Kill Say configuration\n"
			"// Each non-empty, non-// line is a possible message sent when you get a kill.\n"
			"// Supports tags: {killer}, {victim}, {enemyteam}, {friendlyteam}, {random}, {friend}, {ignored}, {highestscore}, {lastkilled}\n"
			"SO FUHIN EASY!!! GET GOOD, GET UNIBOX!\n"
			"{victim} YOURE SO TRASH!\n"
			"WHAT??? YOU DIED TO ME, {killer}??? HOW COULD YOU BE THIS BAD??? {enemyteam} IS CARRYING YOU SO BAD!!!\n"
			"{friendlyteam} IS SUPERIOR, JUST LIKE {killer} AND UNIBOX! UNLIKE {victim} AND {enemyteam}!\n"
			"I AM THE FUCKING SPECTRE\n";
		LoadLines("killsay.txt", m_vKillSayLines, szDefaultContent);
	}

	if (m_vKillSayLines.empty())
		return;

	std::string sVictim;
	{
		player_info_t pi;
		if (I::EngineClient->GetPlayerInfo(iVictim, &pi))
			sVictim = pi.name;
	}

	std::string sKiller;
	{
		player_info_t pi;
		if (I::EngineClient->GetPlayerInfo(I::EngineClient->GetLocalPlayer(), &pi))
			sKiller = pi.name;
	}

	const int iIndex = SDK::RandomInt(0, static_cast<int>(m_vKillSayLines.size()) - 1);
	std::string sLine = ReplaceTags(m_vKillSayLines[iIndex], "", "", sKiller, sVictim);

	if (sLine.length() > 150)
		sLine.resize(150);

	std::string sCommand = Vars::Misc::Automation::KillSay::TeamChat.Value
		? "say_team \"" + sLine + "\""
		: "say \"" + sLine + "\"";

	SDK::Output("KillSay", std::format("Sending: {}", sCommand).c_str(), {}, OUTPUT_CONSOLE | OUTPUT_DEBUG);
	I::EngineClient->ClientCmd_Unrestricted(sCommand.c_str());
}

void CMisc::AutoMvmReadyUp()
{
	if (!Vars::Misc::MannVsMachine::AutoMvmReadyUp.Value)
		return;

	auto pLocal = H::Entities.GetLocal();
	if (!pLocal)
		return;

	auto pGameRules = I::TFGameRules();
	if (!pGameRules)
		return;

	if (!pGameRules->m_bPlayingMannVsMachine() ||
		!pGameRules->m_bInWaitingForPlayers() ||
		pGameRules->m_iRoundState() != GR_STATE_BETWEEN_RNDS)
		return;

	const int iLocalIndex = pLocal->entindex();
	if (iLocalIndex < 0 || iLocalIndex >= 100)
		return;

	if (!pGameRules->IsPlayerReady(iLocalIndex))
		I::EngineClient->ClientCmd_Unrestricted("tournament_player_readystate 1");
}

void CMisc::BuyBotJoinClass(int iClass)
{
	static const std::array<const char*, 10> aClassNames = { "", "scout", "sniper", "soldier", "demoman", "medic", "heavyweapons", "pyro", "spy", "engineer" };
	if (iClass <= TF_CLASS_UNDEFINED || iClass >= TF_CLASS_COUNT || iClass == TF_CLASS_CIVILIAN)
		return;

	float flCurTime = I::GlobalVars->curtime;
	if (m_flBuybotClassClock > flCurTime)
		return;

	I::EngineClient->ClientCmd_Unrestricted(std::format("joinclass {}", aClassNames[iClass]).c_str());
	I::EngineClient->ClientCmd_Unrestricted("menuclosed");
	m_flBuybotClassClock = flCurTime + 1.0f;
}

bool CMisc::BuyBotWalkAwayFromStation(CTFPlayer* pLocal, CUserCmd* pCmd, const Vec3& vStation)
{
	if (!pLocal || !pCmd || vStation.IsZero())
		return false;

	if (m_bBuybotUsingNav)
		F::NavEngine.CancelPath();

	Vec3 vDirection = pLocal->GetAbsOrigin() - vStation;
	vDirection.z = 0.0f;
	if (vDirection.IsZero())
		vDirection = { 1.0f, 0.0f, 0.0f };
	vDirection.Normalize();

	SDK::WalkTo(pCmd, pLocal, pLocal->GetAbsOrigin() + vDirection * 320.0f);
	m_bBuybotUsingNav = false;
	m_flBuybotStationPathStart = 0.0f;
	return true;
}

void CMisc::ExecBuyBot(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!Vars::Misc::MannVsMachine::BuyBot.Value)
	{
		if (m_bBuybotUsingNav)
			F::NavEngine.CancelPath();
		ResetBuyBot();
		return;
	}

	auto pGameRules = I::TFGameRules();
	if (!pGameRules || !pGameRules->m_bPlayingMannVsMachine())
	{
		if (m_bBuybotUsingNav)
			F::NavEngine.CancelPath();
		ResetBuyBot();
		return;
	}

	if (Vars::Misc::MannVsMachine::MaxCash.Value > 0 && pLocal->m_nCurrency() >= Vars::Misc::MannVsMachine::MaxCash.Value)
		m_bBuybotCashLimitReached = true;

	const Vec3 vLocalOrigin = pLocal->GetAbsOrigin();
	bool bFoundStation = false;
	float flBestDist = FLT_MAX;
	Vec3 vBestStation = {};

	for (const auto& tTrigger : G::TriggerStorage)
	{
		if (tTrigger.m_eType != TriggerTypeEnum::UpgradeStation)
			continue;

		Vec3 vStation = tTrigger.m_vCenter.IsZero() ? tTrigger.m_vOrigin : tTrigger.m_vCenter;
		float flDist = vLocalOrigin.DistToSqr(vStation);
		if (flDist >= flBestDist)
			continue;

		flBestDist = flDist;
		vBestStation = vStation;
		bFoundStation = true;
	}

	int iDesiredClass = 0;
	if (Vars::Misc::MannVsMachine::BuyBotAutoClass.Value)
	{
		if (!m_bBuybotCashLimitReached)
			iDesiredClass = TF_CLASS_MEDIC;
		else if (pLocal->m_iClass() == TF_CLASS_MEDIC)
		{
			iDesiredClass = Vars::Misc::MannVsMachine::BuyBotClass.Value;
			if (iDesiredClass == TF_CLASS_MEDIC)
				iDesiredClass = TF_CLASS_HEAVY;
		}
	}

	if (iDesiredClass > TF_CLASS_UNDEFINED && iDesiredClass < TF_CLASS_COUNT && pLocal->m_iClass() != iDesiredClass)
	{
		if (pLocal->m_bInUpgradeZone() && BuyBotWalkAwayFromStation(pLocal, pCmd, vBestStation))
			return;

		BuyBotJoinClass(iDesiredClass);
		return;
	}

	const bool bWaitingForMedicClass = Vars::Misc::MannVsMachine::BuyBotAutoClass.Value && !m_bBuybotCashLimitReached && pLocal->m_iClass() != TF_CLASS_MEDIC;
	if (m_bBuybotFinishedUpgrades)
	{
		if (m_bBuybotUsingNav)
			F::NavEngine.CancelPath();
		m_flBuybotStationPathStart = 0.0f;
		m_bBuybotUsingNav = false;
		return;
	}

	if (bFoundStation)
	{
		if (m_vBuybotStationTarget.IsZero() || m_vBuybotStationTarget.DistToSqr(vBestStation) > 4096.0f)
		{
			m_vBuybotStationTarget = vBestStation;
			m_flBuybotStationPathStart = I::GlobalVars->curtime;
			m_bBuybotUsingNav = false;
		}

		if (!pLocal->m_bInUpgradeZone() && pLocal->IsAlive() && !pLocal->IsAGhost() && pLocal->m_MoveType() == MOVETYPE_WALK && !pLocal->IsSwimming() && !pLocal->IsTaunting())
		{
			const float flCurTime = I::GlobalVars->curtime;
			if (m_flBuybotStationPathStart == 0.0f)
				m_flBuybotStationPathStart = flCurTime;

			if (flCurTime - m_flBuybotStationPathStart >= 12.0f)
			{
				if (!m_bBuybotUsingNav || m_flBuybotNavClock <= flCurTime || m_vBuybotStationTarget.DistToSqr(vBestStation) > 4096.0f)
				{
					F::NavEngine.NavTo(vBestStation, PriorityListEnum::Forced, true, !F::NavEngine.IsPathing());
					m_flBuybotNavClock = flCurTime + 0.5f;
				}
				m_bBuybotUsingNav = true;
			}
			else if (pCmd)
				SDK::WalkTo(pCmd, pLocal, vBestStation);
		}
	}
	else
	{
		m_flBuybotStationPathStart = 0.0f;
		m_bBuybotUsingNav = false;
		m_vBuybotStationTarget = {};
	}

	if (!pLocal->m_bInUpgradeZone())
		return;

	if (m_bBuybotUsingNav)
		F::NavEngine.CancelPath();

	m_flBuybotStationPathStart = 0.0f;
	m_bBuybotUsingNav = false;

	static auto tfMvmRespec = H::ConVars.FindVar("tf_mvm_respec_enabled");
	float flCurTime = I::GlobalVars->curtime;
	if (m_flBuybotClock > flCurTime)
		return;

	if (bWaitingForMedicClass)
		return;

	if (m_bBuybotCashLimitReached)
	{
		if (pLocal->m_iClass() == TF_CLASS_MEDIC)
		{
			m_flBuybotClock = flCurTime + 0.2f;
			return;
		}

		constexpr int iMaxUpgradeIndex = 128;
		constexpr int iMaxUpgradeLevels = 10;
		const int aSlots[] = { 0, -1 };
		const int iSlot = aSlots[m_iBuybotUpgradeSlotStep % std::size(aSlots)];

		I::EngineClient->ServerCmdKeyValues(new KeyValues("MvM_UpgradesBegin"));
		for (int iLevel = 0; iLevel < iMaxUpgradeLevels; iLevel++)
		{
			KeyValues* kv = new KeyValues("MVM_Upgrade");
			KeyValues* sub = kv->FindKey("Upgrade", true);
			sub->SetInt("itemslot", iSlot);
			sub->SetInt("upgrade", m_iBuybotUpgradeIndex);
			sub->SetInt("count", 1);
			I::EngineClient->ServerCmdKeyValues(kv);
		}
		{
			KeyValues* kv = new KeyValues("MvM_UpgradesDone");
			kv->SetInt("num_upgrades", iMaxUpgradeLevels);
			I::EngineClient->ServerCmdKeyValues(kv);
		}

		m_iBuybotUpgradeIndex++;
		if (m_iBuybotUpgradeIndex >= iMaxUpgradeIndex)
		{
			m_iBuybotUpgradeIndex = 0;
			m_iBuybotUpgradeSlotStep++;
			if (m_iBuybotUpgradeSlotStep >= static_cast<int>(std::size(aSlots)))
			{
				m_bBuybotFinishedUpgrades = true;
				m_bBuybotUsingNav = false;
				m_vBuybotStationTarget = {};
			}
		}

		m_flBuybotClock = flCurTime + 0.05f;
		return;
	}

	m_iBuybotUpgradeSlotStep = 0;
	m_iBuybotUpgradeIndex = 0;

	if (tfMvmRespec->GetInt() != 1)
		return;

	switch (m_iBuybotStep)
	{
	case 1:
		I::EngineClient->ServerCmdKeyValues(new KeyValues("MvM_UpgradesBegin"));
		{
			KeyValues* kv = new KeyValues("MVM_Upgrade");
			KeyValues* sub = kv->FindKey("Upgrade", true);
			sub->SetInt("itemslot", 1);
			sub->SetInt("Upgrade", 19);
			sub->SetInt("count", 1);
			I::EngineClient->ServerCmdKeyValues(kv);
		}
		{
			KeyValues* kv = new KeyValues("MVM_Upgrade");
			KeyValues* sub = kv->FindKey("Upgrade", true);
			sub->SetInt("itemslot", 1);
			sub->SetInt("Upgrade", 19);
			sub->SetInt("count", 1);
			I::EngineClient->ServerCmdKeyValues(kv);
		}
		{
			KeyValues* kv = new KeyValues("MvM_UpgradesDone");
			kv->SetInt("num_upgrades", 2);
			I::EngineClient->ServerCmdKeyValues(kv);
		}
		break;
	case 2:
		I::EngineClient->ServerCmdKeyValues(new KeyValues("MvM_UpgradesBegin"));
		{
			KeyValues* kv = new KeyValues("MVM_Upgrade");
			KeyValues* sub = kv->FindKey("Upgrade", true);
			sub->SetInt("itemslot", 1);
			sub->SetInt("Upgrade", 19);
			sub->SetInt("count", -1);
			I::EngineClient->ServerCmdKeyValues(kv);
		}
		{
			KeyValues* kv = new KeyValues("MVM_Upgrade");
			KeyValues* sub = kv->FindKey("Upgrade", true);
			sub->SetInt("itemslot", 1);
			sub->SetInt("Upgrade", 19);
			sub->SetInt("count", 1);
			I::EngineClient->ServerCmdKeyValues(kv);
		}
		I::EngineClient->ServerCmdKeyValues(new KeyValues("MVM_Respec"));
		{
			KeyValues* kv = new KeyValues("MvM_UpgradesDone");
			kv->SetInt("num_upgrades", -1);
			I::EngineClient->ServerCmdKeyValues(kv);
		}
		break;
	case 3:
		I::EngineClient->ServerCmdKeyValues(new KeyValues("MvM_UpgradesBegin"));
		{
			KeyValues* kv = new KeyValues("MVM_Upgrade");
			KeyValues* sub = kv->FindKey("Upgrade", true);
			sub->SetInt("itemslot", 1);
			sub->SetInt("Upgrade", 19);
			sub->SetInt("count", 1);
			I::EngineClient->ServerCmdKeyValues(kv);
		}
		{
			KeyValues* kv = new KeyValues("MVM_Upgrade");
			KeyValues* sub = kv->FindKey("Upgrade", true);
			sub->SetInt("itemslot", 1);
			sub->SetInt("Upgrade", 19);
			sub->SetInt("count", 1);
			I::EngineClient->ServerCmdKeyValues(kv);
		}
		{
			KeyValues* kv = new KeyValues("MVM_Upgrade");
			KeyValues* sub = kv->FindKey("Upgrade", true);
			sub->SetInt("itemslot", 1);
			sub->SetInt("Upgrade", 19);
			sub->SetInt("count", -1);
			I::EngineClient->ServerCmdKeyValues(kv);
		}
		{
			KeyValues* kv = new KeyValues("MVM_Upgrade");
			KeyValues* sub = kv->FindKey("Upgrade", true);
			sub->SetInt("itemslot", 1);
			sub->SetInt("Upgrade", 19);
			sub->SetInt("count", -1);
			I::EngineClient->ServerCmdKeyValues(kv);
		}
		{
			KeyValues* kv = new KeyValues("MvM_UpgradesDone");
			kv->SetInt("num_upgrades", 0);
			I::EngineClient->ServerCmdKeyValues(kv);
		}
		break;
	}
	m_iBuybotStep = m_iBuybotStep % 3 + 1;
	m_flBuybotClock = flCurTime + 0.2f;
}

void CMisc::ResetBuyBot()
{
	m_iBuybotStep = 1;
	m_iBuybotUpgradeSlotStep = 0;
	m_iBuybotUpgradeIndex = 0;
	m_flBuybotClock = 0.0f;
	m_flBuybotStationPathStart = 0.0f;
	m_flBuybotNavClock = 0.0f;
	m_flBuybotClassClock = 0.0f;
	m_flBuybotRetryClock = 0.0f;
	m_bBuybotUsingNav = false;
	m_bBuybotCashLimitReached = false;
	m_bBuybotFinishedUpgrades = false;
	m_vBuybotStationTarget = {};
}

void CMisc::OnBuyBotClassChangeBlocked()
{
	if (!Vars::Misc::MannVsMachine::BuyBot.Value || !Vars::Misc::MannVsMachine::BuyBotAutoClass.Value)
		return;

	auto pGameRules = I::TFGameRules();
	if (!pGameRules || !pGameRules->m_bPlayingMannVsMachine() || pGameRules->m_iRoundState() == GR_STATE_RND_RUNNING)
		return;

	auto pObjectiveResource = H::Entities.GetObjectiveResource();
	if (pObjectiveResource && !pObjectiveResource->m_bMannVsMachineBetweenWaves() && pGameRules->m_iRoundState() != GR_STATE_BETWEEN_RNDS)
		return;

	const float flCurTime = I::GlobalVars->curtime;
	if (m_flBuybotRetryClock > flCurTime)
		return;

	I::EngineClient->ClientCmd_Unrestricted("retry");
	m_flBuybotRetryClock = flCurTime + 10.0f;
}

void CMisc::MicSpam()
{
	static bool bShouldRestore = false;
	static Timer tRecordTimer = {};

	if (Vars::Misc::Automation::Micspam.Value)
	{
		if (I::EngineClient->IsInGame() && tRecordTimer.Run(10.0f))
		{
			I::EngineClient->ClientCmd_Unrestricted("+voicerecord");
			I::EngineClient->ClientCmd_Unrestricted("voice_avggain 1");
#ifdef TEXTMODE
			I::EngineClient->ClientCmd_Unrestricted("volume 0");
			I::EngineClient->ClientCmd_Unrestricted("voice_enable 1");
			I::EngineClient->ClientCmd_Unrestricted("voice_loopback 0");
#endif
		}

		bShouldRestore = true;
	}
	else if (bShouldRestore)
	{
		if (S::Voice_IsRecording.Call<bool>())
		{
			I::EngineClient->ClientCmd_Unrestricted("-voicerecord");
#ifdef TEXTMODE
			I::EngineClient->ClientCmd_Unrestricted("volume 0");
			I::EngineClient->ClientCmd_Unrestricted("voice_enable 0");
			I::EngineClient->ClientCmd_Unrestricted("voice_loopback 0");
#endif
		}

		bShouldRestore = false;
	}
}

CMisc::ProfileDumpResult_t CMisc::DumpProfiles(bool bAnnounce)
{
	ProfileDumpResult_t tResult{};
	auto pResource = H::Entities.GetResource();
	if (!pResource)
	{
		if (bAnnounce)
			SDK::Output("ProfileScraper", "Player resource unavailable");
		return tResult;
	}
	tResult.m_bResourceAvailable = true;

	auto SanitizeName = [](const char* sRaw) -> std::string
		{
			if (!sRaw)
				return {};

			std::string sClean;
			sClean.reserve(std::strlen(sRaw));
			for (unsigned char c : std::string_view{ sRaw })
			{
				if (c < 32 || c > 126)
					continue;
				if (c == ',')
					return {};
				sClean.push_back(static_cast<char>(c));
			}
			return sClean;
		};

	struct ProfileEntry_t
	{
		uint32_t m_uAccountID = 0;
		std::string m_sName = {};
	};

	std::vector<ProfileEntry_t> vProfiles;
	std::unordered_set<uint32_t> setSessionAccounts;
	vProfiles.reserve(I::EngineClient->GetMaxClients());
	setSessionAccounts.reserve(I::EngineClient->GetMaxClients());

	const int iLocalPlayer = I::EngineClient->GetLocalPlayer();
	for (int n = 1; n <= I::EngineClient->GetMaxClients(); n++)
	{
		if (n == iLocalPlayer)
			continue;

		if (!pResource->m_bValid(n) || !pResource->m_bConnected(n) || pResource->IsFakePlayer(n))
			continue;

		tResult.m_uCandidateCount++;

		const uint32_t uAccountID = pResource->m_iAccountID(n);
		if (!uAccountID)
		{
			tResult.m_uSkippedInvalid++;
			continue;
		}

		const char* pszName = pResource->GetName(n);
		if (!pszName)
		{
			tResult.m_uSkippedInvalid++;
			continue;
		}

		if (std::strchr(pszName, ','))
		{
			tResult.m_uSkippedComma++;
			continue;
		}

		std::string sClean = SanitizeName(pszName);
		if (sClean.empty() || sClean == "  " || sClean == "ERRORNAME")
		{
			tResult.m_uSkippedInvalid++;
			continue;
		}

		if (!setSessionAccounts.emplace(uAccountID).second)
		{
			tResult.m_uSkippedSessionDuplicate++;
			continue;
		}

		vProfiles.push_back(ProfileEntry_t{ uAccountID, std::move(sClean) });
	}

	if (!tResult.m_uCandidateCount)
	{
		if (bAnnounce)
			SDK::Output("ProfileScraper", "No player profiles found");
		return tResult;
	}

	if (vProfiles.empty())
	{
		if (bAnnounce)
		{
			const char* pszReason = tResult.m_uSkippedComma ? "All player names contained commas" : "No valid player profiles to save";
			SDK::Output("ProfileScraper", pszReason);
		}
		return tResult;
	}

	auto sPath = std::filesystem::current_path() / "unibox" / "profiles.csv";
	std::error_code ec;
	std::filesystem::create_directories(sPath.parent_path(), ec);

	std::unordered_set<uint64_t> setExistingIDs;
	setExistingIDs.reserve(vProfiles.size() * 2);

	bool bAppendNewline = false;
	if (std::filesystem::exists(sPath))
	{
		std::ifstream input(sPath);
		if (input)
		{
			tResult.m_bFileOpened = true;
			std::string sLine;
			while (std::getline(input, sLine))
			{
				if (sLine.empty())
					continue;
				auto iComma = sLine.find(',');
				if (iComma == std::string::npos)
					continue;
				try
				{
					uint64_t uExisting = std::stoull(sLine.substr(0, iComma));
					setExistingIDs.emplace(uExisting);
				}
				catch (...)
				{
					continue;
				}
			}

			input.clear();
			input.seekg(0, std::ios::end);
			bAppendNewline = input.tellg() > 0;
		}
		else if (bAnnounce)
			SDK::Output("ProfileScraper", std::format("Failed to read existing profiles from {}", sPath.string()).c_str());
	}

	struct ProfileLine_t
	{
		uint64_t m_uSteamID64 = 0;
		std::string m_sName = {};
	};

	std::vector<ProfileLine_t> vNewProfiles;
	vNewProfiles.reserve(vProfiles.size());
	for (const auto& tEntry : vProfiles)
	{
		const uint64_t uSteamID64 = CSteamID(tEntry.m_uAccountID, k_EUniversePublic, k_EAccountTypeIndividual).ConvertToUint64();
		if (setExistingIDs.contains(uSteamID64))
		{
			tResult.m_uSkippedFileDuplicate++;
			continue;
		}

		setExistingIDs.emplace(uSteamID64);
		vNewProfiles.push_back({ uSteamID64, tEntry.m_sName });
	}

	tResult.m_uAppendedCount = vNewProfiles.size();
	tResult.m_outputPath = sPath;

	if (!vNewProfiles.empty())
	{
		std::ofstream file(sPath, std::ios::app);
		if (!file)
		{
			if (bAnnounce)
				SDK::Output("ProfileScraper", std::format("Failed to open {}", sPath.string()).c_str());
			return tResult;
		}

		tResult.m_bFileOpened = true;
		if (bAppendNewline)
			file << '\n';

		for (size_t i = 0; i < vNewProfiles.size(); i++)
		{
			if (i)
				file << '\n';
			file << vNewProfiles[i].m_uSteamID64 << ',' << vNewProfiles[i].m_sName;
		}

		if (!file.good())
		{
			if (bAnnounce)
				SDK::Output("ProfileScraper", "Failed to write profiles");
			return tResult;
		}

		tResult.m_bSuccess = true;
	}
	else if (bAnnounce)
	{
		const size_t uDuplicateCount = tResult.m_uSkippedSessionDuplicate + tResult.m_uSkippedFileDuplicate;
		SDK::Output("ProfileScraper", std::format("No new profiles to save ({} duplicates skipped, {} comma filtered)",
			uDuplicateCount,
			tResult.m_uSkippedComma).c_str());
	}

	auto CaptureAvatar = [](uint32_t uAccountID, std::vector<uint8_t>& vBgra, uint32_t& uWidth, uint32_t& uHeight) -> bool
		{
			if (!I::SteamFriends || !I::SteamUtils)
				return false;

			const CSteamID steamID(uAccountID, k_EUniversePublic, k_EAccountTypeIndividual);
			I::SteamFriends->RequestUserInformation(steamID, true);
			const int nAvatar = I::SteamFriends->GetMediumFriendAvatar(steamID);
			if (nAvatar <= 0)
				return false;

			if (!I::SteamUtils->GetImageSize(nAvatar, &uWidth, &uHeight) || !uWidth || !uHeight)
				return false;

			std::vector<uint8_t> vRgba(static_cast<size_t>(uWidth) * static_cast<size_t>(uHeight) * 4);
			if (!I::SteamUtils->GetImageRGBA(nAvatar, vRgba.data(), static_cast<int>(vRgba.size())))
				return false;

			vBgra.resize(vRgba.size());
			for (uint32_t y = 0; y < uHeight; y++)
			{
				for (uint32_t x = 0; x < uWidth; x++)
				{
					const size_t idx = (static_cast<size_t>(y) * uWidth + x) * 4;
					vBgra[idx + 0] = vRgba[idx + 2];
					vBgra[idx + 1] = vRgba[idx + 1];
					vBgra[idx + 2] = vRgba[idx + 0];
					vBgra[idx + 3] = vRgba[idx + 3];
				}
			}
			return true;
		};

	if (I::SteamFriends && I::SteamUtils)
	{
		for (const auto& tEntry : vProfiles)
		{
			std::vector<uint8_t> vBgra;
			uint32_t uWidth = 0, uHeight = 0;
			if (!CaptureAvatar(tEntry.m_uAccountID, vBgra, uWidth, uHeight))
			{
				tResult.m_uAvatarMissed++;
				continue;
			}

			std::filesystem::path sAvatarPath;
			if (CSteamProfileCache::SaveAvatarToDisk(tEntry.m_uAccountID, vBgra, uWidth, uHeight, &sAvatarPath, false))
			{
				tResult.m_uAvatarsSaved++;
				if (tResult.m_avatarFolder.empty())
					tResult.m_avatarFolder = sAvatarPath.parent_path();
			}
			else
			{
				tResult.m_uAvatarFailed++;
			}
		}
	}
	else if (bAnnounce)
	{
		SDK::Output("ProfileScraper", "Steam avatar interfaces unavailable; skipped avatar scraping.");
	}

	if (bAnnounce)
	{
		const size_t uDuplicateCount = tResult.m_uSkippedSessionDuplicate + tResult.m_uSkippedFileDuplicate;
		SDK::Output("ProfileScraper", std::format(
			"Saved {} new profiles to {} ({} duplicates skipped, {} comma filtered). Avatars: {} saved, {} unavailable, {} failed.",
			tResult.m_uAppendedCount,
			sPath.string(),
			uDuplicateCount,
			tResult.m_uSkippedComma,
			tResult.m_uAvatarsSaved,
			tResult.m_uAvatarMissed,
			tResult.m_uAvatarFailed).c_str());

		if (!tResult.m_avatarFolder.empty())
			SDK::Output("ProfileScraper", std::format("Avatar output directory: {}", tResult.m_avatarFolder.string()).c_str());
	}

	return tResult;
}

std::string CMisc::ReplaceTags(std::string sMsg, std::string sTarget, std::string sInitiator, std::string sKiller, std::string sVictim)
{
	auto ReplaceAll = [&](std::string& str, const std::string& from, const std::string& to)
		{
			if (from.empty()) return;
			size_t start_pos = 0;
			while ((start_pos = str.find(from, start_pos)) != std::string::npos)
			{
				str.replace(start_pos, from.length(), to);
				start_pos += to.length();
			}
		};

	if (!sTarget.empty())
	{
		ReplaceAll(sMsg, "{target}", sTarget);
		ReplaceAll(sMsg, "{triggername}", sTarget);
	}

	if (!sInitiator.empty())
		ReplaceAll(sMsg, "{initiator}", sInitiator);

	if (!sKiller.empty())
		ReplaceAll(sMsg, "{killer}", sKiller);

	if (!sVictim.empty())
		ReplaceAll(sMsg, "{victim}", sVictim);

	if (sMsg.find("{enemyteam}") != std::string::npos || sMsg.find("{friendlyteam}") != std::string::npos)
	{
		auto pLocal = H::Entities.GetLocal();
		if (pLocal)
		{
			int iLocalTeam = pLocal->m_iTeamNum();
			ReplaceAll(sMsg, "{enemyteam}", iLocalTeam == TF_TEAM_RED ? "BLU" : "RED");
			ReplaceAll(sMsg, "{friendlyteam}", iLocalTeam == TF_TEAM_RED ? "RED" : "BLU");
		}
	}

	if (sMsg.find("{lastkilled}") != std::string::npos)
		ReplaceAll(sMsg, "{lastkilled}", m_sLastKilledName.empty() ? "unknown" : m_sLastKilledName);

	if (sMsg.find("{highestscore}") != std::string::npos)
	{
		int iHighestScore = -1;
		std::string sHighestScoreName = "unknown";
		auto pResource = H::Entities.GetResource();
		if (pResource)
		{
			for (int i = 1; i <= I::EngineClient->GetMaxClients(); i++)
			{
				if (!pResource->m_bValid(i)) continue;
				int iScore = pResource->m_iTotalScore(i);
				if (iScore > iHighestScore)
				{
					iHighestScore = iScore;
					sHighestScoreName = pResource->GetName(i);
				}
			}
		}
		ReplaceAll(sMsg, "{highestscore}", sHighestScoreName);
	}

	auto GetRandomPlayer = [&](bool bFriend, bool bIgnored) -> std::string
		{
			std::vector<std::string> vCandidates;
			auto pResource = H::Entities.GetResource();
			if (pResource)
			{
				for (int i = 1; i <= I::EngineClient->GetMaxClients(); i++)
				{
					if (!pResource->m_bValid(i) || pResource->IsFakePlayer(i)) continue;

					bool isFriend = F::PlayerUtils.HasTag(i, F::PlayerUtils.TagToIndex(FRIEND_TAG));
					bool isIgnored = F::PlayerUtils.HasTag(i, F::PlayerUtils.TagToIndex(IGNORED_TAG));

					if (bFriend && !isFriend) continue;
					if (bIgnored && !isIgnored) continue;

					vCandidates.push_back(pResource->GetName(i));
				}
			}
			if (vCandidates.empty()) return "unknown";
			return vCandidates[SDK::RandomInt(0, static_cast<int>(vCandidates.size()) - 1)];
		};

	if (sMsg.find("{random}") != std::string::npos)
		ReplaceAll(sMsg, "{random}", GetRandomPlayer(false, false));

	if (sMsg.find("{friend}") != std::string::npos)
		ReplaceAll(sMsg, "{friend}", GetRandomPlayer(true, false));

	if (sMsg.find("{ignored}") != std::string::npos)
		ReplaceAll(sMsg, "{ignored}", GetRandomPlayer(false, true));

	return sMsg;
}

void CMisc::OnVoteStart(int iCaller, int iTarget, const std::string& sTarget)
{
	if (!Vars::Misc::Automation::ChatSpam::VoteKickReply.Value)
		return;

	static Timer tReloadTimer{};
	if ((m_vF1Messages.empty() && m_vF2Messages.empty()) || tReloadTimer.Run(5.0f))
	{
		m_vF1Messages.clear();
		m_vF2Messages.clear();

		static const char* szDefaultContent =
			"// Vote Kick Reply Configuration\n"
			"// Format: F1: message (supports {target}, {initiator}, {enemyteam}, {friendlyteam})\n"
			"// Format: F2: message (supports {target}, {initiator}, {enemyteam}, {friendlyteam})\n"
			"F1: {initiator} called a vote on {target}! Go {friendlyteam}!\n"
			"F2: {initiator} is trying to kick {target}! Don't let {enemyteam} win!\n";

		std::vector<std::string> vLines;
		if (LoadLines("votekick.txt", vLines, szDefaultContent))
		{
			for (const auto& line : vLines)
			{
				if (line.find("F1:") == 0)
				{
					std::string msg = line.substr(3);
					if (msg.find_first_not_of(" \t") != std::string::npos)
						msg.erase(0, msg.find_first_not_of(" \t"));
					if (!msg.empty()) m_vF1Messages.push_back(msg);
				}
				else if (line.find("F2:") == 0)
				{
					std::string msg = line.substr(3);
					if (msg.find_first_not_of(" \t") != std::string::npos)
						msg.erase(0, msg.find_first_not_of(" \t"));
					if (!msg.empty()) m_vF2Messages.push_back(msg);
				}
			}
		}
	}

	bool bTargetIsFriend = H::Entities.IsFriend(iTarget) || H::Entities.InParty(iTarget) || F::PlayerUtils.IsIgnored(iTarget);
	bool bCallerIsFriend = H::Entities.IsFriend(iCaller) || H::Entities.InParty(iCaller) || F::PlayerUtils.IsIgnored(iCaller);
	bool bTargetIsLocal = iTarget == I::EngineClient->GetLocalPlayer();
	bool bCallerIsLocal = iCaller == I::EngineClient->GetLocalPlayer();

	std::string sReply = "";
	std::string sInitiator = "";
	auto pResource = H::Entities.GetResource();
	if (pResource && pResource->m_bValid(iCaller))
		sInitiator = pResource->GetName(iCaller);

	if ((bTargetIsFriend || bTargetIsLocal) && !bCallerIsLocal)
	{
		if (!m_vF2Messages.empty())
		{
			int index = SDK::RandomInt(0, static_cast<int>(m_vF2Messages.size()) - 1);
			sReply = m_vF2Messages[index];
		}
	}
	else if ((bCallerIsFriend || bCallerIsLocal) && !bTargetIsLocal)
	{
		if (!m_vF1Messages.empty())
		{
			int index = SDK::RandomInt(0, static_cast<int>(m_vF1Messages.size()) - 1);
			sReply = m_vF1Messages[index];
		}
	}

	if (!sReply.empty())
	{
		sReply = ReplaceTags(sReply, sTarget, sInitiator);
		I::EngineClient->ClientCmd_Unrestricted(std::format("say {}", sReply).c_str());
	}
}

bool CMisc::LoadLines(const char* szFileName, std::vector<std::string>& vLines, const char* szDefaultContent)
{
	vLines.clear();

	std::string sPath = F::Configs.m_sConfigPath + szFileName;

	if (!std::filesystem::exists(sPath) && szDefaultContent)
	{
		std::ofstream newFile(sPath);
		if (newFile.good())
			newFile << szDefaultContent;
	}

	std::ifstream file(sPath);
	if (!file.good())
		return false;

	std::string line;
	while (std::getline(file, line))
	{
		if (line.empty() || line.find("//") == 0)
			continue;

		vLines.push_back(line);
	}

	return !vLines.empty();
}

std::vector<std::string> CMisc::ParseTokens(std::string str, char delimiter)
{
	std::vector<std::string> tokens;
	size_t pos = 0;
	std::string token;
	while ((pos = str.find(delimiter)) != std::string::npos)
	{
		token = str.substr(0, pos);
		if (token.find_first_not_of(" \t") != std::string::npos)
		{
			token.erase(0, token.find_first_not_of(" \t"));
			token.erase(token.find_last_not_of(" \t") + 1);
			if (!token.empty())
				tokens.push_back(token);
		}
		str.erase(0, pos + 1);
	}
	token = str;
	if (token.find_first_not_of(" \t") != std::string::npos)
	{
		token.erase(0, token.find_first_not_of(" \t"));
		token.erase(token.find_last_not_of(" \t") + 1);
		if (!token.empty())
			tokens.push_back(token);
	}
	return tokens;
}

void CMisc::OnChatMessage(int iEntIndex, const std::string& sName, const std::string& sMsg)
{
	if (iEntIndex == I::EngineClient->GetLocalPlayer())
		return;

	if (Vars::Misc::Automation::ChatSpam::AutoReply.Value)
	{
		static Timer tReloadTimer{};
		if (m_vAutoReplies.empty() || tReloadTimer.Run(5.0f))
		{
			m_vAutoReplies.clear();

			static const char* szDefaultContent =
				"// Auto Reply Configuration\n"
				"// Format: trigger1, trigger2 : response1, response2\n"
				"// Example:\n"
				"hello, hi : hi there, hello!\n"
				"bot, hacker : I am not a bot, I am just good\n";

			std::vector<std::string> vLines;
			if (LoadLines("autoreply.txt", vLines, szDefaultContent))
			{
				for (const auto& line : vLines)
				{
					size_t delimiterPos = line.find(':');
					if (delimiterPos != std::string::npos)
					{
						std::string triggersStr = line.substr(0, delimiterPos);
						std::string repliesStr = line.substr(delimiterPos + 1);

						AutoReply_t entry;
						entry.vTriggers = ParseTokens(triggersStr, ',');
						entry.vReplies = ParseTokens(repliesStr, ',');

						if (!entry.vTriggers.empty() && !entry.vReplies.empty())
							m_vAutoReplies.push_back(entry);
					}
				}
			}
		}

		auto StripColors = [](std::string str) -> std::string
			{
				std::string out = "";
				for (size_t i = 0; i < str.length(); i++)
				{
					if (str[i] > 0 && str[i] < 32) continue;
					out += str[i];
				}
				return out;
			};

		std::string sCleanMsg = StripColors(sMsg);
		std::transform(sCleanMsg.begin(), sCleanMsg.end(), sCleanMsg.begin(), ::tolower);

		for (const auto& entry : m_vAutoReplies)
		{
			bool bTriggered = false;
			for (const auto& trigger : entry.vTriggers)
			{
				std::string sLowTrigger = trigger;
				std::transform(sLowTrigger.begin(), sLowTrigger.end(), sLowTrigger.begin(), ::tolower);

				if (sCleanMsg.find(sLowTrigger) != std::string::npos)
				{
					bTriggered = true;
					break;
				}
			}

			if (bTriggered)
			{
				if (!entry.vReplies.empty())
				{
					int index = SDK::RandomInt(0, static_cast<int>(entry.vReplies.size()) - 1);
					std::string sReply = ReplaceTags(entry.vReplies[index], sName);
					I::EngineClient->ClientCmd_Unrestricted(std::format("say {}", sReply).c_str());
					break;
				}
			}
		}
	}

}
