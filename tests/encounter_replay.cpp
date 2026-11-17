// Replays synthetic live events (from tools/ei_to_events.py) through the
// Encounter engine and checks its predictions.
//
// Build & run (native, no Windows needed):
//   g++ -std=c++17 -O1 -Isrc tests/encounter_replay.cpp src/Encounter.cpp -o build/encounter_replay
//   build/encounter_replay events.txt [-v]
//
// For each PROBE (3s before a real event), the engine should list a callout with
// that label due in ~3000ms. Prints phase changes and the prediction errors.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "Encounter.h"
#include "EncounterData.h"
#include "Nexus.h"

static uint8_t Hex(char c) { return (uint8_t)(c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10); }

int main(int argc, char** argv)
{
	if (argc < 2) { fprintf(stderr, "usage: %s events.txt [-v]\n", argv[0]); return 2; }
	const bool verbose = argc > 2 && strcmp(argv[2], "-v") == 0;

	std::ifstream in(argv[1]);
	std::string line;
	std::map<std::string, uint32_t> effectIDs; // guid -> session effect id
	uint32_t nextEffectID = 100;
	const char* lastPhase = nullptr;

	struct Stat { int probes = 0, found = 0; double sumAbs = 0, maxAbs = 0; };
	std::map<std::string, Stat> stats; // phase/label

	Encounter::Tick(0, 1437);

	while (std::getline(in, line))
	{
		std::istringstream ss(line);
		uint64_t t; std::string type;
		ss >> t >> type;
		Encounter::Tick(t, 1437);

		ArcCombatEvent_t ev{};
		ev.Time = t;
		ArcAgent_t src{}, dst{};
		EvCombatData_t data{ &ev, &src, &dst, nullptr, 0, 0 };

		if (type == "PROBE")
		{
			std::string label;
			std::getline(ss, label);
			label.erase(0, label.find_first_not_of(' '));
			Encounter::State st = Encounter::GetState(t, 10000, 32);
			const std::string key = std::string(st.PhaseName ? st.PhaseName : "(none)") + " / " + label;
			Stat& s = stats[key];
			s.probes++;
			int best = INT32_MAX;
			for (auto& n : st.Next)
			{
				if (label == n.Label && std::abs(n.InMs - 3000) < std::abs(best - 3000)) { best = n.InMs; }
			}
			if (best != INT32_MAX)
			{
				const double err = std::abs(best - 3000);
				s.found++; s.sumAbs += err; s.maxAbs = std::max(s.maxAbs, err);
				if (verbose) { printf("  %8.1fs PROBE %-16s predicted in %5d ms (err %+5d)\n", t / 1000.0, label.c_str(), best, best - 3000); }
			}
			else if (verbose)
			{
				printf("  %8.1fs PROBE %-16s MISSING (phase %s +%.1fs)\n", t / 1000.0, label.c_str(), st.PhaseName ? st.PhaseName : "-", st.ElapsedMs / 1000.0);
			}
			continue;
		}

		if (type == "ATTACKTARGET")
		{
			uint64_t a; ss >> a;
			ev.IsStatechange = 23; ev.SrcAgent = a; ev.DstAgent = 99; ev.Value = 1;
		}
		else if (type == "TARGETABLE")
		{
			uint64_t a; int on; ss >> a >> on;
			ev.IsStatechange = 24; ev.SrcAgent = a; ev.DstAgent = (uint64_t)on;
		}
		else if (type == "MISSILE")
		{
			uint32_t sk; ss >> sk;
			ev.IsStatechange = 57; ev.SkillID = sk;
		}
		else if (type == "EFFECT")
		{
			std::string g; ss >> g;
			auto it = effectIDs.find(g);
			if (it == effectIDs.end())
			{
				// First use: ArcDPS sends IDTOGUID before the effect.
				it = effectIDs.emplace(g, nextEffectID++).first;
				ArcCombatEvent_t m{};
				m.Time = t; m.IsStatechange = 46; m.SkillID = it->second; m.OverstackValue = 0;
				uint8_t b[16];
				for (int i = 0; i < 16; i++) { b[i] = (uint8_t)(Hex(g[2 * i]) << 4 | Hex(g[2 * i + 1])); }
				memcpy(&m.SrcAgent, b, 8); memcpy(&m.DstAgent, b + 8, 8);
				EvCombatData_t md{ &m, &src, &dst, nullptr, 0, 0 };
				Encounter::OnCombatEvent(&md);
			}
			ev.IsStatechange = 60; ev.SkillID = it->second;
		}
		else if (type == "ORB")
		{
			uint64_t id; uint32_t sp; ss >> id >> sp;
			src.ID = id; src.Profession = sp; src.Elite = 0;
			EvCombatData_t notify{ nullptr, &src, &dst, nullptr, 0, 0 };
			Encounter::OnCombatEvent(&notify);
		}
		else
		{
			continue;
		}
		if (type != "ORB") { Encounter::OnCombatEvent(&data); }

		Encounter::State st = Encounter::GetState(t, 0, 0);
		if (st.PhaseName != lastPhase)
		{
			printf("%8.1fs  phase -> %s   (%s)\n", t / 1000.0, st.PhaseName ? st.PhaseName : "(none)", Encounter::GetDebug().LastSync.c_str());
			lastPhase = st.PhaseName;
		}
	}

	int probes = 0, found = 0;
	printf("\n%-40s %6s %6s %8s %8s\n", "phase / callout", "probes", "found", "avg err", "max err");
	for (auto& [k, s] : stats)
	{
		probes += s.probes; found += s.found;
		printf("%-40s %6d %6d %7.0fms %7.0fms\n", k.c_str(), s.probes, s.found, s.found ? s.sumAbs / s.found : 0.0, s.maxAbs);
	}
	printf("\nTOTAL: %d/%d probes predicted\n", found, probes);
	return 0;
}
