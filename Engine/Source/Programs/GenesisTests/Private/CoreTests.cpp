/*
Copyright (C) 2026 Dazzle Software, LLC

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "doctest.h"
#include "Templates/SharedPointer.h"
#include "TestUtils.h"

#include "CoreMinimal.h"
#include "HAL/File.h"
#include "Misc/App.h"
#include "Services/Services.h"

TEST_CASE("FString holds UTF-8 and converts at the platform boundary")
{
	const FString Text(u8"Café 世界");
	CHECK(Text.Len() == 12); // bytes, not characters
	CHECK(FString::FromWide(Text.ToWide()) == Text);
	CHECK(Text.ToWide() == L"Café 世界");

	// The ANSI code page round trip keeps plain ASCII as is.
	CHECK(FString::FromAnsi("textures\\base") == FString("textures\\base"));
	CHECK(FString("textures\\base").ToAnsi() == "textures\\base");

	CHECK(FString(static_cast<const char*>(nullptr)).IsEmpty());
	CHECK(FString::Printf("%s=%d", "Grid", 16) == FString("Grid=16"));
	CHECK(FString("  padded\t\r\n").TrimStartAndEnd() == FString("padded"));
	CHECK(FString("WorldSpawn").Equals("worldspawn", ESearchCase::IgnoreCase));
	CHECK_FALSE(FString("WorldSpawn").Equals("worldspawn"));
	CHECK(FString("info_player_start").StartsWith("info_"));
	CHECK(FString("map.qle").EndsWith(".QLE", ESearchCase::IgnoreCase));
	CHECK(FString("abcabc").Find("ca") == 2);
	CHECK(FString("abc").Find("x") == -1);

	TMap<FString, int32> Map;
	Map["Draw"] = 1;
	CHECK(Map.count(FString("Draw")) == 1);
}

TEST_CASE("Containers and smart pointers are the standard ones")
{
	TArray<int32> Numbers = {3, 1, 2};
	CHECK(Numbers.size() == 3);
	TSharedPtr<int32> Shared = MakeShared<int32>(5);
	TWeakPtr<int32> Weak = Shared;
	CHECK(*Weak.lock() == 5);
	TUniquePtr<int32> Unique = MakeUnique<int32>(7);
	CHECK(*Unique == 7);
}

TEST_CASE("ensure reports and returns the condition")
{
	CHECK(ensure(1 + 1 == 2));
}

TEST_CASE("FLog passes text to output devices without changing it")
{
	struct FCaptureDevice : FOutputDevice
	{
		FString Captured;
		void Serialize(const FLogCategory&, ELogVerbosity, std::string_view Message) override
		{
			Captured += Message;
		}
	} Sink;

	FLog::AddDevice(&Sink);
	FLog::Write(LogCore, ELogVerbosity::Display, FString::Printf("Loading %s... ", "map").View());
	FLog::Write(LogCore, ELogVerbosity::Display, "OK.\n");
	FLog::RemoveDevice(&Sink);
	FLog::Write(LogCore, ELogVerbosity::Display, "not captured");

	CHECK(Sink.Captured == FString("Loading map... OK.\n"));
}

TEST_CASE("FFile reads, writes and deletes read-only files")
{
	FTempDir Temp;
	const FString Path = Temp.File("file.txt");

	CHECK_FALSE(FFile::Exists(Path));
	REQUIRE(FFile::WriteAllText(Path, u8"line 1\nlïne 2\n"));
	CHECK(FFile::Exists(Path));
	CHECK(FFile::DirectoryExists(Temp.Path));

	FString Text;
	REQUIRE(FFile::ReadAllText(Path, Text));
	CHECK(Text == FString(u8"line 1\nlïne 2\n"));

	FFile File;
	REQUIRE(File.Open(Path, EFileOpenMode::Read));
	CHECK(File.Size() == Text.Len());
	char Buffer[4] = {};
	CHECK(File.Read(Buffer, 4) == 4);
	CHECK(std::string_view(Buffer, 4) == "line");
	CHECK(File.Seek(5));
	CHECK(File.Tell() == 5);
	File.Close();

	REQUIRE(FFile::SetReadOnly(Path, true));
	CHECK(FFile::IsReadOnly(Path));
	CHECK_FALSE(FFile::Delete(Path));
	CHECK(FFile::Delete(Path, true));
	CHECK_FALSE(FFile::Exists(Path));
}

TEST_CASE("Services fall back to UI-free defaults")
{
	CHECK(FServices::GetMessageDialog().Show(EMessageType::Question, EMessageButtons::YesNo, "Extract?") ==
	    EMessageResult::No);
	CHECK(FServices::GetMessageDialog().Show(EMessageType::Error, EMessageButtons::Ok, "Error") == EMessageResult::Ok);

	FString Folder = "unchanged";
	CHECK_FALSE(FServices::GetFileDialogs().BrowseForFolder("Locate", Folder));
	CHECK(Folder == FString("unchanged"));

	FProgressOptions Options;
	Options.Text = "Working";
	TUniquePtr<IProgressTask> Task = FServices::GetProgress().Begin(Options);
	Task->SetPercent(50);
	CHECK_FALSE(Task->IsCancelled());

	struct FAlwaysYes : IMessageDialog
	{
		EMessageResult Show(EMessageType, EMessageButtons, const FString&, const FString&) override
		{
			return EMessageResult::Yes;
		}
	} Yes;
	FServices::SetMessageDialog(&Yes);
	CHECK(FServices::GetMessageDialog().Show(EMessageType::Question, EMessageButtons::YesNo, "Extract?") ==
	    EMessageResult::Yes);
	FServices::SetMessageDialog(nullptr);
	CHECK(FServices::GetMessageDialog().Show(EMessageType::Question, EMessageButtons::YesNo, "Extract?") ==
	    EMessageResult::No);
}

TEST_CASE("FApp knows its name and exit requests")
{
	CHECK(FApp::GetName() == FString("Genesis Editor"));
	CHECK(FFile::DirectoryExists(FPaths::ExecutableDir()));

	bool bHandled = false;
	FApp::SetRequestExitHandler([&] { bHandled = true; });
	FApp::RequestExit();
	FApp::SetRequestExitHandler(nullptr);
	CHECK(bHandled);
	CHECK(FApp::IsExitRequested());
}
