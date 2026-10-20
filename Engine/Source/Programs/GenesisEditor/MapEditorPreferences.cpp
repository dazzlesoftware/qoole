// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Edit > Editor Preferences: Unreal's preferences window. Categories at the
// left (General: Appearance, Graphics, Keyboard Shortcuts, Loading & Saving;
// Level Editor: Viewports, Grid & Snapping), the chosen one's settings at
// the right, and a search box over all of them: categories without a match
// leave the list, and the settings shown are filtered.
//
// Graphics (DirectX 12 or Vulkan, and the debug layer) take effect the next
// time the editor starts, as in Unreal; GenesisEditor.cpp reads them with
// FMapEditorShell::GetStartupGraphics. The command line still wins: -vulkan
// and -d3d12, -debug and -nodebug.

#include "MapEditorShell.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/PropertyGrid/SPropertyGrid.h"
#include "RHI.h"
#include "Services/Services.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/SEditableText.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"

namespace
{
	const FTextBlockStyle& SmallText() { return FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("SmallText"); }

	// Rebinds a command by pressing keys (Esc cancels, Backspace clears).
	class SChordEditor final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SChordEditor)
		SLATE_END_ARGS()

		void Construct(const FArguments&, const TSharedRef<FUICommandInfo>& InCommand, TFunction<void(const FString&)> InLog)
		{
			Command = InCommand.ToSharedPtr();
			Log = std::move(InLog);
			ChildSlot
			[
				SNew(SButton).HAlign(HAlign_Left).IsFocusable(false)
				.Text([this] { return bCapturing ? FString("Press keys (Esc cancels, Backspace clears)") : (Command->GetInputText().IsEmpty() ? FString("(none)") : Command->GetInputText()); })
				.OnClicked([this]
				{
					bCapturing = true;
					return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
				})
			];
		}

		bool SupportsKeyboardFocus() const override { return true; }

		FReply OnPreviewKeyDown(const FGeometry&, const FKeyEvent& Event) override
		{
			if (!bCapturing)
			{
				return FReply::Unhandled();
			}
			const EKey Key = Event.GetKey();
			if (Key == EKey::LeftShift || Key == EKey::RightShift || Key == EKey::LeftControl || Key == EKey::RightControl || Key == EKey::LeftAlt || Key == EKey::RightAlt)
			{
				return FReply::Handled(); // wait for the real key
			}
			bCapturing = false;
			if (Key == EKey::Escape)
			{
				return FReply::Handled().ClearUserFocus();
			}
			FInputChord Chord;
			if (Key != EKey::BackSpace)
			{
				Chord.Key = Key;
				Chord.Modifiers = (Event.IsControlDown() ? EModifierKey::Control : EModifierKey::None) | (Event.IsShiftDown() ? EModifierKey::Shift : EModifierKey::None)
					| (Event.IsAltDown() ? EModifierKey::Alt : EModifierKey::None);
			}
			const TSharedPtr<FUICommandInfo> Displaced = FInputBindingManager::Get().SetUserChord(ToSharedRef(Command), Chord);
			Log(FString::Format("Bound \"{}\" to {}{}", Command->GetLabel(), Chord.IsValid() ? Chord.ToString() : FString("nothing"),
				Displaced ? FString::Format(" (taken from \"{}\")", Displaced->GetLabel()) : FString()));
			return FReply::Handled().ClearUserFocus();
		}

		void OnFocusLost(const FFocusEvent&) override { bCapturing = false; }

	private:
		TSharedPtr<FUICommandInfo> Command;
		TFunction<void(const FString&)> Log;
		bool bCapturing = false;
	};

	// A category's settings, as getters and setters.
	class FPreferenceSource final : public IPropertySource
	{
	public:
		struct FSetting
		{
			FPropertyDesc Desc;
			TFunction<FPropertyValue()> Get;
			TFunction<void(const FPropertyValue&)> Set;
		};

		FPreferenceSource(FString InName, TArray<FSetting> InSettings) : Name(std::move(InName)), Settings(std::move(InSettings)) {}

		FString GetDisplayName() const override { return Name; }

		void GetProperties(TArray<FPropertyDesc>& Out) const override
		{
			for (const FSetting& Setting : Settings)
			{
				Out.push_back(Setting.Desc);
			}
		}

		FPropertyValue GetValue(FName Property) const override
		{
			const FSetting* Setting = Find(Property);
			return Setting ? Setting->Get() : FPropertyValue(false);
		}

		void SetValue(FName Property, const FPropertyValue& Value) override
		{
			if (const FSetting* Setting = Find(Property); Setting && Setting->Set)
			{
				Setting->Set(Value);
			}
		}

		// Whether a setting's name or group has Text in it (for the search).
		bool Matches(const FString& Text) const
		{
			const FString Needle = Text.ToLower();
			return std::any_of(Settings.begin(), Settings.end(), [&Needle](const FSetting& Setting)
			{
				return Setting.Desc.DisplayName.ToLower().Contains(Needle.View()) || Setting.Desc.Category.ToLower().Contains(Needle.View());
			});
		}

	private:
		const FSetting* Find(FName Property) const
		{
			for (const FSetting& Setting : Settings)
			{
				if (Setting.Desc.Name == Property)
				{
					return &Setting;
				}
			}
			return nullptr;
		}

		FString Name;
		TArray<FSetting> Settings;
	};

	struct FPreferenceCategory
	{
		FString Section;     // "General", "Level Editor"
		FString Name;        // "Appearance"
		FString Description;
		TSharedPtr<FPreferenceSource> Source; // null: the keyboard shortcuts
	};

	const char* const BindingContexts[] = { "MapEditor", "MapViewport" };

	// The key bindings of the editor's and the viewports' commands.
	class SKeyboardShortcuts final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SKeyboardShortcuts)
		SLATE_END_ARGS()

		void Construct(const FArguments&, TFunction<void(const FString&)> InLog)
		{
			Log = std::move(InLog);
			ChildSlot
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, 6.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						SNew(STextBlock).TextStyle(&SmallText()).AutoWrapText(true).Text("Click a shortcut, then press the new keys (Esc cancels, Backspace clears).")
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SButton).Text("Reset to Defaults").OnClicked([this]
						{
							FInputBindingManager::Get().ResetToDefaults();
							Log("Key bindings reset");
							return FReply::Handled();
						})
					]
				]
				+ SVerticalBox::Slot().FillHeight(1.0f)
				[
					SAssignNew(List, SListView<TSharedPtr<FUICommandInfo>>)
					.ListItemsSource(&Shown)
					.SelectionMode(ESelectionMode::None)
					.OnGenerateRow([this](TSharedPtr<FUICommandInfo> Command, const TSharedRef<STableViewBase>& Owner) -> TSharedRef<ITableRow>
					{
						const TSharedPtr<FBindingContext> Context = FInputBindingManager::Get().FindContext(Command->GetContext());
						return SNew(STableRow<TSharedPtr<FUICommandInfo>>, Owner).Padding(FMargin(8.0f, 2.0f))
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(0.4f).VAlign(VAlign_Center)[SNew(STextBlock).Text(Command->GetLabel()).ToolTipText(Command->GetDescription())]
							+ SHorizontalBox::Slot().FillWidth(0.2f).VAlign(VAlign_Center)[SNew(STextBlock).TextStyle(&SmallText()).Text(Context ? Context->GetDescription() : FString())]
							+ SHorizontalBox::Slot().FillWidth(0.4f)[SNew(SChordEditor, ToSharedRef(Command), Log)]
						];
					})
				]
			];
			SetFilterText(FString());
		}

		void SetFilterText(const FString& Text)
		{
			const FString Needle = Text.TrimStartAndEnd().ToLower();
			Shown.clear();
			for (const char* ContextName : BindingContexts)
			{
				if (const TSharedPtr<FBindingContext> Context = FInputBindingManager::Get().FindContext(ContextName))
				{
					for (const TSharedRef<FUICommandInfo>& Command : Context->GetCommands())
					{
						if (Needle.IsEmpty() || Command->GetLabel().ToLower().Contains(Needle.View()) || Command->GetInputText().ToLower().Contains(Needle.View()))
						{
							Shown.push_back(Command.ToSharedPtr());
						}
					}
				}
			}
			List->RequestListRefresh();
		}

		static bool Matches(const FString& Text)
		{
			const FString Needle = Text.ToLower();
			for (const char* ContextName : BindingContexts)
			{
				if (const TSharedPtr<FBindingContext> Context = FInputBindingManager::Get().FindContext(ContextName))
				{
					for (const TSharedRef<FUICommandInfo>& Command : Context->GetCommands())
					{
						if (Command->GetLabel().ToLower().Contains(Needle.View()))
						{
							return true;
						}
					}
				}
			}
			return false;
		}

	private:
		TArray<TSharedPtr<FUICommandInfo>> Shown;
		TSharedPtr<SListView<TSharedPtr<FUICommandInfo>>> List;
		TFunction<void(const FString&)> Log;
	};

	// The window's content.
	class SEditorPreferences final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SEditorPreferences)
		SLATE_END_ARGS()

		void Construct(const FArguments&, TArray<FPreferenceCategory> InCategories, TFunction<void(const FString&)> InLog)
		{
			Categories = std::move(InCategories);
			Log = std::move(InLog);
			const TSharedRef<SVerticalBox> Left = SNew(SVerticalBox);
			FString Section;
			for (SIZE_T Index = 0; Index < Categories.size(); ++Index)
			{
				const FPreferenceCategory& Category = Categories[Index];
				if (Category.Section != Section)
				{
					Section = Category.Section;
					Left->AddSlot().AutoHeight().Padding(FMargin(10.0f, Index == 0 ? 6.0f : 14.0f, 10.0f, 4.0f))
					[
						SNew(STextBlock).Text(Section).TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("BoldText"))
					];
				}
				Left->AddSlot().AutoHeight()
				[
					SNew(SButton)
					.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("MenuItem"))
					.HAlign(HAlign_Left)
					.IsFocusable(false)
					.Visibility([this, Index] { return IsListed(Index) ? EVisibility::Visible : EVisibility::Collapsed; })
					.OnClicked([this, Index] { Select(Index); return FReply::Handled(); })
					[
						SNew(STextBlock).Text(Category.Name)
						.ColorAndOpacity([this, Index] { return FSlateColor(Selected == Index ? EStyleColor::Primary : EStyleColor::Foreground); })
					]
				];
			}
			ChildSlot
			[
				SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("Panel")).Padding(0.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SBorder).BorderImage(&FAppStyle::Get().GetBrush("Header")).Padding(FMargin(4.0f, 6.0f))
						[
							SNew(SBox).WidthOverride(230.0f)[SNew(SScrollBox) + SScrollBox::Slot()[Left]]
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(16.0f, 10.0f))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SEditableTextBox).HintText("Search in all settings...").OnTextChanged([this](const FString& Text) { SetSearchText(Text); })
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 14.0f, 0.0f, 2.0f))
						[
							SNew(STextBlock).TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("HeadingText")).Text([this]
							{
								return FString::Format("{} - {}", Categories[Selected].Section, Categories[Selected].Name);
							})
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, 10.0f))
						[
							SNew(STextBlock).TextStyle(&SmallText()).AutoWrapText(true).Text([this] { return Categories[Selected].Description; })
						]
						+ SVerticalBox::Slot().FillHeight(1.0f)[SAssignNew(Host, SBox)]
					]
				]
			];
			Select(0);
		}

		void Select(SIZE_T Index)
		{
			Selected = Index;
			const FPreferenceCategory& Category = Categories[Index];
			if (!Category.Source)
			{
				TSharedPtr<SKeyboardShortcuts> Shortcuts;
				Host->SetContent(SAssignNew(Shortcuts, SKeyboardShortcuts, Log));
				Shortcuts->SetFilterText(SearchText);
				return;
			}
			TSharedPtr<SPropertyGrid> Grid;
			Host->SetContent(SAssignNew(Grid, SPropertyGrid).ShowSearch(false));
			Grid->SetObjects({ TSharedRef<IPropertySource>(StaticCastSharedPtr<IPropertySource>(Category.Source)) });
			Grid->SetFilterText(SearchText);
		}

		void SetSearchText(const FString& Text)
		{
			SearchText = Text.TrimStartAndEnd();
			if (!IsListed(Selected))
			{
				for (SIZE_T Index = 0; Index < Categories.size(); ++Index)
				{
					if (IsListed(Index))
					{
						Select(Index);
						return;
					}
				}
			}
			Select(Selected);
		}

	private:
		bool IsListed(SIZE_T Index) const
		{
			if (SearchText.IsEmpty())
			{
				return true;
			}
			const FPreferenceCategory& Category = Categories[Index];
			return Category.Source ? Category.Source->Matches(SearchText) : SKeyboardShortcuts::Matches(SearchText);
		}

		TArray<FPreferenceCategory> Categories;
		TFunction<void(const FString&)> Log;
		SIZE_T Selected = 0;
		FString SearchText;
		TSharedPtr<SBox> Host;
	};

	TArray<FString> GridSizeOptions()
	{
		TArray<FString> Options;
		for (int32 Size = 1; Size <= 256; Size *= 2)
		{
			Options.push_back(FString::Format("{}", Size));
		}
		return Options;
	}
}

FMapEditorShell::FStartupGraphics FMapEditorShell::GetStartupGraphics()
{
	FStartupGraphics Graphics;
	IConfigStore& Config = FServices::GetConfig();
	FString Text;
	if (Config.GetString("Editor.Graphics", "RHI", Text))
	{
		Graphics.Backend = Text.Equals("Vulkan", ESearchCase::IgnoreCase) ? ERHIBackend::Vulkan : ERHIBackend::D3D12;
	}
	if (Config.GetString("Editor.Graphics", "DebugLayer", Text) && !Text.Equals("Default", ESearchCase::IgnoreCase))
	{
		Graphics.DebugLayer = Text.Equals("On", ESearchCase::IgnoreCase);
	}
	return Graphics;
}

FMapEditorShell::EStartupMap FMapEditorShell::GetStartupMapPreference()
{
	int32 Value = 0;
	FServices::GetConfig().GetInt("Editor.LoadingSaving", "StartupMap", Value);
	return Value == 1 ? EStartupMap::LastOpened : EStartupMap::Empty;
}

void FMapEditorShell::OpenPreferences()
{
	if (const TSharedPtr<SWindow> Existing = PreferencesWindow.lock())
	{
		if (Existing->GetNativeWindow())
		{
			Existing->GetNativeWindow()->BringToFront();
		}
		return;
	}
	using FSetting = FPreferenceSource::FSetting;
	IConfigStore* Config = &FServices::GetConfig();
	FMapViewSettings* Settings = &ViewSettings;
	const auto Changed = [Settings] { Settings->Changed(); };
	TArray<FPreferenceCategory> Categories;

	{
		TArray<FSetting> List;
		List.push_back({ FPropertyDesc("InterfaceScale", EPropertyType::Enum, "Application Scale", "User Interface").SetOptions({ "100%", "125%", "150%", "200%" })
			.SetToolTip("Scales the interface on top of the monitor's DPI"),
			[]
			{
				const float Scale = FSlateApplication::Get().GetApplicationScale();
				return FPropertyValue(static_cast<int32>(Scale >= 1.9f ? 3 : (Scale >= 1.4f ? 2 : (Scale >= 1.2f ? 1 : 0))));
			},
			[this](const FPropertyValue& Value)
			{
				static const float Scales[] = { 1.0f, 1.25f, 1.5f, 2.0f };
				Host.SetAppScale(Scales[FMath::Clamp(std::get<int32>(Value), 0, 3)]);
			} });
		Categories.push_back({ "General", "Appearance", "How the editor looks.", std::make_shared<FPreferenceSource>("Appearance", std::move(List)) });
	}
	{
		const ERHIBackend Running = Host.Device ? Host.Device->GetBackend() : ERHIBackend::D3D12;
		const FString Adapter = Host.Device ? Host.Device->GetAdapterName() : FString();
		TArray<FSetting> List;
		List.push_back({ FPropertyDesc("RHI", EPropertyType::Enum, "Graphics RHI", "Graphics").SetOptions({ "DirectX 12", "Vulkan" })
			.SetToolTip("What the editor draws with. Takes effect the next time the editor starts (-vulkan or -d3d12 on the command line win)."),
			[Config, Running]
			{
				FString Text;
				const bool bVulkan = Config->GetString("Editor.Graphics", "RHI", Text) ? Text.Equals("Vulkan", ESearchCase::IgnoreCase) : Running == ERHIBackend::Vulkan;
				return FPropertyValue(static_cast<int32>(bVulkan ? 1 : 0));
			},
			[this, Config, Running](const FPropertyValue& Value)
			{
				const ERHIBackend Wanted = std::get<int32>(Value) == 1 ? ERHIBackend::Vulkan : ERHIBackend::D3D12;
				Config->SetString("Editor.Graphics", "RHI", Wanted == ERHIBackend::Vulkan ? "Vulkan" : "D3D12");
				Config->Flush();
				Log(Wanted == Running ? FString::Format("Graphics RHI: {}, as now", ToString(Wanted))
					: FString::Format("Graphics RHI: {} from the next start; restart the editor to switch", ToString(Wanted)));
			} });
		List.push_back({ FPropertyDesc("DebugLayer", EPropertyType::Enum, "Debug Layer", "Graphics").SetOptions({ "Default", "On", "Off" })
			.SetToolTip("The D3D12 debug layer or Vulkan validation. Default: on in Debug builds. Takes effect at the next start."),
			[Config]
			{
				FString Text;
				Config->GetString("Editor.Graphics", "DebugLayer", Text);
				return FPropertyValue(static_cast<int32>(Text.Equals("On", ESearchCase::IgnoreCase) ? 1 : (Text.Equals("Off", ESearchCase::IgnoreCase) ? 2 : 0)));
			},
			[Config](const FPropertyValue& Value)
			{
				static const char* const Names[] = { "Default", "On", "Off" };
				Config->SetString("Editor.Graphics", "DebugLayer", Names[FMath::Clamp(std::get<int32>(Value), 0, 2)]);
				Config->Flush();
			} });
		List.push_back({ FPropertyDesc("Running", EPropertyType::String, "Running On", "Graphics").SetReadOnly(),
			[Running, Adapter]
			{
				return FPropertyValue(FString::Format("{}{}", Running == ERHIBackend::Vulkan ? "Vulkan" : "DirectX 12", Adapter.IsEmpty() ? FString() : FString::Format(" on {}", Adapter)));
			},
			nullptr });
		Categories.push_back({ "General", "Graphics", "The graphics API the editor draws with. Changes apply the next time the editor starts.",
			std::make_shared<FPreferenceSource>("Graphics", std::move(List)) });
	}
	Categories.push_back({ "General", "Keyboard Shortcuts", "The keys of the editor's and the viewports' commands.", nullptr });
	{
		TArray<FSetting> List;
		List.push_back({ FPropertyDesc("StartupMap", EPropertyType::Enum, "Load Level at Startup", "Startup").SetOptions({ "Empty Level", "Last Opened" })
			.SetToolTip("What the editor opens when no map is named on the command line"),
			[Config]
			{
				int32 Value = 0;
				Config->GetInt("Editor.LoadingSaving", "StartupMap", Value);
				return FPropertyValue(FMath::Clamp(Value, 0, 1));
			},
			[Config](const FPropertyValue& Value)
			{
				Config->SetInt("Editor.LoadingSaving", "StartupMap", std::get<int32>(Value));
				Config->Flush();
			} });
		Categories.push_back({ "General", "Loading & Saving", "What the editor opens when it starts.", std::make_shared<FPreferenceSource>("Loading & Saving", std::move(List)) });
	}
	{
		TArray<FSetting> List;
		List.push_back({ FPropertyDesc("MouseSensitivity", EPropertyType::Float, "Mouse Sensitivity", "Controls").SetRange(0.1f, 4.0f)
			.SetToolTip("How fast perspective views turn with the mouse"),
			[Settings] { return FPropertyValue(Settings->MouseSensitivity); },
			[Settings, Changed](const FPropertyValue& Value) { Settings->MouseSensitivity = std::get<float>(Value); Changed(); } });
		List.push_back({ FPropertyDesc("InvertMiddleMousePan", EPropertyType::Bool, "Invert Middle Mouse Pan", "Controls"),
			[Settings] { return FPropertyValue(Settings->bInvertMiddleMousePan); },
			[Settings, Changed](const FPropertyValue& Value) { Settings->bInvertMiddleMousePan = std::get<bool>(Value); Changed(); } });
		List.push_back({ FPropertyDesc("ShowGrid", EPropertyType::Bool, "Show Grid", "Look and Feel").SetToolTip("The grid in every view (each view's Show menu can hide it there)"),
			[Settings] { return FPropertyValue(Settings->bShowGrid); },
			[Settings, Changed](const FPropertyValue& Value) { Settings->bShowGrid = std::get<bool>(Value); Changed(); } });
		Categories.push_back({ "Level Editor", "Viewports", "How the viewports move and what they show.", std::make_shared<FPreferenceSource>("Viewports", std::move(List)) });
	}
	{
		TArray<FSetting> List;
		List.push_back({ FPropertyDesc("GridSize", EPropertyType::Enum, "Grid Size", "Grid").SetOptions(GridSizeOptions()).SetToolTip("Map units between grid lines"),
			[Settings]
			{
				int32 Index = 0;
				for (float Size = 1.0f; Size < Settings->GridSize && Index < 8; Size *= 2.0f)
				{
					++Index;
				}
				return FPropertyValue(Index);
			},
			[this](const FPropertyValue& Value) { SetGridSize(static_cast<float>(1 << FMath::Clamp(std::get<int32>(Value), 0, 8))); } });
		List.push_back({ FPropertyDesc("Snap", EPropertyType::Bool, "Snap to Grid", "Grid"),
			[Settings] { return FPropertyValue(Settings->bSnapToGrid); },
			[Settings, Changed](const FPropertyValue& Value) { Settings->bSnapToGrid = std::get<bool>(Value); Changed(); } });
		List.push_back({ FPropertyDesc("SnapRotation", EPropertyType::Bool, "Snap Rotation", "Rotation"),
			[Settings] { return FPropertyValue(Settings->bSnapRotation); },
			[Settings, Changed](const FPropertyValue& Value) { Settings->bSnapRotation = std::get<bool>(Value); Changed(); } });
		List.push_back({ FPropertyDesc("RotateSnap", EPropertyType::Float, "Rotation Increment", "Rotation").SetRange(1.0f, 90.0f).SetToolTip("Degrees the Rotate tool turns by"),
			[Settings] { return FPropertyValue(Settings->RotateSnapDegrees); },
			[Settings, Changed](const FPropertyValue& Value) { Settings->RotateSnapDegrees = std::get<float>(Value); Changed(); } });
		Categories.push_back({ "Level Editor", "Grid & Snapping", "The grid, and how moves and turns snap.", std::make_shared<FPreferenceSource>("Grid & Snapping", std::move(List)) });
	}

	const TSharedRef<SWindow> Window = SNew(SWindow)
		.Title("Editor Preferences")
		.ClientSize(FVector2(1040.0f, 680.0f))
		[
			SNew(SEditorPreferences, std::move(Categories), [this](const FString& Line) { Log(Line); })
		];
	PreferencesWindow = Window.ToSharedPtr();
	FSlateApplication::Get().AddWindow(Window);
}
