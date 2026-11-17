#include "Encounter.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "EncounterData.h"
#include "Nexus.h"

using namespace EncounterData;

namespace Encounter
{
	namespace
	{
		/* ArcDPS statechange codes used here. */
		constexpr uint8_t CBTS_ATTACKTARGET        = 23;
		constexpr uint8_t CBTS_TARGETABLE          = 24;
		constexpr uint8_t CBTS_IDTOGUID            = 46;
		constexpr uint8_t CBTS_EFFECT2             = 51;
		constexpr uint8_t CBTS_MISSILECREATE       = 57;
		constexpr uint8_t CBTS_EFFECTGROUNDCREATE  = 60;
		constexpr uint8_t CBTS_EFFECTAGENTCREATE   = 62;
		constexpr uint8_t CONTENT_EFFECT           = 0;

		constexpr uint32_t HARVEST_TEMPLE   = 1437;
		constexpr int32_t  SYNC_TOLERANCE   = 3000;   // max clock correction from one trigger
		constexpr int32_t  MAX_SYNC_SPREAD  = 600;    // callouts that vary more than this across logs don't move the clock
		constexpr int32_t  TRIGGER_DEBOUNCE = 1500;   // same trigger within this = same event
		constexpr int32_t  IDLE_RESET_MS    = 90000;  // nothing happened for this long -> reset

		std::mutex Mutex;

		int      CurPhase   = -1;
		uint64_t PhaseStart = 0;
		int      LastDragon = -1;        // last dragon phase index seen
		uint64_t LastActivity = 0;
		uint64_t LastNow = 0;            // last Tick() time
		uint32_t MapID = 0;

		std::unordered_map<uint32_t, std::array<uint8_t, 16>> EffectGuids; // effect id -> guid
		std::unordered_set<uint64_t> AttackTargetAgents;  // Dragonvoid's attack-target agent(s)
		std::unordered_set<uint64_t> SeenOrbs;
		uint64_t LastTargetableOn = 0;
		uint64_t LastTriggerTime[TRIGGER_COUNT] = {};

		struct ActiveCast { uint32_t SkillID; uint64_t SrcAgent; uint64_t EndMs; const char* Label; };
		std::vector<ActiveCast> Casts;

		/* Both Nexus streams can deliver the same event: drop repeats. */
		uint64_t Recent[128] = {};
		int      RecentPos   = 0;

		Debug Dbg{};

		uint64_t Hash(const ArcCombatEvent_t* ev)
		{
			uint64_t h = 1469598103934665603ull;
			auto mix = [&](uint64_t v) { h ^= v; h *= 1099511628211ull; };
			mix(ev->Time); mix(ev->SrcAgent); mix(ev->DstAgent); mix((uint32_t)ev->Value);
			mix(ev->SkillID); mix(ev->IsStatechange); mix(ev->IsActivation); mix(ev->IsBuffRemove); mix(ev->Buff);
			return h;
		}

		bool IsDuplicate(const ArcCombatEvent_t* ev)
		{
			const uint64_t h = Hash(ev);
			for (uint64_t r : Recent) { if (r == h) { return true; } }
			Recent[RecentPos] = h;
			RecentPos = (RecentPos + 1) % 128;
			return false;
		}

		bool PhaseHasTrigger(int aPhase, int aTrigger, int32_t* aOutOffsets, int* aOutCount, bool aStableOnly)
		{
			const Phase& p = PHASES[aPhase];
			int n = 0;
			for (int i = 0; i < p.CalloutCount; i++)
			{
				const Callout& c = p.Callouts[i];
				if ((c.Triggers[0] == aTrigger || c.Triggers[1] == aTrigger) && (!aStableOnly || c.SpreadMs <= MAX_SYNC_SPREAD))
				{
					if (aOutOffsets && n < 32) { aOutOffsets[n] = c.OffsetMs; }
					n++;
				}
			}
			if (aOutCount) { *aOutCount = (std::min)(n, 32); }
			return n > 0;
		}

		void EnterPhase(int aPhase, uint64_t aStart, const std::string& aWhy)
		{
			CurPhase   = aPhase;
			PhaseStart = aStart;
			if (PHASES[aPhase].IsDragon) { LastDragon = aPhase; }
			Dbg.LastSync = std::string("enter ") + PHASES[aPhase].Name + " (" + aWhy + ")";
			Dbg.LastSyncDeltaMs = 0;
		}

		int NextDragonPhase()
		{
			for (int i = (std::max)(LastDragon + 1, 0); i < PHASE_COUNT; i++)
			{
				if (PHASES[i].IsDragon) { return i; }
			}
			return -1;
		}

		int NextPurificationPhase()
		{
			// Purifications come after a dragon (or first, at the start).
			const int from = CurPhase >= 0 ? CurPhase : LastDragon;
			for (int i = from + 1; i < PHASE_COUNT; i++)
			{
				if (!PHASES[i].IsDragon) { return i; }
			}
			return from < 0 ? 0 : -1;
		}

		void OnTrigger(int aTrigger, uint64_t aTime)
		{
			// One event can arrive as several effects (e.g. one per player);
			// only the first one counts.
			if (LastTriggerTime[aTrigger] && aTime - LastTriggerTime[aTrigger] < (uint64_t)TRIGGER_DEBOUNCE)
			{
				return;
			}
			LastTriggerTime[aTrigger] = aTime;
			Dbg.TriggerHits++;
			LastActivity = aTime;

			int32_t offsets[32];
			int count = 0;

			// 1) Re-sync within the current phase: nearest scheduled occurrence.
			if (CurPhase >= 0 && PhaseHasTrigger(CurPhase, aTrigger, offsets, &count, true))
			{
				const int64_t elapsed = (int64_t)(aTime - PhaseStart);
				int best = -1;
				int64_t bestDelta = 0;
				for (int i = 0; i < count; i++)
				{
					const int64_t d = elapsed - offsets[i];
					if (best < 0 || std::llabs(d) < std::llabs(bestDelta)) { best = i; bestDelta = d; }
				}
				if (best >= 0 && std::llabs(bestDelta) <= SYNC_TOLERANCE)
				{
					PhaseStart += bestDelta;
					Dbg.LastSync = std::string(TRIGGERS[aTrigger].Name) + " in " + PHASES[CurPhase].Name;
					Dbg.LastSyncDeltaMs = (int32_t)bestDelta;
					return;
				}
			}

			// 2) A trigger that only exists in one phase identifies that phase.
			const int only = TRIGGERS[aTrigger].OnlyPhase;
			if (only >= 0 && only != CurPhase && PhaseHasTrigger(only, aTrigger, offsets, &count, false) && count > 0)
			{
				// Assume it's the first occurrence: we just got here.
				EnterPhase(only, aTime - (uint64_t)offsets[0], TRIGGERS[aTrigger].Name);
			}
		}

		bool GuidEquals(const std::array<uint8_t, 16>& a, const uint8_t* b)
		{
			return memcmp(a.data(), b, 16) == 0;
		}

		void OnEffect(uint32_t aEffectID, uint64_t aTime)
		{
			Dbg.Effects++;
			auto it = EffectGuids.find(aEffectID);
			if (it == EffectGuids.end()) { return; }
			for (int i = 0; i < TRIGGER_COUNT; i++)
			{
				if (TRIGGERS[i].Type == ETrigger::Effect && GuidEquals(it->second, TRIGGERS[i].Guid))
				{
					OnTrigger(i, aTime);
				}
			}
		}

		void OnMissile(uint32_t aSkillID, uint64_t aTime)
		{
			Dbg.Missiles++;
			for (int i = 0; i < TRIGGER_COUNT; i++)
			{
				if (TRIGGERS[i].Type == ETrigger::Missile && TRIGGERS[i].SkillID == aSkillID)
				{
					OnTrigger(i, aTime);
				}
			}
		}

		void OnOrbSpawn(uint64_t aAgentID, uint32_t aSpecies, uint64_t aTime)
		{
			if (!SeenOrbs.insert(aAgentID).second) { return; }
			Dbg.OrbSpawns++;
			LastActivity = aTime;
			int p = -1;
			if (aSpecies == FINAL_ORB_SPECIES)
			{
				for (int i = PHASE_COUNT - 1; i >= 0; i--) { if (!PHASES[i].IsDragon) { p = i; break; } }
			}
			else
			{
				// Only right at the start, or when the current dragon is the
				// one a purification follows. (Other Void Amalgamates exist.)
				const int next = NextPurificationPhase();
				const bool atStart = CurPhase < 0 && LastDragon < 0;
				const bool follows = CurPhase >= 0 && next == CurPhase + 1;
				const bool afterTimeout = CurPhase < 0 && LastDragon >= 0 && next == LastDragon + 1;
				if (atStart || follows || afterTimeout) { p = next; }
			}
			if (p >= 0 && p != CurPhase) { EnterPhase(p, aTime, "orb spawned"); }
		}

		uint32_t SpeciesOf(const ArcAgent_t* aAg)
		{
			// NPCs: elite == 0xFFFFFFFF and prof = species id.
			if (!aAg || aAg->Elite != 0xFFFFFFFF) { return 0; }
			return aAg->Profession;
		}

		void ResetLocked()
		{
			CurPhase = -1;
			PhaseStart = 0;
			LastDragon = -1;
			LastActivity = 0;
			EffectGuids.clear();
			AttackTargetAgents.clear();
			SeenOrbs.clear();
			LastTargetableOn = 0;
			memset(LastTriggerTime, 0, sizeof(LastTriggerTime));
			Casts.clear();
			Dbg = Debug{};
		}
	}

	void OnCombatEvent(const EvCombatData_t* aData)
	{
		if (!aData) { return; }
		std::lock_guard<std::mutex> lock(Mutex);
		if (MapID != HARVEST_TEMPLE) { return; }

		const ArcCombatEvent_t* ev = aData->ev;

		// Agent tracking notification: watch for purification orbs spawning.
		if (!ev)
		{
			const ArcAgent_t* src = aData->src;
			if (src && src->Elite == 0 && src->Profession != 0)
			{
				const uint32_t species = src->Profession;
				if (species == ORB_SPECIES || species == FINAL_ORB_SPECIES)
				{
					// No event time here; these arrive in real time.
					OnOrbSpawn(src->ID, species, LastNow);
				}
			}
			return;
		}

		if (IsDuplicate(ev)) { return; }

		// Orbs we didn't see a tracking notification for (e.g. addon loaded mid-fight).
		for (const ArcAgent_t* ag : { aData->src, aData->dst })
		{
			const uint32_t sp = SpeciesOf(ag);
			if ((sp == ORB_SPECIES || sp == FINAL_ORB_SPECIES) && !SeenOrbs.count(ag->ID))
			{
				OnOrbSpawn(ag->ID, sp, ev->Time);
			}
		}

		switch (ev->IsStatechange)
		{
			case 0:
				break;
			case CBTS_IDTOGUID:
				if (ev->OverstackValue == CONTENT_EFFECT)
				{
					std::array<uint8_t, 16> g;
					memcpy(g.data(), &ev->SrcAgent, 8);
					memcpy(g.data() + 8, &ev->DstAgent, 8);
					EffectGuids[ev->SkillID] = g;
					Dbg.GuidMappings++;
				}
				return;
			case CBTS_EFFECT2:
			case CBTS_EFFECTGROUNDCREATE:
			case CBTS_EFFECTAGENTCREATE:
				OnEffect(ev->SkillID, ev->Time);
				return;
			case CBTS_MISSILECREATE:
				OnMissile(ev->SkillID, ev->Time);
				return;
			case CBTS_ATTACKTARGET:
				Dbg.AttackTargets++;
				AttackTargetAgents.insert(ev->SrcAgent);
				return;
			case CBTS_TARGETABLE:
				Dbg.Targetables++;
				// The Dragonvoid becomes attackable: next dragon phase.
				if (ev->DstAgent == 1 && AttackTargetAgents.count(ev->SrcAgent)
					&& (LastTargetableOn == 0 || ev->Time - LastTargetableOn > 10000))
				{
					LastTargetableOn = ev->Time;
					LastActivity = ev->Time;
					const int next = NextDragonPhase();
					if (next >= 0) { EnterPhase(next, ev->Time, "Dragonvoid targetable"); }
				}
				return;
			default:
				return;
		}

		// Reactive alerts: NPC activations we care about.
		if (ev->IsActivation)
		{
			Dbg.Activations++;
			for (int i = 0; i < REACTIVE_COUNT; i++)
			{
				if (REACTIVES[i].SkillID != ev->SkillID) { continue; }
				Casts.erase(std::remove_if(Casts.begin(), Casts.end(),
					[&](const ActiveCast& c) { return c.SkillID == ev->SkillID && c.SrcAgent == ev->SrcAgent; }), Casts.end());
				if (ev->IsActivation == 1) // start
				{
					const int32_t ms = ev->Value > 0 ? ev->Value : REACTIVES[i].DefaultCastMs;
					Casts.push_back({ ev->SkillID, ev->SrcAgent, ev->Time + (uint64_t)ms, REACTIVES[i].Label });
				}
			}
		}
	}

	void Tick(uint64_t aNowMs, uint32_t aMapID)
	{
		std::lock_guard<std::mutex> lock(Mutex);
		if (aMapID != MapID)
		{
			MapID = aMapID;
			ResetLocked();
			return;
		}
		LastNow = aNowMs;
		if (LastActivity == 0) { LastActivity = aNowMs; }

		if (CurPhase >= 0)
		{
			const int64_t elapsed = (int64_t)aNowMs - (int64_t)PhaseStart;
			if (elapsed > (int64_t)PHASES[CurPhase].MaxDurationMs + 30000)
			{
				Dbg.LastSync = std::string(PHASES[CurPhase].Name) + " timed out";
				CurPhase = -1;
			}
		}
		if (aNowMs > LastActivity + IDLE_RESET_MS && (CurPhase >= 0 || LastDragon >= 0))
		{
			const auto guids = EffectGuids; // mappings stay valid for the session
			ResetLocked();
			EffectGuids = guids;
			LastActivity = aNowMs;
		}
		Casts.erase(std::remove_if(Casts.begin(), Casts.end(),
			[&](const ActiveCast& c) { return c.EndMs + 500 < aNowMs; }), Casts.end());
	}

	State GetState(uint64_t aNowMs, int32_t aHorizonMs, int aMaxItems)
	{
		std::lock_guard<std::mutex> lock(Mutex);
		State s{};
		s.Phase = CurPhase;
		s.PhaseName = CurPhase >= 0 ? PHASES[CurPhase].Name : nullptr;
		if (CurPhase >= 0)
		{
			s.ElapsedMs = (int32_t)((int64_t)aNowMs - (int64_t)PhaseStart);
			const Phase& p = PHASES[CurPhase];
			for (int i = 0; i < p.CalloutCount && (int)s.Next.size() < aMaxItems; i++)
			{
				const int32_t in = p.Callouts[i].OffsetMs - s.ElapsedMs;
				if (in < -1000 || in > aHorizonMs) { continue; }
				s.Next.push_back({ p.Callouts[i].Label, in, p.Callouts[i].BigAlert != 0, p.Callouts[i].SpreadMs });
			}
		}
		for (const ActiveCast& c : Casts)
		{
			s.Reactives.push_back({ c.Label, (int32_t)((int64_t)c.EndMs - (int64_t)aNowMs) });
		}
		return s;
	}

	Debug GetDebug()
	{
		std::lock_guard<std::mutex> lock(Mutex);
		return Dbg;
	}

	void ForcePhase(int aPhase, uint64_t aStartMs)
	{
		std::lock_guard<std::mutex> lock(Mutex);
		if (aPhase < 0 || aPhase >= PHASE_COUNT) { CurPhase = -1; return; }
		EnterPhase(aPhase, aStartMs, "preview");
		LastActivity = aStartMs;
	}

	void Reset()
	{
		std::lock_guard<std::mutex> lock(Mutex);
		ResetLocked();
	}
}
