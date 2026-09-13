#include "SkinChanger.h"
#include "../../Configs/Configs.h"
#include "../../../Utils/NetVars/NetVars.h"
#include "../../../SDK/SDK.h"
#include <fstream>

// ─── Signatures ──────────────────────────────────────────────────────────────

MAKE_SIGNATURE(SkinChanger_GetItemSchema,
	"client.dll",
	"48 83 EC ? E8 ? ? ? ? 48 83 C0 ? 48 83 C4 ? C3 CC CC CC",
	0x0);

MAKE_SIGNATURE(SkinChanger_GetAttributeDefinition,
	"client.dll",
	"89 54 24 ? 53 48 83 EC ? 48 8B D9 48 8D 54 24 ? 48 81 C1 ? ? ? ? E8 ? ? ? ? 8B D0 3B 83 ? ? ? ? 73 ? 8B 83 ? ? ? ? 83 F8 ? 74 ? 3B D0 7F ? 48 81 C3 ? ? ? ? 44 8B C2 83 FA ? 74 ? 48 8B 03 8B CA",
	0x0);

MAKE_SIGNATURE(SkinChanger_SetRuntimeAttributeValue,
	"client.dll",
	"48 89 5C 24 10 55 56 57 48 8B EC 48 83 EC 50 44",
	0x0);

// ─── Attribute list layout ────────────────────────────────────────────────────
// CAttributeList is looked up dynamically via the "CEconEntity" DataTable so
// the code survives TF2 updates that shift class member offsets.  The literal
// 3512 (0xDB8) is kept as a compile-time fallback in case the netvar walk
// fails for any reason.
//
//  CAttributeList (x64):
//    offset  0 : void*  pad
//    offset  8 : CUtlVector<CEconItemAttribute> m_Attributes
//      offset  8 : T*   m_pMemory       (CUtlMemory)
//      offset 16 : int  m_nAllocationCount
//      offset 20 : int  m_nGrowSize
//      offset 24 : int  m_Size             <- real element count
//    offset 32 : void* m_pManager

static std::uintptr_t GetAttrListOffset()
{
	static const std::uintptr_t kOffset = []
	{
		const int n = U::NetVars.GetNetVar("CEconEntity", "m_AttributeList");
		return static_cast<std::uintptr_t>(n > 0 ? n : 3512);
	}();
	return kOffset;
}

// Mirror of the in-game CAttributeList memory layout.
// Only used for Count() and as the 'this' pointer for SetRuntimeAttributeValue.
struct GameCAttributeList_t
{
	char _pad[8];      // CAttributeList::pad  (offset 0)
	void* m_pMemory;   // CUtlMemory::m_pMemory (offset 8)
	int   m_nAllocCount; // offset 16
	int   m_nGrowSize;   // offset 20
	int   m_nSize;       // CUtlVector::m_Size — true element count (offset 24)

	int Count() const { return m_nSize; }

	void SetAttr(int nIndex, float flValue)
	{
		if (!S::SkinChanger_GetItemSchema()
		 || !S::SkinChanger_GetAttributeDefinition()
		 || !S::SkinChanger_SetRuntimeAttributeValue())
			return;

		using FnGetSchema = void* (__fastcall*)();
		using FnGetAttr   = void* (__fastcall*)(void*, int);
		using FnSetValue  = void  (__fastcall*)(GameCAttributeList_t*, void*, float);

		void* pSchema = reinterpret_cast<FnGetSchema>(S::SkinChanger_GetItemSchema())();
		if (!pSchema)
			return;

		void* pAttrDef = reinterpret_cast<FnGetAttr>(S::SkinChanger_GetAttributeDefinition())(pSchema, nIndex);
		if (!pAttrDef)
		{
			SDK::Output("SkinChanger", std::format("  SetAttr FAIL: no attrdef for index {}", nIndex).c_str(),
				{ 255, 100, 100, 255 }, OUTPUT_DEBUG);
			return;
		}

		// Debug: log the raw bits we're writing so we can catch integer-encoding mismatches.
		uint32_t rawBits = 0; memcpy(&rawBits, &flValue, 4);
		SDK::Output("SkinChanger", std::format("  SetAttr attr={} val={} bits={:#010x} attrdef={:#x}",
			nIndex, flValue, rawBits, reinterpret_cast<uintptr_t>(pAttrDef)).c_str(),
			{ 180, 255, 180, 255 }, OUTPUT_DEBUG);
		reinterpret_cast<FnSetValue>(S::SkinChanger_SetRuntimeAttributeValue())(this, pAttrDef, flValue);
	}
};

// ─── Helpers ──────────────────────────────────────────────────────────────────

// Number of runtime attributes that certain weapons pre-populate on their own.
// The sniper rifle has a "no_jump" attribute added by the game.
static int PreFilledAttrCount(int nDefIndex)
{
	using namespace SkinWeaponID;
	switch (nDefIndex)
	{
	case SniperRifle:
	case SniperRifleR:
		return 1;
	default:
		return 0;
	}
}

// Encode a paintkit integer ID as the bit-pattern float the game expects.
static float IntAsFloat(int n)
{
	float f;
	memcpy(&f, &n, sizeof(f));
	return f;
}

static int FloatAsInt(float f)
{
	int n;
	memcpy(&n, &f, sizeof(n));
	return n;
}

// ─── Warpaint name table ──────────────────────────────────────────────────────
// Names sourced from the TF2 GC schema (via schema.autobot.tf).
// IDs 0-86:  weapon-specific (e.g. "King of the Jungle" only on Minigun)
// IDs 100+:  universal war paints, work on any decorated weapon
static const PaintkitEntry s_PaintkitTable[] =
{
	// Weapon-specific (Jungle Inferno / seasonal collections)
	{  0, "Red Rock Roscoe"},
	{  1, "Sand Cannon"},
	{  2, "Wrapped Reviver"},
	{  3, "Psychedelic Slugger"},
	{  4, "Carpet Bomber"},
	{  5, "Masked Mender"},
	{  6, "Woodland Warrior"},
	{  7, "Purple Range"},
	{  8, "Sudden Flurry"},
	{  9, "Forest Fire"},
	{ 10, "King of the Jungle"},
	{ 11, "Night Terror"},
	{ 12, "Backwoods Boomstick"},
	{ 13, "Woodsy Widowmaker"},
	{ 14, "Night Owl"},
	{ 15, "Tartan Torpedo"},
	{ 16, "Rustic Ruiner"},
	{ 17, "Barn Burner"},
	{ 18, "Homemade Heater"},
	{ 19, "Lumber From Down Under"},
	{ 20, "Iron Wood"},
	{ 21, "Country Crusher"},
	{ 22, "Plaid Potshotter"},
	{ 23, "Shot in the Dark"},
	{ 24, "Blasted Bombardier"},
	{ 25, "Reclaimed Reanimator"},
	{ 26, "Antique Annihilator"},
	{ 27, "Old Country"},
	{ 28, "American Pastoral"},
	{ 29, "Backcountry Blaster"},
	{ 30, "Bovine Blazemaker"},
	{ 31, "War Room"},
	{ 32, "Treadplate Tormenter"},
	{ 33, "Bogtrotter"},
	{ 34, "Earth, Sky and Fire"},
	{ 35, "Hickory Hole-Puncher"},
	{ 36, "Spruce Deuce"},
	{ 37, "Team Sprayer"},
	{ 38, "Rooftop Wrangler"},
	{ 39, "Civil Servant"},
	{ 40, "Citizen Pain"},
	{ 41, "Local Hero"},
	{ 42, "Mayor"},
	{ 43, "Smalltown Bringdown"},
	{ 44, "Civic Duty"},
	{ 45, "Liquid Asset"},
	{ 46, "Black Dahlia"},
	{ 47, "Lightning Rod"},
	{ 48, "Pink Elephant"},
	{ 49, "Flash Fryer"},
	{ 50, "Spark of Life"},
	{ 51, "Dead Reckoner"},
	{ 52, "Shell Shocker"},
	{ 53, "Current Event"},
	{ 54, "Turbine Torcher"},
	{ 55, "Brick House"},
	{ 56, "Sandstone Special"},
	{ 57, "Aqua Marine"},
	{ 58, "Low Profile"},
	{ 59, "Thunderbolt"},
	{ 60, "Autumn"},
	{ 61, "Nutcracker"},
	{ 62, "Pumpkin Patch"},
	{ 63, "Macabre Web"},
	{ 64, "Boneyard"},
	{ 65, "Wildwood"},
	{ 66, "Balloonicorn"},
	{ 67, "Rainbow"},
	{ 68, "Sweet Dreams"},
	{ 69, "Blue Mew"},
	{ 70, "Brain Candy"},
	{ 71, "Stabbed to Hell"},
	{ 72, "Flower Power"},
	{ 73, "Mister Cuddles"},
	{ 74, "Shot to Hell"},
	{ 75, "Torqued to Hell"},
	{ 76, "Coffin Nail"},
	{ 77, "Top Shelf"},
	{ 78, "Dressed to Kill"},
	{ 79, "High Roller's"},
	{ 80, "Warhawk"},
	{ 81, "Blitzkrieg"},
	{ 82, "Airwolf"},
	{ 83, "Corsair"},
	{ 84, "Butcher Bird"},
	{ 85, "Killer Bee"},
	{ 86, "Red Bear"},
	// Universal War Paints
	{102, "Wrapped Reviver Mk.II"},
	{104, "Carpet Bomber Mk.II"},
	{105, "Masked Mender Mk.II"},
	{106, "Woodland Warrior Mk.II"},
	{109, "Forest Fire Mk.II"},
	{112, "Backwoods Boomstick Mk.II"},
	{113, "Woodsy Widowmaker Mk.II"},
	{114, "Night Owl Mk.II"},
	{120, "Iron Wood Mk.II"},
	{122, "Plaid Potshotter Mk.II"},
	{130, "Bovine Blazemaker Mk.II"},
	{139, "Civil Servant Mk.II"},
	{143, "Smalltown Bringdown Mk.II"},
	{144, "Civic Duty Mk.II"},
	{151, "Dead Reckoner Mk.II"},
	{160, "Autumn Mk.II"},
	{161, "Nutcracker Mk.II"},
	{163, "Macabre Web Mk.II"},
	{200, "Bloom Buffed"},
	{201, "Quack Canvassed"},
	{202, "Bank Rolled"},
	{203, "Merc Stained"},
	{204, "Kill Covered"},
	{205, "Fire Glazed"},
	{206, "Pizza Polished"},
	{207, "Bonk Varnished"},
	{208, "Star Crossed"},
	{209, "Clover Camo'd"},
	{210, "Freedom Wrapped"},
	{211, "Cardboard Boxed"},
	{212, "Dream Piped"},
	{213, "Miami Element"},
	{214, "Neo Tokyo"},
	{215, "Geometrical Teams"},
	{217, "Bomber Soul"},
	{218, "Uranium"},
	{220, "Cabin Fevered"},
	{221, "Polar Surprise"},
	{223, "Hana"},
	{224, "Dovetailed"},
	{225, "Cosmic Calamity"},
	{226, "Hazard Warning"},
	{228, "Mosaic"},
	{230, "Jazzy"},
	{232, "Alien Tech"},
	{234, "Damascus and Mahogany"},
	{235, "Skull Study"},
	{236, "Haunted Ghosts"},
	{237, "Spectral Shimmered"},
	{238, "Spirit of Halloween"},
	{239, "Horror Holiday"},
	{240, "Totally Boned"},
	{241, "Electroshocked"},
	{242, "Ghost Town"},
	{243, "Tumor Toasted"},
	{244, "Calavera Canvas"},
	{245, "Snow Covered"},
	{246, "Frost Ornamented"},
	{247, "Smissmas Village"},
	{248, "Igloo"},
	{249, "Seriously Snowed"},
	{250, "Smissmas Camo"},
	{251, "Sleighin' Style"},
	{252, "Alpine"},
	{253, "Gift Wrapped"},
	{254, "Winterland Wrapped"},
	{255, "Helldriver"},
	{256, "Organ-ically Hellraised"},
	{257, "Spectrum Splattered"},
	{258, "Candy Coated"},
	{259, "Pumpkin Pied"},
	{260, "Sweet Toothed"},
	{261, "Crawlspace Critters"},
	{262, "Portal Plastered"},
	{263, "Death Deluxe"},
	{264, "Raving Dead"},
	{265, "Eyestalker"},
	{266, "Spider's Cluster"},
	{267, "Gourdy Green"},
	{268, "Mummified Mimic"},
	{269, "Spider Season"},
	{270, "Gingerbread Winner"},
	{271, "Saccharine Striped"},
	{272, "Elfin Enamel"},
	{273, "Peppermint Swirl"},
	{275, "Snow Globalization"},
	{276, "Gifting Mann's Wrapping Paper"},
	{277, "Snowflake Swirled"},
	{278, "Smissmas Spycrabs"},
	{279, "Frozen Aurora"},
	{280, "Starlight Serenity"},
	{281, "Frosty Delivery"},
	{282, "Glacial Glazed"},
	{283, "Cookie Fortress"},
	{284, "Sarsaparilla Sprayed"},
	{285, "Swashbuckled"},
	{286, "Skull Cracked"},
	{287, "Misfortunate"},
	{289, "Neon-ween"},
	{290, "Simple Spirits"},
	{291, "Broken Bones"},
	{292, "Potent Poison"},
	{293, "Searing Souls"},
	{294, "Party Phantoms"},
	{295, "Polter-Guised"},
	{296, "Kiln and Conquer"},
	{297, "Necromanced"},
	{300, "Yeti Coated"},
	{301, "Park Pigmented"},
	{302, "Mannana Peeled"},
	{303, "Macaw Masked"},
	{304, "Sax Waxed"},
	{305, "Anodized Aloha"},
	{306, "Bamboo Brushed"},
	{307, "Tiger Buffed"},
	{308, "Croc Dusted"},
	{309, "Piña Polished"},
	{310, "Leopard Printed"},
	{390, "Dragon Slayer"},
	{391, "Smissmas Sweater"},
	{400, "Ghoul Blaster"},
	{401, "Cream Corned"},
	{402, "Sunriser"},
	{403, "Sacred Slayer"},
	{404, "Metalized Soul"},
	{405, "Bonzo Gnawed"},
	{406, "Health and Hell"},
	{407, "Health and Hell (Green)"},
	{408, "Hypergon"},
	{409, "Pumpkin Plastered"},
	{410, "Chilly Autumn"},
	{411, "Steel Brushed"},
	{412, "Secretly Serviced"},
	{413, "Sky Stallion"},
	{414, "Bomb Carrier"},
	{415, "Business Class"},
	{416, "Deadly Dragon"},
	{417, "Team Serviced"},
	{418, "Warborn"},
	{419, "Pacific Peacemaker"},
	{420, "Mechanized Monster"},
	{421, "Stardust"},
	{422, "Team Detail"},
	{423, "Gobi Glazed"},
	{424, "Sleek Greek"},
	{425, "Graphite Gripped"},
	{426, "Stealth Specialist"},
	{427, "Piranha Mania"},
	{428, "Team Charged"},
	{429, "Brawler's Iron"},
	{430, "Necropolish"},
	{431, "Blackout"},
	{432, "Broken Record"},
};

const PaintkitEntry* GetPaintkitEntries(int& outCount)
{
	outCount = static_cast<int>(std::size(s_PaintkitTable));
	return s_PaintkitTable;
}

static const UnusualEffectEntry s_UnusualEffectTable[] =
#include "UnusualEffects.inl"
;

const UnusualEffectEntry* GetUnusualEffectEntries(int& outCount)
{
	outCount = static_cast<int>(std::size(s_UnusualEffectTable));
	return s_UnusualEffectTable;
}

// --- Reskin option tables ---
// True reskins only: items that share the base weapon's model (skin-only variants
// such as Festive / Botkiller), plus all-class melees. These render correctly via a
// plain def-index swap. Generated from the item schema. For any other weapon, use the
// manual def-index field in the menu.

static const ReskinEntry s_ReskinScattergun[] = {
	{   669, "Festive" },
	{   799, "Silver Botkiller Mk.I" },
	{   808, "Gold Botkiller Mk.I" },
	{   888, "Rust Botkiller Mk.I" },
	{   897, "Blood Botkiller Mk.I" },
	{   906, "Carbonado Botkiller Mk.I" },
	{   915, "Diamond Botkiller Mk.I" },
	{   964, "Silver Botkiller Mk.II" },
	{   973, "Gold Botkiller Mk.II" },
};
static const ReskinEntry s_ReskinRocketLauncher[] = {
	{   658, "Festive" },
	{   800, "Silver Botkiller Mk.I" },
	{   809, "Gold Botkiller Mk.I" },
	{   889, "Rust Botkiller Mk.I" },
	{   898, "Blood Botkiller Mk.I" },
	{   907, "Carbonado Botkiller Mk.I" },
	{   916, "Diamond Botkiller Mk.I" },
	{   965, "Silver Botkiller Mk.II" },
	{   974, "Gold Botkiller Mk.II" },
};
static const ReskinEntry s_ReskinFlameThrower[] = {
	{   659, "Festive" },
	{   798, "Silver Botkiller Mk.I" },
	{   807, "Gold Botkiller Mk.I" },
	{   887, "Rust Botkiller Mk.I" },
	{   896, "Blood Botkiller Mk.I" },
	{   905, "Carbonado Botkiller Mk.I" },
	{   914, "Diamond Botkiller Mk.I" },
	{   963, "Silver Botkiller Mk.II" },
	{   972, "Gold Botkiller Mk.II" },
};
static const ReskinEntry s_ReskinGrenadeLauncher[] = {
	{  1007, "Festive" },
};
static const ReskinEntry s_ReskinStickyLauncher[] = {
	{   661, "Festive" },
	{   797, "Silver Botkiller Mk.I" },
	{   806, "Gold Botkiller Mk.I" },
	{   886, "Rust Botkiller Mk.I" },
	{   895, "Blood Botkiller Mk.I" },
	{   904, "Carbonado Botkiller Mk.I" },
	{   913, "Diamond Botkiller Mk.I" },
	{   962, "Silver Botkiller Mk.II" },
	{   971, "Gold Botkiller Mk.II" },
};
static const ReskinEntry s_ReskinMinigun[] = {
	{    41, "Natascha" },
	{   654, "Festive" },
	{   793, "Silver Botkiller Mk.I" },
	{   802, "Gold Botkiller Mk.I" },
	{   850, "Deflector" },
	{   882, "Rust Botkiller Mk.I" },
	{   891, "Blood Botkiller Mk.I" },
	{   900, "Carbonado Botkiller Mk.I" },
	{   909, "Diamond Botkiller Mk.I" },
	{   958, "Silver Botkiller Mk.II" },
	{   967, "Gold Botkiller Mk.II" },
};
static const ReskinEntry s_ReskinWrench[] = {
	{   169, "Golden" },
	{   662, "Festive" },
	{   795, "Silver Botkiller Mk.I" },
	{   804, "Gold Botkiller Mk.I" },
	{   884, "Rust Botkiller Mk.I" },
	{   893, "Blood Botkiller Mk.I" },
	{   902, "Carbonado Botkiller Mk.I" },
	{   911, "Diamond Botkiller Mk.I" },
	{   960, "Silver Botkiller Mk.II" },
	{   969, "Gold Botkiller Mk.II" },
	{   423, "Saxxy" },
	{  1071, "Golden Frying Pan" },
	{  1123, "Necro Smasher" },
};
static const ReskinEntry s_ReskinShotgun[] = {
	{  1141, "Festive" },
};
static const ReskinEntry s_ReskinMediGun[] = {
	{    35, "Kritzkrieg" },
	{   663, "Festive" },
	{   796, "Silver Botkiller Mk.I" },
	{   805, "Gold Botkiller Mk.I" },
	{   885, "Rust Botkiller Mk.I" },
	{   894, "Blood Botkiller Mk.I" },
	{   903, "Carbonado Botkiller Mk.I" },
	{   912, "Diamond Botkiller Mk.I" },
	{   961, "Silver Botkiller Mk.II" },
	{   970, "Gold Botkiller Mk.II" },
	{   998, "Vaccinator" },
};
static const ReskinEntry s_ReskinSniperRifle[] = {
	{   664, "Festive" },
	{   792, "Silver Botkiller Mk.I" },
	{   801, "Gold Botkiller Mk.I" },
	{   881, "Rust Botkiller Mk.I" },
	{   890, "Blood Botkiller Mk.I" },
	{   899, "Carbonado Botkiller Mk.I" },
	{   908, "Diamond Botkiller Mk.I" },
	{   957, "Silver Botkiller Mk.II" },
	{   966, "Gold Botkiller Mk.II" },
};
static const ReskinEntry s_ReskinSMG[] = {
	{  1149, "Festive" },
};
static const ReskinEntry s_ReskinKnife[] = {
	{   665, "Festive" },
	{   794, "Silver Botkiller Mk.I" },
	{   803, "Gold Botkiller Mk.I" },
	{   883, "Rust Botkiller Mk.I" },
	{   892, "Blood Botkiller Mk.I" },
	{   901, "Carbonado Botkiller Mk.I" },
	{   910, "Diamond Botkiller Mk.I" },
	{   959, "Silver Botkiller Mk.II" },
	{   968, "Gold Botkiller Mk.II" },
};
static const ReskinEntry s_ReskinRevolver[] = {
	{  1142, "Festive" },
};
static const ReskinEntry s_ReskinBat[] = {
	{   660, "Festive" },
	{   264, "Frying Pan" },
	{   423, "Saxxy" },
	{   474, "Conscientious Objector" },
	{   880, "Freedom Staff" },
	{   939, "Bat Outta Hell" },
	{   954, "Memory Maker" },
	{  1013, "Ham Shank" },
	{  1071, "Golden Frying Pan" },
	{  1123, "Necro Smasher" },
	{  1127, "Crossing Guard" },
};
static const ReskinEntry s_ReskinShovel[] = {
	{   264, "Frying Pan" },
	{   423, "Saxxy" },
	{   474, "Conscientious Objector" },
	{   880, "Freedom Staff" },
	{   939, "Bat Outta Hell" },
	{   954, "Memory Maker" },
	{  1013, "Ham Shank" },
	{  1071, "Golden Frying Pan" },
	{  1123, "Necro Smasher" },
	{  1127, "Crossing Guard" },
};
static const ReskinEntry s_ReskinFireAxe[] = {
	{  1000, "Festive Axtinguisher" },
	{   264, "Frying Pan" },
	{   423, "Saxxy" },
	{   474, "Conscientious Objector" },
	{   880, "Freedom Staff" },
	{   939, "Bat Outta Hell" },
	{   954, "Memory Maker" },
	{  1013, "Ham Shank" },
	{  1071, "Golden Frying Pan" },
	{  1123, "Necro Smasher" },
	{  1127, "Crossing Guard" },
};
static const ReskinEntry s_ReskinBottle[] = {
	{   264, "Frying Pan" },
	{   423, "Saxxy" },
	{   474, "Conscientious Objector" },
	{   880, "Freedom Staff" },
	{   939, "Bat Outta Hell" },
	{   954, "Memory Maker" },
	{  1013, "Ham Shank" },
	{  1071, "Golden Frying Pan" },
	{  1123, "Necro Smasher" },
	{  1127, "Crossing Guard" },
};
static const ReskinEntry s_ReskinBonesaw[] = {
	{  1143, "Festive" },
	{   264, "Frying Pan" },
	{   423, "Saxxy" },
	{   474, "Conscientious Objector" },
	{   880, "Freedom Staff" },
	{   939, "Bat Outta Hell" },
	{   954, "Memory Maker" },
	{  1013, "Ham Shank" },
	{  1071, "Golden Frying Pan" },
	{  1123, "Necro Smasher" },
	{  1127, "Crossing Guard" },
};
static const ReskinEntry s_ReskinKukri[] = {
	{   264, "Frying Pan" },
	{   423, "Saxxy" },
	{   474, "Conscientious Objector" },
	{   880, "Freedom Staff" },
	{   939, "Bat Outta Hell" },
	{   954, "Memory Maker" },
	{  1013, "Ham Shank" },
	{  1071, "Golden Frying Pan" },
	{  1123, "Necro Smasher" },
	{  1127, "Crossing Guard" },
};
static const ReskinEntry s_ReskinEngiePistol[] = {
	{   160, "Lugermorph" },
	{   294, "Lugermorph" },
	{  1202, "Reissued Lugermorph" },
};

const ReskinEntry* GetReskinOptions(int nRedirectedDefIndex, int& outCount)
{
	using namespace SkinWeaponID;
#define RESKIN_CASE(redir, tbl) case redir: outCount = static_cast<int>(std::size(tbl)); return tbl;
	switch (nRedirectedDefIndex)
	{
		RESKIN_CASE(ScattergunR,      s_ReskinScattergun)
		RESKIN_CASE(RocketLauncherR,  s_ReskinRocketLauncher)
		RESKIN_CASE(FlameThrowerR,    s_ReskinFlameThrower)
		RESKIN_CASE(GrenadeLauncherR, s_ReskinGrenadeLauncher)
		RESKIN_CASE(StickyLauncherR,  s_ReskinStickyLauncher)
		RESKIN_CASE(MinigunR,         s_ReskinMinigun)
		RESKIN_CASE(WrenchR,          s_ReskinWrench)
		RESKIN_CASE(EngieShotR,       s_ReskinShotgun)
		RESKIN_CASE(MediGunR,         s_ReskinMediGun)
		RESKIN_CASE(SniperRifleR,     s_ReskinSniperRifle)
		RESKIN_CASE(SMGR,             s_ReskinSMG)
		RESKIN_CASE(KnifeR,           s_ReskinKnife)
		RESKIN_CASE(RevolverR,        s_ReskinRevolver)
		RESKIN_CASE(BatR,             s_ReskinBat)
		RESKIN_CASE(ShovelR,          s_ReskinShovel)
		RESKIN_CASE(FireAxeR,         s_ReskinFireAxe)
		RESKIN_CASE(BottleR,          s_ReskinBottle)
		RESKIN_CASE(BonesawR,         s_ReskinBonesaw)
		RESKIN_CASE(KukriR,           s_ReskinKukri)
		RESKIN_CASE(EngiePistolR,     s_ReskinEngiePistol)
	default: outCount = 0; return nullptr;
	}
#undef RESKIN_CASE
}

// ─── Festive weapon defindex mapping ──────────────────────────────────────────
// Maps a weapon's (redirected) defindex to its Festive variant defindex.
// Returns 0 if no Festive variant exists.
static int GetFestiveDefIndex(int nDefIndex)
{
	switch (nDefIndex)
	{
	// Stock weapons (redirected/paintable IDs)
	case 200: return 669;   // Scattergun
	case 190: return 660;   // Bat
	case 205: return 658;   // Rocket Launcher
	case 208: return 659;   // Flame Thrower
	case 206: return 1007;  // Grenade Launcher
	case 207: return 661;   // Stickybomb Launcher
	case 202: return 654;   // Minigun
	case 197: return 662;   // Wrench
	case 199: return 1141;  // Shotgun
	case 211: return 663;   // Medi Gun
	case 201: return 664;   // Sniper Rifle
	case 203: return 1149;  // SMG
	case 194: return 665;   // Knife
	case 210: return 1142;  // Revolver
	case 198: return 1143;  // Bonesaw

	// Non-stock weapons (original IDs, no redirect)
	case 45:  return 1078;  // Force-a-Nature
	case 46:  return 1145;  // Bonk! Atomic Punch
	case 221: return 999;   // Holy Mackerel
	case 228: return 1085;  // Black Box
	case 129: return 1001;  // Buff Banner
	case 40:  return 1146;  // Backburner
	case 39:  return 1081;  // Flare Gun
	case 38:  return 1000;  // Axtinguisher
	case 131: return 1144;  // Chargin' Targe
	case 132: return 1082;  // Eyelander
	case 42:  return 1002;  // Sandvich
	case 239: return 1084;  // Gloves of Running Urgently
	case 141: return 1004;  // Frontier Justice
	case 140: return 1086;  // Wrangler
	case 305: return 1079;  // Crusader's Crossbow
	case 37:  return 1003;  // Ubersaw
	case 56:  return 1005;  // Huntsman
	case 58:  return 1083;  // Jarate
	case 61:  return 1006;  // Ambassador
	case 735: return 1080;  // Sapper
	default:  return 0;
	}
}

// ─── CSkinChanger ─────────────────────────────────────────────────────────────

void CSkinChanger::RedirectIndex(int& nDefIndex)
{
	using namespace SkinWeaponID;
#define REDIR(from, to) case from: nDefIndex = to; return;
	switch (nDefIndex)
	{
		REDIR(Scattergun,      ScattergunR)
		REDIR(RocketLauncher,  RocketLauncherR)
		REDIR(FlameThrower,    FlameThrowerR)
		REDIR(GrenadeLauncher, GrenadeLauncherR)
		REDIR(StickyLauncher,  StickyLauncherR)
		REDIR(Minigun,         MinigunR)
		REDIR(Wrench,          WrenchR)
		REDIR(EngieShot,       EngieShotR)
		REDIR(EngiePistol,     EngiePistolR)
		REDIR(MediGun,         MediGunR)
		REDIR(SniperRifle,     SniperRifleR)
		REDIR(SMG,             SMGR)
		REDIR(Knife,           KnifeR)
		REDIR(Revolver,        RevolverR)
		REDIR(Bat,             BatR)
		REDIR(Shovel,          ShovelR)
		REDIR(FireAxe,         FireAxeR)
		REDIR(Bottle,          BottleR)
		REDIR(Bonesaw,         BonesawR)
		REDIR(Kukri,           KukriR)
		REDIR(SoldierShot,     SoldierShotR)
		REDIR(PyroShot,        PyroShotR)
		REDIR(HeavyShot,       HeavyShotR)
	default: break;
	}
#undef REDIR
}

void CSkinChanger::ApplySkin(CTFWeaponBase* pWeapon)
{
	if (!pWeapon)
		return;

	int& nDefIndex  = pWeapon->m_iItemDefinitionIndex();
	int nRedirected = nDefIndex;
	RedirectIndex(nRedirected);

	// Config is always keyed under the redirected DefIndex.
	auto it = m_mSkins.find(nRedirected);
	if (it == m_mSkins.end())
	{
		// DefIndex was already changed to the reskin value by a previous frame (server delta
		// compression doesn't re-send unchanged fields, so our write persists).  Track it.
		if (m_sKnownReskinDefIndices.count(nDefIndex))
			m_sReskinnedEntities.insert(pWeapon->entindex());
		return;
	}

	const SkinConfig_t& cfg = it->second;
	if (cfg.m_vAttributes.empty() && !cfg.m_nReskinDefIndex)
		return;

	// Scan attrs to decide what companion attrs to auto-inject.
	bool bHasPaintkit = false;
	bool bHasSeedLo   = false;
	bool bHasSeedHi   = false;
	bool bHasInspect  = false;
	bool bHasWear     = false;
	bool bFestive     = false;
	bool bFestivized  = false;
	int nUnusualDynamic = 0;
	int nUnusualStatic = 0;
	for (const auto& a : cfg.m_vAttributes)
	{
		if (a.m_nIndex == SkinAttr::PaintkitProtoDef)   bHasPaintkit = true;
		if (a.m_nIndex == SkinAttr::PaintkitSeedLo)     bHasSeedLo   = true;
		if (a.m_nIndex == SkinAttr::PaintkitSeedHi)     bHasSeedHi   = true;
		if (a.m_nIndex == SkinAttr::WeaponAllowInspect) bHasInspect  = true;
		if (a.m_nIndex == SkinAttr::SetWear)             bHasWear     = true;
		if (a.m_nIndex == SkinAttr::IsFestive)           bFestive     = true;
		if (a.m_nIndex == SkinAttr::IsFestivized)        bFestivized  = true;
		if (a.m_nIndex == SkinAttr::SetParticle)         nUnusualDynamic = FloatAsInt(a.m_flValue);
		if (a.m_nIndex == SkinAttr::SetParticleStatic)   nUnusualStatic = FloatAsInt(a.m_flValue);
	}

	// Defindex priority: festive > reskin > redirected paintable variant.
	if (bFestive)
	{
		int nFestive = GetFestiveDefIndex(nRedirected);
		nDefIndex = (nFestive > 0) ? nFestive : nRedirected;
	}
	else if (cfg.m_nReskinDefIndex)
	{
		nDefIndex = cfg.m_nReskinDefIndex;
		m_sReskinnedEntities.insert(pWeapon->entindex());

		// Apply left-hand flip for reskins that render that way (e.g. The Original).
		int nReskinCount = 0;
		const ReskinEntry* pReskins = GetReskinOptions(nRedirected, nReskinCount);
		// Flip is intentionally not applied — DrawOverriddenViewmodel suppression
		// means the CTFViewModel base draw handles orientation natively.
	}
	else
	{
		nDefIndex = nRedirected;
	}

	// Pure reskin with no attributes — defindex swap is all we need.
	if (cfg.m_vAttributes.empty())
		return;

	auto pAttrList = reinterpret_cast<GameCAttributeList_t*>(
		reinterpret_cast<std::uintptr_t>(pWeapon) + GetAttrListOffset());

	// Skip re-injection when attrs are already present and config hasn't changed.
	// m_sDirtyWeapons is set on every SetAttribute/RemoveAttribute call.
	const bool bDirty = m_sDirtyWeapons.erase(nRedirected) > 0;
	const int nCountBefore = pAttrList->Count();
	if (!bDirty && nCountBefore > PreFilledAttrCount(nDefIndex))
		return;

	SDK::Output("SkinChanger",
		std::format("ApplySkin wep={} redirected={} dirty={} count_before={} unusual_dyn={} unusual_static={}",
			nDefIndex, nRedirected, bDirty ? 1 : 0, nCountBefore, nUnusualDynamic, nUnusualStatic).c_str(),
		{ 140, 190, 255, 255 }, OUTPUT_DEBUG);

	// Warpaints require seed and inspect attrs; inject defaults when missing.
	if (bHasPaintkit)
	{
		if (!bHasSeedLo)  pAttrList->SetAttr(SkinAttr::PaintkitSeedLo,     0.f);
		if (!bHasSeedHi)  pAttrList->SetAttr(SkinAttr::PaintkitSeedHi,     0.f);
		if (!bHasInspect) pAttrList->SetAttr(SkinAttr::WeaponAllowInspect,  1.f);
		if (!bHasWear)    pAttrList->SetAttr(SkinAttr::SetWear,             0.2f);
	}

	for (const auto& attr : cfg.m_vAttributes)
	{
		// IsFestive is a synthetic flag for defindex swap, not a real attribute.
		if (attr.m_nIndex == SkinAttr::IsFestive)
			continue;
		pAttrList->SetAttr(attr.m_nIndex, attr.m_flValue);
	}
}

void CSkinChanger::ApplySkins()
{
	if (!m_bInitialized)
	{
		Load();
		m_bInitialized = true;
	}

	// H::Entities.GetLocal() returns null here because Store() hasn't run yet
	// (POSTDATAUPDATE_START fires before FRAME_NET_UPDATE_END where Store() runs).
	// Bypass the cache and get the local player directly from the engine.
	const int nLocalIdx = I::EngineClient->GetLocalPlayer();
	if (nLocalIdx <= 0)
		return;
	auto pLocalEntity = I::ClientEntityList->GetClientEntity(nLocalIdx);
	if (!pLocalEntity)
		return;
	auto pLocal = pLocalEntity->As<CTFPlayer>();
	if (!pLocal)
		return;

	m_sReskinnedEntities.clear();

	if (m_mSkins.empty())
		return;

	bool bHasKillstreakSkin = false;
	for (int i = 0; i < MAX_WEAPONS; i++)
	{
		auto pWeapon = pLocal->GetWeaponFromSlot(i);
		if (!pWeapon)
			continue;
		ApplySkin(pWeapon);

		// Check if any weapon has a killstreak sheen/effect configured.
		if (!bHasKillstreakSkin)
		{
			int nDef = pWeapon->m_iItemDefinitionIndex();
			RedirectIndex(nDef);
			auto it = m_mSkins.find(nDef);
			if (it != m_mSkins.end())
			{
				for (const auto& a : it->second.m_vAttributes)
				{
					if (a.m_nIndex == SkinAttr::KillstreakTier && a.m_flValue >= 2.f)
					{
						bHasKillstreakSkin = true;
						break;
					}
				}
			}
		}
	}

	// Force a minimum killstreak count so sheens/effects are visible without needing kills.
	if (bHasKillstreakSkin)
	{
		if (pLocal->m_nStreaks(kTFStreak_Kills) < 5)
			pLocal->m_nStreaks(kTFStreak_Kills) = 5;
		if (pLocal->m_nStreaks(kTFStreak_KillsAll) < 5)
			pLocal->m_nStreaks(kTFStreak_KillsAll) = 5;
	}

	// Track whether the active weapon is a visual reskin (used by DrawModelExecute
	// to know when to suppress the stock CTFViewModel draw).
	m_bActiveWeaponReskinned = false;

	if (pLocal->m_hActiveWeapon().IsValid())
	{
		auto pActNet = I::ClientEntityList->GetClientNetworkableFromHandle(pLocal->m_hActiveWeapon());
		if (pActNet)
			m_bActiveWeaponReskinned = m_sReskinnedEntities.count(pActNet->entindex()) > 0;
	}
}

void CSkinChanger::UpdateViewmodels(CTFPlayer* pLocal)
{
	if (!pLocal || !pLocal->IsAlive())
		return;

	auto pWeapon = pLocal->m_hActiveWeapon().Get() ? pLocal->m_hActiveWeapon().Get()->As<CTFWeaponBase>() : nullptr;
	if (!pWeapon)
		return;

	const int nEntIndex = pWeapon->entindex();
	const int nDefIndex = pWeapon->m_iItemDefinitionIndex();

	// only care about weapons we reskinned (and revert ones we tracked before)
	auto it = m_mLastVMDefIndex.find(nEntIndex);
	const bool bTracked = it != m_mLastVMDefIndex.end();
	if (!bTracked && !m_sReskinnedEntities.count(nEntIndex))
		return;
	if (bTracked && it->second == nDefIndex)
		return;

	m_mLastVMDefIndex[nEntIndex] = nDefIndex;

	// The game re-applies the swapped item's model to the viewmodel every frame (the econ
	// override inside C_TFViewModel::DrawModel) but never re-runs deploy, so sequences and
	// arms stay from the old weapon and the model floats. Same-weapon reselect is skipped
	// by the game, so bounce the selection through another weapon to force two real deploys.
	if (m_nRedeployStage)
		return;

	for (int iSlot = 0; iSlot <= SLOT_MELEE; iSlot++)
	{
		auto pOther = pLocal->GetWeaponFromSlot(iSlot);
		if (pOther && pOther != pWeapon)
		{
			m_nRedeployOther = pOther->entindex();
			m_nRedeployTarget = nEntIndex;
			m_nRedeployStage = 1;
			return;
		}
	}
}

void CSkinChanger::SetAttribute(int nWeaponIndex, uint16_t nAttrIndex, float flValue)
{
	if (nWeaponIndex < 0)
		return;

	// Normalize: always store under the paintable-variant ID so ApplySkin() can
	// find the config regardless of whether the entity's def index has already
	// been redirected or not.
	RedirectIndex(nWeaponIndex);

	// Attributes marked stored_as_integer in the schema must be bit-encoded.
	if (nAttrIndex == SkinAttr::PaintkitProtoDef
	 || nAttrIndex == SkinAttr::PaintkitSeedLo
	 || nAttrIndex == SkinAttr::PaintkitSeedHi
	 || nAttrIndex == SkinAttr::LootRarity)
		flValue = IntAsFloat(static_cast<int>(flValue));

	if (nAttrIndex == SkinAttr::IsAustralium)
		flValue = IntAsFloat(static_cast<int>(flValue));

	auto& cfg = m_mSkins[nWeaponIndex];

	for (auto& attr : cfg.m_vAttributes)
	{
		if (attr.m_nIndex == nAttrIndex)
		{
			attr.m_flValue = flValue;
			m_sDirtyWeapons.insert(nWeaponIndex);
			I::ClientState->ForceFullUpdate();
			return;
		}
	}

	cfg.m_vAttributes.push_back({ nAttrIndex, flValue });
	m_sDirtyWeapons.insert(nWeaponIndex);
	I::ClientState->ForceFullUpdate();
}

void CSkinChanger::RemoveAttribute(int nWeaponIndex, uint16_t nAttrIndex)
{
	RedirectIndex(nWeaponIndex);
	auto it = m_mSkins.find(nWeaponIndex);
	if (it == m_mSkins.end())
		return;

	auto& attrs = it->second.m_vAttributes;
	for (auto jt = attrs.begin(); jt != attrs.end(); ++jt)
	{
		if (jt->m_nIndex == nAttrIndex)
		{
			attrs.erase(jt);
			m_sDirtyWeapons.insert(nWeaponIndex);
			I::ClientState->ForceFullUpdate();
			return;
		}
	}
}

void CSkinChanger::ClearWeapon(int nWeaponIndex)
{
	RedirectIndex(nWeaponIndex);
	auto it = m_mSkins.find(nWeaponIndex);
	if (it == m_mSkins.end())
		return;
	if (it->second.m_nReskinDefIndex)
		m_sKnownReskinDefIndices.erase(it->second.m_nReskinDefIndex);
	m_mSkins.erase(it);
	I::ClientState->ForceFullUpdate();
}

const SkinConfig_t* CSkinChanger::GetConfig(int nWeaponIndex) const
{
	RedirectIndex(nWeaponIndex);
	auto it = m_mSkins.find(nWeaponIndex);
	return (it != m_mSkins.end()) ? &it->second : nullptr;
}

void CSkinChanger::SetReskin(int nWeaponIndex, int nReskinDefIndex)
{
	RedirectIndex(nWeaponIndex);
	int nOld = m_mSkins[nWeaponIndex].m_nReskinDefIndex;
	m_mSkins[nWeaponIndex].m_nReskinDefIndex = nReskinDefIndex;
	if (nOld && nOld != nReskinDefIndex)
		m_sKnownReskinDefIndices.erase(nOld);
	if (nReskinDefIndex)
		m_sKnownReskinDefIndices.insert(nReskinDefIndex);
	m_sDirtyWeapons.insert(nWeaponIndex);
}

void CSkinChanger::RemoveReskin(int nWeaponIndex)
{
	RedirectIndex(nWeaponIndex);
	auto it = m_mSkins.find(nWeaponIndex);
	if (it == m_mSkins.end())
		return;
	m_sKnownReskinDefIndices.erase(it->second.m_nReskinDefIndex);
	it->second.m_nReskinDefIndex = 0;
	m_sDirtyWeapons.insert(nWeaponIndex);
	if (it->second.m_vAttributes.empty())
		m_mSkins.erase(it);
}

void CSkinChanger::SetReskinAllMelees(int nReskinDefIndex)
{
	using namespace SkinWeaponID;
	// All redirected melee defindexes across every class.
	static constexpr int kMelees[] = { BatR, ShovelR, FireAxeR, BottleR, BonesawR, KukriR };
	for (int nDef : kMelees)
		SetReskin(nDef, nReskinDefIndex);
}

void CSkinChanger::ClearAllReskins()
{
	m_sKnownReskinDefIndices.clear();
	for (auto it = m_mSkins.begin(); it != m_mSkins.end(); )
	{
		it->second.m_nReskinDefIndex = 0;
		m_sDirtyWeapons.insert(it->first);
		if (it->second.m_vAttributes.empty())
			it = m_mSkins.erase(it);
		else
			++it;
	}
}

int CSkinChanger::GetReskin(int nWeaponIndex) const
{
	RedirectIndex(nWeaponIndex);
	auto it = m_mSkins.find(nWeaponIndex);
	return (it != m_mSkins.end()) ? it->second.m_nReskinDefIndex : 0;
}

int CSkinChanger::GetUnusualEffect(int nWeaponIndex) const
{
	RedirectIndex(nWeaponIndex);
	auto it = m_mSkins.find(nWeaponIndex);
	if (it == m_mSkins.end())
		return 0;
	for (const auto& attr : it->second.m_vAttributes)
	{
		if (attr.m_nIndex == SkinAttr::SetParticle)
			return static_cast<int>(attr.m_flValue);
	}
	return 0;
}

bool CSkinChanger::GetReskinFlip(int nWeaponIndex) const
{
	int nReskin = GetReskin(nWeaponIndex);
	if (!nReskin)
		return false;

	// Look up the flip flag from the reskin table for the redirected weapon.
	int nRedirected = nWeaponIndex;
	RedirectIndex(nRedirected);
	int nCount = 0;
	const ReskinEntry* pEntries = GetReskinOptions(nRedirected, nCount);
	for (int i = 0; i < nCount; i++)
	{
		if (pEntries[i].nDefIndex == nReskin)
			return pEntries[i].bFlip;
	}
	return false;
}

// ─── Save / Load (skins.json) ─────────────────────────────────────────────────
// Stored alongside Amalgam's main configs.
static std::string GetSkinsPath()
{
	return F::Configs.m_sConfigPath + "skins.json";
}

void CSkinChanger::Save()
{
	std::ofstream file(GetSkinsPath());
	if (!file)
		return;

	file << "{\n";
	bool bFirstWep = true;
	for (const auto& [nIndex, cfg] : m_mSkins)
	{
		if (nIndex < 0 || (cfg.m_vAttributes.empty() && !cfg.m_nReskinDefIndex))
			continue;
		if (!bFirstWep)
			file << ",\n";
		bFirstWep = false;
		file << '\t' << '"' << nIndex << "\": {";
		bool bFirstAttr = true;
		if (cfg.m_nReskinDefIndex)
		{
			file << "\"reskin\": " << cfg.m_nReskinDefIndex;
			bFirstAttr = false;
		}
		for (const auto& attr : cfg.m_vAttributes)
		{
			if (!bFirstAttr)
				file << ", ";
			bFirstAttr = false;
			file << '"' << attr.m_nIndex << "\": " << attr.m_flValue;
		}
		file << '}';
	}
	file << "\n}\n";
}

void CSkinChanger::Load()
{
	std::ifstream file(GetSkinsPath());
	if (!file)
		return;

	m_mSkins.clear();

	std::string s((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	size_t pos = 0;

	auto skipWs = [&]() { while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\r' || s[pos] == '\n')) ++pos; };
	auto readInt = [&]() -> int { skipWs(); int v = 0; while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') v = v * 10 + (s[pos++] - '0'); return v; };
	auto readFloat = [&]() -> float { skipWs(); char* end = nullptr; float v = std::strtof(s.data() + pos, &end); if (end) pos = static_cast<size_t>(end - s.data()); return v; };

	try
	{
		skipWs(); if (pos >= s.size() || s[pos++] != '{') return;
		while (pos < s.size())
		{
			skipWs();
			if (pos >= s.size() || s[pos] == '}') break;
			if (s[pos] == ',') { ++pos; continue; }
			if (s[pos] != '"') { ++pos; continue; }
			++pos;
			int nWep = readInt();
			if (pos < s.size() && s[pos] == '"') ++pos;
			skipWs(); if (pos < s.size() && s[pos] == ':') ++pos;
			skipWs(); if (pos < s.size() && s[pos] == '{') ++pos;
			while (pos < s.size())
			{
				skipWs();
				if (pos >= s.size() || s[pos] == '}') { if (pos < s.size()) ++pos; break; }
				if (s[pos] == ',') { ++pos; continue; }
				if (s[pos] != '"') { ++pos; continue; }
				++pos;
				if (pos < s.size() && !std::isdigit(static_cast<unsigned char>(s[pos])))
				{
					// Text key (e.g. "reskin")
					std::string sKey;
					while (pos < s.size() && s[pos] != '"') sKey += s[pos++];
					if (pos < s.size() && s[pos] == '"') ++pos;
					skipWs(); if (pos < s.size() && s[pos] == ':') ++pos;
					if (sKey == "reskin")
					{
						skipWs();
						int nReskin = readInt();
						if (nWep >= 0)
							m_mSkins[nWep].m_nReskinDefIndex = nReskin;
					}
					continue;
				}
				int nAttr = readInt();
				if (pos < s.size() && s[pos] == '"') ++pos;
				skipWs(); if (pos < s.size() && s[pos] == ':') ++pos;
				float flVal = readFloat();
				if (nWep >= 0)
					m_mSkins[nWep].m_vAttributes.push_back({ static_cast<uint16_t>(nAttr), flVal });
			}
		}
	}
	catch (...) {}

	// Mark every loaded weapon dirty so ApplySkin injects on the first frame.
	// Also rebuild the reverse reskin lookup.
	m_sKnownReskinDefIndices.clear();
	for (const auto& [nIdx, cfg] : m_mSkins)
	{
		m_sDirtyWeapons.insert(nIdx);
		if (cfg.m_nReskinDefIndex)
			m_sKnownReskinDefIndices.insert(cfg.m_nReskinDefIndex);
	}
}
