// Combat event recorder: writes ArcDPS combat events to CSV files so we can
// find skill/buff/species IDs and mechanic timings from real runs.
//
// - One file per map instance, opened lazily on the first recorded event.
// - Event callbacks only format a line into a memory buffer; a background
//   thread writes it to disk about once a second.
#pragma once

#include <atomic>
#include <cstdint>
#include <string>

struct EvCombatData_t;

namespace Recorder
{
	extern std::atomic<bool> Enabled;     // master switch
	extern std::atomic<bool> AnyMap;      // record outside Harvest Temple too

	struct Stats_t
	{
		bool        FileOpen;
		std::string FilePath;
		uint64_t    Lines;
		uint64_t    Bytes;
		uint64_t    Dropped;  // lines dropped because the writer fell behind
	};

	/// Starts the writer thread. aDirectory is created if needed.
	void Init(const std::string& aDirectory);

	/// Flushes, closes the file and stops the writer thread.
	void Shutdown();

	/// Called every frame with MumbleLink's map/instance info. Closes the
	/// current file when the instance changes.
	void SetLocation(uint32_t aMapID, uint32_t aShardID, uint32_t aInstance);

	/// Close the current file; the next event starts a new one.
	void Split();

	/// aStream: "local" or "squad" (which Nexus event the data came from).
	void OnCombatEvent(const EvCombatData_t* aData, const char* aStream);

	Stats_t GetStats();

	const std::string& GetDirectory();
}
