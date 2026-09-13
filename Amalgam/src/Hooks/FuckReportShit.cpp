#include "../SDK/SDK.h"

MAKE_SIGNATURE(AbuseReportManager, "client.dll", "48 8B 0D ? ? ? ? 48 8B 01 48 83 C4 ? 48 FF A0 ? ? ? ? 48 8B 0D", 0x0);
MAKE_SIGNATURE(ReportAbuseShit, "client.dll", "48 8B 0D ? ? ? ? 48 85 C9 75 ? 48 8D 0D ? ? ? ? 48 FF 25", 0x0);

// I COULDN'T FIGURE OUT HOW TO UNBIND IT FROM MY THIRDPERSON KEY AND IT KEPT MAKING SCREENSHOTS SO I DECIDED TO DO THIS
MAKE_HOOK(ReportAbuseShit, S::ReportAbuseShit(), void*)
{
	*reinterpret_cast<void**>(U::Memory.RelToAbs(S::AbuseReportManager())) = nullptr; // report manager is dead tf2 is now saved.
	return CALL_ORIGINAL();
}