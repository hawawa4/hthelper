// Minimal subset of the Raidcore Nexus addon API (API revision 6), mirroring
// the layout of Nexus/Host/Addons/API/ApiV6.h and Definitions/AddonDefV1.h in
// https://github.com/RaidcoreGG/Nexus. Only the fields this addon uses are
// typed; the rest are kept as opaque pointers so the offsets stay correct.
// Fields after DataLink are omitted entirely since we only ever read through
// a pointer handed to us by Nexus.
#pragma once

#include <cstddef>
#include <cstdint>

struct ImGuiContext;

#define NEXUS_API_VERSION 6

enum class ERenderType : uint32_t
{
	PreRender,
	Render,
	PostRender,
	OptionsRender
};

enum class ELogLevel : uint32_t
{
	OFF      = 0,
	CRITICAL = 1,
	WARNING  = 2,
	INFO     = 3,
	DEBUG    = 4,
	TRACE    = 5,
	ALL
};

enum class EAddonFlags : uint32_t
{
	None              = 0,
	IsVolatile        = 1 << 0,
	DisableHotloading = 1 << 1,
	LaunchOnly        = 1 << 2
};

enum class EUpdateProvider : uint32_t
{
	None,
	Raidcore,
	GitHub,
	Direct,
	Self
};

typedef void  (*GUI_RENDER)       ();
typedef void  (*EVENT_CONSUME)    (void* aEventArgs);
typedef void  (*GUI_ADDRENDER)    (ERenderType aRenderType, GUI_RENDER aRenderCallback);
typedef void  (*GUI_REMRENDER)    (GUI_RENDER aRenderCallback);
typedef void  (*LOGGER_LOG2)      (ELogLevel aLogLevel, const char* aChannel, const char* aStr);
typedef void  (*EVENTS_SUBSCRIBE) (const char* aIdentifier, EVENT_CONSUME aConsumeEventCallback);
typedef void* (*DATALINK_GET)     (const char* aIdentifier);
typedef const char* (*PATHS_GETADDONDIR)(const char* aName);

struct ImFont;
struct ImFontConfig;
typedef void (*FONTS_RECEIVECALLBACK)(const char* aIdentifier, ImFont* aFont);
typedef void (*FONTS_GETRELEASE)     (const char* aIdentifier, FONTS_RECEIVECALLBACK aCallback);
typedef void (*FONTS_ADDFROMMEMORY)  (const char* aIdentifier, float aFontSize, void* aData, size_t aSize, FONTS_RECEIVECALLBACK aCallback, ImFontConfig* aConfig);
typedef void (*FONTS_RESIZE)         (const char* aIdentifier, float aFontSize);

struct AddonAPI_t
{
	/* Renderer */
	void*         SwapChain;
	ImGuiContext* ImguiContext;
	void*         ImguiMalloc;
	void*         ImguiFree;
	struct { GUI_ADDRENDER Register; GUI_REMRENDER Deregister; } Renderer;

	/* Updater */
	void*         RequestUpdate;

	/* Logging */
	LOGGER_LOG2   Log;

	/* UI: SendAlert, RegisterCloseOnEscape, DeregisterCloseOnEscape */
	void*         UI[3];

	/* Paths */
	struct
	{
		void*              GetGameDirectory;
		PATHS_GETADDONDIR  GetAddonDirectory;
		void*              GetCommonDirectory;
	} Paths;

	/* MinHook: Create, Remove, Enable, Disable */
	void*         MinHook[4];

	/* Events */
	struct
	{
		void*            Raise;
		void*            RaiseNotification;
		void*            RaiseTargeted;
		void*            RaiseNotificationTargeted;
		EVENTS_SUBSCRIBE Subscribe;
		EVENTS_SUBSCRIBE Unsubscribe;
	} Events;

	/* WndProc: Register, Deregister, SendToGameOnly */
	void*         WndProc[3];

	/* InputBinds: Invoke, RegisterWithString, RegisterWithStruct, Deregister */
	void*         InputBinds[4];

	/* GameBinds: PressAsync, ReleaseAsync, InvokeAsync, Press, Release, IsBound */
	void*         GameBinds[6];

	/* DataLink */
	struct { DATALINK_GET Get; void* Share; } DataLink;

	/* Textures: Get, GetOrCreateFromFile/Resource/URL/Memory, LoadFromFile/Resource/URL/Memory */
	void*         Textures[9];

	/* QuickAccess: Add, Remove, Notify, AddContextMenu, RemoveContextMenu */
	void*         QuickAccess[5];

	/* Localization: Translate, TranslateTo, SetTranslatedString */
	void*         Localization[3];

	/* Fonts */
	struct
	{
		FONTS_GETRELEASE    Get;
		FONTS_GETRELEASE    Release;
		void*               AddFromFile;
		void*               AddFromResource;
		FONTS_ADDFROMMEMORY AddFromMemory;
		FONTS_RESIZE        Resize;
	} Fonts;
};

typedef void (*ADDON_LOAD)   (AddonAPI_t* aAPI);
typedef void (*ADDON_UNLOAD) ();

struct AddonVersion_t
{
	uint16_t Major;
	uint16_t Minor;
	uint16_t Build;
	uint16_t Revision;
};

struct AddonDefinition_t
{
	uint32_t        Signature;
	uint32_t        APIVersion;
	const char*     Name;
	AddonVersion_t  Version;
	const char*     Author;
	const char*     Description;
	ADDON_LOAD      Load;
	ADDON_UNLOAD    Unload;
	EAddonFlags     Flags;
	EUpdateProvider Provider;
	const char*     UpdateLink;
};

/* ---- ArcDPS combat events, forwarded by Nexus' ArcDPS bridge ---- */

struct ArcAgent_t
{
	char*     Name;
	uintptr_t ID;
	uint32_t  Profession;
	uint32_t  Elite;
	uint32_t  Self;
	uint16_t  Team;
};

struct ArcCombatEvent_t
{
	uint64_t Time;
	uint64_t SrcAgent;
	uint64_t DstAgent;
	int32_t  Value;
	int32_t  BuffDmg;
	uint32_t OverstackValue;
	uint32_t SkillID;
	uint16_t SrcInstID;
	uint16_t DstInstID;
	uint16_t SrcMasterInstID;
	uint16_t DstMasterInstID;
	uint8_t  IFF;
	uint8_t  Buff;
	uint8_t  Result;
	uint8_t  IsActivation;
	uint8_t  IsBuffRemove;
	uint8_t  IsNinety;
	uint8_t  IsFifty;
	uint8_t  IsMoving;
	uint8_t  IsStatechange;
	uint8_t  IsFlanking;
	uint8_t  IsShields;
	uint8_t  IsOffcycle;
	uint8_t  Pad[4];
};

struct EvCombatData_t
{
	ArcCombatEvent_t* ev;
	ArcAgent_t*       src;
	ArcAgent_t*       dst;
	char*             skillname;
	uint64_t          id;
	uint64_t          revision;
};

enum ECombatStateChange : uint8_t
{
	CBTS_NONE        = 0,
	CBTS_ENTERCOMBAT = 1,
	CBTS_EXITCOMBAT  = 2,
	CBTS_CHANGEUP    = 3,
	CBTS_CHANGEDEAD  = 4,
	CBTS_CHANGEDOWN  = 5,
	CBTS_SPAWN       = 6,
	CBTS_DESPAWN     = 7
};

/* ---- MumbleLink (DL_MUMBLE_LINK) ---- */

struct MumbleContext_t
{
	uint8_t  ServerAddress[28];
	uint32_t MapID;
	uint32_t MapType;
	uint32_t ShardID;
	uint32_t Instance;
	uint32_t BuildID;
	/* ... more fields follow; unused. */
};

struct MumbleLinkedMem_t
{
	uint32_t        UIVersion;
	uint32_t        UITick;
	float           AvatarPosition[3];
	float           AvatarFront[3];
	float           AvatarTop[3];
	wchar_t         Name[256];
	float           CameraPosition[3];
	float           CameraFront[3];
	float           CameraTop[3];
	wchar_t         Identity[256];
	uint32_t        ContextLength;
	union
	{
		MumbleContext_t Context;
		uint8_t         ContextRaw[256];
	};
	wchar_t         Description[2048];
};

/* Layout checks against the real Nexus / ArcDPS / MumbleLink structs (x64). */
#include <cstddef>
static_assert(offsetof(AddonAPI_t, Renderer)   == 4 * 8,  "AddonAPI_t layout");
static_assert(offsetof(AddonAPI_t, Log)        == 7 * 8,  "AddonAPI_t layout");
static_assert(offsetof(AddonAPI_t, Events)     == 18 * 8, "AddonAPI_t layout");
static_assert(offsetof(AddonAPI_t, DataLink)   == 37 * 8, "AddonAPI_t layout");
static_assert(offsetof(AddonAPI_t, Fonts)      == 56 * 8, "AddonAPI_t layout");
static_assert(sizeof(AddonDefinition_t)        == 72,     "AddonDefinition_t layout");
static_assert(sizeof(ArcCombatEvent_t)         == 64,     "cbtevent layout");
static_assert(offsetof(MumbleLinkedMem_t, Context) == 1108, "LinkedMem layout");
