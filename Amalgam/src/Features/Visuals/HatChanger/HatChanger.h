#pragma once
#include "../../../SDK/SDK.h"
#include <unordered_map>
#include <vector>
#include <utility>

class CHatChanger
{
private:
	std::unordered_map<int, int> m_mOverrides;   // original DefIndex → display DefIndex
	std::unordered_map<int, int> m_mUnusuals;    // original DefIndex → unusual effect id
	std::unordered_map<int, int> m_mPaints;      // original DefIndex → paint color (RGB int)
	std::unordered_map<int, int> m_mPaints2;     // original DefIndex → paint color 2 (team paint BLU)
	bool m_bManualHudUnusual = false;
	bool m_bProxyInstalled = false;

	void InstallProxy();

	// Recv proxy hook — intercepts m_iItemDefinitionIndex BEFORE PostDataUpdate
	static RecvVarProxyFn s_fnOriginalProxy;
	static void __cdecl HookedItemDefProxy(const CRecvProxyData* pData, void* pStruct, void* pOut);

	// Standalone hat management
	std::vector<int> m_vStandaloneHats; // def indices drawn manually (no anchor)
	int  PrecacheModel(const char* szPath);

public:
	// Populated every POSTDATAUPDATE_START: {slot, original_def_index}
	std::vector<std::pair<int, int>> m_vLastSeen;

	void ApplyHats();          // called at POSTDATAUPDATE_START
	void RefreshUnusuals(bool bIncludeHudWearables = false); // lightweight attr reapply

	void SetOverride(int nOrigDef, int nNewDef);
	void ClearOverride(int nOrigDef);
	void SetUnusual(int nOrigDef, int nEffectID);
	void ClearUnusual(int nOrigDef);
	void SetPaint(int nOrigDef, int nColor, int nColor2 = 0);
	void ClearPaint(int nOrigDef);
	void ClearAll();

	int  GetOverride(int nOrigDef) const;
	int  GetUnusual(int nOrigDef) const;
	int  GetPaint(int nOrigDef) const;
	int  GetPaint2(int nOrigDef) const;
	bool HasOverride(int nOrigDef) const;
	int  GetAnyUnusual() const;
	void SetManualHudUnusual(bool bEnable) { m_bManualHudUnusual = bEnable; }
	bool GetManualHudUnusual() const { return m_bManualHudUnusual; }
	void ManualHudUnusualTick();
	void ManualHudUnusualOnEntity(CBaseEntity* pEntity);
	void ManualHudUnusualAtPos(const Vec3& vOrigin, const Vec3& vAngles, int nKey = 0);
	void ManualHudUnusualFromPanel();

	const std::unordered_map<int, int>& GetOverrides() const { return m_mOverrides; }

	// Standalone hats — rendered manually each frame, bonemerged to the local
	// player. Requires no equipped cosmetic to anchor on. Drawn from the
	// DrawModelExecute hook using the player's own bone-to-world matrices.
	void WearStandaloneHat(int nDefIndex);
	void RemoveStandaloneHat(int nDefIndex);
	void ClearStandaloneHats();
	bool HasStandaloneHat(int nDefIndex) const;
	const std::vector<int>& GetStandaloneHats() const { return m_vStandaloneHats; }
	// Called from IVModelRender_DrawModelExecute while drawing the local player
	// body. pPlayerBones is the player's bone-to-world array (indexed by the
	// player studiohdr bone index in playerState.m_pStudioHdr).
	void DrawStandaloneHats(const DrawModelState_t& playerState, const ModelRenderInfo_t& playerInfo, matrix3x4* pPlayerBones);

	void Save();
	void Load();
	void Unload();
};

ADD_FEATURE(CHatChanger, HatChanger);
