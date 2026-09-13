#ifndef TEXTMODE
#include "../SDK/SDK.h"

// CProxyAnimatedWeaponSheen::OnBind - animates the killstreak sheen on every sheened
// weapon in view. Signature verified against the current client.dll: it resolves to
// exactly one function and matches vt[1] of the vtable the
// "AnimatedWeaponSheen_IMaterialProxy003" factory installs.
MAKE_SIGNATURE(CProxyAnimatedWeaponSheen_OnBind, "client.dll", "48 89 54 24 ? 55 57 41 54 48 8D 6C 24 ? 48 81 EC ? 00 00 00", 0x0);

// The engine's IMaterialVar vtable does not line up with our SDK header, so instead of
// guessing a slot we call the tint write exactly the way OnBind itself does: vtable
// slot 12 with (const float*, int numcomps).
using SetVecValueFn = void(__fastcall*)(IMaterialVar*, const float*, int);

MAKE_HOOK(CProxyAnimatedWeaponSheen_OnBind, S::CProxyAnimatedWeaponSheen_OnBind(), void, void* pProxy, void* pEntity)
{
	DEBUG_RETURN(CProxyAnimatedWeaponSheen_OnBind, pProxy, pEntity);
	CALL_ORIGINAL(pProxy, pEntity);

	const int iMode = Vars::Visuals::Other::SheenColor.Value;
	if (iMode == Vars::Visuals::Other::SheenColorEnum::Off)
		return;

	// +0x28 = m_pTint ($sheenmaptint) in the current layout; OnBind reads the mask
	// vars from +0x40..+0x60 and the frame texture from +0x08, which all check out
	const auto pTint = *reinterpret_cast<IMaterialVar**>(reinterpret_cast<uintptr_t>(pProxy) + 0x28);
	if (!pTint)
		return;

	byte r = Vars::Colors::KillstreakSheen.Value.r;
	byte g = Vars::Colors::KillstreakSheen.Value.g;
	byte b = Vars::Colors::KillstreakSheen.Value.b;
	if (iMode == Vars::Visuals::Other::SheenColorEnum::Rainbow)
	{
		// full hue wheel every 4 seconds
		const float flHue = fmodf(I::EngineClient->Time() * 0.25f, 1.0f);
		const float flH = flHue * 6.0f;
		const int i = int(flH);
		const float f = flH - i;
		const float q = 1.0f - f;
		switch (i % 6)
		{
		case 0: r = 255; g = byte(f * 255); b = 0; break;
		case 1: r = byte(q * 255); g = 255; b = 0; break;
		case 2: r = 0; g = 255; b = byte(f * 255); break;
		case 3: r = 0; g = byte(q * 255); b = 255; break;
		case 4: r = byte(f * 255); g = 0; b = 255; break;
		default: r = 255; g = 0; b = byte(q * 255); break;
		}
	}

	const float flTint[4] = { r / 255.f, g / 255.f, b / 255.f, 1.f };
	const auto ppVT = *reinterpret_cast<void***>(pTint);
	reinterpret_cast<SetVecValueFn>(ppVT[12])(pTint, flTint, 4);
}
#endif
