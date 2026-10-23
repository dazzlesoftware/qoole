// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "SOutputLog.h"
#include "Containers/ContainerAliases.h"
#include "Internationalization/TextLocalization.h"

#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/Text/SEditableText.h"
#include "Widgets/Text/STextBlock.h"

// ---------------------------------------------------------------------------
// FOutputLogHistory

FOutputLogHistory& FOutputLogHistory::Get()
{
	static FOutputLogHistory Instance;
	return Instance;
}

void FOutputLogHistory::Start()
{
	if (bStarted)
	{
		return;
	}
	bStarted = true;
	StartTime = FPlatformTime::Seconds();
	FLog::AddDevice(this);
}

void FOutputLogHistory::Stop()
{
	if (bStarted)
	{
		FLog::RemoveDevice(this);
		bStarted = false;
	}
}

void FOutputLogHistory::Serialize(const FLogCategory& Category, ELogVerbosity Verbosity, std::string_view Message)
{
	// Called with FLog's lock held: no logging from here.
	while (!Message.empty() && (Message.back() == '\n' || Message.back() == '\r'))
	{
		Message.remove_suffix(1);
	}
	std::scoped_lock Lock(Mutex);
	FOutputLogMessage& Line = Messages.emplace_back();
	Line.Category = FString(Category.Name);
	Line.Verbosity = Verbosity;
	Line.Text = FString(Message);
	Line.Time = FPlatformTime::Seconds() - StartTime;
	if (Messages.size() > MaxMessages)
	{
		const SIZE_T Drop = Messages.size() - MaxMessages;
		Messages.erase(Messages.begin(), Messages.begin() + static_cast<std::ptrdiff_t>(Drop));
		FirstSerial += Drop;
	}
}

uint64 FOutputLogHistory::GetMessages(uint64 First, TArray<FOutputLogMessage>& Out) const
{
	std::scoped_lock Lock(Mutex);
	const uint64 Next = FirstSerial + Messages.size();
	for (uint64 Serial = FMath::Max(First, FirstSerial); Serial < Next; ++Serial)
	{
		Out.push_back(Messages[static_cast<SIZE_T>(Serial - FirstSerial)]);
	}
	return Next;
}

uint64 FOutputLogHistory::GetNextSerial() const
{
	std::scoped_lock Lock(Mutex);
	return FirstSerial + Messages.size();
}

uint64 FOutputLogHistory::GetFirstSerial() const
{
	std::scoped_lock Lock(Mutex);
	return FirstSerial;
}

void FOutputLogHistory::Clear()
{
	std::scoped_lock Lock(Mutex);
	FirstSerial += Messages.size();
	Messages.clear();
}

// ---------------------------------------------------------------------------
// SOutputLog

namespace
{
	FColor VerbosityColor(ELogVerbosity Verbosity)
	{
		switch (Verbosity)
		{
		case ELogVerbosity::Fatal:
		case ELogVerbosity::Error:
			return FColor(255, 96, 88);
		case ELogVerbosity::Warning:
			return FColor(240, 200, 80);
		case ELogVerbosity::Display:
			return FColor(208, 214, 224);
		default:
			return FColor(150, 158, 172);
		}
	}

	// The map core prints its own "Error." lines as messages.
	bool IsErrorText(const FString& Text)
	{
		return Text.StartsWith("Error", ESearchCase::IgnoreCase);
	}
} // namespace

void SOutputLog::Construct(const FArguments&)
{
	const FTextBlockStyle* Small = &FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("SmallText");
	const TFunction<TSharedRef<SWidget>(const char*, bool FFilter::*)> Toggle =
	    [this](const char* Label, bool FFilter::* Member) -> TSharedRef<SWidget>
	{
		return SNew(SCheckBox)
		    .Text(Label)
		    .IsChecked([this, Member] { return Filter.*Member ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
		    .OnCheckStateChanged([this, Member](ECheckBoxState State)
		{
			Filter.*Member = State == ECheckBoxState::Checked;
			Refilter();
		});
	};
	ChildSlot[SNew(SVerticalBox) +
	          SVerticalBox::Slot().AutoHeight().Padding(FMargin(
	              6.0f, 4.0f))[SNew(SHorizontalBox) +
	                           SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(0.0f, 0.0f, 10.0f, 0.0f))
	                               [SNew(SEditableTextBox)
	                                    .HintText(GENESIS_TEXT("Search Log"))
	                                    .OnTextChanged([this](const FString& Text) { SetFilterText(Text); })] +
	                           SHorizontalBox::Slot()
	                               .AutoWidth()
	                               .VAlign(VAlign_Center)
	                               .Padding(FMargin(0.0f, 0.0f, 10.0f, 0.0f))[Toggle("Errors", &FFilter::bErrors)] +
	                           SHorizontalBox::Slot()
	                               .AutoWidth()
	                               .VAlign(VAlign_Center)
	                               .Padding(FMargin(0.0f, 0.0f, 10.0f, 0.0f))[Toggle("Warnings", &FFilter::bWarnings)] +
	                           SHorizontalBox::Slot()
	                               .AutoWidth()
	                               .VAlign(VAlign_Center)
	                               .Padding(FMargin(0.0f, 0.0f, 10.0f, 0.0f))[Toggle("Messages", &FFilter::bMessages)] +
	                           SHorizontalBox::Slot().AutoWidth()[SNew(SButton)
	                                                                  .Text(GENESIS_TEXT("Clear"))
	                                                                  .ToolTipText(GENESIS_TEXT("Clear the log"))
	                                                                  .OnClicked(
	                                                                      [this]
	                                                                      {
		                                                                      FOutputLogHistory::Get().Clear();
		                                                                      All.clear();
		                                                                      NextSerial = FOutputLogHistory::Get()
		                                                                                       .GetNextSerial();
		                                                                      Refilter();
		                                                                      return FReply::Handled();
	                                                                      })]] +
	          SVerticalBox::Slot().FillHeight(1.0f)
	              [SAssignNew(List, SListView<int32>)
	                   .ListItemsSource(&Shown)
	                   .SelectionMode(ESelectionMode::Multi)
	                   .OnGenerateRow(
	                       [this, Small](int32 Index, const TSharedRef<STableViewBase>& Owner) -> TSharedRef<ITableRow>
	                       {
		                       const bool bValid = Index >= 0 && Index < static_cast<int32>(All.size());
		                       const FOutputLogMessage Message =
		                           bValid ? All[static_cast<SIZE_T>(Index)] : FOutputLogMessage();
		                       const ELogVerbosity Shown =
		                           IsErrorText(Message.Text) ? ELogVerbosity::Error : Message.Verbosity;
		                       return SNew(STableRow<int32>, Owner)
		                           .Padding(
		                               FMargin(8.0f, 0.0f))[SNew(STextBlock)
		                                                        .TextStyle(Small)
		                                                        .ColorAndOpacity(FSlateColor(VerbosityColor(Shown)))
		                                                        .Text(FString::Format("[{:8.3f}] {}: {}", Message.Time,
		                                                                              Message.Category, Message.Text))];
	                       })]];
	Poll();
	RegisterActiveTimer(0.1f, [this](double, float)
	{
		Poll();
		return EActiveTimerReturn::Continue;
	});
}

bool SOutputLog::Passes(const FOutputLogMessage& Message) const
{
	const bool bError = Message.Verbosity <= ELogVerbosity::Error || IsErrorText(Message.Text);
	const bool bWarning = !bError && Message.Verbosity == ELogVerbosity::Warning;
	if ((bError && !Filter.bErrors) || (bWarning && !Filter.bWarnings) || (!bError && !bWarning && !Filter.bMessages))
	{
		return false;
	}
	if (Filter.Text.IsEmpty())
	{
		return true;
	}
	const FString Needle = Filter.Text.ToLower();
	return Message.Text.ToLower().Contains(Needle.View()) || Message.Category.ToLower().Contains(Needle.View());
}

void SOutputLog::SetFilterText(const FString& Text)
{
	Filter.Text = Text.TrimStartAndEnd();
	Refilter();
}

void SOutputLog::Refilter()
{
	Shown.clear();
	for (SIZE_T Index = 0; Index < All.size(); ++Index)
	{
		if (Passes(All[Index]))
		{
			Shown.push_back(static_cast<int32>(Index));
		}
	}
	List->RequestListRefresh();
	List->ScrollToBottom();
}

void SOutputLog::Poll()
{
	FOutputLogHistory& History = FOutputLogHistory::Get();
	if (History.GetNextSerial() == NextSerial)
	{
		return;
	}
	const SIZE_T Before = All.size();
	NextSerial = History.GetMessages(NextSerial, All);
	bool bAdded = false;
	for (SIZE_T Index = Before; Index < All.size(); ++Index)
	{
		if (Passes(All[Index]))
		{
			Shown.push_back(static_cast<int32>(Index));
			bAdded = true;
		}
	}
	if (All.size() > FOutputLogHistory::MaxMessages)
	{
		const SIZE_T Drop = All.size() - FOutputLogHistory::MaxMessages;
		All.erase(All.begin(), All.begin() + static_cast<std::ptrdiff_t>(Drop));
		Refilter();
		return;
	}
	if (bAdded)
	{
		List->RequestListRefresh();
		List->ScrollToBottom();
	}
}
