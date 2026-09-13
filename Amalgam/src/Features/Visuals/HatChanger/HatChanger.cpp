#include "HatChanger.h"
#include "HatModels.h"
#include "../../Configs/Configs.h"
#include "../../../SDK/SDK.h"
#include "../../../Utils/NetVars/NetVars.h"
#include "../SkinChanger/SkinChanger.h"

#include <fstream>
#include <format>
#include <algorithm>
#include <iterator>

MAKE_SIGNATURE(HatChanger_GetItemSchema_Shared,
	"client.dll",
	"48 83 EC ? E8 ? ? ? ? 48 83 C0 ? 48 83 C4 ? C3 CC CC CC",
	0x0);

MAKE_SIGNATURE(HatChanger_GetAttributeDefinition,
	"client.dll",
	"89 54 24 ? 53 48 83 EC ? 48 8B D9 48 8D 54 24 ? 48 81 C1 ? ? ? ? E8 ? ? ? ? 8B D0 3B 83 ? ? ? ? 73 ? 8B 83 ? ? ? ? 83 F8 ? 74 ? 3B D0 7F ? 48 81 C3 ? ? ? ? 44 8B C2 83 FA ? 74 ? 48 8B 03 8B CA",
	0x0);

MAKE_SIGNATURE(HatChanger_SetRuntimeAttributeValue,
	"client.dll",
	"48 89 5C 24 10 55 56 57 48 8B EC 48 83 EC 50 44",
	0x0);

namespace
{
	constexpr uint16_t kAttrSetParticle = 134;
	constexpr uint16_t kAttrSetParticleStatic = 370;
	constexpr uint16_t kAttrPaintRGB = 142;
	constexpr uint16_t kAttrPaintRGB2 = 261;

	static std::uintptr_t GetAttrListOffset()
	{
		static const std::uintptr_t kOffset = []
		{
			const int n = U::NetVars.GetNetVar("CEconEntity", "m_AttributeList");
			return static_cast<std::uintptr_t>(n > 0 ? n : 3512);
		}();
		return kOffset;
	}

	struct GameCAttributeList_t
	{
		char _pad[8];
		void* m_pMemory;
		int   m_nAllocCount;
		int   m_nGrowSize;
		int   m_nSize;

		void SetAttr(int nIndex, float flValue)
		{
			if (!S::HatChanger_GetItemSchema_Shared()
				|| !S::HatChanger_GetAttributeDefinition()
				|| !S::HatChanger_SetRuntimeAttributeValue())
				return;

			using FnGetSchema = void* (__fastcall*)();
			using FnGetAttr = void* (__fastcall*)(void*, int);
			using FnSetValue = void(__fastcall*)(GameCAttributeList_t*, void*, float);

			void* pSchema = reinterpret_cast<FnGetSchema>(S::HatChanger_GetItemSchema_Shared())();
			if (!pSchema)
				return;

			void* pAttrDef = reinterpret_cast<FnGetAttr>(S::HatChanger_GetAttributeDefinition())(pSchema, nIndex);
			if (!pAttrDef)
				return;

			reinterpret_cast<FnSetValue>(S::HatChanger_SetRuntimeAttributeValue())(this, pAttrDef, flValue);
		}
	};

	static void ApplyWearableUnusual(CEconWearable* pWearable, int nEffectID)
	{
		if (!pWearable)
			return;

		const auto nOff = GetAttrListOffset();
		auto* pList = reinterpret_cast<GameCAttributeList_t*>(reinterpret_cast<uintptr_t>(pWearable) + nOff);
		if (!pList)
			return;

		const float flValue = nEffectID > 0 ? static_cast<float>(nEffectID) : 0.0f;
		pList->SetAttr(kAttrSetParticle, flValue);
		pList->SetAttr(kAttrSetParticleStatic, flValue);
	}

	static const char* ResolveManualParticleNameByEffectID(int nEffectID)
	{
		switch (nEffectID)
		{
		case 6:  return "unusual_confetti_green";
		case 7:  return "unusual_confetti_purple";
		case 8:  return "unusual_halloween_ghosts";
		case 9:  return "superrare_greenenergy";
		case 10: return "superrare_purpleenergy";
		case 13: return "superrare_burning1";
		case 14: return "superrare_burning2";
		case 17: return "superrare_beams1";
		case 29: return "unusual_storm";
		case 30: return "unusual_blizzard";
		case 31: return "unusual_orbit_nutsnbolts";
		case 32: return "unusual_orbit_planets";
		case 38: return "unusual_storm_cloud";
		case 56: return "unusual_zap_yellow";
		case 57: return "unusual_zap_green";
		case 59: return "unusual_aces_high";
		default: break;
		}

		int nCount = 0;
		const UnusualEffectEntry* pFx = GetUnusualEffectEntries(nCount);
		if (!pFx)
			return nullptr;

		const char* szName = nullptr;
		for (int i = 0; i < nCount; i++)
		{
			if (pFx[i].id == nEffectID)
			{
				szName = pFx[i].name;
				break;
			}
		}
		if (!szName || !*szName)
			return nullptr;

		switch (FNV1A::Hash32(szName))
		{
		case FNV1A::Hash32Const("Burning Flames"): return "superrare_burning1";
		case FNV1A::Hash32Const("Scorching Flames"): return "superrare_burning2";
		case FNV1A::Hash32Const("Sunbeams"): return "superrare_beams1";
		case FNV1A::Hash32Const("Purple Energy"): return "superrare_purpleenergy";
		case FNV1A::Hash32Const("Green Energy"): return "superrare_greenenergy";
		case FNV1A::Hash32Const("Purple Confetti"): return "unusual_confetti_purple";
		case FNV1A::Hash32Const("Green Confetti"): return "unusual_confetti_green";
		case FNV1A::Hash32Const("Haunted Ghosts"): return "unusual_halloween_ghosts";
		case FNV1A::Hash32Const("Cloudy Moon"): return "unusual_storm_cloud";
		case FNV1A::Hash32Const("Stormy Storm"): return "unusual_storm";
		case FNV1A::Hash32Const("Blizzardy Storm"): return "unusual_blizzard";
		case FNV1A::Hash32Const("Nuts n' Bolts"): return "unusual_orbit_nutsnbolts";
		case FNV1A::Hash32Const("Orbiting Planets"): return "unusual_orbit_planets";
		case FNV1A::Hash32Const("Aces High"): return "unusual_aces_high";
		case FNV1A::Hash32Const("Kill-a-Watt"): return "unusual_zap_yellow";
		case FNV1A::Hash32Const("Terror-Watt"): return "unusual_zap_green";
		default: return nullptr;
		}
	}

	// Build a bone-space matrix from a bone's local bind pose (pos + quat).
	// Used for hat bones that have no matching player bone to merge onto.
	static void QuatPosToMatrix(const Quaternion& q, const Vector& pos, matrix3x4& m)
	{
		m[0][0] = 1.f - 2.f * q.y * q.y - 2.f * q.z * q.z;
		m[1][0] = 2.f * q.x * q.y + 2.f * q.w * q.z;
		m[2][0] = 2.f * q.x * q.z - 2.f * q.w * q.y;

		m[0][1] = 2.f * q.x * q.y - 2.f * q.w * q.z;
		m[1][1] = 1.f - 2.f * q.x * q.x - 2.f * q.z * q.z;
		m[2][1] = 2.f * q.y * q.z + 2.f * q.w * q.x;

		m[0][2] = 2.f * q.x * q.z + 2.f * q.w * q.y;
		m[1][2] = 2.f * q.y * q.z - 2.f * q.w * q.x;
		m[2][2] = 1.f - 2.f * q.x * q.x - 2.f * q.y * q.y;

		m[0][3] = pos.x;
		m[1][3] = pos.y;
		m[2][3] = pos.z;
	}
}

// TF2 per-class model strings often contain multiple "%s" tokens (same class name
// repeated, e.g. "models/player/items/%s/%s_halloween.mdl"). A single snprintf
// argument leaves extra "%s" unsatisfied; the CRT then reads garbage pointers
// and crashes inside strnlen.
static void SPrintfHatModelPath(char* dst, size_t dstSize, const HatModelEntry_t* pModel, const char* szClass)
{
	if (!dst || dstSize == 0)
		return;
	dst[0] = '\0';
	if (!pModel || !pModel->szModelPath)
		return;

	if (!pModel->bPerClass)
	{
		snprintf(dst, dstSize, "%s", pModel->szModelPath);
		return;
	}

	const char* cls = (szClass && szClass[0]) ? szClass : "scout";
	const char* r = pModel->szModelPath;
	char* w = dst;
	char* const wEnd = dst + dstSize - 1;

	while (*r && w < wEnd)
	{
		if (r[0] == '%' && r[1] == 's')
		{
			for (const char* c = cls; *c && w < wEnd; ++c)
				*w++ = *c;
			r += 2;
		}
		else if (r[0] == '%' && r[1] == '%')
		{
			*w++ = '%';
			r += 2;
		}
		else
			*w++ = *r++;
	}
	*w = '\0';
}

// ─── Static recv proxy data ──────────────────────────────────────────────────
RecvVarProxyFn CHatChanger::s_fnOriginalProxy = nullptr;

// ─── Recv proxy hook ─────────────────────────────────────────────────────────
// Intercepts m_iItemDefinitionIndex writes from the server.  Fires BEFORE
// PostDataUpdate, so if we change the value here the game will resolve the
// model for our overridden def index naturally.
//
// IMPORTANT: Do NOT call virtual functions (IsWearable, GetClientClass, etc.)
// on the entity here — it may be mid-construction with an invalid vtable.
// Instead, just check the incoming def index against our override map.
// TF2 item def indices are globally unique so no weapon/cosmetic collision.
void __cdecl CHatChanger::HookedItemDefProxy(const CRecvProxyData* pData, void* pStruct, void* pOut)
{
	const int nServerDef = pData->m_Value.m_Int;

	auto oit = F::HatChanger.m_mOverrides.find(nServerDef);
	if (oit != F::HatChanger.m_mOverrides.end())
	{
		// Write the overridden value directly into the netvar slot
		*reinterpret_cast<int*>(pOut) = oit->second;
		return; // skip original proxy — we already wrote the value
	}

	// No override: call original proxy
	if (s_fnOriginalProxy)
		s_fnOriginalProxy(pData, pStruct, pOut);
}

// ─── Install proxy hook ──────────────────────────────────────────────────────
void CHatChanger::InstallProxy()
{
	if (m_bProxyInstalled)
		return;

	RecvProp* pProp = U::NetVars.GetNetProp("CEconEntity", "m_iItemDefinitionIndex");
	if (!pProp)
	{
		SDK::Output("HatChanger", "FATAL: could not find m_iItemDefinitionIndex RecvProp",
			{ 255, 50, 50, 255 }, OUTPUT_DEBUG);
		return;
	}

	s_fnOriginalProxy = pProp->GetProxyFn();
	pProp->SetProxyFn(HookedItemDefProxy);
	m_bProxyInstalled = true;

	SDK::Output("HatChanger",
		std::format("recv proxy hooked (orig={:#x})",
			reinterpret_cast<uintptr_t>(s_fnOriginalProxy)).c_str(),
		{ 100, 255, 100, 255 }, OUTPUT_DEBUG);
}

// ─── ApplyHats ───────────────────────────────────────────────────────────────
// Called at POSTDATAUPDATE_START.  The recv proxy already changed the def index
// at the network level.  This function:
//   1. Lazy-init: install the proxy + load config on first call.
//   2. Build m_vLastSeen for the menu UI.
//   3. Swap m_nModelIndex so the visual model matches the overridden def.

void CHatChanger::ApplyHats()
{
	// Lazy init
	if (!m_bProxyInstalled)
	{
		InstallProxy();
		Load();
	}

	static int   s_nCallCount = 0;
	static float s_flNextLog  = 0.f;
	++s_nCallCount;
	const float flNow = SDK::PlatFloatTime();
	const bool bLog = (s_nCallCount <= 60) || (flNow >= s_flNextLog);
	if (bLog)
		s_flNextLog = flNow + 1.0f;

	const int nLocalIdx = I::EngineClient->GetLocalPlayer();
	if (nLocalIdx <= 0)
	{
		m_vLastSeen.clear();
		return;
	}

	auto pLocalEntity = I::ClientEntityList->GetClientEntity(nLocalIdx);
	if (!pLocalEntity)
	{
		m_vLastSeen.clear();
		return;
	}

	auto pLocal = pLocalEntity->As<CTFPlayer>();
	const int nClassIdx = pLocal ? pLocal->m_iClass() : 0;
	const char* szClassName = SDK::GetClassByIndex(nClassIdx, true);

	m_vLastSeen.clear();

	const int nHighest = I::ClientEntityList->GetHighestEntityIndex();
	int nSlot = 0;

	for (int n = 1; n <= nHighest; n++)
	{
		auto pClientEnt = I::ClientEntityList->GetClientEntity(n);
		auto pEntity = pClientEnt ? pClientEnt->As<CBaseEntity>() : nullptr;
		if (!pEntity)
			continue;
		if (!pEntity->IsWearable())
			continue;

		const int nOwnerIdx = pEntity->m_hOwnerEntity().GetEntryIndex();
		if (nOwnerIdx != nLocalIdx)
			continue;

		auto pWearable = pEntity->As<CEconWearable>();
		if (!pWearable)
			continue;

		// The recv proxy already changed m_iItemDefinitionIndex to the
		// overridden value.  We need to figure out the ORIGINAL def index
		// to show in the UI.  The current def is already the override target.
		const int nCurDef = pWearable->m_iItemDefinitionIndex();

		// Reverse-lookup: if nCurDef is a VALUE in m_mOverrides, the
		// original is the corresponding key.
		int nOrigDef = nCurDef;
		for (const auto& [nKey, nVal] : m_mOverrides)
		{
			if (nVal == nCurDef)
			{
				nOrigDef = nKey;
				break;
			}
		}

		m_vLastSeen.emplace_back(nSlot++, nOrigDef);

		// Redefine/override mode only: keep real wearables visible.
		pWearable->m_fEffects() &= ~EF_NODRAW;
		ApplyWearableUnusual(pWearable, GetUnusual(nOrigDef));

		// Check if this wearable has an active override
		auto oit = m_mOverrides.find(nOrigDef);
		if (oit == m_mOverrides.end())
			continue;

		const int nTargetDef = oit->second;

		// ── Model swap ─────────────────────────────────────────────────
		const HatModelEntry_t* pModel = FindHatModel(nTargetDef);
		if (!pModel)
		{
			if (bLog)
				SDK::Output("HatChanger",
					std::format("  no model for target def {}", nTargetDef).c_str(),
					{ 255, 200, 50, 255 }, OUTPUT_DEBUG);
			continue;
		}

		// Build resolved model path (multi-%s safe)
		char szResolvedPath[260];
		SPrintfHatModelPath(szResolvedPath, sizeof(szResolvedPath), pModel, szClassName);

		int nNewModelIdx = PrecacheModel(szResolvedPath);

		if (nNewModelIdx > 0)
		{
			pWearable->m_nModelIndex() = nNewModelIdx;
		}
		else if (bLog)
		{
			SDK::Output("HatChanger",
				std::format("  GetModelIndex('{}') returned {} (precache failed?)",
					szResolvedPath, nNewModelIdx).c_str(),
				{ 255, 100, 50, 255 }, OUTPUT_DEBUG);
		}
	}
}

void CHatChanger::RefreshUnusuals(bool bIncludeHudWearables)
{
	const int nLocalIdx = I::EngineClient->GetLocalPlayer();
	if (nLocalIdx <= 0)
		return;

	const int nHighest = I::ClientEntityList->GetHighestEntityIndex();
	for (int n = 1; n <= nHighest; n++)
	{
		auto pClientEnt = I::ClientEntityList->GetClientEntity(n);
		auto pEntity = pClientEnt ? pClientEnt->As<CBaseEntity>() : nullptr;
		if (!pEntity || !pEntity->IsWearable())
			continue;
		const int nOwnerIdx = pEntity->m_hOwnerEntity().GetEntryIndex();
		if (!bIncludeHudWearables && nOwnerIdx != nLocalIdx)
			continue;

		auto pWearable = pEntity->As<CEconWearable>();
		if (!pWearable)
			continue;

		// Resolve original def from active override map.
		const int nCurDef = pWearable->m_iItemDefinitionIndex();
		int nOrigDef = nCurDef;
		for (const auto& [nKey, nVal] : m_mOverrides)
		{
			if (nVal == nCurDef)
			{
				nOrigDef = nKey;
				break;
			}
		}

		const int nFx = GetUnusual(nOrigDef);
		const int nPaint = GetPaint(nOrigDef);
		const int nPaint2 = GetPaint2(nOrigDef);
		if (bIncludeHudWearables)
		{
			// HUD pass: avoid touching unrelated wearables.
			if (nFx <= 0 && nPaint <= 0)
				continue;
		}
		ApplyWearableUnusual(pWearable, nFx);

		// Apply paint colors
		if (nPaint > 0 || nPaint2 > 0)
		{
			const auto nOff = GetAttrListOffset();
			auto* pList = reinterpret_cast<GameCAttributeList_t*>(reinterpret_cast<uintptr_t>(pWearable) + nOff);
			if (pList)
			{
				if (nPaint > 0)
					pList->SetAttr(kAttrPaintRGB, static_cast<float>(nPaint));
				if (nPaint2 > 0)
					pList->SetAttr(kAttrPaintRGB2, static_cast<float>(nPaint2));
			}
		}
	}
}

// ─── Override management ──────────────────────────────────────────────────────

void CHatChanger::SetOverride(int nOrigDef, int nNewDef)
{
	m_mOverrides[nOrigDef] = nNewDef;
	I::ClientState->ForceFullUpdate();
}

void CHatChanger::ClearOverride(int nOrigDef)
{
	m_mOverrides.erase(nOrigDef);
	I::ClientState->ForceFullUpdate();
}

void CHatChanger::SetUnusual(int nOrigDef, int nEffectID)
{
	if (nEffectID > 0)
		m_mUnusuals[nOrigDef] = nEffectID;
	else
		m_mUnusuals.erase(nOrigDef);

	SDK::Output("HatChanger",
		std::format("SetUnusual: orig_def={} effect_id={}", nOrigDef, nEffectID).c_str(),
		{ 200, 230, 120, 255 }, OUTPUT_DEBUG);
	I::ClientState->ForceFullUpdate();
}

void CHatChanger::ClearUnusual(int nOrigDef)
{
	m_mUnusuals.erase(nOrigDef);
	SDK::Output("HatChanger",
		std::format("ClearUnusual: orig_def={}", nOrigDef).c_str(),
		{ 200, 230, 120, 255 }, OUTPUT_DEBUG);
	I::ClientState->ForceFullUpdate();
}

void CHatChanger::ClearAll()
{
	m_mOverrides.clear();
	m_mUnusuals.clear();
	m_mPaints.clear();
	m_mPaints2.clear();
	m_vStandaloneHats.clear();
	I::ClientState->ForceFullUpdate();
}

int CHatChanger::GetOverride(int nOrigDef) const
{
	auto it = m_mOverrides.find(nOrigDef);
	return (it != m_mOverrides.end()) ? it->second : -1;
}

int CHatChanger::GetUnusual(int nOrigDef) const
{
	auto it = m_mUnusuals.find(nOrigDef);
	return (it != m_mUnusuals.end()) ? it->second : 0;
}

void CHatChanger::SetPaint(int nOrigDef, int nColor, int nColor2)
{
	if (nColor > 0)
		m_mPaints[nOrigDef] = nColor;
	else
		m_mPaints.erase(nOrigDef);

	if (nColor2 > 0)
		m_mPaints2[nOrigDef] = nColor2;
	else
		m_mPaints2.erase(nOrigDef);

	SDK::Output("HatChanger",
		std::format("SetPaint: orig_def={} color={} color2={}", nOrigDef, nColor, nColor2).c_str(),
		{ 200, 230, 120, 255 }, OUTPUT_DEBUG);
	RefreshUnusuals();
}

void CHatChanger::ClearPaint(int nOrigDef)
{
	m_mPaints.erase(nOrigDef);
	m_mPaints2.erase(nOrigDef);
	SDK::Output("HatChanger",
		std::format("ClearPaint: orig_def={}", nOrigDef).c_str(),
		{ 200, 230, 120, 255 }, OUTPUT_DEBUG);
	RefreshUnusuals();
}

int CHatChanger::GetPaint(int nOrigDef) const
{
	auto it = m_mPaints.find(nOrigDef);
	return (it != m_mPaints.end()) ? it->second : 0;
}

int CHatChanger::GetPaint2(int nOrigDef) const
{
	auto it = m_mPaints2.find(nOrigDef);
	return (it != m_mPaints2.end()) ? it->second : 0;
}

int CHatChanger::GetAnyUnusual() const
{
	for (const auto& [nOrig, nFx] : m_mUnusuals)
	{
		if (nFx > 0)
			return nFx;
	}
	return 0;
}

void CHatChanger::ManualHudUnusualTick()
{
	if (!m_bManualHudUnusual)
		return;

	const int nFx = GetAnyUnusual();
	if (nFx <= 0)
		return;

	const char* pszParticle = ResolveManualParticleNameByEffectID(nFx);
	if (!pszParticle)
		return;

	const int nLocalIdx = I::EngineClient->GetLocalPlayer();
	if (nLocalIdx <= 0)
		return;
	auto pLocalEnt = I::ClientEntityList->GetClientEntity(nLocalIdx);
	auto pLocal = pLocalEnt ? pLocalEnt->As<CBaseEntity>() : nullptr;
	if (!pLocal)
		return;

	static float s_flNextEmit = 0.0f;
	const float flNow = SDK::PlatFloatTime();
	if (flNow < s_flNextEmit)
		return;
	s_flNextEmit = flNow + 0.5f;

	H::Particles.DispatchParticleEffect(pszParticle, pLocal->GetAbsOrigin(), pLocal->GetAbsAngles(), pLocal);
}

void CHatChanger::ManualHudUnusualOnEntity(CBaseEntity* pEntity)
{
	if (!m_bManualHudUnusual || !pEntity)
		return;

	const int nFx = GetAnyUnusual();
	if (nFx <= 0)
		return;

	const char* pszParticle = ResolveManualParticleNameByEffectID(nFx);
	if (!pszParticle)
		return;

	static std::unordered_map<int, float> s_mNextEmit;
	const int nEntIdx = pEntity->entindex();
	const float flNow = SDK::PlatFloatTime();
	if (s_mNextEmit[nEntIdx] > flNow)
		return;
	s_mNextEmit[nEntIdx] = flNow + 0.4f;

	const int nPS = H::Particles.GetParticleSystemIndex(pszParticle);
	if (nPS <= 0)
		return;

	H::Particles.DispatchParticleEffect(pszParticle, pEntity->GetAbsOrigin(), pEntity->GetAbsAngles(), pEntity);
}

void CHatChanger::ManualHudUnusualAtPos(const Vec3& vOrigin, const Vec3& vAngles, int nKey)
{
	if (!m_bManualHudUnusual)
		return;

	const int nFx = GetAnyUnusual();
	if (nFx <= 0)
		return;

	const char* pszParticle = ResolveManualParticleNameByEffectID(nFx);
	if (!pszParticle)
		return;

	static std::unordered_map<int, float> s_mNextEmit;
	const int nThrottleKey = nKey > 0 ? nKey : 1;
	const float flNow = SDK::PlatFloatTime();
	if (s_mNextEmit[nThrottleKey] > flNow)
		return;
	s_mNextEmit[nThrottleKey] = flNow + 0.35f;

	const int nPS = H::Particles.GetParticleSystemIndex(pszParticle);
	if (nPS <= 0)
		return;

	// HUD fallback: dispatch at the panel model position rather than entity-attach.
	// Some HUD render paths suppress attached particle children.
	H::Particles.DispatchParticleEffect(pszParticle, vOrigin, vAngles, nullptr);
}

void CHatChanger::ManualHudUnusualFromPanel()
{
	if (!m_bManualHudUnusual)
		return;

	const int nLocalIdx = I::EngineClient->GetLocalPlayer();
	if (nLocalIdx <= 0)
		return;
	auto pLocalClient = I::ClientEntityList->GetClientEntity(nLocalIdx);
	auto pLocal = pLocalClient ? pLocalClient->As<CBaseEntity>() : nullptr;
	if (!pLocal)
		return;

	// HUD preview can use a separate player entity. Prefer non-local CTFPlayer entities.
	CBaseEntity* pBest = nullptr;
	const int nHighest = I::ClientEntityList->GetHighestEntityIndex();
	for (int n = 1; n <= nHighest; n++)
	{
		if (n == nLocalIdx)
			continue;
		auto pClientEnt = I::ClientEntityList->GetClientEntity(n);
		if (!pClientEnt || pClientEnt->GetClassID() != ETFClassID::CTFPlayer)
			continue;
		auto pEnt = pClientEnt->As<CBaseEntity>();
		if (!pEnt)
			continue;
		pBest = pEnt;
		break;
	}

	if (pBest)
	{
		ManualHudUnusualOnEntity(pBest);
		return;
	}

	// Last-resort: local origin with upward bias.
	Vec3 vOrigin = pLocal->GetAbsOrigin();
	Vec3 vAngles = pLocal->GetAbsAngles();
	vOrigin.z += 72.0f;
	ManualHudUnusualAtPos(vOrigin, vAngles, 424242);
}

bool CHatChanger::HasOverride(int nOrigDef) const
{
	return m_mOverrides.count(nOrigDef) > 0;
}

// ─── Precache ─────────────────────────────────────────────────────────────────

int CHatChanger::PrecacheModel(const char* szPath)
{
	int nIdx = I::ModelInfoClient->GetModelIndex(szPath);
	if (nIdx > 0)
		return nIdx;

	if (!I::NetworkStringTableClient)
		return -1;

	auto pTable = I::NetworkStringTableClient->FindTable("modelprecache");
	if (!pTable)
		return -1;

	pTable->AddString(false, szPath);
	return I::ModelInfoClient->GetModelIndex(szPath);
}

// ─── Standalone hats ──────────────────────────────────────────────────────────
// Render hats with no anchor wearable.  Unlike the override path, this does not
// create an entity or touch any existing wearable; the model is drawn manually
// each frame, bonemerged to the local player using the player's own bone matrices
// (handed to us by the DrawModelExecute hook).  No entity lifetime = no crashes.

void CHatChanger::WearStandaloneHat(int nDefIndex)
{
	if (nDefIndex <= 0)
		return;
	if (HasStandaloneHat(nDefIndex))
		return;
	if (!FindHatModel(nDefIndex))
	{
		SDK::Output("HatChanger",
			std::format("WearStandaloneHat: no model entry for def {}", nDefIndex).c_str(),
			{ 255, 180, 120, 255 }, OUTPUT_DEBUG);
		return;
	}
	m_vStandaloneHats.push_back(nDefIndex);
	SDK::Output("HatChanger",
		std::format("WearStandaloneHat: added def {} (count={})", nDefIndex, m_vStandaloneHats.size()).c_str(),
		{ 120, 255, 180, 255 }, OUTPUT_DEBUG);
}

void CHatChanger::RemoveStandaloneHat(int nDefIndex)
{
	std::erase(m_vStandaloneHats, nDefIndex);
}

void CHatChanger::ClearStandaloneHats()
{
	m_vStandaloneHats.clear();
}

bool CHatChanger::HasStandaloneHat(int nDefIndex) const
{
	return std::find(m_vStandaloneHats.begin(), m_vStandaloneHats.end(), nDefIndex) != m_vStandaloneHats.end();
}

void CHatChanger::DrawStandaloneHats(const DrawModelState_t& playerState, const ModelRenderInfo_t& playerInfo, matrix3x4* pPlayerBones)
{
	static float s_flNextLog = 0.f;
	const float flLogNow = SDK::PlatFloatTime();
	const bool bLog = flLogNow >= s_flNextLog;
	if (bLog)
		s_flNextLog = flLogNow + 1.0f;

	if (m_vStandaloneHats.empty() || !pPlayerBones)
	{
		if (bLog && !m_vStandaloneHats.empty())
			SDK::Output("StandaloneHat", "no pPlayerBones", { 255, 100, 100, 255 }, OUTPUT_DEBUG);
		return;
	}

	studiohdr_t* pPlayerHdr = playerState.m_pStudioHdr;
	if (!pPlayerHdr || pPlayerHdr->numbones <= 0)
	{
		if (bLog) SDK::Output("StandaloneHat", "no player studiohdr/bones", { 255, 100, 100, 255 }, OUTPUT_DEBUG);
		return;
	}

	const int nLocalIdx = I::EngineClient->GetLocalPlayer();
	if (nLocalIdx <= 0)
	{
		if (bLog) SDK::Output("StandaloneHat", "no local player idx", { 255, 100, 100, 255 }, OUTPUT_DEBUG);
		return;
	}
	auto pLocalEnt = I::ClientEntityList->GetClientEntity(nLocalIdx);
	auto pLocal = pLocalEnt ? pLocalEnt->As<CTFPlayer>() : nullptr;
	if (!pLocal)
	{
		if (bLog) SDK::Output("StandaloneHat", "no local CTFPlayer", { 255, 100, 100, 255 }, OUTPUT_DEBUG);
		return;
	}

	const int nClass = pLocal->m_iClass();
	const char* szClassName = SDK::GetClassByIndex(nClass, true);
	if (!szClassName || !*szClassName)
	{
		if (bLog)
			SDK::Output("StandaloneHat",
				std::format("no class name for class={}", nClass).c_str(),
				{ 255, 100, 100, 255 }, OUTPUT_DEBUG);
		return;
	}

	if (bLog)
		SDK::Output("StandaloneHat",
			std::format("attempting draw: hats={} class='{}' playerbones={}",
				m_vStandaloneHats.size(), szClassName, pPlayerHdr->numbones).c_str(),
			{ 180, 200, 255, 255 }, OUTPUT_DEBUG);

	// Map player bone name → index so hat bones can merge by name.
	std::unordered_map<uint32_t, int> mPlayerBones;
	mPlayerBones.reserve(pPlayerHdr->numbones);
	for (int i = 0; i < pPlayerHdr->numbones; i++)
	{
		const char* szName = pPlayerHdr->pBone(i)->pszName();
		if (szName)
			mPlayerBones[FNV1A::Hash32(szName)] = i;
	}

	auto pRenderable = pLocalEnt->GetClientRenderable();

	for (int nDefIndex : m_vStandaloneHats)
	{
		const HatModelEntry_t* pModel = FindHatModel(nDefIndex);
		if (!pModel)
		{
			if (bLog) SDK::Output("StandaloneHat", std::format("def {}: no HatModels entry", nDefIndex).c_str(), { 255, 180, 100, 255 }, OUTPUT_DEBUG);
			continue;
		}

		char szPath[260];
		SPrintfHatModelPath(szPath, sizeof(szPath), pModel, szClassName);
		const int nModelIdx = PrecacheModel(szPath);
		if (nModelIdx <= 0)
		{
			if (bLog) SDK::Output("StandaloneHat", std::format("def {}: precache failed for '{}' (idx={})", nDefIndex, szPath, nModelIdx).c_str(), { 255, 180, 100, 255 }, OUTPUT_DEBUG);
			continue;
		}

		const model_t* pHatModel = I::ModelInfoClient->GetModel(nModelIdx);
		if (!pHatModel)
		{
			if (bLog) SDK::Output("StandaloneHat", std::format("def {}: GetModel({}) null", nDefIndex, nModelIdx).c_str(), { 255, 180, 100, 255 }, OUTPUT_DEBUG);
			continue;
		}
		studiohdr_t* pHatHdr = I::ModelInfoClient->GetStudiomodel(pHatModel);
		if (!pHatHdr || pHatHdr->numbones <= 0)
		{
			if (bLog) SDK::Output("StandaloneHat", std::format("def {}: no hat studiohdr/bones", nDefIndex).c_str(), { 255, 180, 100, 255 }, OUTPUT_DEBUG);
			continue;
		}

		const MDLHandle_t hHat = I::ModelInfoClient->GetCacheHandle(pHatModel);
		studiohwdata_t* pHWData = (hHat != MDLHANDLE_INVALID && I::MDLCache) ? I::MDLCache->GetHardwareData(hHat) : nullptr;
		if (!pHWData)
		{
			if (bLog) SDK::Output("StandaloneHat", std::format("def {}: no hardware data (handle valid={})", nDefIndex, hHat != MDLHANDLE_INVALID).c_str(), { 255, 180, 100, 255 }, OUTPUT_DEBUG);
			continue;
		}

		if (bLog) SDK::Output("StandaloneHat", std::format("def {}: drawing '{}' ({} bones)", nDefIndex, szPath, pHatHdr->numbones).c_str(), { 120, 255, 180, 255 }, OUTPUT_DEBUG);

		// Bonemerge: derive each hat bone's world matrix from the player skeleton.
		matrix3x4 aHatBones[MAXSTUDIOBONES];
		const int nBones = pHatHdr->numbones < MAXSTUDIOBONES ? pHatHdr->numbones : MAXSTUDIOBONES;
		for (int i = 0; i < nBones; i++)
		{
			mstudiobone_t* pBone = pHatHdr->pBone(i);
			const char* szName = pBone ? pBone->pszName() : nullptr;
			auto it = szName ? mPlayerBones.find(FNV1A::Hash32(szName)) : mPlayerBones.end();

			if (it != mPlayerBones.end() && it->second < pPlayerHdr->numbones)
				Math::MatrixCopy(pPlayerBones[it->second], aHatBones[i]); // merged bone follows the player
			else if (pBone && pBone->parent >= 0 && pBone->parent < i)
			{
				// Decorative sub-bone: place relative to its (already-computed) parent.
				matrix3x4 mLocal;
				QuatPosToMatrix(pBone->quat, pBone->pos, mLocal);
				Math::ConcatTransforms(aHatBones[pBone->parent], mLocal, aHatBones[i]);
			}
			else
				Math::MatrixCopy(pPlayerBones[0], aHatBones[i]); // unmatched root: pin to player root
		}

		DrawModelState_t state{};
		state.m_pStudioHdr = pHatHdr;
		state.m_pStudioHWData = pHWData;
		state.m_pRenderable = pRenderable;
		state.m_pModelToWorld = nullptr;
		state.m_decals = STUDIORENDER_DECAL_INVALID;
		state.m_drawFlags = STUDIO_RENDER;
		state.m_lod = 0;

		ModelRenderInfo_t info{};
		info.origin = playerInfo.origin;
		info.angles = playerInfo.angles;
		info.pRenderable = pRenderable;
		info.pModel = pHatModel;
		info.pModelToWorld = nullptr;
		info.pLightingOffset = nullptr;
		info.pLightingOrigin = nullptr;
		info.flags = STUDIO_RENDER;
		info.entity_index = nLocalIdx;
		info.skin = 0;
		info.body = 0;
		info.hitboxset = 0;
		info.instance = MODEL_INSTANCE_INVALID;

		// Reuse the engine's lighting state from the player draw we just followed.
		static auto pHook = U::Hooks.m_mHooks["IVModelRender_DrawModelExecute"];
		if (pHook)
			pHook->As<DrawModelExecuteFn>()(I::ModelRender, state, info, aHatBones);
	}
}

void CHatChanger::Unload()
{
	// Restore original recv proxy
	if (m_bProxyInstalled && s_fnOriginalProxy)
	{
		RecvProp* pProp = U::NetVars.GetNetProp("CEconEntity", "m_iItemDefinitionIndex");
		if (pProp)
			pProp->SetProxyFn(s_fnOriginalProxy);
		s_fnOriginalProxy = nullptr;
		m_bProxyInstalled = false;
	}

	// Restore EF_NODRAW on real wearables that were hidden while overrides were active
	const int nLocalIdx = I::EngineClient->GetLocalPlayer();
	if (nLocalIdx > 0)
	{
		const int nHighest = I::ClientEntityList->GetHighestEntityIndex();
		for (int n = 1; n <= nHighest; n++)
		{
			auto pClientEnt = I::ClientEntityList->GetClientEntity(n);
			auto pEntity = pClientEnt ? pClientEnt->As<CBaseEntity>() : nullptr;
			if (!pEntity || !pEntity->IsWearable())
				continue;
			if (pEntity->m_hOwnerEntity().GetEntryIndex() != nLocalIdx)
				continue;
			pEntity->m_fEffects() &= ~EF_NODRAW;
		}
	}

	// Clear state
	m_mOverrides.clear();
	m_mUnusuals.clear();
	m_vStandaloneHats.clear();
	m_vLastSeen.clear();
}

// ─── Persistence ──────────────────────────────────────────────────────────────

void CHatChanger::Save()
{
	const std::string sPath = F::Configs.m_sConfigPath + "hats.json";
	std::ofstream out(sPath);
	if (!out.is_open())
		return;

	out << "{\n";
	bool bFirst = true;
	for (auto& [nOrig, nNew] : m_mOverrides)
	{
		if (!bFirst)
			out << ",\n";
		out << "  \"" << nOrig << "\": " << nNew;
		bFirst = false;
	}
	out << "\n}\n";

	const std::string sUnusualPath = F::Configs.m_sConfigPath + "hats_unusual.json";
	std::ofstream outUnusual(sUnusualPath);
	if (!outUnusual.is_open())
		return;

	outUnusual << "{\n";
	bool bFirstUnusual = true;
	for (auto& [nOrig, nFx] : m_mUnusuals)
	{
		if (!bFirstUnusual)
			outUnusual << ",\n";
		outUnusual << "  \"" << nOrig << "\": " << nFx;
		bFirstUnusual = false;
	}
	outUnusual << "\n}\n";

	const std::string sPaintPath = F::Configs.m_sConfigPath + "hats_paint.json";
	std::ofstream outPaint(sPaintPath);
	if (outPaint.is_open())
	{
		outPaint << "{\n";
		bool bFirstPaint = true;
		for (auto& [nOrig, nColor] : m_mPaints)
		{
			if (!bFirstPaint)
				outPaint << ",\n";
			int nColor2 = GetPaint2(nOrig);
			outPaint << "  \"" << nOrig << "\": [" << nColor << ", " << nColor2 << "]";
			bFirstPaint = false;
		}
		outPaint << "\n}\n";
	}

	const std::string sStandalonePath = F::Configs.m_sConfigPath + "hats_standalone.json";
	std::ofstream outStandalone(sStandalonePath);
	if (outStandalone.is_open())
	{
		outStandalone << "[";
		for (size_t i = 0; i < m_vStandaloneHats.size(); i++)
		{
			if (i)
				outStandalone << ", ";
			outStandalone << m_vStandaloneHats[i];
		}
		outStandalone << "]\n";
	}
}

void CHatChanger::Load()
{
	const std::string sPath = F::Configs.m_sConfigPath + "hats.json";
	std::ifstream in(sPath);
	m_mOverrides.clear();
	m_mUnusuals.clear();
	m_mPaints.clear();
	m_mPaints2.clear();
	m_vStandaloneHats.clear();
	std::string sLine;
	if (in.is_open())
	{
		while (std::getline(in, sLine))
		{
			const auto nColon = sLine.find(':');
			if (nColon == std::string::npos)
				continue;

			const auto nQ1 = sLine.find('"');
			const auto nQ2 = sLine.find('"', nQ1 + 1);
			if (nQ1 == std::string::npos || nQ2 == std::string::npos || nQ2 <= nQ1)
				continue;

			std::string sKey = sLine.substr(nQ1 + 1, nQ2 - nQ1 - 1);
			std::string sVal = sLine.substr(nColon + 1);

			while (!sVal.empty() && (sVal.back() == ',' || sVal.back() == ' '
				|| sVal.back() == '\r' || sVal.back() == '\n'))
				sVal.pop_back();
			while (!sVal.empty() && sVal.front() == ' ')
				sVal.erase(sVal.begin());

			try { m_mOverrides[std::stoi(sKey)] = std::stoi(sVal); }
			catch (...) {}
		}
	}

	const std::string sUnusualPath = F::Configs.m_sConfigPath + "hats_unusual.json";
	std::ifstream inUnusual(sUnusualPath);
	if (inUnusual.is_open())
	{
		while (std::getline(inUnusual, sLine))
		{
			const auto nColon = sLine.find(':');
			if (nColon == std::string::npos)
				continue;

			const auto nQ1 = sLine.find('"');
			const auto nQ2 = sLine.find('"', nQ1 + 1);
			if (nQ1 == std::string::npos || nQ2 == std::string::npos || nQ2 <= nQ1)
				continue;

			std::string sKey = sLine.substr(nQ1 + 1, nQ2 - nQ1 - 1);
			std::string sVal = sLine.substr(nColon + 1);

			while (!sVal.empty() && (sVal.back() == ',' || sVal.back() == ' '
				|| sVal.back() == '\r' || sVal.back() == '\n'))
				sVal.pop_back();
			while (!sVal.empty() && sVal.front() == ' ')
				sVal.erase(sVal.begin());

			try { m_mUnusuals[std::stoi(sKey)] = std::stoi(sVal); }
			catch (...) {}
		}
	}

	const std::string sPaintPath = F::Configs.m_sConfigPath + "hats_paint.json";
	std::ifstream inPaint(sPaintPath);
	if (inPaint.is_open())
	{
		while (std::getline(inPaint, sLine))
		{
			const auto nColon = sLine.find(':');
			if (nColon == std::string::npos)
				continue;

			const auto nQ1 = sLine.find('"');
			const auto nQ2 = sLine.find('"', nQ1 + 1);
			if (nQ1 == std::string::npos || nQ2 == std::string::npos || nQ2 <= nQ1)
				continue;

			std::string sKey = sLine.substr(nQ1 + 1, nQ2 - nQ1 - 1);
			std::string sVal = sLine.substr(nColon + 1);

			// Parse [color1, color2]
			const auto nBrk1 = sVal.find('[');
			const auto nBrk2 = sVal.find(']');
			if (nBrk1 == std::string::npos || nBrk2 == std::string::npos)
				continue;

			std::string sInner = sVal.substr(nBrk1 + 1, nBrk2 - nBrk1 - 1);
			const auto nComma = sInner.find(',');
			try
			{
				int nOrig = std::stoi(sKey);
				int nC1 = std::stoi(sInner.substr(0, nComma));
				int nC2 = nComma != std::string::npos ? std::stoi(sInner.substr(nComma + 1)) : 0;
				if (nC1 > 0) m_mPaints[nOrig] = nC1;
				if (nC2 > 0) m_mPaints2[nOrig] = nC2;
			}
			catch (...) {}
		}
	}

	// Standalone hats: simple "[a, b, c]" integer list.
	const std::string sStandalonePath = F::Configs.m_sConfigPath + "hats_standalone.json";
	std::ifstream inStandalone(sStandalonePath);
	if (inStandalone.is_open())
	{
		std::string sAll((std::istreambuf_iterator<char>(inStandalone)), std::istreambuf_iterator<char>());
		std::string sNum;
		for (char c : sAll)
		{
			if ((c >= '0' && c <= '9') || c == '-')
				sNum += c;
			else if (!sNum.empty())
			{
				try { int n = std::stoi(sNum); if (n > 0 && !HasStandaloneHat(n)) m_vStandaloneHats.push_back(n); }
				catch (...) {}
				sNum.clear();
			}
		}
		if (!sNum.empty())
		{
			try { int n = std::stoi(sNum); if (n > 0 && !HasStandaloneHat(n)) m_vStandaloneHats.push_back(n); }
			catch (...) {}
		}
	}

	// Force full update so the proxy processes all existing entities
	if (!m_mOverrides.empty())
		I::ClientState->ForceFullUpdate();
}
