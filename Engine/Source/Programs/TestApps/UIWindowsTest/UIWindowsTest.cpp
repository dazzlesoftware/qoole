// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// UIWindowsTest: windows. A main window, a borderless inspector window with
// a title bar drawn by widgets (it drags and resizes like an OS one), more
// OS windows, and what happens to them: DPI changes, minimize and restore,
// a move to another monitor, resize, and the application scale.
//
// Self-test: opens and closes windows, simulates DPI changes (100% and
// 200%), minimizes and restores, moves to another monitor if there is one,
// resizes, and sets the app scale; screenshots of every window after each.

#include "TestApp.h"
#include "TestWidgets.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

class FWindowsTestApp final : public FTestApp
{
public:
	FWindowsTestApp() : FTestApp({ "UIWindowsTest", "Genesis UI - Windows and DPI", FVector2(900.0f, 600.0f) }) {}

protected:
	TSharedRef<SWidget> MakeContent() override
	{
		const auto Line = [](TAttribute<FString> Text) -> TSharedRef<SWidget> { return SNew(STextBlock).Text(Text).AutoWrapText(true); };
		return TestFrame(*this, "Windows and DPI", TestPage({
			TestSection("This window", "F2 cycles the app scale, F3 simulates a move to a monitor with another DPI.",
				Line([this]
				{
					const TSharedPtr<SWindow>& Main = GetMainWindow();
					if (!Main)
					{
						return FString();
					}
					const FIntPoint Size = Main->GetNativeWindow()->GetClientSize();
					const FIntPoint Pos = Main->GetNativeWindow()->GetClientPosition();
					return FString::Format("DPI scale {:.2f}, app scale {:.2f}: {}x{} px at {}, {}; {} windows open", Main->GetNativeWindow()->GetDpiScale(), GetAppScale(),
						Size.X, Size.Y, Pos.X, Pos.Y, GetApp().GetWindows().size());
				})),
			TestSection("Monitors", "As the platform reports them.", Line([this]
			{
				FDisplayMetrics Metrics;
				GetPlatform().GetDisplayMetrics(Metrics);
				FString Text;
				for (const FMonitorInfo& Monitor : Metrics.Monitors)
				{
					Text += FString::Format("{}{}{}: {}x{} at {}, {}, {:.0f}%", Text.IsEmpty() ? "" : "\n", Monitor.Name.Mid(4), Monitor.bIsPrimary ? " (primary)" : "",
						Monitor.DisplayRect.Width(), Monitor.DisplayRect.Height(), Monitor.DisplayRect.Left, Monitor.DisplayRect.Top, Monitor.DpiScale * 100.0f);
				}
				return Text;
			})),
			TestSection("More windows", "The inspector has no OS frame: its title bar is widgets. Other windows are ordinary ones.",
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.0f, 0.0f, 8.0f, 0.0f))[SNew(SButton).Text("Toggle inspector").OnClicked([this] { ToggleInspector(); return FReply::Handled(); })]
				+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text("New window").OnClicked([this] { OpenWindow(); return FReply::Handled(); })]),
		}));
	}

	void OnStarted() override
	{
		CreateInspector();
	}

	FString GetWindowLabel(const SWindow& Window) const override
	{
		return Inspector && &Window == Inspector.get() ? FString("inspector") : FTestApp::GetWindowLabel(Window);
	}

	void OnShutdown() override
	{
		Inspector.reset();
	}

	void BuildSelfTest(FTestScript& Script) override
	{
		Script.Capture("start")
			.Check("the inspector is open", [this] { return Inspector && GetApp().GetWindows().size() == 2; })
			.Do([this] { PointAtText("New window", true); })
			.Check("a new window opens", [this] { return GetApp().GetWindows().size() == 3; }, 5)
			.Capture("three_windows")
			.Do([this] { GetApp().GetWindows().back()->RequestClose(); })
			.Check("closing it leaves two", [this] { return GetApp().GetWindows().size() == 2; }, 5)
			// The inspector's close button is a widget.
			.Do([this] { PointAtText("×", true, Inspector.get()); })
			.Check("the inspector closes from its own button", [this] { return !Inspector && GetApp().GetWindows().size() == 1; }, 5)
			.Do([this] { ToggleInspector(); })
			.Check("and opens again", [this] { return Inspector != nullptr; }, 5)
			// DPI changes keep the logical size.
			.Do([this] { SimulateDpiChange(1.0f); })
			.Check("100%: the window's pixels are its logical size", [this] { return FMath::Abs(GetMainWindow()->GetPixelSize().X - 900) < 30; }, 30)
			.Capture("dpi100")
			.Do([this] { SimulateDpiChange(2.0f); })
			.Check("200%: twice the pixels", [this] { return FMath::Abs(GetMainWindow()->GetPixelSize().X - 1800) < 60; }, 30)
			.Capture("dpi200")
			.Do([this] { SimulateDpiChange(GetNativeDpiScale()); })
			// Minimize and restore.
			.Do([this] { GetMainWindow()->GetNativeWindow()->Minimize(); }, 20)
			.Check("minimized", [this] { return GetMainWindow()->GetNativeWindow()->IsMinimized(); }, 30)
			.Do([this] { GetMainWindow()->GetNativeWindow()->Restore(); })
			.Check("restored", [this] { return !GetMainWindow()->GetNativeWindow()->IsMinimized(); }, 30)
			.Capture("restored")
			// Another monitor, when there is one.
			.Do([this] { MoveToOtherMonitor(); })
			.Capture("monitor2", 40)
			// Resize and app scale.
			.Do([this]
			{
				const TSharedRef<IPlatformWindow> Native = ToSharedRef(GetMainWindow()->GetNativeWindow());
				const FIntPoint Pos = Native->GetClientPosition();
				Native->ReshapeWindow(Pos.X, Pos.Y, 1500, 1000);
			})
			.Capture("resized", 30)
			.Do([this] { SetAppScale(1.25f); })
			.Check("the app scale applies to every window", [this] { return FMath::IsNearlyEqual(Inspector->GetScale(), Inspector->GetNativeWindow()->GetDpiScale() * 1.25f, 0.01f); }, 30)
			.Capture("appscale125");
	}

private:
	TSharedRef<SWidget> MakeInspectorContent()
	{
		const auto Line = [](TAttribute<FString> Text, bool bHeading = false) -> TSharedRef<SWidget>
		{
			return SNew(STextBlock).Text(Text).AutoWrapText(true).TextStyle(&TestTextStyle(bHeading ? "BoldText" : "NormalText"));
		};
		const TSharedRef<SVerticalBox> Lines = SNew(SVerticalBox);
		const auto Add = [&Lines](const TSharedRef<SWidget>& Widget, float Top = 2.0f) { Lines->AddSlot().AutoHeight().Padding(FMargin(0.0f, Top, 0.0f, 0.0f))[Widget]; };
		Add(Line("This window", true), 0.0f);
		Add(Line([this]
		{
			const FIntPoint Size = Inspector ? Inspector->GetNativeWindow()->GetClientSize() : FIntPoint();
			const FIntPoint Pos = Inspector ? Inspector->GetNativeWindow()->GetClientPosition() : FIntPoint();
			return FString::Format("DPI scale {:.2f} (app scale {:.2f}), {}x{} px at {}, {}",
				Inspector ? Inspector->GetNativeWindow()->GetDpiScale() : 1.0f, GetAppScale(), Size.X, Size.Y, Pos.X, Pos.Y);
		}));
		Add(Line("Drag the title bar to move it; resize from any edge."));
		Add(Line("Input", true), 10.0f);
		Add(Line([this]
		{
			const TSharedPtr<SWidget> Focused = GetApp().GetKeyboardFocusedWidget();
			const TArray<TSharedRef<SWidget>> Hovered = GetApp().GetHoveredPath();
			return FString::Format("Focus: {}\nUnder the pointer: {}\nCaptured by: {}\nMenus open: {}",
				Focused ? Focused->GetTypeName() : FString("none"), Hovered.empty() ? FString("none") : Hovered.back()->GetTypeName(),
				GetApp().GetMouseCaptor() ? GetApp().GetMouseCaptor()->GetTypeName() : FString("none"), GetApp().GetMenuCount());
		}));
		Add(Line("Renderer", true), 10.0f);
		Add(Line([this]
		{
			const FSlateRenderStats& Stats = GetWindowRenderer().GetStats(*GetMainWindow()->GetNativeWindow());
			return FString::Format("{}: {}\nMain window: {} elements, {} batches\nGlyph atlas pages: {}\nFrames: {}", ToString(GetBackend()), GetDevice().GetAdapterName(),
				Stats.Elements, Stats.Batches, GetFontCache().GetPageCount(), GetApp().GetFrameNumber());
		}));

		const FWindowStyle& WindowStyle = FAppStyle::Get().GetWidgetStyle<FWindowStyle>("Window");
		return SNew(SVerticalBox)
			// The UI-drawn title bar: the empty part drags the window (the
			// window's TitleBarHeight), the close button claims its own area.
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBorder).BorderImage(&WindowStyle.TitleBar).Padding(FMargin(12.0f, 0.0f, 0.0f, 0.0f))
				[
					SNew(SBox).HeightOverride(TOptional<float>(34.0f))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[SNew(STextBlock).Text("Inspector — borderless window").TextStyle(&WindowStyle.TitleText)]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(SButton).ButtonStyle(&WindowStyle.CloseButton).IsFocusable(false).ContentPadding(FMargin(16.0f, 0.0f)).Text("×")
							.OnClicked([this] { ToggleInspector(); return FReply::Handled(); })
						]
					]
				]
			]
			+ SVerticalBox::Slot().FillHeight(1.0f).Padding(FMargin(14.0f, 12.0f))[Lines];
	}

	void CreateInspector()
	{
		// To the right of the main window when there is room.
		const TSharedPtr<SWindow>& Main = GetMainWindow();
		const FIntPoint MainPos = Main->GetNativeWindow()->GetClientPosition();
		const FIntPoint MainSize = Main->GetNativeWindow()->GetClientSize();
		const float Dpi = Main->GetNativeWindow()->GetDpiScale();
		FIntPoint Position(MainPos.X + MainSize.X + FMath::RoundToInt(16 * Dpi), MainPos.Y);
		FDisplayMetrics Metrics;
		GetPlatform().GetDisplayMetrics(Metrics);
		if (const FMonitorInfo* Primary = Metrics.GetPrimary())
		{
			const int32 Width = FMath::RoundToInt(380.0f * Primary->DpiScale);
			if (Position.X + Width > Primary->WorkArea.Right)
			{
				Position = FIntPoint(Primary->WorkArea.Right - Width - 16, MainPos.Y + 40);
			}
		}
		Inspector = SNew(SWindow)
			.Title("Inspector")
			.ClientSize(FVector2(380.0f, 420.0f))
			.ScreenPosition(TOptional<FIntPoint>(Position))
			.UseOSBorder(false)
			.SupportsMaximize(false)
			.AppearsInTaskbar(false)
			.TitleBarHeight(34.0f)
			.OnWindowClosed([this](const TSharedRef<SWindow>&) { Inspector.reset(); })
			[
				MakeInspectorContent()
			];
		GetApp().AddWindow(ToSharedRef(Inspector));
	}

	void ToggleInspector()
	{
		if (Inspector)
		{
			Inspector->RequestClose();
		}
		else
		{
			CreateInspector();
		}
	}

	void OpenWindow()
	{
		const int32 Number = ++WindowsOpened;
		const TSharedRef<SWindow> Window = SNew(SWindow)
			.Title(FString::Format("Window {}", Number))
			.ClientSize(FVector2(360.0f, 200.0f))
			[
				SNew(SBox).Padding(FMargin(16.0f)).VAlign(VAlign_Center).HAlign(HAlign_Center)
				[
					SNew(STextBlock).Text(FString::Format("Window {}: an ordinary OS window", Number))
				]
			];
		GetApp().AddWindow(Window);
		SetLastEvent(FString::Format("Opened window {}", Number));
	}

	void MoveToOtherMonitor()
	{
		FDisplayMetrics Metrics;
		GetPlatform().GetDisplayMetrics(Metrics);
		const TSharedRef<IPlatformWindow> Native = ToSharedRef(GetMainWindow()->GetNativeWindow());
		const FIntPoint Pos = Native->GetClientPosition();
		for (const FMonitorInfo& Monitor : Metrics.Monitors)
		{
			const bool bContains = Pos.X >= Monitor.DisplayRect.Left && Pos.X < Monitor.DisplayRect.Right && Pos.Y >= Monitor.DisplayRect.Top && Pos.Y < Monitor.DisplayRect.Bottom;
			if (!bContains)
			{
				GE_LOG(LogTestApp, Display, "Moving the main window to monitor {} ({:.0f}%)", Monitor.Name, Monitor.DpiScale * 100.0f);
				Native->MoveWindowTo(Monitor.WorkArea.Left + 80, Monitor.WorkArea.Top + 80);
				if (Inspector)
				{
					Inspector->GetNativeWindow()->MoveWindowTo(Monitor.WorkArea.Left + 80 + Native->GetClientSize().X + 24, Monitor.WorkArea.Top + 80);
				}
				return;
			}
		}
		GE_LOG(LogTestApp, Display, "One monitor: nothing to move to");
	}

	TSharedPtr<SWindow> Inspector;
	int32 WindowsOpened = 0;
};

GENESIS_TEST_APP(FWindowsTestApp)
