#include "Recorder.h"

#include <windows.h>
#include <mmsystem.h>

#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "Nexus.h"

namespace Recorder
{
	std::atomic<bool> Enabled{ true };
	std::atomic<bool> AnyMap{ false };

	namespace
	{
		constexpr uint32_t HARVEST_TEMPLE = 1437;
		constexpr size_t   MAX_PENDING    = 32 * 1024 * 1024; // drop lines past this

		/* ---------------------------------------------------------------- */
		/* Queue between event callbacks and the writer thread.             */
		/* ---------------------------------------------------------------- */

		enum class EOp { Open, Data, Close };

		struct Item
		{
			EOp         Op;
			std::string Text; // Open: file path; Data: lines
		};

		std::mutex              Mutex;          // guards everything below
		std::condition_variable WakeWriter;
		std::vector<Item>       Queue;
		size_t                  PendingBytes = 0;
		bool                    StopWriter   = false;
		std::thread             Writer;

		std::string Directory;
		bool        FileActive = false; // an Open has been queued without a Close
		std::string FilePath;
		uint64_t    Lines   = 0;
		uint64_t    Dropped = 0;
		std::atomic<uint64_t> BytesWritten{ 0 };

		uint32_t MapID = 0, ShardID = 0, Instance = 0;

		/* Squad damage to NPCs, summed per second, per stream. */
		struct DmgAcc
		{
			std::string Name;
			uint32_t    Prof   = 0;
			uint64_t    Damage = 0;
			uint32_t    Hits   = 0;
		};
		std::unordered_map<uint64_t, DmgAcc> Dmg[2];   // key: dst agent id
		uint64_t                             DmgSecond[2] = { 0, 0 };

		/* ---------------------------------------------------------------- */
		/* Formatting helpers                                               */
		/* ---------------------------------------------------------------- */

		const char* COLUMNS =
			"time,stream,kind,sub,"
			"src_id,src_name,src_prof,src_elite,src_team,src_self,"
			"dst_id,dst_name,dst_prof,dst_elite,dst_team,dst_self,"
			"skill_id,skill_name,value,buff_dmg,result,"
			"src_instid,dst_instid,src_master_instid,dst_master_instid\n";

		// ArcDPS statechange IDs (from the ArcDPS evtc docs; names are for
		// readability only, the numeric code is always written too).
		const char* StatechangeName(uint8_t aSC)
		{
			static const char* names[] = {
				"NONE", "ENTERCOMBAT", "EXITCOMBAT", "CHANGEUP", "CHANGEDEAD", "CHANGEDOWN",
				"SPAWN", "DESPAWN", "HEALTHPCTUPDATE", "SQCOMBATSTART", "SQCOMBATEND",
				"WEAPSWAP", "MAXHEALTHUPDATE", "POINTOFVIEW", "LANGUAGE", "GWBUILD", "SHARDID",
				"REWARD", "BUFFINITIAL", "POSITION", "VELOCITY", "FACING", "TEAMCHANGE",
				"ATTACKTARGET", "TARGETABLE", "MAPID", "REPLINFO", "STACKACTIVE", "STACKRESET",
				"GUILD", "BUFFINFO", "BUFFFORMULA", "SKILLINFO", "SKILLTIMING", "BREAKBARSTATE",
				"BREAKBARPERCENT", "INTEGRITY", "MARKER", "BARRIERPCTUPDATE", "STATRESET",
				"EXTENSION", "APIDELAYED", "INSTANCESTART", "RATEHEALTH", "LAST90BEFOREDOWN",
				"EFFECT_OLD", "IDTOGUID", "LOGNPCUPDATE", "IDLEEVENT", "EXTENSIONCOMBAT",
				"FRACTALSCALE", "EFFECT2", "RULESET", "SQUADMARKER", "ARCBUILD", "GLIDER",
				"STUNBREAK",
			};
			return aSC < sizeof(names) / sizeof(names[0]) ? names[aSC] : "UNKNOWN";
		}

		const char* ActivationName(uint8_t aA)
		{
			switch (aA)
			{
				case 1:  return "START";
				case 2:  return "QUICKNESS_UNUSED";
				case 3:  return "CANCEL_FIRE";
				case 4:  return "CANCEL_CANCEL";
				case 5:  return "RESET";
				default: return "UNKNOWN";
			}
		}

		const char* BuffRemoveName(uint8_t aR)
		{
			switch (aR)
			{
				case 1:  return "ALL";
				case 2:  return "SINGLE";
				case 3:  return "MANUAL";
				default: return "UNKNOWN";
			}
		}

		void AppendCsvString(std::string& aOut, const char* aStr)
		{
			if (!aStr || !aStr[0]) { return; }
			bool quote = strpbrk(aStr, ",\"\n\r") != nullptr;
			if (!quote) { aOut += aStr; return; }
			aOut += '"';
			for (const char* p = aStr; *p; p++)
			{
				if (*p == '"') { aOut += '"'; }
				if (*p == '\n' || *p == '\r') { aOut += ' '; continue; }
				aOut += *p;
			}
			aOut += '"';
		}

		void AppendU64(std::string& aOut, uint64_t aV) { aOut += std::to_string(aV); }
		void AppendI64(std::string& aOut, int64_t aV)  { aOut += std::to_string(aV); }

		void AppendAgent(std::string& aOut, const ArcAgent_t* aAg)
		{
			if (!aAg) { aOut += ",,,,,"; return; }
			AppendU64(aOut, aAg->ID);         aOut += ',';
			AppendCsvString(aOut, aAg->Name); aOut += ',';
			AppendU64(aOut, aAg->Profession); aOut += ',';
			AppendU64(aOut, aAg->Elite);      aOut += ',';
			AppendU64(aOut, aAg->Team);       aOut += ',';
			AppendU64(aOut, aAg->Self);
		}

		bool IsPlayer(const ArcAgent_t* aAg)
		{
			return aAg && aAg->Elite != 0xFFFFFFFF && aAg->Profession >= 1 && aAg->Profession <= 9;
		}

		bool IsNpc(const ArcAgent_t* aAg)
		{
			return aAg && aAg->ID != 0 && !IsPlayer(aAg);
		}

		/* One CSV row. */
		std::string FormatRow(uint64_t aTime, const char* aStream, const char* aKind, const char* aSub,
			const EvCombatData_t* aData)
		{
			std::string s;
			s.reserve(256);
			AppendU64(s, aTime); s += ',';
			s += aStream;        s += ',';
			s += aKind;          s += ',';
			s += aSub;           s += ',';
			AppendAgent(s, aData->src); s += ',';
			AppendAgent(s, aData->dst); s += ',';

			const ArcCombatEvent_t* ev = aData->ev;
			if (ev)
			{
				AppendU64(s, ev->SkillID);             s += ',';
				AppendCsvString(s, aData->skillname);  s += ',';
				AppendI64(s, ev->Value);               s += ',';
				AppendI64(s, ev->BuffDmg);             s += ',';
				AppendU64(s, ev->Result);              s += ',';
				AppendU64(s, ev->SrcInstID);           s += ',';
				AppendU64(s, ev->DstInstID);           s += ',';
				AppendU64(s, ev->SrcMasterInstID);     s += ',';
				AppendU64(s, ev->DstMasterInstID);
			}
			else
			{
				s += ",,,,,,,,";
			}
			s += '\n';
			return s;
		}

		/* ---------------------------------------------------------------- */
		/* Queue operations (call with Mutex held)                          */
		/* ---------------------------------------------------------------- */

		void PushData(std::string&& aText)
		{
			if (PendingBytes + aText.size() > MAX_PENDING) { Dropped++; return; }
			PendingBytes += aText.size();
			Lines++;
			if (!Queue.empty() && Queue.back().Op == EOp::Data)
			{
				Queue.back().Text += aText;
			}
			else
			{
				Queue.push_back({ EOp::Data, std::move(aText) });
			}
		}

		void FlushDamage(int aStream)
		{
			const char* stream = aStream == 0 ? "local" : "squad";
			for (auto& [id, acc] : Dmg[aStream])
			{
				// time, stream, kind, sub, src(6 empty), dst id/name/prof, ..., value, hits in buff_dmg column
				std::string s;
				AppendU64(s, DmgSecond[aStream] * 1000); s += ',';
				s += stream; s += ",DMG_SEC,,";
				s += ",,,,,,";                     // src_*
				AppendU64(s, id);           s += ',';
				AppendCsvString(s, acc.Name.c_str()); s += ',';
				AppendU64(s, acc.Prof);     s += ",,,,";  // dst_prof, elite, team, self
				s += ",,";                         // skill_id, skill_name
				AppendU64(s, acc.Damage);   s += ',';
				AppendU64(s, acc.Hits);            // hit count in buff_dmg column
				s += ",,,,,\n";
				PushData(std::move(s));
			}
			Dmg[aStream].clear();
		}

		void EnsureFileOpen()
		{
			if (FileActive) { return; }

			SYSTEMTIME st;
			GetLocalTime(&st);
			char name[96];
			static uint32_t fileCounter = 0;
			snprintf(name, sizeof(name), "map%u_%04u-%02u-%02u_%02u-%02u-%02u_%u.csv",
				MapID, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, ++fileCounter);

			FilePath = (std::filesystem::path(Directory) / name).string();
			Queue.push_back({ EOp::Open, FilePath });
			FileActive = true;

			char header[256];
			snprintf(header, sizeof(header),
				"# HTCM Helper recording. map=%u shard=%u instance=%u started=%04u-%02u-%02u %02u:%02u:%02u\n",
				MapID, ShardID, Instance, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
			PushData(std::string(header) + COLUMNS);
		}

		void CloseFileLocked()
		{
			if (!FileActive) { return; }
			FlushDamage(0);
			FlushDamage(1);
			Queue.push_back({ EOp::Close, {} });
			FileActive = false;
			WakeWriter.notify_one();
		}

		bool ShouldRecord()
		{
			return Enabled && !Directory.empty() && (AnyMap || MapID == HARVEST_TEMPLE);
		}

		/* ---------------------------------------------------------------- */
		/* Writer thread                                                    */
		/* ---------------------------------------------------------------- */

		void WriterLoop()
		{
			FILE* file = nullptr;

			for (;;)
			{
				std::vector<Item> items;
				bool stop;
				{
					std::unique_lock<std::mutex> lock(Mutex);
					WakeWriter.wait_for(lock, std::chrono::seconds(1), [] { return StopWriter; });
					items.swap(Queue);
					PendingBytes = 0;
					stop = StopWriter;
				}

				for (Item& it : items)
				{
					switch (it.Op)
					{
						case EOp::Open:
						{
							if (file) { fclose(file); }
							std::error_code ec;
							std::filesystem::create_directories(std::filesystem::path(it.Text).parent_path(), ec);
							file = fopen(it.Text.c_str(), "ab");
							break;
						}
						case EOp::Data:
							if (file)
							{
								fwrite(it.Text.data(), 1, it.Text.size(), file);
								BytesWritten += it.Text.size();
							}
							break;
						case EOp::Close:
							if (file) { fclose(file); file = nullptr; }
							break;
					}
				}
				if (file) { fflush(file); }

				if (stop) { break; }
			}

			if (file) { fclose(file); }
		}
	}

	void Init(const std::string& aDirectory)
	{
		{
			std::lock_guard<std::mutex> lock(Mutex);
			Directory  = aDirectory;
			StopWriter = false;
		}
		std::error_code ec;
		std::filesystem::create_directories(aDirectory, ec);
		Writer = std::thread(WriterLoop);
	}

	void Shutdown()
	{
		{
			std::lock_guard<std::mutex> lock(Mutex);
			CloseFileLocked();
			StopWriter = true;
		}
		WakeWriter.notify_one();
		if (Writer.joinable()) { Writer.join(); }
	}

	void SetLocation(uint32_t aMapID, uint32_t aShardID, uint32_t aInstance)
	{
		std::lock_guard<std::mutex> lock(Mutex);
		if (aMapID == MapID && aShardID == ShardID && aInstance == Instance) { return; }
		CloseFileLocked();
		MapID    = aMapID;
		ShardID  = aShardID;
		Instance = aInstance;
	}

	void Split()
	{
		std::lock_guard<std::mutex> lock(Mutex);
		CloseFileLocked();
	}

	void OnCombatEvent(const EvCombatData_t* aData, const char* aStream)
	{
		if (!aData) { return; }

		const int streamIdx = strcmp(aStream, "local") == 0 ? 0 : 1;
		const ArcCombatEvent_t* ev = aData->ev;

		std::lock_guard<std::mutex> lock(Mutex);
		if (!ShouldRecord())
		{
			// Turning recording off (or leaving the map) closes the file.
			CloseFileLocked();
			return;
		}

		/* ev == nullptr: agent tracking / target change notifications. */
		if (!ev)
		{
			if (!aData->src) { return; }
			const char* kind = aData->src->Elite != 0
				? "TARGET_CHANGE"
				: (aData->src->Profession != 0 ? "AGENT_ADD" : "AGENT_REMOVE");
			EnsureFileOpen();
			PushData(FormatRow(timeGetTime(), aStream, kind, "", aData));
			return;
		}

		/* State changes (skip the very high-volume movement ones). */
		if (ev->IsStatechange)
		{
			const uint8_t sc = ev->IsStatechange;
			if (sc == 19 || sc == 20 || sc == 21) { return; } // POSITION, VELOCITY, FACING
			char sub[48];
			snprintf(sub, sizeof(sub), "%u:%s", sc, StatechangeName(sc));
			EnsureFileOpen();
			PushData(FormatRow(ev->Time, aStream, "STATECHANGE", sub, aData));
			return;
		}

		/* Skill casts by NPCs / gadgets. */
		if (ev->IsActivation)
		{
			if (IsPlayer(aData->src)) { return; }
			EnsureFileOpen();
			PushData(FormatRow(ev->Time, aStream, "CAST", ActivationName(ev->IsActivation), aData));
			return;
		}

		const bool npcInvolved = IsNpc(aData->src) || IsNpc(aData->dst);
		const bool onSelf      = aData->dst && aData->dst->Self;

		/* Buff removal. */
		if (ev->IsBuffRemove)
		{
			if (!npcInvolved && !onSelf) { return; }
			EnsureFileOpen();
			PushData(FormatRow(ev->Time, aStream, "BUFF_REMOVE", BuffRemoveName(ev->IsBuffRemove), aData));
			return;
		}

		/* Buff application (value = duration). */
		if (ev->Buff && ev->Value != 0)
		{
			if (!npcInvolved && !onSelf) { return; }
			EnsureFileOpen();
			PushData(FormatRow(ev->Time, aStream, "BUFF_APPLY", "", aData));
			return;
		}

		/* Damage: condition tick (buff && value == 0) or direct hit. Only
		 * squad -> NPC damage, summed per second per target. */
		int64_t damage = 0;
		if (ev->Buff)
		{
			damage = ev->BuffDmg;
		}
		else
		{
			// normal, crit, glance, interrupt, killingblow, downed
			const uint8_t r = ev->Result;
			if (r == 0 || r == 1 || r == 2 || r == 5 || r == 8 || r == 9) { damage = ev->Value; }
		}
		if (damage <= 0) { return; }

		const bool fromSquad = IsPlayer(aData->src) || ev->SrcMasterInstID != 0;
		if (!fromSquad || !IsNpc(aData->dst)) { return; }

		const uint64_t second = ev->Time / 1000;
		if (second != DmgSecond[streamIdx])
		{
			if (FileActive) { FlushDamage(streamIdx); } else { Dmg[streamIdx].clear(); }
			DmgSecond[streamIdx] = second;
		}

		EnsureFileOpen();
		DmgAcc& acc = Dmg[streamIdx][aData->dst->ID];
		if (acc.Name.empty() && aData->dst->Name) { acc.Name = aData->dst->Name; }
		acc.Prof    = aData->dst->Profession;
		acc.Damage += (uint64_t)damage;
		acc.Hits++;
	}

	Stats_t GetStats()
	{
		std::lock_guard<std::mutex> lock(Mutex);
		return { FileActive, FilePath, Lines, BytesWritten.load(), Dropped };
	}

	const std::string& GetDirectory()
	{
		return Directory;
	}
}
