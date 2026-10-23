// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "SFoliageEditorPanel.h"
#include "Containers/ContainerAliases.h"
#include "Templates/SharedPointer.h"
#include "Internationalization/TextLocalization.h"
#include "Widgets/FeaturePanelWidgets.h"
#include "MapCoreHeaders.h"
#include "MapDocument.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SColorPicker.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Text/SEditableText.h"
#include "Services/MessageDialog.h"
#include "Services/Services.h"
#include "Services/FileDialogs.h"
#include "MapLandscapeTextures.h"
#include <filesystem>
using namespace FeaturePanelWidgets;
namespace
{
	const FTextBlockStyle& SmallText()
	{
		return FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("SmallText");
	}
} // namespace
void SFoliageEditorPanel::Construct(const FArguments&, FMapFoliageMode* InMode, FMapFeaturePanelHost InHost)
{
	check(InMode);
	Mode = InMode;
	Host = std::move(InHost);
	ChildSlot[MakePanel()];
}

// ----- Foliage -----

namespace
{
	// A labeled number for a foliage type's setting: a drag is one undo step.
	TSharedRef<SWidget> FoliageSpin(TFunction<void()> Begin, TFunction<void()> End, const char* Label, const char* Tip,
	    TFunction<float()> Get, TFunction<void(float)> Set, float Min, float Max, int32 Digits)
	{
		return SNew(SHorizontalBox).ToolTipText(GENESIS_TEXT(Tip)) +
		    SHorizontalBox::Slot().FillWidth(0.5f).VAlign(VAlign_Center)[SNew(STextBlock).Text(GENESIS_TEXT(Label))] +
		    SHorizontalBox::Slot().FillWidth(0.5f)[SNew(SSpinBox<float>)
		            .MinValue(Min)
		            .MaxValue(Max)
		            .FractionalDigits(Digits)
		            .Value([Get] { return TOptional<float>(Get()); })
		            .OnBeginSliderMovement([Begin] { Begin(); })
		            .OnEndSliderMovement([End](float)
		{ End(); }).OnValueChanged([Set](float Value) { Set(Value); })];
	}
} // namespace

TSharedRef<SWidget> SFoliageEditorPanel::MakePanel()
{
	FMapFoliageMode* Leaves = Mode;
	const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	struct FPadBuilder
	{
		const TSharedRef<SVerticalBox>& Box;
		void operator()(const TSharedRef<SWidget>& Widget, float Top = 4.0f) const
		{
			Box->AddSlot().AutoHeight().Padding(FMargin(8.0f, Top, 8.0f, 0.0f))[Widget];
		}
	};
	const FPadBuilder Pad{Box};

	// The tools, as Unreal's Foliage mode toolbar.
	Box->AddSlot().AutoHeight()[PanelHeader("Foliage")];
	const TSharedRef<SWrapBox> Tools = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2(4.0f, 4.0f));
	const TPair<EFoliageTool, const char*> ToolList[] = {
	    {EFoliageTool::Paint, "Paint the checked foliage types under the brush (Shift erases them)"},
	    {EFoliageTool::Erase, "Take the checked foliage types away under the brush"},
	    {EFoliageTool::Single, "Place one instance of the first checked type where you click"},
	};
	for (const std::pair<EFoliageTool, const char*>& ToolBinding : ToolList)
	{
		const EFoliageTool& Tool = ToolBinding.first;
		const char* const& Tip = ToolBinding.second;

		const EFoliageTool ThisTool = Tool;
		Tools->AddSlot()[SNew(SChoiceButton, FString(ToString(Tool)), FString(Tip))
		        .IsChosen([Leaves, ThisTool] { return Leaves->Tool == ThisTool; })
		        .OnClicked([this, Leaves, ThisTool]
		{
			Leaves->Tool = ThisTool;
			InvalidateViews();
		})];
	}
	Pad(Tools, 6.0f);

	Box->AddSlot().AutoHeight().Padding(FMargin(0.0f, 12.0f, 0.0f, 0.0f))[PanelHeader("Brush")];
	Pad(SpinRow<float>("Brush Size", "The brush's radius in map units", [Leaves] { return Leaves->Brush.Radius; },
	        [Leaves](float V) { Leaves->Brush.Radius = V; }, FLandscapeBrush::MinRadius, FLandscapeBrush::MaxRadius, 0),
	    6.0f);
	Pad(SpinRow<float>("Paint Density", "How much of each type's density a stroke paints (Unreal's Paint Density)",
	    [Leaves] { return Leaves->Brush.Strength; }, [Leaves](float V) { Leaves->Brush.Strength = V; }, 0.01f, 1.0f,
	    2));
	Pad(SpinRow<float>("Brush Falloff", "The outer part of the radius over which foliage thins out",
	    [Leaves] { return Leaves->Brush.Falloff; }, [Leaves](float V) { Leaves->Brush.Falloff = V; }, 0.0f, 1.0f, 2));

	// The foliage types (Unreal's foliage type list).
	Box->AddSlot().AutoHeight().Padding(FMargin(0.0f, 12.0f, 0.0f, 0.0f))[PanelHeader("Foliage Types")];
	TSharedPtr<SBox> TypeHost;
	Pad(SAssignNew(TypeHost, SBox), 6.0f);
	FoliageTypeHost = TypeHost;
	const TSharedPtr<std::vector<FString, std::allocator<FString>>> Presets =
	    std::make_shared<TArray<FString>>(FMapFoliageMode::GetPresetNames());
	Pad(SNew(SHorizontalBox) +
	        SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Text(GENESIS_TEXT("Add"))] +
	        SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(6.0f, 0.0f, 0.0f, 0.0f))[SNew(SComboBox<FString>)
	                .OptionsSource(Presets.get())
	                .ToolTipText(GENESIS_TEXT("A new foliage type: one of the usual ones, ready to paint"))
	                .IsEnabled([this] { return GetDocument() != nullptr; })
	                .OnGenerateWidget(
	                    [Presets](FString Name) -> TSharedRef<SWidget> { return SNew(STextBlock).Text(Name); })
	                .OnSelectionChanged(
	                    [this, Leaves, Presets](FString Name, ESelectInfo)
	{
		if (Leaves->AddType(Name))
		{
			SelectedFoliageType = Leaves->GetFoliageObject()->GetFoliage().GetNumTypes() - 1;
			FoliageSignature = "(changed)";
			RefreshFoliageTypes();
			Log(FTextLocalization::Format("Added foliage type {}", Name));
		}
	})[SNew(STextBlock).Text(GENESIS_TEXT("Foliage Type..."))]],
	    6.0f);

	Box->AddSlot().AutoHeight().Padding(FMargin(0.0f, 12.0f, 0.0f, 0.0f))[PanelHeader("Type Settings")];
	TSharedPtr<SBox> SettingsHost;
	Pad(SAssignNew(SettingsHost, SBox), 6.0f);
	FoliageSettingsHost = SettingsHost;
	FoliageSignature = "(none yet)";
	TypeHost->SetContent(MakeFoliageTypeList());
	SettingsHost->SetContent(MakeFoliageTypeSettings());
	Pad(SNew(STextBlock)
	        .TextStyle(&SmallText())
	        .AutoWrapText(true)
	        .Text(GENESIS_TEXT(
	            "Left drag over the ground in any view to paint the checked types; Shift erases. A stroke is one "
	            "undo step. "
	            "[ and ] size the brush, Ctrl+[ and Ctrl+] its density. Sculpting a landscape moves the foliage on "
	            "it.")),
	    10.0f);
	return SNew(SScrollBox) + SScrollBox::Slot()[Box];
}

void SFoliageEditorPanel::RefreshFoliageTypes()
{
	const TSharedPtr<SBox> TypeHost = FoliageTypeHost.lock();
	if (!TypeHost || bFoliageQueued)
	{
		return;
	}
	// Only when the list would change: painting changes instances, not types.
	FString Signature;
	if (const Object* Obj = Mode->GetFoliageObject())
	{
		Signature = FString::Format("{}", static_cast<const void*>(Obj));
		for (const FoliageType& Type : Obj->GetFoliage().GetTypes())
		{
			Signature += FString::Format("|{} {}", Type.name, Type.mesh);
		}
	}
	Signature += FString::Format("#{}", SelectedFoliageType);
	if (Signature == FoliageSignature)
	{
		return;
	}
	FoliageSignature = Signature;
	// Next frame: the change may come from a widget in the list.
	bFoliageQueued = true;
	RegisterActiveTimer(0.0f, [this](double, float)
	{
		bFoliageQueued = false;
		if (const TSharedPtr<SBox> Pinned = FoliageTypeHost.lock())
		{
			Pinned->SetContent(MakeFoliageTypeList());
		}
		if (const TSharedPtr<SBox> Pinned = FoliageSettingsHost.lock())
		{
			Pinned->SetContent(MakeFoliageTypeSettings());
		}
		return EActiveTimerReturn::Stop;
	});
}

TSharedRef<SWidget> SFoliageEditorPanel::MakeFoliageTypeList()
{
	FMapFoliageMode* Leaves = Mode;
	const Object* Obj = Leaves->GetFoliageObject();
	if (!Obj || Obj->GetFoliage().GetNumTypes() == 0)
	{
		return SNew(STextBlock)
		    .TextStyle(&SmallText())
		    .AutoWrapText(true)
		    .Text(GENESIS_TEXT("No foliage types yet: add one below (Grass, Bush, Tree, Pine, Rock)."));
	}
	// Each row: painted or not, its name (click to edit it below), how many
	// there are, and x to remove the type and its instances.
	const TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	for (int32 Index = 0; Index < Obj->GetFoliage().GetNumTypes(); ++Index)
	{
		const FString Name(Obj->GetFoliage().GetTypes()[static_cast<SIZE_T>(Index)].name);
		List->AddSlot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, 4.0f))
		    [SNew(SHorizontalBox) +
		     SHorizontalBox::Slot()
		         .AutoWidth()
		         .VAlign(VAlign_Center)
		         .Padding(FMargin(0.0f, 0.0f, 6.0f, 0.0f))
		             [SNew(SCheckBox)
		                  .ToolTipText(GENESIS_TEXT("Paint (and erase) this type"))
		                  .IsChecked(
		                      [Leaves, Index]
		                      { return Leaves->IsActive(Index) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
		                  .OnCheckStateChanged([Leaves, Index](ECheckBoxState State)
		                                       { Leaves->SetActive(Index, State == ECheckBoxState::Checked); })] +
		     SHorizontalBox::Slot()
		         .FillWidth(1.0f)
		         .VAlign(VAlign_Center)
		         .Padding(FMargin(
		             0.0f, 0.0f, 6.0f, 0.0f))[SNew(SChoiceButton, Name, FString("Edit this type's settings"))
		                                          .IsChosen([this, Index] { return SelectedFoliageType == Index; })
		                                          .OnClicked(
		                                              [this, Index]
		                                              {
			                                              SelectedFoliageType = Index;
			                                              RefreshFoliageTypes();
		                                              })] +
		     SHorizontalBox::Slot()
		         .AutoWidth()
		         .VAlign(VAlign_Center)
		         .Padding(FMargin(0.0f, 0.0f, 6.0f,
		                          0.0f))[SNew(STextBlock)
		                                     .TextStyle(&SmallText())
		                                     .Text(
		                                         [Leaves, Index]
		                                         {
			                                         const Object* Current = Leaves->GetFoliageObject();
			                                         return Current && Index < Current->GetFoliage().GetNumTypes()
			                                                    ? FString::Format(
			                                                          "{}", Current->GetFoliage().CountInstances(Index))
			                                                    : FString();
		                                         })] +
		     SHorizontalBox::Slot().AutoWidth().VAlign(
		         VAlign_Center)[SNew(SButton)
		                            .Text(GENESIS_TEXT("x"))
		                            .IsFocusable(false)
		                            .ToolTipText(GENESIS_TEXT("Remove this type and all of its instances"))
		                            .OnClicked(
		                                [this, Leaves, Index]
		                                {
			                                if (Leaves->RemoveType(Index))
			                                {
				                                SelectedFoliageType = FMath::Max(
				                                    0, FMath::Min(
				                                           SelectedFoliageType,
				                                           Leaves->GetFoliageObject()->GetFoliage().GetNumTypes() - 1));
			                                }
			                                return FReply::Handled();
		                                })]];
	}
	return List;
}

TSharedRef<SWidget> SFoliageEditorPanel::MakeFoliageTypeSettings()
{
	FMapFoliageMode* Leaves = Mode;
	const Object* Obj = Leaves->GetFoliageObject();
	const int32 Index = SelectedFoliageType;
	if (!Obj || Index < 0 || Index >= Obj->GetFoliage().GetNumTypes())
	{
		return SNew(STextBlock)
		    .TextStyle(&SmallText())
		    .AutoWrapText(true)
		    .Text(GENESIS_TEXT("Pick a foliage type above to edit it."));
	}
	// Reads and writes of the type, by index, on whatever the object now holds.
	const TFunction<FoliageType()> Get = [Leaves, Index]() -> FoliageType
	{
		const Object* Current = Leaves->GetFoliageObject();
		return Current && Index < Current->GetFoliage().GetNumTypes()
		    ? Current->GetFoliage().GetTypes()[static_cast<SIZE_T>(Index)]
		    : FoliageType();
	};
	const TFunction<void(const TFunction<void(FoliageType&)>&)> Edit = [Leaves, Index, Get](
	                                                                       const TFunction<void(FoliageType&)>& Change)
	{
		FoliageType Type = Get();
		Change(Type);
		Leaves->SetType(Index, Type);
	};
	const TFunction<TSharedRef<SWidget>(const char*, const char*, float FoliageType::*, float, float, int32)> Number =
	    [this, Get, Edit](
	        const char* Label, const char* Tip, float FoliageType::* Field, float Min, float Max, int32 Digits)
	{
		return FoliageSpin([this] { BeginInteraction("Change Foliage Type"); }, [this] { EndInteraction(); }, Label,
		    Tip, [Get, Field] { return Get().*Field; },
		    [Edit, Field](float V) { Edit([Field, V](FoliageType& Type) { Type.*Field = V; }); }, Min, Max, Digits);
	};
	const TFunction<TSharedRef<SWidget>(const char*, const char*, bool FoliageType::*)> Check =
	    [Get, Edit](const char* Label, const char* Tip, bool FoliageType::* Field)
	{
		return SNew(SCheckBox)
		    .Text(FString(Label))
		    .ToolTipText(GENESIS_TEXT(Tip))
		    .IsChecked([Get, Field] {
			return Get().*Field ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		}).OnCheckStateChanged([Edit, Field](ECheckBoxState State) {
			Edit([Field, State](FoliageType& Type) { Type.*Field = State == ECheckBoxState::Checked; });
		});
	};

	// Its mesh: the foliage meshes, then the basic shapes.
	const TSharedPtr<std::vector<FString, std::allocator<FString>>> Meshes = std::make_shared<TArray<FString>>();
	for (const std::string& Name : StaticMesh::GetFoliageMeshNames())
	{
		Meshes->push_back(FString(Name));
	}
	for (const std::string& Name : StaticMesh::GetBasicShapeNames())
	{
		Meshes->push_back(FString(Name));
	}
	const FString CurrentMesh(Get().mesh);
	const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	const TFunction<void(const TSharedRef<SWidget>&)> Row = [&Box](const TSharedRef<SWidget>& Widget)
	{ Box->AddSlot().AutoHeight().Padding(FMargin(0.0f, 0.0f, 0.0f, 4.0f))[Widget]; };
	Row(SNew(SHorizontalBox).ToolTipText(GENESIS_TEXT("The static mesh its instances show")) +
	    SHorizontalBox::Slot().FillWidth(0.5f).VAlign(VAlign_Center)[SNew(STextBlock).Text(GENESIS_TEXT("Mesh"))] +
	    SHorizontalBox::Slot().FillWidth(0.5f)[SNew(SComboBox<FString>)
	            .OptionsSource(Meshes.get())
	            .InitiallySelectedItem(TOptional<FString>(CurrentMesh))
	            .OnGenerateWidget([Meshes](FString Name) -> TSharedRef<SWidget> {
		return SNew(STextBlock).Text(Name);
	}).OnSelectionChanged([Edit, Meshes](FString Name, ESelectInfo) {
		Edit([Name](FoliageType& Type) { Type.mesh = Name.ToAnsi(); });
	})[SNew(STextBlock).Text([Get] { return FString(Get().mesh); })]]);
	Row(Number("Density", "Instances on 1000 x 1000 units of ground", &FoliageType::density, 0.1f, 100000.0f, 1));
	Row(Number("Radius", "No two instances of it closer than this", &FoliageType::radius, 0.0f, 10000.0f, 0));
	Row(Number("Scale Min", "The smallest an instance is", &FoliageType::scaleMin, 0.01f, 100.0f, 2));
	Row(Number("Scale Max", "The biggest an instance is", &FoliageType::scaleMax, 0.01f, 100.0f, 2));
	Row(Check("Align to Normal", "Stand along the ground's slope, not upright", &FoliageType::alignToNormal));
	Row(Number(
	    "Align Max Angle", "Degrees an aligned instance leans at most", &FoliageType::alignMaxAngle, 0.0f, 90.0f, 0));
	Row(Check("Random Yaw", "Turn each instance at random", &FoliageType::randomYaw));
	Row(Number(
	    "Ground Slope Min", "Degrees: it grows on slopes at least this steep", &FoliageType::slopeMin, 0.0f, 90.0f, 0));
	Row(Number("Ground Slope Max", "Degrees: and at most this steep", &FoliageType::slopeMax, 0.0f, 90.0f, 0));
	Row(Number("Height Min", "The lowest ground it grows on", &FoliageType::heightMin, -65536.0f, 65536.0f, 0));
	Row(Number("Height Max", "The highest", &FoliageType::heightMax, -65536.0f, 65536.0f, 0));
	Row(Number("Z Offset", "Up from the ground (negative sinks it in)", &FoliageType::zOffset, -1000.0f, 1000.0f, 0));
	Row(Number("Cull Distance", "Not drawn farther from the camera than this (0: always)", &FoliageType::cullDistance,
	    0.0f, 100000.0f, 0));
	Row(SNew(SHorizontalBox) +
	    SHorizontalBox::Slot().AutoWidth()[SNew(SButton)
	            .Text(GENESIS_TEXT("Remove All"))
	            .ToolTipText(GENESIS_TEXT("Take away every instance of this type"))
	            .OnClicked([Leaves, Index]
	{
		Leaves->ClearType(Index);
		return FReply::Handled();
	})]);
	return Box;
}

// ----- Content Browsers -----
