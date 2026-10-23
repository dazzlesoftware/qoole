// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// The output log, as in Unreal (OutputLog module):
//
//   FOutputLogHistory  an output device that keeps every line written with
//                      GENESIS_LOG (category, verbosity, time) from when it starts.
//   SOutputLog         the panel: the lines, colored by verbosity, filtered
//                      by a search text and by verbosity (errors, warnings,
//                      messages), with Clear; it follows new lines while it
//                      is scrolled to the bottom.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

#include <mutex>

struct FOutputLogMessage
{
	FString Category;
	ELogVerbosity Verbosity = ELogVerbosity::Log;
	FString Text;
	double Time = 0.0; // seconds since the history started
};

class FOutputLogHistory final : public FOutputDevice
{
public:
	static FOutputLogHistory& Get();

	// Adds the device to FLog (once); lines written before are not kept.
	void Start();
	void Stop();

	// Messages with a serial number from First on, and the next serial.
	uint64 GetMessages(uint64 First, TArray<FOutputLogMessage>& Out) const;
	uint64 GetNextSerial() const;
	// Serial numbers below this were cleared (or dropped: the history keeps
	// the last MaxMessages).
	uint64 GetFirstSerial() const;
	void Clear();

	static constexpr SIZE_T MaxMessages = 20000;

	// FOutputDevice
	void Serialize(const FLogCategory& Category, ELogVerbosity Verbosity, std::string_view Message) override;

private:
	mutable std::mutex Mutex;
	TArray<FOutputLogMessage> Messages; // serials FirstSerial ...
	uint64 FirstSerial = 0;
	double StartTime = 0.0;
	bool bStarted = false;
};

class SOutputLog final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOutputLog)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);

	// The lines shown (after the filters), for tests.
	int32 GetNumShown() const
	{
		return static_cast<int32>(Shown.size());
	}
	void SetFilterText(const FString& Text);

private:
	struct FFilter
	{
		FString Text;
		bool bErrors = true;
		bool bWarnings = true;
		bool bMessages = true;
	};

	bool Passes(const FOutputLogMessage& Message) const;
	void Refilter();
	void Poll();

	TArray<FOutputLogMessage> All; // every message read from the history
	TArray<int32> Shown;           // indices into All
	uint64 NextSerial = 0;
	FFilter Filter;
	TSharedPtr<SListView<int32>> List;
};
