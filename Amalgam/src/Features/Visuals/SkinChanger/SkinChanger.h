#pragma once
#include <unordered_set>
#include "../../../SDK/SDK.h"

// Attribute indices for CEconItem runtime attributes
namespace SkinAttr
{
	constexpr uint16_t PaintkitProtoDef    = 834;
	constexpr uint16_t PaintkitSeedLo      = 866;
	constexpr uint16_t PaintkitSeedHi      = 867;
	constexpr uint16_t HasTeamColorPaint   = 745;
	constexpr uint16_t SetWear             = 725;
	constexpr uint16_t WeaponAllowInspect  = 731;
	constexpr uint16_t SetParticleStatic   = 370;
	constexpr uint16_t SetParticle         = 134;
	constexpr uint16_t IsFestivized        = 2053;
	constexpr uint16_t IsFestive           = 60000; // synthetic: defindex swap to Festive variant
	constexpr uint16_t IsAustralium        = 2027;
	constexpr uint16_t LootRarity          = 2022;
	constexpr uint16_t ItemStyleOverride   = 542;
	constexpr uint16_t KillstreakTier      = 2025;
	constexpr uint16_t KillstreakSheen     = 2014; // sheen (idle glow on weapon), values 1-7
	constexpr uint16_t KillstreakEffect    = 2013; // killstreaker (eye effect after kills), values 2002-2008
}

// Weapon IDs used for redirection of stock weapons to paintable variants
namespace SkinWeaponID
{
	// Scout
	constexpr int Scattergun          = 13,  ScattergunR       = 200;
	// Soldier
	constexpr int RocketLauncher      = 18,  RocketLauncherR   = 205;
	// Pyro
	constexpr int FlameThrower        = 21,  FlameThrowerR     = 208;
	// Demo
	constexpr int GrenadeLauncher     = 19,  GrenadeLauncherR  = 206;
	constexpr int StickyLauncher      = 20,  StickyLauncherR   = 207;
	// Heavy
	constexpr int Minigun             = 15,  MinigunR          = 202;
	// Engi
	constexpr int Wrench              = 7,   WrenchR           = 197;
	constexpr int EngieShot           = 9,   EngieShotR        = 199;
	constexpr int EngiePistol         = 22,  EngiePistolR      = 209;
	// Medic
	constexpr int MediGun             = 29,  MediGunR          = 211;
	// Sniper
	constexpr int SniperRifle         = 14,  SniperRifleR      = 201;
	constexpr int SMG                 = 16,  SMGR              = 203;
	// Spy
	constexpr int Knife               = 4,   KnifeR            = 194;
	constexpr int Revolver            = 24,  RevolverR         = 210;
	// Shared
	constexpr int Bat                 = 0,   BatR              = 190;
	constexpr int Shovel              = 6,   ShovelR           = 196;
	constexpr int FireAxe             = 2,   FireAxeR          = 192;
	constexpr int Bottle              = 1,   BottleR           = 191;
	constexpr int Bonesaw             = 8,   BonesawR          = 198;
	constexpr int Kukri               = 3,   KukriR            = 193;
	constexpr int SoldierShot         = 10,  SoldierShotR      = 199;
	constexpr int PyroShot            = 12,  PyroShotR         = 199;
	constexpr int HeavyShot           = 11,  HeavyShotR        = 199;
}

// Warpaint (paintkit proto def) name → ID mapping.
// IDs are the paintkit_proto_def_index values from items_game.txt.
// Verify unlisted ones at backpack.tf or via the TF2 schema API.
struct PaintkitEntry
{
	int         id;
	const char* name;
};

const PaintkitEntry* GetPaintkitEntries(int& outCount);

struct UnusualEffectEntry
{
	int         id;
	const char* name;
};

const UnusualEffectEntry* GetUnusualEffectEntries(int& outCount);

struct SkinAttribute_t
{
	uint16_t m_nIndex;
	float    m_flValue;
};

struct SkinConfig_t
{
	std::vector<SkinAttribute_t> m_vAttributes;
	int m_nReskinDefIndex = 0; // 0 = no reskin; non-zero = override visual defindex
};

// Reskin option entry: a defindex+name pair for weapon appearance override
struct ReskinEntry
{
	int         nDefIndex;
	const char* name;
	bool        bFlip    = false;    // true if this weapon renders left-handed
	const char* vmPath   = nullptr;  // viewmodel model path, nullptr = same as base weapon
};

// Returns the curated list of reskin options for a given redirected weapon defindex.
// Empty span means no known reskins for that weapon.
const ReskinEntry* GetReskinOptions(int nRedirectedDefIndex, int& outCount);

class CSkinChanger
{
public:
	static void  RedirectIndex(int& nWeaponIndex);

private:
	std::unordered_map<int, SkinConfig_t> m_mSkins;
	std::unordered_set<int> m_sDirtyWeapons;
	std::unordered_set<int> m_sReskinnedEntities;     // weapon entity indices with active reskins (rebuilt each frame)
	std::unordered_set<int> m_sKnownReskinDefIndices; // all defindexes currently used as reskins (for fast reverse lookup)
	bool m_bInitialized = false;
	bool m_bActiveWeaponReskinned = false;
	std::unordered_map<int, int> m_mLastVMDefIndex = {}; // weapon entindex -> defindex its viewmodel was last deployed for
	// two tick weapon bounce to force the game's own deploy path (rebuilds model, sequences and arms)
	int m_nRedeployStage = 0; // 0 = idle, 1 = select other weapon, 2 = select original back
	int m_nRedeployOther = 0;
	int m_nRedeployTarget = 0;

	void  ApplySkin(CTFWeaponBase* pWeapon);

public:
	void  ApplySkins();
	void  UpdateViewmodels(CTFPlayer* pLocal);
	int   ConsumeRedeploy()
	{
		switch (m_nRedeployStage)
		{
		case 1: m_nRedeployStage = 2; return m_nRedeployOther;
		case 2: m_nRedeployStage = 0; return m_nRedeployTarget;
		default: return 0;
		}
	}

	void  SetAttribute(int nWeaponIndex, uint16_t nAttrIndex, float flValue);
	void  RemoveAttribute(int nWeaponIndex, uint16_t nAttrIndex);
	void  ClearWeapon(int nWeaponIndex);
	bool  HasConfig(int nWeaponIndex) const { RedirectIndex(nWeaponIndex); return m_mSkins.count(nWeaponIndex) > 0; }
	const SkinConfig_t* GetConfig(int nWeaponIndex) const;
	const std::unordered_map<int, SkinConfig_t>& GetAllSkins() const { return m_mSkins; }
	bool HasActiveReskin(int nWeaponEntIdx) const { return m_sReskinnedEntities.count(nWeaponEntIdx) > 0; }
	bool IsActiveWeaponReskinned() const { return m_bActiveWeaponReskinned; }
	int  GetUnusualEffect(int nWeaponIndex) const;

	void  SetReskin(int nWeaponIndex, int nReskinDefIndex);
	void  SetReskinAllMelees(int nReskinDefIndex);
	void  RemoveReskin(int nWeaponIndex);
	void  ClearAllReskins();
	int   GetReskin(int nWeaponIndex) const;
	bool  GetReskinFlip(int nWeaponIndex) const;

	void  Save();
	void  Load();
};

ADD_FEATURE(CSkinChanger, SkinChanger);
