#include "Recorder.h"

#include "../Configs/Configs.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>

// Per-map route files. The format is a plain binary blob: magic, route count, then each
// route as [name length][name][frame count][frames]. Version 3 ("RMR3") is the current one.
static constexpr uint32_t REC_MAGIC = 0x33524D52; // "RMR3"

// The integer tickrate of the current session (66 on stock TF2, 64 on a 64-tick server).
// Truncated, NOT rounded, so 1/0.015 -> 66 (rounding would give a bogus 67). Per-tick
// route data is only valid at the tickrate it was recorded at, hence the filename suffix.
static int RecorderTickrate()
{
	const float flInterval = I::GlobalVars ? I::GlobalVars->interval_per_tick : 0.015f;
	const int iTick = (flInterval > 0.f) ? int(1.f / flInterval) : 66;
	return std::clamp(iTick, 1, 1000);
}

static std::string RecorderDir()
{
	std::string sDir = F::Configs.m_sConfigPath + "Recordings\\";
	try { if (!std::filesystem::exists(sDir)) std::filesystem::create_directories(sDir); } catch (...) {}
	return sDir;
}

static std::string RecorderFile(const std::string& sMap)
{
	// Map names can contain '/' (e.g. workshop/x.ugc123) - flatten to a safe filename.
	std::string sSafe = sMap.empty() ? "unknown" : sMap;
	for (auto& c : sSafe) if (c == '/' || c == '\\' || c == ':') c = '_';
	// e.g. "pl_upward_66.rec" - the tickrate suffix keeps 64- and 66-tick routes apart.
	return RecorderDir() + sSafe + "_" + std::to_string(RecorderTickrate()) + ".rec";
}

std::string CRecorder::RecorderCurrentMap() const
{
	return I::EngineClient->GetLevelName() ? I::EngineClient->GetLevelName() : "";
}

void CRecorder::LoadRoutes(const std::string& sMap)
{
	m_vRoutes.clear();
	m_sRoutesMap = sMap;
	m_iSelectedRoute = -1;

	std::ifstream f(RecorderFile(sMap), std::ios::binary);
	if (!f.is_open())
		return;

	uint32_t uMagic = 0, uRoutes = 0;
	f.read(reinterpret_cast<char*>(&uMagic), sizeof(uMagic));
	f.read(reinterpret_cast<char*>(&uRoutes), sizeof(uRoutes));
	if (!f || uMagic != REC_MAGIC || uRoutes == 0 || uRoutes > 4096)
		return;

	for (uint32_t r = 0; r < uRoutes; r++)
	{
		uint32_t uNameLen = 0;
		f.read(reinterpret_cast<char*>(&uNameLen), sizeof(uNameLen));
		if (!f || uNameLen > 256)
			return;
		Route_t tRoute;
		tRoute.m_sName.resize(uNameLen);
		if (uNameLen)
			f.read(tRoute.m_sName.data(), uNameLen);

		uint32_t uFrames = 0;
		f.read(reinterpret_cast<char*>(&uFrames), sizeof(uFrames));
		if (!f || uFrames == 0 || uFrames > 200000)
			return;
		tRoute.m_vFrames.resize(uFrames);
		f.read(reinterpret_cast<char*>(tRoute.m_vFrames.data()), std::streamsize(uFrames * sizeof(RecFrame_t)));
		if (!f)
			return;
		m_vRoutes.push_back(std::move(tRoute));
	}
	// Leave nothing selected: Play then defaults to the NEAREST route's start (move-to-start),
	// while the menu can still pick a specific route to force it.
}

bool CRecorder::WriteRoutes(const std::string& sMap)
{
	std::ofstream f(RecorderFile(sMap), std::ios::binary | std::ios::trunc);
	if (!f.is_open())
		return false;

	const uint32_t uMagic = REC_MAGIC;
	const uint32_t uRoutes = uint32_t(m_vRoutes.size());
	f.write(reinterpret_cast<const char*>(&uMagic), sizeof(uMagic));
	f.write(reinterpret_cast<const char*>(&uRoutes), sizeof(uRoutes));
	for (const auto& tRoute : m_vRoutes)
	{
		const uint32_t uNameLen = uint32_t(tRoute.m_sName.size());
		const uint32_t uFrames = uint32_t(tRoute.m_vFrames.size());
		f.write(reinterpret_cast<const char*>(&uNameLen), sizeof(uNameLen));
		f.write(tRoute.m_sName.data(), uNameLen);
		f.write(reinterpret_cast<const char*>(&uFrames), sizeof(uFrames));
		f.write(reinterpret_cast<const char*>(tRoute.m_vFrames.data()), std::streamsize(uFrames * sizeof(RecFrame_t)));
	}
	return bool(f);
}

void CRecorder::RecorderSaveActiveAs(const std::string& sName)
{
	if (m_vRecording.empty())
		return;
	const std::string sMap = RecorderCurrentMap();
	if (m_sRoutesMap != sMap)
		LoadRoutes(sMap);

	Route_t tRoute;
	tRoute.m_sName = sName.empty() ? std::format("route {}", m_vRoutes.size() + 1) : sName;
	tRoute.m_vFrames = m_vRecording;
	m_vRoutes.push_back(std::move(tRoute));
	m_iSelectedRoute = int(m_vRoutes.size()) - 1;
	WriteRoutes(sMap);
}

void CRecorder::RecorderDeleteRoute(int i)
{
	if (i < 0 || i >= int(m_vRoutes.size()))
		return;
	m_vRoutes.erase(m_vRoutes.begin() + i);
	if (m_iSelectedRoute >= int(m_vRoutes.size()))
		m_iSelectedRoute = int(m_vRoutes.size()) - 1;
	WriteRoutes(m_sRoutesMap.empty() ? RecorderCurrentMap() : m_sRoutesMap);
}

void CRecorder::RecorderReload()
{
	LoadRoutes(RecorderCurrentMap());
}

// Cut flStartSec off the front and flEndSec off the back of a saved route, then persist.
// Always leaves at least one frame so the route (and its start marker) survives.
void CRecorder::RecorderTrimRoute(int i, float flStartSec, float flEndSec)
{
	if (i < 0 || i >= int(m_vRoutes.size()))
		return;
	auto& vFrames = m_vRoutes[i].m_vFrames;
	const int iTotal = int(vFrames.size());
	if (iTotal <= 1)
		return;

	int iCutFront = std::max(0, int(std::lround(flStartSec / TICK_INTERVAL)));
	int iCutBack = std::max(0, int(std::lround(flEndSec / TICK_INTERVAL)));
	// Keep at least one frame: never let the two cuts overlap into nothing.
	if (iCutFront + iCutBack >= iTotal)
		iCutBack = std::max(0, iTotal - 1 - iCutFront);
	if (iCutFront >= iTotal)
		iCutFront = iTotal - 1;

	if (iCutBack > 0)
		vFrames.erase(vFrames.end() - iCutBack, vFrames.end());
	if (iCutFront > 0)
		vFrames.erase(vFrames.begin(), vFrames.begin() + iCutFront);

	WriteRoutes(m_sRoutesMap.empty() ? RecorderCurrentMap() : m_sRoutesMap);
}

// Velocity-aware "arrive" steer: pick the movement that gets us to the target while
// killing the remaining velocity, so we settle on the point instead of orbiting it.
static void RecorderSteerToward(CUserCmd* pCmd, const Vec3& vFrom, const Vec3& vVel, const Vec3& vTarget)
{
	const Vec3 vRem = { vTarget.x - vFrom.x, vTarget.y - vFrom.y, 0.f };
	const float flR = vRem.Length2D();
	const float flDt = std::max(TICK_INTERVAL, 0.0001f);

	// Velocity we'd want this tick: just enough to land on the target in one tick, capped to
	// a sane max. As flR -> 0 the wanted speed -> 0, so the controller eases to a dead stop.
	Vec3 vWish;
	if (flR > 1e-5f)
	{
		const float flDesired = std::min(flR / flDt, 450.f);
		const Vec3 vDir = vRem / flR;
		vWish = { vDir.x * flDesired - vVel.x, vDir.y * flDesired - vVel.y, 0.f };
	}
	else
		vWish = { -vVel.x, -vVel.y, 0.f }; // dead on target: just cancel any residual drift

	// Rotate the world wish into forward/side (same yaw convention as SDK::ComputeMove).
	const float flYawRad = Math::Deg2Rad(pCmd->viewangles.y);
	const float flCos = cosf(flYawRad), flSin = sinf(flYawRad);
	pCmd->forwardmove = std::clamp(vWish.x * flCos + vWish.y * flSin, -450.f, 450.f);
	pCmd->sidemove = std::clamp(vWish.x * flSin - vWish.y * flCos, -450.f, 450.f);
}

void CRecorder::Run(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!Vars::Misc::Movement::MovementRecorder.Value || !pLocal || !pLocal->IsAlive())
		return;

	const std::string sMap = RecorderCurrentMap();
	if (sMap != m_sRoutesMap) // map changed (or first run): pull this map's routes from disk
		LoadRoutes(sMap);

	const Vec3 vOrigin = pLocal->m_vecOrigin();

	const bool bRec = Vars::Misc::Movement::MovementRecorderRecord.Value;
	const bool bPlay = Vars::Misc::Movement::MovementRecorderPlay.Value;
	const bool bStop = Vars::Misc::Movement::MovementRecorderStop.Value;
	const bool bSave = Vars::Misc::Movement::MovementRecorderSave.Value;
	const bool bLoad = Vars::Misc::Movement::MovementRecorderLoad.Value;
	const bool bClear = Vars::Misc::Movement::MovementRecorderClear.Value;

	const bool bBusy = m_bPlaying || m_bMovingToStart || m_bWishToStart;

	if (bRec && !m_bRecKeyHeld) // record toggle (cancels any playback)
	{
		m_bRecording = !m_bRecording;
		m_bPlaying = m_bMovingToStart = m_bSmoothToStart = m_bWishToStart = false;
		if (m_bRecording) { m_vRecording.clear(); m_iPlayRoute = -1; }
	}
	m_bRecKeyHeld = bRec;

	if (bPlay && !m_bPlayKeyHeld && !m_bRecording && !bBusy) // begin a route: move-to-start then replay
	{
		// Prefer the menu-selected route; else the nearest route start to the player.
		int iRoute = -1;
		if (m_iSelectedRoute >= 0 && m_iSelectedRoute < int(m_vRoutes.size()) && !m_vRoutes[m_iSelectedRoute].m_vFrames.empty())
			iRoute = m_iSelectedRoute;
		else
		{
			float flBest = float(Vars::Misc::Movement::MovementRecorderMoveToStartDist.Value);
			if (flBest <= 0.f) flBest = 99999.f;
			for (int i = 0; i < int(m_vRoutes.size()); i++)
			{
				if (m_vRoutes[i].m_vFrames.empty())
					continue;
				const float flDist = vOrigin.DistTo(m_vRoutes[i].m_vFrames.front().m_vOrigin);
				if (flDist < flBest) { flBest = flDist; iRoute = i; }
			}
		}

		if (iRoute >= 0)
		{
			m_iPlayRoute = iRoute;
			m_vRecording = m_vRoutes[iRoute].m_vFrames; // active buffer = the route being replayed
			m_iPlaybackIdx = 0;

			// A route that started while already moving fast can't be matched by creeping in
			// (the arrive controller decelerates to a stop on the point), so its trick won't
			// reproduce reliably - flag it so the HUD warns you to re-record it.
			m_bRouteUnreliable = m_vRecording.front().m_vVelocity.Length2D() > 30.f;

			const Vec3 vStart = m_vRecording.front().m_vOrigin;
			const float flTol = Vars::Misc::Movement::MovementRecorderStartTolerance.Value;
			const float flSpd = float(Vars::Misc::Movement::MovementRecorderStartSpeed.Value);
			// Only skip the approach if we're ALREADY at the exact start AND near standstill;
			// otherwise creep in so position + velocity match the recording before frame 0 runs.
			if (Vars::Misc::Movement::MovementRecorderMoveToStart.Value
				&& (vOrigin.DistTo(vStart) > flTol || pLocal->m_vecVelocity().Length2D() > flSpd))
			{
				m_bMovingToStart = true;
				m_bSmoothToStart = Vars::Misc::Movement::MovementRecorderLockView.Value;
				m_bWishToStart = true;
				m_iSettleTicks = m_iStallTicks = 0;
				m_flAlignBest = FLT_MAX;
			}
			else
				m_bPlaying = true; // already at the start: replay immediately
		}
	}
	m_bPlayKeyHeld = bPlay;

	if (bStop && !m_bStopKeyHeld) // stop playback / move-to-start (leaves recording alone)
	{
		m_bPlaying = m_bMovingToStart = m_bSmoothToStart = m_bWishToStart = false;
		m_iPlaybackIdx = 0;
	}
	m_bStopKeyHeld = bStop;

	if (bSave && !m_bSaveKeyHeld) RecorderSaveActiveAs(""); // quick-save the active recording
	m_bSaveKeyHeld = bSave;

	if (bLoad && !m_bLoadKeyHeld) LoadRoutes(sMap);
	m_bLoadKeyHeld = bLoad;

	if (bClear && !m_bClearKeyHeld)
	{
		m_vRecording.clear();
		m_bRecording = m_bPlaying = m_bMovingToStart = m_bWishToStart = false;
		m_iPlaybackIdx = 0;
	}
	m_bClearKeyHeld = bClear;

	// --- Shadowplay: the save bind dumps the rolling buffer into the active recording, so you
	// can keep a moment after it happened (then Save it as a route, or Play it immediately). ---
	const bool bShadowSave = Vars::Misc::Movement::MovementRecorderShadowSave.Value;
	if (bShadowSave && !m_bShadowSaveHeld && !m_dqShadow.empty())
	{
		m_vRecording.assign(m_dqShadow.begin(), m_dqShadow.end()); // last N seconds become the recording
		m_bRecording = m_bPlaying = m_bMovingToStart = m_bWishToStart = m_bSmoothToStart = false;
		m_iPlaybackIdx = 0;
		m_iPlayRoute = -1;
	}
	m_bShadowSaveHeld = bShadowSave;

	// While idle, keep a rolling buffer of the last N seconds of movement. Game thread only -
	// the render thread never reads m_dqShadow, so there is no race. Freed when turned off.
	if (Vars::Misc::Movement::MovementRecorderShadowPlay.Value
		&& !m_bRecording && !m_bPlaying && !m_bMovingToStart && !m_bWishToStart)
	{
		RecFrame_t tShadow;
		tShadow.m_vViewAngles = pCmd->viewangles;
		tShadow.m_flForward = pCmd->forwardmove;
		tShadow.m_flSide = pCmd->sidemove;
		tShadow.m_flUp = pCmd->upmove;
		tShadow.m_iButtons = pCmd->buttons;
		tShadow.m_vOrigin = vOrigin;
		tShadow.m_vVelocity = pLocal->m_vecVelocity();
		m_dqShadow.push_back(tShadow);
		const size_t iMax = std::max<size_t>(1, size_t(Vars::Misc::Movement::MovementRecorderShadowSeconds.Value / TICK_INTERVAL));
		while (m_dqShadow.size() > iMax)
			m_dqShadow.pop_front();
	}
	else if (!Vars::Misc::Movement::MovementRecorderShadowPlay.Value && !m_dqShadow.empty())
		m_dqShadow.clear();

	// --- Recording: capture the final command + position each tick ---
	if (m_bRecording)
	{
		RecFrame_t tFrame;
		tFrame.m_vViewAngles = pCmd->viewangles;
		tFrame.m_flForward = pCmd->forwardmove;
		tFrame.m_flSide = pCmd->sidemove;
		tFrame.m_flUp = pCmd->upmove;
		tFrame.m_iButtons = pCmd->buttons;
		tFrame.m_vOrigin = vOrigin;
		tFrame.m_vVelocity = pLocal->m_vecVelocity(); // pre-sim velocity - the start frame's is the gate target
		m_vRecording.push_back(tFrame);
		if (m_vRecording.size() > 120000) // ~30 min @ 66 tick safety cap
			m_bRecording = false;
		return;
	}

	if (m_vRecording.empty())
		return;
	const Vec3 vStart = m_vRecording.front().m_vOrigin;

	// --- Move-to-start: creep to the EXACT recorded start, align the view, then gate playback ---
	if (m_bMovingToStart)
	{
		const Vec3 vVel = pLocal->m_vecVelocity();
		const float flDist = vOrigin.DistTo(vStart);

		m_flAlignDist = flDist;             // HUD feedback
		m_flAlignSpeed = vVel.Length2D();

		// Track the closest we've gotten. Once the distance stops improving we've hit the floor
		// ground physics allows for this point, so there's no value in waiting out the timeout.
		if (flDist < m_flAlignBest - 0.003f) { m_flAlignBest = flDist; m_iStallTicks = 0; }
		else m_iStallTicks++;

		// Velocity-aware arrive: decelerate onto the EXACT point and keep shimmying it tighter.
		RecorderSteerToward(pCmd, vOrigin, vVel, vStart);

		// Optionally rotate the view onto the recorded start angle while approaching.
		bool bViewOK = true;
		if (m_bSmoothToStart)
		{
			pCmd->mousedx = pCmd->mousedy = 0;
			const Vec3& vWant = m_vRecording.front().m_vViewAngles;
			Vec3 vDelta = { vWant.x - pCmd->viewangles.x, Math::NormalizeAngle(vWant.y - pCmd->viewangles.y), 0.f };
			bViewOK = vDelta.Length2D() < 1.f;
			if (!bViewOK)
			{
				pCmd->viewangles.x += vDelta.x / 8.f;
				pCmd->viewangles.y += vDelta.y / 8.f;
				I::EngineClient->SetViewAngles(pCmd->viewangles);
			}
		}

		// Gate: exact position + settled speed (+ view aligned). If the route was recorded
		// mid-motion its start speed is already high and can't be matched by creeping in, so
		// gate on position only there.
		const float flTol = Vars::Misc::Movement::MovementRecorderStartTolerance.Value;
		const float flSpd = float(Vars::Misc::Movement::MovementRecorderStartSpeed.Value);
		const Vec3& vRecStartVel = m_vRecording.front().m_vVelocity;
		const float flRecStartSpeed = vRecStartVel.Length2D();
		const bool bPosOK = flDist <= flTol;
		// Match the recorded start VELOCITY as a vector (not just our raw speed): a route
		// recorded at rest needs us to be at rest, not merely slow.
		const Vec3 vVelErr = { vVel.x - vRecStartVel.x, vVel.y - vRecStartVel.y, 0.f };
		const bool bSpeedOK = (flRecStartSpeed > 30.f) ? true : (vVelErr.Length2D() <= flSpd);
		// Settled: the shimmy hasn't improved for ~0.25s and we're already close - that's the
		// physical floor, so commit rather than burning the timeout chasing a decimal.
		const bool bSettled = m_iStallTicks > int(0.25f / TICK_INTERVAL) && flDist <= flTol * 4.f;
		const bool bTimeout = ++m_iSettleTicks > int(3.f / TICK_INTERVAL); // safety so we can't hard-lock

		if (((bPosOK || bSettled) && bSpeedOK && bViewOK) || bTimeout)
		{
			m_bMovingToStart = m_bSmoothToStart = m_bWishToStart = false;
			m_bPlaying = true;
			m_iPlaybackIdx = 0;
			// fall through to playback so frame 0 runs from the matched state THIS tick
		}
	}

	// --- Playback: replay the route verbatim ---
	if (m_bPlaying)
	{
		if (m_iPlaybackIdx >= m_vRecording.size())
			m_bPlaying = false;
		else
		{
			const RecFrame_t& tFrame = m_vRecording[m_iPlaybackIdx++];
			pCmd->viewangles = tFrame.m_vViewAngles;
			pCmd->forwardmove = tFrame.m_flForward;
			pCmd->sidemove = tFrame.m_flSide;
			pCmd->upmove = tFrame.m_flUp;
			pCmd->buttons = tFrame.m_iButtons;

			// --- Drift correction (closed loop) ---
			bool bDoDrift = Vars::Misc::Movement::MovementRecorderDriftCorrect.Value
				&& !Vars::Misc::Movement::MovementRecorderVerbatim.Value
				&& (pLocal->m_fFlags() & FL_ONGROUND);
			if (bDoDrift)
			{
				// Freeze correction around any jump/duck transition - those are the trick's own
				// setup ticks (e.g. the duck that arms a pixelsurf), and nudging them breaks it.
				const size_t iCur = m_iPlaybackIdx - 1;
				constexpr int iEdgeWin = 4;
				const int iButtonMask = IN_JUMP | IN_DUCK;
				const size_t iLo = (iCur > size_t(iEdgeWin)) ? iCur - iEdgeWin : 0;
				const size_t iHi = std::min(iCur + iEdgeWin, m_vRecording.size() - 1);
				for (size_t i = iLo; i < iHi; i++)
				{
					if ((m_vRecording[i].m_iButtons & iButtonMask) != (m_vRecording[i + 1].m_iButtons & iButtonMask))
					{
						bDoDrift = false;
						break;
					}
				}
			}
			if (bDoDrift)
			{
				const Vec3 vErr = { tFrame.m_vOrigin.x - vOrigin.x, tFrame.m_vOrigin.y - vOrigin.y, 0.f };
				const float flErr = vErr.Length2D();
				if (flErr > 0.5f) // deadzone (u): below this, replay verbatim
				{
					const float flCap = Vars::Misc::Movement::MovementRecorderDriftStrength.Value;
					const float flDesired = std::min(flErr / std::max(TICK_INTERVAL, 0.0001f), flCap);
					const Vec3 vDir = vErr / flErr;
					const float flYawRad = Math::Deg2Rad(pCmd->viewangles.y);
					const float flCos = cosf(flYawRad), flSin = sinf(flYawRad);
					const float flFwd = vDir.x * flCos + vDir.y * flSin;
					const float flSide = vDir.x * flSin - vDir.y * flCos;
					pCmd->forwardmove = std::clamp(pCmd->forwardmove + flFwd * flDesired, -450.f, 450.f);
					pCmd->sidemove = std::clamp(pCmd->sidemove + flSide * flDesired, -450.f, 450.f);
				}
			}

			I::EngineClient->SetViewAngles(pCmd->viewangles); // keep the view following the replay
		}
	}
}

void CRecorder::Draw()
{
	if (!Vars::Misc::Movement::MovementRecorder.Value || !Vars::Misc::Movement::MovementRecorderHud.Value)
		return;
	if (!m_bRecording && !m_bPlaying && !m_bMovingToStart)
		return;

	// Title + value-in-seconds + tint per state. Ticks -> seconds via the engine tick interval.
	std::string sTitle, sValue;
	Color_t tCol;
	if (m_bRecording)
	{
		sTitle = "recording";
		sValue = std::format("{:.1f}", m_vRecording.size() * TICK_INTERVAL);
		tCol = Color_t(255, 80, 80, 255);
	}
	else if (m_bMovingToStart)
	{
		// "aligning" while still walking in; "settling" once on the point but waiting for the
		// speed to bleed off (so frame 0 starts from the recorded standstill).
		const float flTol = Vars::Misc::Movement::MovementRecorderStartTolerance.Value;
		sTitle = (m_flAlignDist <= flTol) ? "settling" : "aligning";
		sValue = std::format("{:.2f}u", m_flAlignDist);
		tCol = Color_t(255, 210, 90, 255);
	}
	else // playing
	{
		sTitle = (m_iPlayRoute >= 0 && m_iPlayRoute < int(m_vRoutes.size())) ? m_vRoutes[m_iPlayRoute].m_sName : "route";
		if (sTitle.empty())
			sTitle = "route";
		sValue = std::format("{} / {}", int(m_iPlaybackIdx * TICK_INTERVAL), int(m_vRecording.size() * TICK_INTERVAL));
		tCol = Color_t(120, 255, 140, 255);
	}
	// A route recorded mid-motion can't be reproduced by creeping to its start - amber-tint and
	// prefix it so you know to re-record it from a dead standstill.
	if (m_bRouteUnreliable && !m_bRecording)
	{
		sTitle = "! " + sTitle;
		tCol = Color_t(255, 150, 60, 255);
	}

	const auto& tFont = H::Fonts.GetFont(FONT_INDICATORS);
	const int iLineH = std::max(int(H::Draw.GetTextSize("0", tFont).y), int(H::Draw.Scale(11)));
	const int iPad = int(H::Draw.Scale(8));
	const int iGap = int(H::Draw.Scale(6));
	const int iW = int(H::Draw.Scale(120));
	const int iH = iPad * 2 + iLineH * 2 + iGap; // pad + title + gap + value + pad

	// DragBox stores centre-x / top-y (see CMenu::AddDraggable); centre the fixed-width box on it.
	// Never positioned (0,0) -> default to the upper centre instead of half off the left edge.
	int iCX = Vars::Menu::RecorderDisplay.Value.x;
	int iTop = Vars::Menu::RecorderDisplay.Value.y;
	if (iCX == 0 && iTop == 0)
	{
		iCX = H::Draw.m_nScreenW / 2;
		iTop = int(H::Draw.m_nScreenH * 0.18f);
	}
	const int iLeft = iCX - iW / 2;

	// Panel bg + outline. Upstream has no StyledPanel helper, so compose it from primitives
	// using the same colours the other HUD boxes use.
	const Color_t tBg = Color_t(0, 0, 0, 180);
	const Color_t tOutline = Color_t(60, 60, 60, 255);
	H::Draw.FillRect(iLeft, iTop, iW, iH, tBg);
	H::Draw.LineRect(iLeft, iTop, iW, iH, tOutline);

	int iY = iTop + iPad;
	H::Draw.String(tFont, iCX, iY, tCol, ALIGN_TOP, sTitle.c_str());
	iY += iLineH + iGap;
	H::Draw.String(tFont, iCX, iY, tCol, ALIGN_TOP, sValue.c_str());
}

// Start circles + path line on the world. Draws every saved route's start when idle; the
// active route's path while recording/playing.
void CRecorder::DrawRoutes(CTFPlayer* pLocal)
{
	if (!Vars::Misc::Movement::MovementRecorder.Value || !Vars::Misc::Movement::MovementRecorderShowRoutes.Value)
		return;
	if (!pLocal || !pLocal->IsAlive() || !I::EngineClient->IsInGame())
		return;

	const Color_t tAccent = Vars::Menu::Theme::Accent.Value;
	const Vec3 vOrigin = pLocal->m_vecOrigin();

	const bool bSolid = Vars::Misc::Movement::MovementRecorderRouteSolid.Value;
	const float flRingU = Vars::Misc::Movement::MovementRecorderRouteRadius.Value;
	const int iPoints = std::max(3, Vars::Misc::Movement::MovementRecorderRoutePoints.Value);

	// A flat ring on the floor around a route start, route name centred in the middle.
	auto DrawGroundRing = [&](const Vec3& vCenter, const Color_t& tBase, int iAlpha, const std::string& sName)
	{
		const int iSeg = bSolid ? std::max(iPoints, 24) : iPoints;
		std::vector<Vec3> vScreens;
		vScreens.reserve(iSeg);
		bool bAny = false;
		for (int i = 0; i < iSeg; i++)
		{
			const float flA = 2.f * Math::PI * float(i) / float(iSeg);
			Vec3 vScr;
			if (SDK::W2S(vCenter + Vec3(cosf(flA) * flRingU, sinf(flA) * flRingU, 0.f), vScr))
			{
				vScreens.push_back(vScr);
				bAny = true;
			}
			else
				vScreens.emplace_back(FLT_MAX, FLT_MAX, 0.f); // off-screen / behind camera
		}
		if (!bAny)
			return;

		const Color_t tCol = Color_t(tBase.r, tBase.g, tBase.b, int(tBase.a * iAlpha / 255));
		if (bSolid)
		{
			for (int i = 0; i < iSeg; i++)
			{
				const Vec3& a = vScreens[i];
				const Vec3& b = vScreens[(i + 1) % iSeg];
				if (a.x == FLT_MAX || b.x == FLT_MAX)
					continue;
				H::Draw.Line(int(a.x), int(a.y), int(b.x), int(b.y), tCol);
			}
		}
		else
		{
			const float flDot = H::Draw.Scale(2.5f, Scale_Round);
			for (const Vec3& s : vScreens)
			{
				if (s.x == FLT_MAX)
					continue;
				H::Draw.FillCircle(int(s.x), int(s.y), flDot, 16, tCol);
			}
		}

		// Route name in the middle, small font.
		Vec3 vMid;
		if (!sName.empty() && SDK::W2S(vCenter, vMid))
		{
			const auto& tFont = H::Fonts.GetFont(FONT_ESP);
			H::Draw.String(tFont, int(vMid.x), int(vMid.y), Color_t(255, 255, 255, iAlpha), ALIGN_CENTER, sName.c_str());
		}
	};

	// Active route path while replaying: connect the frames with an accent line + start ring.
	// Only while playing/moving-to-start - the buffer is immutable then; during recording it grows.
	if ((m_bPlaying || m_bMovingToStart) && m_vRecording.size() > 1)
	{
		if (Vars::Misc::Movement::MovementRecorderShowPath.Value)
		{
			Vec3 vPrev;
			bool bHavePrev = false;
			// Sample every few frames so long routes stay cheap (~66 segments/sec is plenty).
			const int iStep = std::max(1, int(m_vRecording.size()) / 600);
			for (int i = 0; i < int(m_vRecording.size()); i += iStep)
			{
				Vec3 vScreen;
				if (SDK::W2S(m_vRecording[i].m_vOrigin, vScreen))
				{
					if (bHavePrev)
						H::Draw.Line(int(vPrev.x), int(vPrev.y), int(vScreen.x), int(vScreen.y), tAccent);
					vPrev = vScreen;
					bHavePrev = true;
				}
				else
					bHavePrev = false;
			}
		}

		const std::string sActive = (m_iPlayRoute >= 0 && m_iPlayRoute < int(m_vRoutes.size())) ? m_vRoutes[m_iPlayRoute].m_sName : std::string();
		DrawGroundRing(m_vRecording.front().m_vOrigin, tAccent, 255, sActive);
		return;
	}

	// Idle: a ground ring at each saved route's start, name in the middle, faded by distance.
	const Color_t tRouteCol = Vars::Misc::Movement::MovementRecorderRouteColor.Value;
	for (const auto& tRoute : m_vRoutes)
	{
		if (tRoute.m_vFrames.empty())
			continue;
		const Vec3 vStart = tRoute.m_vFrames.front().m_vOrigin;
		const float flDist = vOrigin.DistTo(vStart);
		int iAlpha = 255;
		if (flDist > 900.f)
			iAlpha = int(std::clamp((1400.f - flDist) / 500.f, 0.f, 1.f) * 255.f);
		if (iAlpha <= 0)
			continue;
		DrawGroundRing(vStart, tRouteCol, iAlpha, tRoute.m_sName);
	}
}
