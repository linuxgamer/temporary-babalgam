#pragma once
#include "../../SDK/SDK.h"

struct AngleHistory_t
{
	Vec3 m_vAngle;
	bool m_bAttacking;
};

struct PlayerInfo
{
	uint32_t m_uAccountID = 0;
	std::string m_sName = "";

	int m_iDetections = 0;

	struct PacketChoking_t
	{
		std::deque<int> m_vChokes = {}; // store last 3 choke counts
		bool m_bInfract = false; // infract the user for choking?

		struct LagCompAbuse_t
		{
			std::deque<int> m_vBurstTicks = {}; // tickcounts of recent multi-cmd bursts
			std::deque<int> m_vDeltaCmds = {}; // delta cmd counts for reference
			bool m_bInfract = false;
		} m_LagComp;
	} m_PacketChoking;

	struct AimFlicking_t
	{
		std::deque<AngleHistory_t> m_vAngles = {}; // store last 3 angles & if damage was dealt
	} m_AimFlicking;
					
	struct DuckSpeed_t
	{
		int m_iStartTick = 0;
	} m_DuckSpeed;

	struct CritTracker_t
	{
		struct WeaponHistory_t
		{
			std::deque<bool> m_vHistory = {};
			int m_iCrits = 0;
		};

		std::unordered_map<int, WeaponHistory_t> m_mWeaponHistory = {};
		bool m_bInfract = false;
	} m_CritTracker;

	struct MovementAbuse_t
	{
		int m_iLastSimTicks = -1;
		Vec3 m_vLastOrigin = {};
		float m_flTickbaseWait = 0.f;
		float m_flSpeedhackWait = 0.f;
		int m_iSpeedhackViolations = 0;
	} m_MovementAbuse;
};

class CCheatDetection
{
private:
	bool ShouldScan();

	bool InvalidPitch(CTFPlayer* pEntity);
	bool IsChoking(CTFPlayer* pEntity);
	bool IsFlicking(CTFPlayer* pEntity);
	bool IsDuckSpeed(CTFPlayer* pEntity);
	bool IsLagCompAbusing(CTFPlayer* pEntity, int iDeltaTicks);
	bool IsCritManipulating(CTFPlayer* pEntity);
	void TrackCritEvent(CTFPlayer* pEntity, CTFWeaponBase* pWeapon, bool bCrit);
	bool IsTickbaseAbusing(CTFPlayer* pEntity);
	bool IsSpeedhacking(CTFPlayer* pEntity);

	void Infract(CTFPlayer* pEntity, const char* sReason);

	std::unordered_map<CTFPlayer*, PlayerInfo> mData = {};

public:
	void Run();

	void ReportChoke(CTFPlayer* pEntity, int iChoke);
	void ReportDamage(IGameEvent* pEvent);
	void Reset();
};

ADD_FEATURE(CCheatDetection, CheatDetection);
