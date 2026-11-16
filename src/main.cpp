// HTCM Helper - a Nexus addon for the Harvest Temple CM strike.
//
// v0.1: when your character dies, flash "LEAVE THE INSTANCE NOW" across the
// screen in big red letters.
//
// Death detection uses ArcDPS combat events, which Nexus forwards as
// EV_ARCDPS_COMBATEVENT_LOCAL_RAW (requires ArcDPS to be installed). The map
// filter and reset-on-map-change use MumbleLink via Nexus' DL_MUMBLE_LINK.

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "imgui.h"
#include "Nexus.h"
#include "font_anton.h"

namespace
{
	constexpr const char* ADDON_NAME     = "HTCM Helper";
	constexpr uint32_t    HARVEST_TEMPLE = 1437; // Harvest Temple strike map ID

	AddonAPI_t*        APIDefs = nullptr;
	MumbleLinkedMem_t* Mumble  = nullptr;

	std::atomic<bool>      IsDead{ false };
	uint32_t               LastMapID = 0;

	// Self agent ID, learned from ArcDPS agent-tracking notifications. Backup
	// in case ag::self isn't set on a statechange event.
	std::atomic<uintptr_t> SelfAgentID{ 0 };

	// Diagnostics, shown in the options panel.
	std::atomic<uint32_t>  EventsLocal{ 0 };
	std::atomic<uint32_t>  EventsSquad{ 0 };
	std::atomic<uint32_t>  SelfStatechanges{ 0 };
	std::atomic<int>       LastSelfStatechange{ -1 };
	std::atomic<uint32_t>  DeathsSeen{ 0 };

	// Options (not persisted yet).
	bool OnlyInHarvestTemple = true;
	bool Preview             = false;

	void Log(ELogLevel aLevel, const char* aMsg)
	{
		if (APIDefs && APIDefs->Log) { APIDefs->Log(aLevel, ADDON_NAME, aMsg); }
	}

	uint32_t CurrentMapID()
	{
		return Mumble ? Mumble->Context.MapID : 0;
	}

	void HandleCombatEvent(EvCombatData_t* data)
	{
		if (!data) { return; }

		// ev == nullptr: agent tracking notification. src->elite == 0 && src->prof != 0
		// means "agent added"; dst->self marks the local player; src->id is its agent ID.
		if (!data->ev)
		{
			if (data->src && data->dst && data->src->Elite == 0 && data->src->Profession != 0 && data->dst->Self)
			{
				SelfAgentID = data->src->ID;
			}
			return;
		}

		if (!data->ev->IsStatechange) { return; }

		const uintptr_t self = SelfAgentID;
		const bool isSelf = (data->src && data->src->Self)
			|| (self != 0 && data->ev->SrcAgent == self);
		if (!isSelf) { return; }

		SelfStatechanges++;
		LastSelfStatechange = data->ev->IsStatechange;

		switch (data->ev->IsStatechange)
		{
			case CBTS_CHANGEDEAD:
				IsDead = true;
				DeathsSeen++;
				Log(ELogLevel::INFO, "Player died.");
				break;
			case CBTS_CHANGEUP:
			case CBTS_SPAWN:
				IsDead = false;
				break;
			default:
				break;
		}
	}

	void OnCombatLocal(void* aEventArgs)
	{
		EventsLocal++;
		HandleCombatEvent(static_cast<EvCombatData_t*>(aEventArgs));
	}

	void OnCombatSquad(void* aEventArgs)
	{
		EventsSquad++;
		HandleCombatEvent(static_cast<EvCombatData_t*>(aEventArgs));
	}

	const char* StatechangeName(int aState)
	{
		switch (aState)
		{
			case -1:               return "(none yet)";
			case CBTS_ENTERCOMBAT: return "ENTERCOMBAT";
			case CBTS_EXITCOMBAT:  return "EXITCOMBAT";
			case CBTS_CHANGEUP:    return "CHANGEUP";
			case CBTS_CHANGEDEAD:  return "CHANGEDEAD";
			case CBTS_CHANGEDOWN:  return "CHANGEDOWN";
			case CBTS_SPAWN:       return "SPAWN";
			case CBTS_DESPAWN:     return "DESPAWN";
			default:               return "other";
		}
	}

	/* ---- Fonts ----
	 * Anton (SIL OFL), subset to printable ASCII so even large sizes keep the
	 * shared Nexus font atlas small. Registered through Nexus' font API, which
	 * copies the data and rebuilds the atlas; we get ImFont* via callback. */

	constexpr const char* TITLE_TEXT = "LEAVE THE INSTANCE NOW";
	constexpr const char* SUB_TEXT   = "You are dead.";
	constexpr const char* NOTE_TEXT  = "Also buff trouba plz";

	enum EFont { FONT_TITLE, FONT_SUB, FONT_NOTE, FONT_COUNT };

	struct ManagedFont
	{
		const char* ID;
		ImFont*     Font;      // nullptr until Nexus has built it
		float       Requested; // pixel size we asked Nexus to rasterize at
	};

	ManagedFont Fonts[FONT_COUNT] = {
		{ "HTCM_HELPER_FONT_TITLE", nullptr, 0.0f },
		{ "HTCM_HELPER_FONT_SUB",   nullptr, 0.0f },
		{ "HTCM_HELPER_FONT_NOTE",  nullptr, 0.0f },
	};

	constexpr float TITLE_MAX_PX = 200.0f;

	void OnFontReceived(const char* aIdentifier, ImFont* aFont)
	{
		if (!aIdentifier) { return; }
		for (ManagedFont& f : Fonts)
		{
			if (strcmp(f.ID, aIdentifier) == 0) { f.Font = aFont; }
		}
	}

	void DesiredFontSizes(ImVec2 aScreen, float aOut[FONT_COUNT])
	{
		// Title width per pixel of font size; measured from the real font once
		// it's loaded, otherwise a guess for Anton.
		float widthPerPx = 9.0f;
		if (ImFont* f = Fonts[FONT_TITLE].Font)
		{
			widthPerPx = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0.0f, TITLE_TEXT).x / f->FontSize;
		}

		float title = std::round(aScreen.x * 0.8f / (std::max)(1.0f, widthPerPx));
		title = (std::min)((std::max)(title, 24.0f), TITLE_MAX_PX);

		aOut[FONT_TITLE] = title;
		aOut[FONT_SUB]   = std::round(title * 0.4f);
		aOut[FONT_NOTE]  = std::round((std::max)(18.0f, aScreen.y * 0.025f));
	}

	void LoadFonts()
	{
		ImVec2 screen = ImGui::GetIO().DisplaySize;
		if (screen.x <= 0 || screen.y <= 0) { screen = ImVec2(1920, 1080); }

		float sizes[FONT_COUNT];
		DesiredFontSizes(screen, sizes);

		// Big glyphs don't need horizontal oversampling; it would triple atlas usage.
		ImFontConfig cfg;
		cfg.OversampleH = 1;
		cfg.OversampleV = 1;
		cfg.PixelSnapH  = true;

		for (int i = 0; i < FONT_COUNT; i++)
		{
			Fonts[i].Requested = sizes[i];
			APIDefs->Fonts.AddFromMemory(Fonts[i].ID, sizes[i],
				(void*)AntonAscii_ttf, AntonAscii_ttf_len, OnFontReceived, &cfg);
		}
	}

	void UnloadFonts()
	{
		for (ManagedFont& f : Fonts)
		{
			APIDefs->Fonts.Release(f.ID, OnFontReceived);
			f.Font = nullptr;
		}
	}

	void UpdateFontSizes(ImVec2 aScreen)
	{
		if (aScreen.x <= 0 || aScreen.y <= 0) { return; }

		float sizes[FONT_COUNT];
		DesiredFontSizes(aScreen, sizes);

		for (int i = 0; i < FONT_COUNT; i++)
		{
			if (std::fabs(sizes[i] - Fonts[i].Requested) >= 2.0f)
			{
				Fonts[i].Requested = sizes[i];
				APIDefs->Fonts.Resize(Fonts[i].ID, sizes[i]);
			}
		}
	}

	void DrawOutlinedText(ImDrawList* aDL, ImFont* aFont, float aSize, ImVec2 aPos, ImU32 aColor, ImU32 aOutline, const char* aText)
	{
		const float o = (std::max)(2.0f, aSize / 24.0f);
		for (int dx = -1; dx <= 1; dx++)
		{
			for (int dy = -1; dy <= 1; dy++)
			{
				if (dx == 0 && dy == 0) { continue; }
				aDL->AddText(aFont, aSize, ImVec2(aPos.x + dx * o, aPos.y + dy * o), aOutline, aText);
			}
		}
		aDL->AddText(aFont, aSize, aPos, aColor, aText);
	}

	void OnRender()
	{
		// Reset when the map changes: you left (or got kicked from) the instance.
		uint32_t mapID = CurrentMapID();
		if (mapID != LastMapID)
		{
			LastMapID = mapID;
			IsDead = false;
		}

		bool show = Preview;
		if (IsDead)
		{
			show |= !OnlyInHarvestTemple || mapID == HARVEST_TEMPLE;
		}

		ImGuiIO&    io   = ImGui::GetIO();
		const ImVec2 screen = io.DisplaySize;

		// Keep the fonts rasterized at the size this resolution needs (only
		// triggers an atlas rebuild when the resolution changes).
		UpdateFontSizes(screen);

		if (!show) { return; }

		ImDrawList* dl   = ImGui::GetForegroundDrawList();

		// Steady (no flashing): full-strength text/border, fixed tint.
		const float pulse = 0.7f;
		const float alpha = 1.0f;

		// Red screen tint + thick red border.
		dl->AddRectFilled(ImVec2(0, 0), screen, IM_COL32(180, 0, 0, (int)(70 * pulse)));
		const float border = 24.0f;
		dl->AddRect(ImVec2(border / 2, border / 2), ImVec2(screen.x - border / 2, screen.y - border / 2),
			IM_COL32(255, 0, 0, (int)(255 * alpha)), 0.0f, ImDrawCornerFlags_All, border);

		// Each text is drawn with a font rasterized at (or very near) the size
		// it's drawn at, so there's no upscaling blur/pixelation.
		ImFont* fTitle = Fonts[FONT_TITLE].Font ? Fonts[FONT_TITLE].Font : ImGui::GetFont();
		ImFont* fSub   = Fonts[FONT_SUB].Font   ? Fonts[FONT_SUB].Font   : ImGui::GetFont();
		ImFont* fNote  = Fonts[FONT_NOTE].Font  ? Fonts[FONT_NOTE].Font  : ImGui::GetFont();
		const float sTitle = Fonts[FONT_TITLE].Requested;
		const float sSub   = Fonts[FONT_SUB].Requested;
		const float sNote  = Fonts[FONT_NOTE].Requested;

		const ImVec2 ext1 = fTitle->CalcTextSizeA(sTitle, FLT_MAX, 0.0f, TITLE_TEXT);
		const ImVec2 ext2 = fSub->CalcTextSizeA(sSub, FLT_MAX, 0.0f, SUB_TEXT);

		const float y = std::floor(screen.y * 0.35f - ext1.y / 2);
		DrawOutlinedText(dl, fTitle, sTitle, ImVec2(std::floor((screen.x - ext1.x) / 2), y),
			IM_COL32(255, 20, 20, (int)(255 * alpha)), IM_COL32(0, 0, 0, (int)(255 * alpha)), TITLE_TEXT);
		DrawOutlinedText(dl, fSub, sSub, ImVec2(std::floor((screen.x - ext2.x) / 2), y + ext1.y + 8.0f),
			IM_COL32(255, 255, 255, 230), IM_COL32(0, 0, 0, 230), SUB_TEXT);

		// Small note in the bottom-left corner, inside the red border.
		const ImVec2 noteExt = fNote->CalcTextSizeA(sNote, FLT_MAX, 0.0f, NOTE_TEXT);
		const float margin   = border + 16.0f;
		DrawOutlinedText(dl, fNote, sNote, ImVec2(margin, std::floor(screen.y - margin - noteExt.y)),
			IM_COL32(255, 255, 255, 230), IM_COL32(0, 0, 0, 230), NOTE_TEXT);
	}

	void OnOptionsRender()
	{
		ImGui::Checkbox("Only show in Harvest Temple", &OnlyInHarvestTemple);
		ImGui::Checkbox("Preview overlay", &Preview);
		ImGui::TextDisabled("Requires ArcDPS for death detection.");

		ImGui::Separator();
		ImGui::Text("Diagnostics");
		ImGui::Text("Map ID: %u (Harvest Temple = %u)%s", CurrentMapID(), HARVEST_TEMPLE,
			CurrentMapID() == HARVEST_TEMPLE ? "  [match]" : "");
		ImGui::Text("MumbleLink: %s", Mumble ? "ok" : "MISSING");
		ImGui::Text("ArcDPS events received: local %u, squad %u", EventsLocal.load(), EventsSquad.load());
		ImGui::Text("Self agent ID: %llu", (unsigned long long)SelfAgentID.load());
		ImGui::Text("Self statechanges: %u, last: %s", SelfStatechanges.load(), StatechangeName(LastSelfStatechange));
		ImGui::Text("Deaths seen: %u, dead now: %s", DeathsSeen.load(), IsDead ? "YES" : "no");
	}

	void AddonLoad(AddonAPI_t* aApi)
	{
		APIDefs = aApi;

		ImGui::SetCurrentContext(aApi->ImguiContext);
		ImGui::SetAllocatorFunctions(
			(void* (*)(size_t, void*))aApi->ImguiMalloc,
			(void  (*)(void*, void*))aApi->ImguiFree);

		Mumble = static_cast<MumbleLinkedMem_t*>(aApi->DataLink.Get("DL_MUMBLE_LINK"));

		LoadFonts();

		aApi->Events.Subscribe("EV_ARCDPS_COMBATEVENT_LOCAL_RAW", OnCombatLocal);
		aApi->Events.Subscribe("EV_ARCDPS_COMBATEVENT_SQUAD_RAW", OnCombatSquad);
		aApi->Renderer.Register(ERenderType::Render, OnRender);
		aApi->Renderer.Register(ERenderType::OptionsRender, OnOptionsRender);

		Log(ELogLevel::INFO, "Loaded.");
	}

	void AddonUnload()
	{
		APIDefs->Renderer.Deregister(OnOptionsRender);
		APIDefs->Renderer.Deregister(OnRender);
		APIDefs->Events.Unsubscribe("EV_ARCDPS_COMBATEVENT_LOCAL_RAW", OnCombatLocal);
		APIDefs->Events.Unsubscribe("EV_ARCDPS_COMBATEVENT_SQUAD_RAW", OnCombatSquad);
		UnloadFonts();

		Mumble  = nullptr;
		APIDefs = nullptr;
	}

	AddonDefinition_t AddonDef{};
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID)
{
	return TRUE;
}

extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef()
{
	AddonDef.Signature   = 0x48544D43; // "HTMC" - arbitrary, must be unique among addons
	AddonDef.APIVersion  = NEXUS_API_VERSION;
	AddonDef.Name        = ADDON_NAME;
	AddonDef.Version     = { 0, 1, 4, 0 };
	AddonDef.Author      = "hawa";
	AddonDef.Description = "Harvest Temple CM helper: flashes LEAVE THE INSTANCE NOW when you die.";
	AddonDef.Load        = AddonLoad;
	AddonDef.Unload      = AddonUnload;
	AddonDef.Flags       = EAddonFlags::None;
	AddonDef.Provider    = EUpdateProvider::None;
	AddonDef.UpdateLink  = nullptr;
	return &AddonDef;
}
