#include "../SDK/SDK.h"
#include <unordered_set>

#include "../Features/Visuals/Chams/Chams.h"
#include "../Features/Visuals/Glow/Glow.h"
#include "../Features/Visuals/Materials/Materials.h"
#include "../Features/Visuals/CameraWindow/CameraWindow.h"
#include "../Features/Visuals/SkinChanger/SkinChanger.h"
#include "../Features/Visuals/HatChanger/HatChanger.h"

MAKE_SIGNATURE(CBaseAnimating_InternalDrawModel, "client.dll", "48 8B C4 55 56 48 8D 6C 24 ? 48 81 EC ? ? ? ? 44 8B 81", 0x0);
MAKE_SIGNATURE(CBaseViewModel_DrawModel, "client.dll", "40 53 55 56 48 83 EC ? 80 B9", 0x0);

static bool s_bDrawingViewmodel = false;

// Weapon "unusual" particle systems (schema attribute_controlled_attached_particles ->
// weapon_unusual_effects / other_particles). These need control-point attachments
// (unusual_0..unusual_5) that only exist on Valve's genuine promotional Unusual weapon
// models, so the engine's own attribute-driven attach silently no-ops on any other
// weapon model (confirmed: the attribute write itself succeeds and is visible on the
// HUD loadout preview, just not on a live viewmodel). Until the real attach API
// (CNewParticleEffect::Create, non-exported, no stable signature found) is hooked, this
// re-dispatches the same particle at the weapon's current muzzle-bone position every
// few frames as a manual approximation of a continuously-attached effect.
static const char* GetWeaponUnusualParticleName(int nEffectID)
{
	switch (nEffectID)
	{
	case 4:   return "community_sparkle";
	case 701: return "weapon_unusual_hot";
	case 702: return "weapon_unusual_isotope";
	case 703: return "weapon_unusual_cool";
	case 704: return "weapon_unusual_energyorb";
	default:  return nullptr;
	}
}

// Re-dispatch a manually-attached weapon unusual effect at the current muzzle-bone
// position. Throttled (not every frame) since each dispatch spawns a fresh particle
// system instance rather than updating an existing one's control points.
static void UpdateManualWeaponUnusual(const DrawModelState_t& pState, matrix3x4* pBoneToWorld)
{
	static float s_flNextLog = 0.f;
	const float flLogNow = SDK::PlatFloatTime();
	const bool bLog = flLogNow >= s_flNextLog;
	if (bLog)
		s_flNextLog = flLogNow + 1.0f;

	auto pLocal = H::Entities.GetLocal();
	if (!pLocal)
	{
		if (bLog) SDK::Output("WeaponUnusual", "no local player", { 255, 100, 100, 255 }, OUTPUT_DEBUG);
		return;
	}
	auto pWeapon = pLocal->m_hActiveWeapon()->As<CTFWeaponBase>();
	if (!pWeapon)
	{
		if (bLog) SDK::Output("WeaponUnusual", "no active weapon", { 255, 100, 100, 255 }, OUTPUT_DEBUG);
		return;
	}

	const int nEffectID = F::SkinChanger.GetUnusualEffect(pWeapon->m_iItemDefinitionIndex());
	if (nEffectID <= 0)
	{
		if (bLog)
			SDK::Output("WeaponUnusual",
				std::format("no unusual configured, def={}", pWeapon->m_iItemDefinitionIndex()).c_str(),
				{ 255, 180, 100, 255 }, OUTPUT_DEBUG);
		return;
	}
	const char* pszParticle = GetWeaponUnusualParticleName(nEffectID);
	if (!pszParticle)
	{
		if (bLog)
			SDK::Output("WeaponUnusual",
				std::format("unmapped effect id={}", nEffectID).c_str(),
				{ 255, 180, 100, 255 }, OUTPUT_DEBUG);
		return;
	}

	studiohdr_t* pHdr = pState.m_pStudioHdr;
	if (!pHdr || !pBoneToWorld)
	{
		if (bLog) SDK::Output("WeaponUnusual", "no studiohdr/bones on this draw call", { 255, 180, 100, 255 }, OUTPUT_DEBUG);
		return;
	}

	int nMuzzleBone = -1;
	for (int i = 0; i < pHdr->numbones; i++)
	{
		const char* szName = pHdr->pBone(i)->pszName();
		if (szName && _stricmp(szName, "muzzle") == 0)
		{
			nMuzzleBone = i;
			break;
		}
	}
	if (nMuzzleBone < 0)
	{
		if (bLog)
		{
			std::string sBones;
			for (int i = 0; i < pHdr->numbones && i < 40; i++)
			{
				const char* szName = pHdr->pBone(i)->pszName();
				sBones += szName ? szName : "?";
				sBones += ", ";
			}
			SDK::Output("WeaponUnusual",
				std::format("no 'muzzle' bone on model '{}' ({} bones): {}",
					pHdr->pszName() ? pHdr->pszName() : "?", pHdr->numbones, sBones).c_str(),
				{ 255, 180, 100, 255 }, OUTPUT_DEBUG);
		}
		return;
	}

	static float s_flNextDispatch = 0.f;
	if (flLogNow < s_flNextDispatch)
		return;
	s_flNextDispatch = flLogNow + 0.08f; // ~12/sec - follows the weapon closely without spawning a new instance every frame

	const Vec3 vOrigin = { pBoneToWorld[nMuzzleBone][0][3], pBoneToWorld[nMuzzleBone][1][3], pBoneToWorld[nMuzzleBone][2][3] };
	if (bLog)
		SDK::Output("WeaponUnusual",
			std::format("dispatching '{}' at ({:.1f}, {:.1f}, {:.1f}) bone={}",
				pszParticle, vOrigin.x, vOrigin.y, vOrigin.z, nMuzzleBone).c_str(),
			{ 120, 255, 180, 255 }, OUTPUT_DEBUG);
	H::Particles.DispatchParticleEffect(pszParticle, vOrigin, pLocal->GetEyeAngles(), pWeapon);
}

MAKE_HOOK(IVModelRender_DrawModelExecute, U::Memory.GetVirtual(I::ModelRender, 19), void,
	IVModelRender* rcx, const DrawModelState_t& pState, const ModelRenderInfo_t& pInfo, matrix3x4* pBoneToWorld)
{
	DEBUG_RETURN(IVModelRender_DrawModelExecute, rcx, pState, pInfo, pBoneToWorld);

#ifndef TEXTMODE
	if (I::EngineVGui->IsGameUIVisible() || SDK::CleanScreenshot()
		|| F::CameraWindow.m_bDrawing || !F::Materials.m_bLoaded || G::Unload)
		return CALL_ORIGINAL(rcx, pState, pInfo, pBoneToWorld);

	if (F::Chams.m_bRendering)
		return F::Chams.RenderHandler(rcx, pState, pInfo, pBoneToWorld);
	if (F::Glow.m_bRendering)
		return F::Glow.RenderHandler(rcx, pState, pInfo, pBoneToWorld);

	if (F::Chams.m_mEntities.contains(pInfo.entity_index))
		return;

	auto pEntityBase = I::ClientEntityList->GetClientEntity(pInfo.entity_index);
	auto pEntity = pEntityBase ? pEntityBase->As<CBaseEntity>() : nullptr;

	// TEMP DIAGNOSTIC: only on the real render pass (matches every other gated
	// pState.m_pStudioHdr access in this file) - the earlier crash came from doing
	// this unconditionally, including during shadow/depth prepasses where the
	// studiohdr isn't fully valid.
	if (pState.m_drawFlags & STUDIO_RENDER)
	{
		static std::unordered_set<int> s_sLoggedClasses;
		const int nClassID = pEntity ? static_cast<int>(pEntity->GetClassID()) : -1;
		if (s_sLoggedClasses.insert(nClassID).second)
		{
			SDK::Output("DrawModelDiag",
				std::format("classID={} entidx={} isViewmodel={} isWearableVM={}",
					nClassID, pInfo.entity_index,
					pEntity ? (pEntity->IsViewmodel() ? 1 : 0) : -1,
					pEntity ? (pEntity->IsWearableVM() ? 1 : 0) : -1).c_str(),
				{ 180, 200, 255, 255 }, OUTPUT_DEBUG);
		}
	}

	if (pEntity && pEntity->IsWearableVM() /*pEntity->IsViewmodel()*/)
	{
		// When the active weapon is a visual reskin, its viewmodel's DrawModel fires twice
		// per frame (once with the stock model, once with the reskin override) while the
		// arms/hands viewmodel only draws once. Track draw counts per-entity across frames
		// so only the reskinned weapon's redundant first pass is dropped.
		if (F::SkinChanger.IsActiveWeaponReskinned() && (pState.m_drawFlags & STUDIO_RENDER))
		{
			static int s_nLastFrame = -1;
			static std::unordered_map<int, int> s_mDrawsThisFrame;
			static std::unordered_map<int, int> s_mDrawsLastFrame;
			const int nFrame = I::GlobalVars->framecount;
			if (nFrame != s_nLastFrame)
			{
				s_nLastFrame = nFrame;
				s_mDrawsLastFrame = s_mDrawsThisFrame;
				s_mDrawsThisFrame.clear();
			}

			const int nThisDraw = ++s_mDrawsThisFrame[pInfo.entity_index];
			const auto itLast = s_mDrawsLastFrame.find(pInfo.entity_index);
			const bool bDoubleDrawer = itLast != s_mDrawsLastFrame.end() && itLast->second >= 2;
			if (bDoubleDrawer && nThisDraw == 1)
				return;
		}

		F::Glow.RenderViewmodel(rcx, pState, pInfo, pBoneToWorld);
		if (F::Chams.RenderViewmodel(rcx, pState, pInfo, pBoneToWorld))
			return;
	}

	// Weapon "muzzle" bone lives on the actual weapon/hands viewmodel model
	// (CBaseViewModel/CTFViewModel), not the narrower CTFWearableVM class used above
	// for reskin double-draw suppression - check independently and broader here.
	if (pEntity && pEntity->IsViewmodel() && (pState.m_drawFlags & STUDIO_RENDER))
		UpdateManualWeaponUnusual(pState, pBoneToWorld);

	CALL_ORIGINAL(rcx, pState, pInfo, pBoneToWorld);

	// Draw standalone hats (no equipped cosmetic required) bonemerged to the
	// local player, reusing the same bone matrices the engine just used for the
	// player body. Only on the real render pass, not shadow/depth prepasses.
	const int nLocalIdx = I::EngineClient->GetLocalPlayer();
	const bool bIsLocalPlayerModel = (nLocalIdx > 0 && pInfo.entity_index == nLocalIdx);

	{
		static float s_flNextLog = 0.f;
		const float flNow = SDK::PlatFloatTime();
		if (flNow >= s_flNextLog)
		{
			s_flNextLog = flNow + 1.0f;
			SDK::Output("StandaloneHatDiag",
				std::format("localIdx={} entidx={} isLocalPlayerModel={} studioRender={} hatCount={}",
					nLocalIdx, pInfo.entity_index, bIsLocalPlayerModel ? 1 : 0,
					(pState.m_drawFlags & STUDIO_RENDER) ? 1 : 0,
					F::HatChanger.GetStandaloneHats().size()).c_str(),
				{ 180, 200, 255, 255 }, OUTPUT_DEBUG);
		}
	}

	if (bIsLocalPlayerModel && (pState.m_drawFlags & STUDIO_RENDER) && !F::HatChanger.GetStandaloneHats().empty())
		F::HatChanger.DrawStandaloneHats(pState, pInfo, pBoneToWorld);
#endif
}

#ifndef TEXTMODE
MAKE_HOOK(CBaseAnimating_InternalDrawModel, S::CBaseAnimating_InternalDrawModel(), int,
	CBaseAnimating* rcx, int flags)
{
	DEBUG_RETURN(CBaseAnimating_InternalDrawModel, rcx, flags);

	if (!s_bDrawingViewmodel /*|| !(flags & STUDIO_RENDER)*/)
		return CALL_ORIGINAL(rcx, flags);

	int iReturn;
	F::Glow.RenderViewmodel(rcx, flags);
	if (F::Chams.RenderViewmodel(rcx, flags, &iReturn))
		return iReturn;

	return CALL_ORIGINAL(rcx, 1);
}


MAKE_HOOK(CBaseViewModel_DrawModel, S::CBaseViewModel_DrawModel(), int,
	void* rcx, int flags)
{
	DEBUG_RETURN(CBaseAnimating_DrawModel, rcx, flags);

	if (s_bDrawingViewmodel || I::EngineVGui->IsGameUIVisible() || SDK::CleanScreenshot()
		|| F::CameraWindow.m_bDrawing || !F::Materials.m_bLoaded || G::Unload)
		return CALL_ORIGINAL(rcx, flags);

	s_bDrawingViewmodel = true;
	int iReturn = CALL_ORIGINAL(rcx, flags);
	s_bDrawingViewmodel = false;
	return iReturn;
}
#endif
