// HTCM encounter tracker: works out which phase is running and when it
// started, from live ArcDPS events, and exposes upcoming callouts.
//
// Platform-independent (no Windows headers) so it can be tested natively.
// Thread-safe: OnCombatEvent is called from ArcDPS' thread, the getters from
// the render thread.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct EvCombatData_t;

namespace Encounter
{
	struct Upcoming
	{
		const char* Label;
		int32_t     InMs;      // time until it happens (negative = just happened)
		bool        BigAlert;
		int32_t     SpreadMs;
	};

	struct ActiveReactive
	{
		const char* Label;
		int32_t     RemainingMs;
	};

	struct State
	{
		int         Phase;        // index into EncounterData::PHASES, -1 = none
		const char* PhaseName;
		int32_t     ElapsedMs;    // since phase start
		std::vector<Upcoming>       Next;
		std::vector<ActiveReactive> Reactives;
	};

	struct Debug
	{
		std::string LastSync;     // what last moved the phase clock
		int32_t     LastSyncDeltaMs;
		uint32_t    Missiles, Effects, GuidMappings, Targetables, AttackTargets, OrbSpawns, Activations;
		uint32_t    TriggerHits;
	};

	/// Feed every combat event (both Nexus streams; duplicates are dropped).
	void OnCombatEvent(const EvCombatData_t* aData);

	/// Call every frame. aNowMs is on ArcDPS' clock (timeGetTime). Also resets
	/// everything when the map changes.
	void Tick(uint64_t aNowMs, uint32_t aMapID);

	/// Upcoming callouts within aHorizonMs, plus active reactive alerts.
	State GetState(uint64_t aNowMs, int32_t aHorizonMs, int aMaxItems);

	Debug GetDebug();

	/// For previewing the UI: pretend phase aPhase started at aStartMs.
	void ForcePhase(int aPhase, uint64_t aStartMs);
	void Reset();
}
