#pragma once
#include "../../../../../SDK/SDK.h"

struct PasstimeGoalInfo
{
	CFuncPasstimeGoal* m_pGoal = nullptr;
	int m_iGoalType = CFuncPasstimeGoal::TYPE_HOOP;
	int m_iTeam = TEAM_UNASSIGNED;
	Vector m_vOrigin = {};
	Vector m_vMins = {};
	Vector m_vMaxs = {};
};

class CPasstimeController
{
private:
	std::vector<CFuncPasstimeGoal*> m_vGoals = {};
	CPasstimeBall* m_pBall = nullptr;
	CTFPasstimeLogic* m_pLogic = nullptr;

	int GetGoalTeam(CFuncPasstimeGoal* pGoal) const;
	Vector GetThrowTargetPos(const PasstimeGoalInfo& tGoal, const Vector& vRelativePos);

public:
	void Init();
	void Update();

	CPasstimeBall* GetBall();
	CTFPasstimeLogic* GetLogic() { return m_pLogic; }
	int GetCarrier();
	float GetMaxPassRange() { return m_pLogic ? m_pLogic->m_flMaxPassRange() : FLT_MAX; }

	bool GetGoalInfo(int iScoringTeam, const Vector& vRelativePos, PasstimeGoalInfo& tOut);
	bool GetGoalPos(int iScoringTeam, const Vector& vRelativePos, Vector& vOut);
	bool GetBallPos(Vector& vOut);
	bool IsEndzoneGoal(int iGoalType) const { return iGoalType == CFuncPasstimeGoal::TYPE_ENDZONE; }
	bool IsPointInGoal(const PasstimeGoalInfo& tGoal, const Vector& vPoint) const;
};

ADD_FEATURE(CPasstimeController, PasstimeController);
