#pragma once
#include "../../SDK/SDK.h"
#include <filesystem>
#include <mutex>

Enum(EBStage, Normal = 0, NormalInverted, Random, RandomInverted);

class CMisc
{
private:
	void AutoJump(CTFPlayer* pLocal, CUserCmd* pCmd);
	void JumpBug(CTFPlayer* pLocal, CUserCmd* pCmd);
	void PixelSurf(CTFPlayer* pLocal, CUserCmd* pCmd);
	void RestoreEntityToPredicted();
	// movement suite
	void LongJump(CTFPlayer* pLocal, CUserCmd* pCmd);
	void MiniJump(CTFPlayer* pLocal, CUserCmd* pCmd);
	void MiniJumpPre(CTFPlayer* pLocal, CUserCmd* pCmd);
	void AutoAlign(CTFPlayer* pLocal, CUserCmd* pCmd);
	void TB_RefreshWallCache(CTFPlayer* pLocal);
	void TextureBug(CTFPlayer* pLocal, CUserCmd* pCmd);
	void HeadSurf(CTFPlayer* pLocal, CUserCmd* pCmd);
	void WallClimb(CTFPlayer* pLocal, CUserCmd* pCmd);
	void AirStuck(CTFPlayer* pLocal, CUserCmd* pCmd);
	void PixelFinder(CTFPlayer* pLocal, CUserCmd* pCmd);
	void PixelSurfLine(CTFPlayer* pLocal, CUserCmd* pCmd);
	float SimJumpReach(CTFPlayer* pLocal, CUserCmd* pCmd, int iJumpType, float flAtFlatDist = -1.f);
	struct JumpCatchResult_t
	{
		bool m_bCatch = false;
		bool m_bGraze = false;
		bool m_bGridCatch = false;
		float m_flApexGain = 0.f;
		float m_flAtDistGain = 0.f;
		float m_flGap = 1e9f;
		float m_flGapVz = 0.f;
		float m_flGapFlat = 1e9f;
		float m_flLaunchZ = 0.f;
		int m_iLaunchDelay = -1;
	};
	JumpCatchResult_t SimJumpCatch(CTFPlayer* pLocal, CUserCmd* pCmd, int iJumpType, const Vec3& vTarget, float flAtFlatDist);
	void PixelSurfAssist(CTFPlayer* pLocal, CUserCmd* pCmd);
	struct AssistJumpBox_t
	{
		int m_iJumpType = 0;
		float m_flTargetZ = 0.f;
		float m_flCurTime = 0.f;
	} m_tAssistJumpBox;
	enum EJumpType { JT_NONE = 0, JT_REGULAR, JT_CROUCH, JT_MINI, JT_MINIDUCK, JT_LONG, JT_LONGDUCK, JT_AIRDASH };
	void CorrectMovement(CUserCmd* pCmd, Vec3 vWishAngle, Vec3 vOldAngles);
	bool GetAssistJumpBox(std::string& sTextOut, float& flFadeOut);
	void LoadAssistPoints(const std::string& sMap);
	bool WriteAssistPoints(const std::string& sMap);
	void EdgebugDetect(CTFPlayer* pLocal);
	void AutoEdgebug(CTFPlayer* pLocal, CUserCmd* pCmd);
	void AutoRevJump(CTFPlayer* pLocal, CUserCmd* pCmd);
	void AutoStrafe(CTFPlayer* pLocal, CUserCmd* pCmd);
	void MovementLock(CTFPlayer* pLocal, CUserCmd* pCmd);
	void BreakJump(CTFPlayer* pLocal, CUserCmd* pCmd);
	void BreakShootSound(CTFPlayer* pLocal, CUserCmd* pCmd);
	void AntiAFK(CTFPlayer* pLocal, CUserCmd* pCmd);
	void InstantRespawnMVM(CTFPlayer* pLocal);
	void ExecBuyBot(CTFPlayer* pLocal, CUserCmd* pCmd);
	void ResetBuyBot();
	void BuyBotJoinClass(int iClass);
	bool BuyBotWalkAwayFromStation(CTFPlayer* pLocal, CUserCmd* pCmd, const Vec3& vStation);
	void VoiceCommandSpam(CTFPlayer* pLocal);
	void ChatSpam(CTFPlayer* pLocal);
	void AutoDisguise(CTFPlayer* pLocal);

	void AchievementSpam(CTFPlayer* pLocal);
	void NoiseSpam(CTFPlayer* pLocal);
	void CallVoteSpam(CTFPlayer* pLocal);

	void AutoReport();
	void AutoRetry(CTFPlayer* pLocal);
	void CheatsBypass();
	void WeaponSway();

	void TauntKartControl(CTFPlayer* pLocal, CUserCmd* pCmd);
	void AutoCrouchNavbot(CTFPlayer* pLocal, CUserCmd* pCmd);
	void FastMovement(CTFPlayer* pLocal, CUserCmd* pCmd);

	void AutoPeek(CTFPlayer* pLocal, CUserCmd* pCmd, bool bPost = false);
	void EdgeJump(CTFPlayer* pLocal, CUserCmd* pCmd, bool bPost = false);

	int m_iEdgeBugTicksLeft = 0;
	int m_iEdgeBugTicksTotal = 0;
	int m_iEdgeBugTicksUntilLand = 0;
	int m_iEdgeBugMoveStage = EBStageEnum::Normal;
	float m_flEdgeBugYawDelta = 0.f;
	float m_flEdgeBugStartYaw = 0.f;
	Vector2D m_vEdgeBugMove = {};
	bool m_bEdgeBugCrouch = false;
	bool m_bEdgeBugRepredict = false;
	bool m_bEdgeBug = false;
	bool m_bDuckSpeedActive = false;
	float m_flJumpBugTime = 0.f;
	float m_flJumpBugMissTime = 0.f;
	float m_flEdgeBugTime = 0.f;
	Vec3 m_vEdgeBugPrevVel = {};
	int m_iEdgeBugPrevFlags = 0;
	// pixel surf state
	int m_iPrePredictionFlags = 0; // flags before prediction, captured at the top of RunPre
	Vec3 m_vPrePredictionVelocity = {}; // velocity before prediction (surf ride detection)
	bool m_bShouldPixelSurf = false; // the catcher decided to duck, riding is assumed
	int m_iPixelSurfTicks = 0; // tickcount until which we keep ducking
	bool m_bPixelSurfingNow = false; // actually riding a surf this tick
	bool m_bPrevSurfVel = false; // ride-velocity condition held last tick

	// LongJump data
	int m_iLongJumpSavedTick = 0;
	bool m_bLongJumpDetected = false;
	// MiniJump data
	bool m_bMiniJumpShouldDuck = false;
	bool m_bMiniJumpDetected = false;
	int m_iMiniJumpDuckTick = 0;
	int m_iMiniJumpPendingUntil = 0;
	int m_iMiniJumpLastJumpCmdTick = 0;
	int m_iMiniJumpLandTick = 0;
	bool m_bMiniJumpPrevGround = false;
	// AutoAlign data
	bool m_bWallDetected = false;
	float m_flAutoAlignStartCircle = 0.f;
	// TextureBug / choke data
	float m_flTBCachedForward = -1.f;
	float m_flTBCachedAngleOffset = 0.f;
	float m_flTBCachedWallAngle = -1.f;
	bool m_bTBCachedDuck = false;
	int m_iTBChokeTicksLeft = 0;
	struct WallCache_t { bool valid = false; CGameTrace trace; float foundAngle = 0.f; };
	WallCache_t m_TB_WallCache;
	bool m_bTBLocked = false;
	bool m_bTBArrestLock = false;
	float m_flTBLockedForward = 0.f;
	float m_flTBLockedSide = 0.f;
	bool m_bTBLockedDuck = false;
	int m_iTBSteerAwayTicks = 0;
	int m_iTBPendingCatchTicks = 0;
	float m_flTBPrevVz = 0.f;
	float m_flTBLockedZ = 0.f;
	bool m_bTBMissValid = false;
	bool m_bTBMissSettled = false;
	float m_flTBMissZ = 0.f;
	float m_flTBMissWallYaw = 0.f;
	int m_iTBSweepPhase = 0;
	bool  m_bTBBrushValid = false;
	float m_flTBBrushBottomZ = 0.f;
	float m_flTBBrushHeight = 0.f;
	float m_flTBBrushMeasZ = 0.f;
	float m_flTBBrushMeasYaw = 0.f;
	int   m_iTBHugSign = 0;
	// WallClimb data
	float m_flWallClimbCachedForward = -1.f;
	float m_flWallClimbCachedAngleOffset = 0.f;
	float m_flWallClimbCachedWallAngle = -1.f;
	bool m_bWallClimbCachedDuck = false;
	int m_iWallClimbColdSkip = 0;
	// HeadSurf data
	float m_flHSCachedForward = -1.f;
	float m_flHSCachedAngleOffset = 0.f;
	bool  m_bHSCachedDuck = false;
	bool  m_bHSSurfingNow = false;
	bool  m_bHSPrevSurfVel = false;
	bool  m_bHSLocked = false;
	float m_flHSLockedForward = 0.f;
	float m_flHSLockedSide = 0.f;
	bool  m_bHSLockedDuck = false;
	int   m_iHSColdSkip = 0;
	// AirStuck
	struct AirStuckState_t
	{
		float start_circle = 0.f;
		int   stuck_hold_ticks = 0;
		float stuck_last_yaw = 3.402823466e+38f;
		float stuck_last_move = 3.402823466e+38f;
		float stuck_last_side = 0.f;
		float cached_wall_angle = -1.f;
	};
	void AirStuckCore(CTFPlayer* pLocal, CUserCmd* pCmd, bool bEnabled, bool bSlantedOnly, bool& bHitFlag, AirStuckState_t& tState);
	AirStuckState_t m_AirStuckState{};
	// per-tick hit flags (indicator side can read these later)
	bool m_bTextureBugHit = false;
	bool m_bTBOwnsCmd = false;
	bool m_bHeadSurfHit = false;
	bool m_bWallClimbHit = false;
	bool m_bAirStuckHit = false;
	// Pixel finder / assist points
	struct PixelSurfPoint_t
	{
		Vec3 m_vPos = {};
		std::string m_sMap;
		float m_flSize = 0.f;
		bool m_bActive = true;
		float m_flRadius = 0.f;
		int m_iTypeMask = 127;
	};
	bool m_bPixelFinderHeld = false;
	bool m_bPixelFinderFirst = false;
	Vec3 m_vPixelFinderStart = {};
	Vec3 m_vPixelFinderEnd = {};
	Vec3 m_vPixelFinderWallNormal = {};
	std::vector<PixelSurfPoint_t> m_vPixelSurfPoints = {};
	bool m_bPixelDeleteHeldDraw = false;
	// Pixel surf line
	struct SurfLineSegment_t
	{
		std::vector<Vec3> m_vPoints;
		std::string m_sMap;
	};
	std::vector<SurfLineSegment_t> m_vSurfLineSegments = {};
	std::vector<Vec3> m_vSurfLineCurrent = {};
	bool m_bWasInSurfLine = false;
	int m_iSurfLineGrace = 0;
	bool m_bSurfLineDeleteHeldDraw = false;
	// Pixel surf assist
	std::vector<PixelSurfPoint_t> m_vAssistPoints = {};
	std::string m_sAssistPointsMap = "";
	bool m_bAssistSetHeld = false;
	bool m_bAssistDeleteHeld = false;
	bool m_bPixelSurfAssistActive = false;
	int m_iAssistPointHover = -1; // assist point under the crosshair this frame (-1 = none)
	int m_iAssistPointMenu = -1;  // point whose settings window is open (-1 = closed)
	int m_iAssistJumpType = 0;
	int m_iAssistJumpStartTick = 0;
	Vec3 m_vAssistWishDir = {};
	float m_flAssistWishMag = 0.f;
	int m_iAssistPendingType = 0;
	int m_iAssistPendingUntil = 0;
	std::vector<Vec3> m_vEdgebugPath;
	std::vector<Vec3> m_vEdgebugScratchPath = {}; // reused per attempt, avoids per tick heap churn
	CMoveData m_tEdgebugMoveData = {}; // reused across attempts, the struct is large
	byte* m_pEdgebugBackup = nullptr; // cached prediction backup buffer, grown on demand
	size_t m_uEdgebugBackupSize = 0;

	bool m_bPeekPlaced = false;
	Vec3 m_vPeekReturnPos = {};

	// ported from the lua movement recorder, replays recorded cmds including cheat bind states
	struct RecorderTick_t
	{
		Vec3 m_vAngles = {};
		Vec3 m_vOrigin = {};
		float m_flForwardMove = 0.f;
		float m_flSideMove = 0.f;
		int m_iButtons = 0;
		std::vector<char> m_vBinds = {};
	};
	void Recorder(CTFPlayer* pLocal, CUserCmd* pCmd);
	std::string GetRecordingsFolder();
	void SaveRecording();
	std::vector<RecorderTick_t> m_vRecording = {};
	std::string m_sRecordingName = "";
	size_t uRecorderTick = 0;
	bool m_bRecording = false;
	bool m_bPlaying = false;
	bool m_bRecorderAtPos = false;
	int m_iRecorderSetupTimer = 128;
	// recorder state is touched from both the game thread and the menu (render thread), guard it all
	mutable std::recursive_mutex m_mRecorderMutex;

	//bool bSteamCleared = false;

	std::vector<std::string> m_vChatSpamLines;
	std::vector<std::string> m_vKillSayLines;
	void DoKillSay(int iVictim);
	void EnsureChatUtilsDoc();
	struct AutoReply_t { std::vector<std::string> vTriggers; std::vector<std::string> vReplies; };
	std::vector<AutoReply_t> m_vAutoReplies;
	std::vector<std::string> m_vF1Messages;
	std::vector<std::string> m_vF2Messages;
	Timer m_tChatSpamTimer;
	int m_iCurrentChatSpamIndex = 0;

	bool LoadLines(const char* szFileName, std::vector<std::string>& vLines, const char* szDefaultContent = nullptr);
	std::vector<std::string> ParseTokens(std::string str, char delimiter);

	enum class AchievementSpamState
	{
		IDLE,
		CLEARING,
		WAITING,
		AWARDING
	};

	AchievementSpamState m_eAchievementSpamState = AchievementSpamState::IDLE;
	Timer m_tAchievementSpamTimer;
	Timer m_tAchievementDelayTimer;
	int m_iAchievementSpamID = 0;
	std::string m_sAchievementSpamName = "";
	Timer m_tCallVoteSpamTimer;
	bool m_bAutoBalanceTeamChangePending = false;

	int m_iBuybotStep = 1;
	int m_iBuybotUpgradeSlotStep = 0;
	int m_iBuybotUpgradeIndex = 0;
	float m_flBuybotClock = 0.0f;
	float m_flBuybotStationPathStart = 0.0f;
	float m_flBuybotNavClock = 0.0f;
	float m_flBuybotClassClock = 0.0f;
	float m_flBuybotRetryClock = 0.0f;
	bool m_bBuybotUsingNav = false;
	bool m_bBuybotCashLimitReached = false;
	bool m_bBuybotFinishedUpgrades = false;
	Vec3 m_vBuybotStationTarget = {};

public:
	struct ProfileDumpResult_t
	{
		bool m_bResourceAvailable = false;
		bool m_bFileOpened = false;
		bool m_bSuccess = false;
		size_t m_uCandidateCount = 0;
		size_t m_uSkippedInvalid = 0;
		size_t m_uSkippedComma = 0;
		size_t m_uSkippedSessionDuplicate = 0;
		size_t m_uSkippedFileDuplicate = 0;
		size_t m_uAppendedCount = 0;
		size_t m_uAvatarsSaved = 0;
		size_t m_uAvatarMissed = 0;
		size_t m_uAvatarFailed = 0;
		std::filesystem::path m_outputPath;
		std::filesystem::path m_avatarFolder;
	};

	void RunPre(CTFPlayer* pLocal, CUserCmd* pCmd);
	void RunPost(CTFPlayer* pLocal, CUserCmd* pCmd);
	void Draw(CTFPlayer* pLocal);
	void DrawPixelSurf(CTFPlayer* pLocal); // surface-drawn world overlay, must run from the Paint hook
	bool ConsumeTextureBugChoke();
	void RecorderToggleRecord(std::string sName = "");
	void RecorderTogglePlay();
	void RecorderReset();
	bool SelectRecording(const std::string& sName);
	void RecorderDelete(const std::string& sName);
	std::vector<std::string> GetRecordingNames(const std::string& sFilter = "");
	const std::string& GetCurrentRecordingName() const { return m_sRecordingName; }
	bool IsRecording() const { return m_bRecording; }
	bool IsPlaying() const { return m_bPlaying; }
	size_t GetRecorderTick() const { return uRecorderTick; }
	size_t GetRecorderSize() const { return m_vRecording.size(); }
	bool IsDuckSpeedActive() const { return m_bDuckSpeedActive; }

	void Event(IGameEvent* pEvent, uint32_t uNameHash);
	int AntiBackstab(CTFPlayer* pLocal, CUserCmd* pCmd);
	void AutoFaNJump(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void MicSpam();

	void PingReducer();
	void UnlockAchievements();
	void UnlockItemAchievements();
	void LockAchievements();
	void LockItemAchievements();
	void SetAutoBalanceTeamChangePending(bool bPending) { m_bAutoBalanceTeamChangePending = bPending; }
	void AutoMvmReadyUp();
	void OnBuyBotClassChangeBlocked();
	void OnVoteStart(int iCaller, int iTarget, const std::string& sTarget);
	void OnChatMessage(int iEntIndex, const std::string& sName, const std::string& sMsg);
	std::string ReplaceTags(std::string sMsg, std::string sTarget = "", std::string sInitiator = "", std::string sKiller = "", std::string sVictim = "");
	ProfileDumpResult_t DumpProfiles(bool bAnnounce = true);

	int m_iWishCmdrate = -1;
	//int m_iWishUpdaterate = -1;
	bool m_bAntiAFK = false;
	std::string m_sLastKilledName = "";
};

ADD_FEATURE(CMisc, Misc);
