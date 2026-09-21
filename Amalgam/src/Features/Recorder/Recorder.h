#pragma once
#include "../../SDK/SDK.h"
#include <deque>

// Movement recorder: records the outgoing CUserCmd each tick and replays it verbatim.
// Routes are named and persist per map (one file per map + tickrate), so a recorded
// line survives map reloads and restarts.
class CRecorder
{
private:
	// One frame of movement. Viewangles + the three move floats + buttons reproduce the
	// command exactly; origin/velocity are stored so we can draw the start ring, steer to
	// the route start, and gate playback on a matching state.
	struct RecFrame_t
	{
		Vec3 m_vViewAngles = {};
		float m_flForward = 0.f;
		float m_flSide = 0.f;
		float m_flUp = 0.f;
		int m_iButtons = 0;
		Vec3 m_vOrigin = {};
		Vec3 m_vVelocity = {};
	};

	// A named route = one finished recording. Many routes live in a single per-map file.
	struct Route_t
	{
		std::string m_sName;
		std::vector<RecFrame_t> m_vFrames;
	};

	void LoadRoutes(const std::string& sMap);  // load this map's routes file into m_vRoutes
	bool WriteRoutes(const std::string& sMap); // (re)write this map's routes file from m_vRoutes

	// The in-progress / active recording buffer (also holds the route being replayed).
	std::vector<RecFrame_t> m_vRecording = {};
	std::vector<Route_t> m_vRoutes = {};   // all saved routes for the currently-loaded map
	std::string m_sRoutesMap = "";         // which map m_vRoutes was loaded for
	int m_iSelectedRoute = -1;             // menu selection (also the route Play prefers)

	bool m_bRecording = false;
	bool m_bPlaying = false;
	size_t m_iPlaybackIdx = 0;
	int m_iPlayRoute = -1;                 // index into m_vRoutes being replayed (-1 = active buffer)

	// Move-to-start state machine: creep to the EXACT start, optionally smooth the view onto
	// the start angle, then begin playback only once position + velocity match the recording.
	bool m_bMovingToStart = false;
	bool m_bSmoothToStart = false;
	bool m_bWishToStart = false;
	int m_iSettleTicks = 0;          // ticks spent creeping (safety timeout so it can't hard-lock)
	float m_flAlignDist = 0.f;       // live distance to the start (HUD feedback)
	float m_flAlignSpeed = 0.f;      // live horizontal speed (HUD feedback)
	float m_flAlignBest = 0.f;       // closest distance achieved so far
	int m_iStallTicks = 0;           // ticks since the distance last improved - once it stops
	                                 // improving we're at the physical floor, so commit there
	bool m_bRouteUnreliable = false; // route was recorded mid-motion (start speed too high to
	                                 // reproduce by creeping in) - HUD warns to re-record it

	bool m_bRecKeyHeld = false, m_bPlayKeyHeld = false, m_bStopKeyHeld = false;
	bool m_bSaveKeyHeld = false, m_bLoadKeyHeld = false, m_bClearKeyHeld = false;

	// Shadowplay: a rolling buffer of the last N seconds of movement, captured while idle.
	// The shadow-save bind dumps it into m_vRecording so you can keep a moment after it happened.
	std::deque<RecFrame_t> m_dqShadow = {};
	bool m_bShadowSaveHeld = false;

public:
	void Run(CTFPlayer* pLocal, CUserCmd* pCmd);
	void Draw();                     // REC/PLAY text HUD
	void DrawRoutes(CTFPlayer* pLocal); // start circles + path line on the world

	// Route API consumed by the Movement > RECORDER menu tab.
	int RecorderRouteCount() const { return int(m_vRoutes.size()); }
	std::string RecorderRouteName(int i) const { return (i >= 0 && i < int(m_vRoutes.size())) ? m_vRoutes[i].m_sName : ""; }
	int RecorderRouteFrames(int i) const { return (i >= 0 && i < int(m_vRoutes.size())) ? int(m_vRoutes[i].m_vFrames.size()) : 0; }
	float RecorderRouteSeconds(int i) const { return RecorderRouteFrames(i) * TICK_INTERVAL; }
	void RecorderTrimRoute(int i, float flStartSec, float flEndSec); // cut N seconds off a route + persist
	int RecorderSelected() const { return m_iSelectedRoute; }
	void RecorderSelect(int i) { m_iSelectedRoute = (i >= 0 && i < int(m_vRoutes.size())) ? i : -1; }
	void RecorderSaveActiveAs(const std::string& sName); // save m_vRecording as a new route + write file
	void RecorderDeleteRoute(int i);                     // erase a route + rewrite the file
	void RecorderReload();                               // reload the current map's routes file
	bool RecorderIsRecording() const { return m_bRecording; }
	bool RecorderIsPlaying() const { return m_bPlaying || m_bMovingToStart; }
	int RecorderActiveFrames() const { return int(m_vRecording.size()); }
	int RecorderPlaybackIdx() const { return int(m_iPlaybackIdx); }
	std::string RecorderCurrentMap() const;
};

ADD_FEATURE(CRecorder, Recorder);
