// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "TestHelpers.h"

#include "Null/NullPlatformApplication.h"

namespace
{
	class FRecordingHandler final : public IPlatformMessageHandler
	{
	public:
		bool OnKeyDown(EKey Key, uint32, bool) override
		{
			Log.push_back(FString::Format("down {}", ToString(Key)));
			return true;
		}
		bool OnKeyChar(char32_t Character, bool) override
		{
			Log.push_back(FString::Format("char {}", static_cast<uint32>(Character)));
			return true;
		}
		bool OnMouseDown(const TSharedRef<IPlatformWindow>&, EMouseButton, FVector2 Position) override
		{
			Log.push_back(FString::Format("click {},{}", Position.X, Position.Y));
			return true;
		}
		void OnDpiScaleChanged(const TSharedRef<IPlatformWindow>&, float NewScale) override
		{
			Log.push_back(FString::Format("dpi {}", NewScale));
		}
		void OnSizeChanged(const TSharedRef<IPlatformWindow>&, int32 Width, int32 Height, bool bMinimized) override
		{
			Log.push_back(FString::Format("size {}x{}{}", Width, Height, bMinimized ? " min" : ""));
		}

		TArray<FString> Log;
	};
} // namespace

TEST_SUITE("Platform")
{
	TEST_CASE("Null platform: windows scale their logical size by DPI")
	{
		FNullPlatformApplication App(1.5f);
		FWindowDesc Desc;
		Desc.Size = {400.0f, 300.0f};
		const TSharedRef<IPlatformWindow> Window = App.MakeWindow(Desc);
		CHECK(Window->GetDpiScale() == 1.5f);
		CHECK(Window->GetClientSize() == FIntPoint(600, 450));
		CHECK(Window->IsVisible());
		CHECK(App.IsHeadless());
	}

	TEST_CASE("Null platform: scripted input is deferred until ProcessDeferredEvents")
	{
		FNullPlatformApplication App;
		std::shared_ptr<FRecordingHandler> Handler = std::make_shared<FRecordingHandler>();
		App.SetMessageHandler(TSharedRef<IPlatformMessageHandler>(Handler));
		const TSharedRef<IPlatformWindow> Window = App.MakeWindow(FWindowDesc());

		App.QueueKeyDown(EKey::A);
		App.QueueChar(U'é');
		App.QueueMouseDown(Window, EMouseButton::Left, {12.0f, 34.0f});
		CHECK(Handler->Log.empty());
		CHECK(App.GetPendingEventCount() == 3);

		App.ProcessDeferredEvents(0.0f);
		REQUIRE(Handler->Log.size() == 3);
		CHECK(Handler->Log[0] == "down A");
		CHECK(Handler->Log[1] == "char 233");
		CHECK(Handler->Log[2] == "click 12,34");
		CHECK(App.GetPendingEventCount() == 0);
	}

	TEST_CASE("Null platform: DPI changes and minimize reach the handler immediately")
	{
		FNullPlatformApplication App;
		std::shared_ptr<FRecordingHandler> Handler = std::make_shared<FRecordingHandler>();
		App.SetMessageHandler(TSharedRef<IPlatformMessageHandler>(Handler));
		FWindowDesc Desc;
		Desc.Size = {200.0f, 100.0f};
		const TSharedRef<IPlatformWindow> Window = App.MakeWindow(Desc);

		static_cast<FNullPlatformWindow&>(Window.Get()).SimulateDpiChange(2.0f);
		Window->Minimize();
		CHECK(Window->GetClientSize() == FIntPoint(0, 0));
		Window->Restore();
		REQUIRE(Handler->Log.size() == 4);
		CHECK(Handler->Log[0] == "dpi 2");
		CHECK(Handler->Log[1] == "size 400x200");
		CHECK(Handler->Log[2] == "size 0x0 min");
		CHECK(Handler->Log[3] == "size 400x200");
	}

	TEST_CASE("Null platform: clipboard and raw mouse")
	{
		FNullPlatformApplication App;
		std::shared_ptr<FRecordingHandler> Handler = std::make_shared<FRecordingHandler>();
		App.SetMessageHandler(TSharedRef<IPlatformMessageHandler>(Handler));
		App.ClipboardCopy("copied");
		CHECK(App.ClipboardPaste() == "copied");
		App.QueueRawMouseMove(3, 4); // dropped: raw input disabled
		App.SetRawMouseInputEnabled(true);
		CHECK(App.IsRawMouseEnabled());
		App.ProcessDeferredEvents(0.0f);
	}
}
