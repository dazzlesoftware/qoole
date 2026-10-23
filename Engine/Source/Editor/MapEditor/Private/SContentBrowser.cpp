// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "SContentBrowser.h"
#include "Containers/ContainerAliases.h"
#include "Templates/SharedPointer.h"
#include "Internationalization/TextLocalization.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialParameterCollection.h"
#include "AssetPackage.h"
#include "AssetRegistry.h"
#include "MapAssetImport.h"

#include "Framework/Application/SlateApplication.h"
#include "MapEditorIcons.h"
#include "MapEditorPrivate.h"
#include "Services/FileDialogs.h"
#include "Services/Services.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/SEditableText.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SWindow.h"

#include <algorithm>
#include <filesystem>

using namespace MapEditorPrivate;

const char* ToString(EContentAssetType Type)
{
	switch (Type)
	{
	case EContentAssetType::Folder:
		return "Folder";
	case EContentAssetType::Prefab:
		return "Prefab";
	case EContentAssetType::StaticMesh:
		return "Static Mesh";
	case EContentAssetType::Material:
		return "Material";
	case EContentAssetType::MaterialFunction:
		return "Material Function";
	case EContentAssetType::MaterialParameterCollection:
		return "Parameter Collection";
	case EContentAssetType::Map:
		return "Map";
	case EContentAssetType::QuakeMap:
		return "Quake Map";
	case EContentAssetType::EntityList:
		return "Entity List";
	case EContentAssetType::Texture:
		return "Texture";
	case EContentAssetType::Font:
		return "Font";
	case EContentAssetType::Text:
		return "Text";
	case EContentAssetType::File:
		return "File";
	case EContentAssetType::Package:
		return "Asset";
	case EContentAssetType::InvalidPackage:
		return "Content Issue";
	}
	return "?";
}

namespace
{
	namespace fs = std::filesystem;

	fs::path ToPath(const FString& Path)
	{
		return fs::path(Path.ToWide());
	}
	FString FromPath(const fs::path& Path)
	{
		return FString::FromWide(Path.wstring());
	}

	// The same folder, ignoring case and the kind of slash.
	FString Normalized(const FString& Path)
	{
		FString Result = FromPath(ToPath(Path).lexically_normal()).ToLower();
		while (Result.EndsWith("\\") || Result.EndsWith("/"))
		{
			Result = Result.Left(Result.Len() - 1);
		}
		return Result;
	}

	bool IsUnder(const FString& Path, const FString& Folder)
	{
		const FString P = Normalized(Path);
		const FString F = Normalized(Folder);
		return !F.IsEmpty() &&
		    (P == F ||
		        (P.StartsWith(F.View()) &&
		            (P.View()[static_cast<SIZE_T>(F.Len())] == '\\' || P.View()[static_cast<SIZE_T>(F.Len())] == '/')));
	}

	EIcon AssetIcon(EContentAssetType Type)
	{
		switch (Type)
		{
		case EContentAssetType::Folder:
			return EIcon::Folder;
		case EContentAssetType::Prefab:
			return EIcon::Prefab;
		case EContentAssetType::Map:
		case EContentAssetType::QuakeMap:
			return EIcon::Map;
		case EContentAssetType::EntityList:
			return EIcon::Entity;
		default:
			return EIcon::File;
		}
	}

	// The strip along an asset tile's foot (Unreal's asset type colors).
	FColor TypeColor(EContentAssetType Type)
	{
		switch (Type)
		{
		case EContentAssetType::Prefab:
			return FColor(96, 168, 255);
		case EContentAssetType::Map:
		case EContentAssetType::QuakeMap:
			return FColor(232, 150, 60);
		case EContentAssetType::EntityList:
			return FColor(120, 200, 110);
		case EContentAssetType::Texture:
			return FColor(200, 80, 80);
		case EContentAssetType::Material:
			return FColor(80, 180, 100);
		case EContentAssetType::MaterialFunction:
			return FColor(120, 100, 210);
		case EContentAssetType::MaterialParameterCollection:
			return FColor(80, 170, 190);
		case EContentAssetType::Font:
			return FColor(180, 120, 220);
		case EContentAssetType::InvalidPackage:
			return FColor(240, 60, 60);
		default:
			return FColor(120, 126, 138);
		}
	}

	// One asset or folder: a picture, its name and, for assets, its type.
	class SAssetTile final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SAssetTile)
		SLATE_ATTRIBUTE(bool, IsSelected, false)
		SLATE_EVENT(FSimpleDelegate, OnSelect)
		SLATE_EVENT(FSimpleDelegate, OnActivate)
		SLATE_END_ARGS()

		static constexpr float Width = 96.0f;

		void Construct(const FArguments& Args, const FContentBrowserItem& InItem)
		{
			Item = InItem;
			IsSelected.Assign(Args._IsSelected);
			OnSelect = Args._OnSelect;
			OnActivate = Args._OnActivate;
			const bool bFolder = Item.Type == EContentAssetType::Folder;
			const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox) +
			    SVerticalBox::Slot()
			        .AutoHeight()
			        .HAlign(HAlign_Center)
			        .Padding(FMargin(
			            0.0f, 6.0f, 0.0f, 4.0f))[SNew(SMapViewportIcon).Icon(AssetIcon(Item.Type)).Size(64.0f)] +
			    SVerticalBox::Slot()
			        .AutoHeight()
			        .HAlign(HAlign_Center)
			        .Padding(FMargin(4.0f, 0.0f))[SNew(SBox).MaxDesiredWidth(Width - 8.0f)[SNew(STextBlock)
			                .Text(Item.Name)
			                .AutoWrapText(true)
			                .Clipping(EWidgetClipping::ClipToBounds)
			                .TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("SmallText"))
			                .ColorAndOpacity(FSlateColor(EStyleColor::Foreground))]];
			if (!bFolder)
			{
				Box->AddSlot()
				    .AutoHeight()
				    .HAlign(HAlign_Center)
				    .Padding(FMargin(0.0f, 2.0f, 0.0f, 4.0f))[SNew(STextBlock)
				            .Text(Item.Schema == "Genesis.MaterialInstance" ? "Material Instance" : ToString(Item.Type))
				            .TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("SmallText"))];
			}
			ChildSlot[SNew(SBox).WidthOverride(Width).MinDesiredHeight(118.0f)[Box]];
			FString Tip = FString::Format("{}\n{}\n{}", Item.Name, ToString(Item.Type),
			    Item.PackagePath.IsEmpty() ? Item.Path : Item.PackagePath);
			if (!Item.Schema.IsEmpty())
				Tip += "\n" + Item.Schema;
			if (!Item.Error.IsEmpty())
				Tip += "\n" + Item.Error;
			SetToolTipText(Tip);
		}

		FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent& Event) override
		{
			if (Event.GetEffectingButton() == EMouseButton::Right)
			{
				OnSelect.ExecuteIfBound();
				return FReply::Unhandled(); // Bubble to the browser's creation menu.
			}
			if (Event.GetEffectingButton() != EMouseButton::Left)
			{
				return FReply::Unhandled();
			}
			OnSelect.ExecuteIfBound();
			return FReply::Handled().DetectDrag(SharedThis(this), EMouseButton::Left);
		}

		FReply OnMouseButtonDoubleClick(const FGeometry&, const FPointerEvent& Event) override
		{
			if (Event.GetEffectingButton() != EMouseButton::Left)
			{
				return FReply::Unhandled();
			}
			OnActivate.ExecuteIfBound();
			return FReply::Handled();
		}

		FReply OnDragDetected(const FGeometry&, const FPointerEvent&) override
		{
			if (!Item.PackageId.IsEmpty() &&
			    (Item.Schema == "Genesis.Texture2D" || Item.Schema == "Genesis.MaterialFunction" ||
			        Item.Schema == "Genesis.MaterialParameterCollection"))
				return FReply::Handled().BeginDragDrop(
				    FAssetDragDropOp::New({Item.PackageId, Item.PackagePath}, Item.Schema, Item.Name));
			// Prefabs drag into the viewports.
			if (Item.Type != EContentAssetType::Prefab && Item.Type != EContentAssetType::StaticMesh)
			{
				return FReply::Unhandled();
			}
			FMapPlaceable Placeable;
			Placeable.Kind =
			    Item.Type == EContentAssetType::StaticMesh ? EMapPlaceableKind::MeshAsset : EMapPlaceableKind::Prefab;
			Placeable.Id = Item.Path;
			Placeable.Label = Item.Name;
			Placeable.Category = "Geometry";
			if (!Item.PackagePath.IsEmpty())
				Placeable.SourceAsset = {Item.PackageId, Item.PackagePath};
			return FReply::Handled().BeginDragDrop(FMapPlaceDragDrop::New(Placeable));
		}

	protected:
		int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect,
		    FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& Style) const override
		{
			const bool bSelected = IsSelected.Get();
			if (Item.Type != EContentAssetType::Folder)
			{
				// Assets sit on a card with their type's color at the foot.
				Out.AddRoundedBox(
				    LayerId, Geometry.ToPaintGeometry(), FSlateColor(EStyleColor::Recessed).Resolve(), 4.0f);
				Out.AddSolidBox(LayerId,
				    Geometry.ToPaintGeometry(FRect(0.0f, Geometry.Size.Y - 3.0f, Geometry.Size.X, Geometry.Size.Y)),
				    TypeColor(Item.Type));
			}
			if (bSelected || IsHovered())
			{
				Out.AddRoundedBox(LayerId, Geometry.ToPaintGeometry(), FColor(0, 0, 0, 0), 4.0f,
				    FSlateColor(bSelected ? EStyleColor::Primary : EStyleColor::Border).Resolve(),
				    bSelected ? 2.0f : 1.0f);
			}
			return SCompoundWidget::OnPaint(Args, Geometry, CullingRect, Out, LayerId + 1, Style);
		}

	private:
		FContentBrowserItem Item;
		TWidgetAttribute<bool, EInvalidateWidgetReason::Paint> IsSelected{*this, false};
		FSimpleDelegate OnSelect;
		FSimpleDelegate OnActivate;
	};

	const FButtonStyle& ToolButton()
	{
		static const FButtonStyle Style = []
		{
			FButtonStyle S = FAppStyle::Get().GetWidgetStyle<FButtonStyle>("Button");
			S.NormalPadding = FMargin(8.0f, 3.0f);
			S.PressedPadding = FMargin(8.0f, 4.0f, 8.0f, 2.0f);
			return S;
		}();
		return Style;
	}

	TSharedRef<SWidget> IconLabel(EIcon Icon, const FString& Label)
	{
		const TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox) +
		    SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SMapViewportIcon).Icon(Icon)];
		if (!Label.IsEmpty())
		{
			Row->AddSlot()
			    .AutoWidth()
			    .VAlign(VAlign_Center)
			    .Padding(FMargin(5.0f, 0.0f, 0.0f, 0.0f))[SNew(STextBlock).Text(Label)];
		}
		return Row;
	}
} // namespace

// ---------------------------------------------------------------------------

void SContentBrowser::Construct(const FArguments& Args)
{
	Roots = Args._Roots;
	Registry = Args._Registry;
	SetCanTick(Registry != nullptr);
	RegistryRevision = Registry ? Registry->GetRevision() : 0;
	PrefabFolders = Args._PrefabFolders;
	OnAssetActivated = Args._OnAssetActivated;
	OnSaveAll = Args._OnSaveAll;
	OnCreateMesh = Args._OnCreateMesh;
	TreeRoot.Name = "All";
	TreeRoot.bScanned = true;
	for (const FContentBrowserRoot& Root : Roots)
	{
		TUniquePtr<SContentBrowser::FFolderNode> Node = MakeUnique<FFolderNode>();
		Node->Name = Root.Name;
		Node->Path = Root.Path;
		Node->Parent = &TreeRoot;
		TreeRoot.Children.push_back(std::move(Node));
	}
	TreeRoots = {&TreeRoot};

	const FTextBlockStyle* Small = &FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("SmallText");
	ChildSlot[SNew(SVerticalBox)
	          // Toolbar.
	          + SVerticalBox::Slot().AutoHeight().Padding(FMargin(
	                6.0f, 4.0f))[SNew(SHorizontalBox) +
	                             SHorizontalBox::Slot().AutoWidth().Padding(FMargin(
	                                 0.0f, 0.0f, 4.0f, 0.0f))[SNew(SComboButton)
	                                                              .ButtonStyle(&ToolButton())
	                                                              .IsFocusable(false)
	                                                              .ToolTipText(GENESIS_TEXT("Add a folder"))
	                                                              .OnGetMenuContent([this] { return MakeAddMenu(); })
	                                                              .ButtonContent()[IconLabel(EIcon::Add, "Add")]] +
	                             SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.0f, 0.0f, 4.0f, 0.0f))
	                                 [SNew(SButton)
	                                      .ButtonStyle(&ToolButton())
	                                      .IsFocusable(false)
	                                      .ToolTipText(GENESIS_TEXT("Copy a file into this folder"))
	                                      .IsEnabled([this] { return !Path.IsEmpty(); })
	                                      .OnClicked(
	                                          [this]
	                                          {
		                                          FFileDialogOptions Options;
		                                          Options.Title = "Import into this folder";
		                                          Options.InitialPath = Path;
		                                          Options.Filters = {
		                                              {"Import meshes and textures (*.obj, *.png, *.bmp, *.jpg, *.tif)",
		                                               "*.obj;*.png;*.bmp;*.jpg;*.jpeg;*.tif;*.tiff"},
		                                              {"Genesis packages (*.asset, *.gmap)", "*.asset;*.gmap"},
		                                              {"Legacy maps (*.qle, *.map)", "*.qle;*.map"},
		                                              {"All files (*.*)", "*.*"}};
		                                          FString Source;
		                                          if (FServices::GetFileDialogs().OpenFile(Options, Source))
		                                          {
			                                          ImportFile(Source);
		                                          }
		                                          return FReply::Handled();
	                                          })[IconLabel(EIcon::Import, "Import")]] +
	                             SHorizontalBox::Slot().AutoWidth().Padding(FMargin(
	                                 0.0f, 0.0f, 10.0f, 0.0f))[SNew(SButton)
	                                                               .ButtonStyle(&ToolButton())
	                                                               .IsFocusable(false)
	                                                               .ToolTipText(GENESIS_TEXT("Save the map"))
	                                                               .IsEnabled([this] { return OnSaveAll.IsBound(); })
	                                                               .OnClicked(
	                                                                   [this]
	                                                                   {
		                                                                   OnSaveAll.ExecuteIfBound();
		                                                                   return FReply::Handled();
	                                                                   })[IconLabel(EIcon::Save, "Save All")]] +
	                             SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.0f, 0.0f, 6.0f, 0.0f))
	                                 [SNew(SButton)
	                                      .ButtonStyle(&ToolButton())
	                                      .IsFocusable(false)
	                                      .Text(GENESIS_TEXT("Refresh"))
	                                      .ToolTipText(GENESIS_TEXT("Discover content changes in all mounted folders"))
	                                      .OnClicked(
	                                          [this]
	                                          {
		                                          Refresh();
		                                          return FReply::Handled();
	                                          })] +
	                             SHorizontalBox::Slot().AutoWidth()
	                                 [SNew(SButton)
	                                      .ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("FlatButton"))
	                                      .IsFocusable(false)
	                                      .ToolTipText(GENESIS_TEXT("Back"))
	                                      .IsEnabled([this] { return !BackStack.empty(); })
	                                      .OnClicked(
	                                          [this]
	                                          {
		                                          GoBack();
		                                          return FReply::Handled();
	                                          })[SNew(SMapViewportIcon).Icon(EIcon::Back)]] +
	                             SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.0f, 0.0f, 8.0f, 0.0f))
	                                 [SNew(SButton)
	                                      .ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("FlatButton"))
	                                      .IsFocusable(false)
	                                      .ToolTipText(GENESIS_TEXT("Forward"))
	                                      .IsEnabled([this] { return !ForwardStack.empty(); })
	                                      .OnClicked(
	                                          [this]
	                                          {
		                                          GoForward();
		                                          return FReply::Handled();
	                                          })[SNew(SMapViewportIcon).Icon(EIcon::Forward)]] +
	                             SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
	                                 [SNew(SBorder)
	                                      .BorderImage(&FAppStyle::Get().GetBrush("Recessed"))
	                                      .Padding(FMargin(6.0f, 2.0f))
	                                      .Clipping(EWidgetClipping::ClipToBounds)[SAssignNew(BreadcrumbHost, SBox)]]] +
	          SVerticalBox::Slot().FillHeight(1.0f)
	              [SNew(SSplitter).Orientation(EOrientation::Horizontal) +
	               SSplitter::Slot().Value(0.22f).MinSize(80.0f)
	                   [SAssignNew(Tree, STreeView<FFolderNode*>)
	                        .TreeItemsSource(&TreeRoots)
	                        .SelectionMode(ESelectionMode::Single)
	                        .OnGetChildren(
	                            [this](FFolderNode* Node, TArray<FFolderNode*>& Out)
	                            {
		                            ScanChildren(*Node);
		                            for (const TUniquePtr<FFolderNode>& Child : Node->Children)
		                            {
			                            Out.push_back(Child.get());
		                            }
	                            })
	                        .OnGenerateRow(
	                            [](FFolderNode* Node, const TSharedRef<STableViewBase>& Owner) -> TSharedRef<ITableRow>
	                            {
		                            return SNew(STableRow<FFolderNode*>, Owner)
		                                .Padding(FMargin(
		                                    2.0f,
		                                    1.0f))[SNew(SHorizontalBox) +
		                                           SHorizontalBox::Slot()
		                                               .AutoWidth()
		                                               .VAlign(VAlign_Center)
		                                               .Padding(
		                                                   FMargin(0.0f, 0.0f, 5.0f,
		                                                           0.0f))[SNew(SMapViewportIcon).Icon(EIcon::Folder)] +
		                                           SHorizontalBox::Slot().FillWidth(1.0f).VAlign(
		                                               VAlign_Center)[SNew(STextBlock)
		                                                                  .Text(Node->Name)
		                                                                  .Clipping(EWidgetClipping::ClipToBounds)]];
	                            })
	                        .OnSelectionChanged(
	                            [this](FFolderNode* Node, ESelectInfo)
	                            {
		                            if (Node && !bSyncingTree)
		                            {
			                            Navigate(Node->Path, true);
		                            }
	                            })] +
	               SSplitter::Slot().Value(0.78f).MinSize(
	                   160.0f)[SNew(SVerticalBox) +
	                           SVerticalBox::Slot().AutoHeight().Padding(FMargin(6.0f, 4.0f))
	                               [SNew(SEditableTextBox)
	                                    .Text([this] { return SearchText; })
	                                    .HintText([this] { return FString::Format("Search {}", GetDisplayPath(Path)); })
	                                    .OnTextChanged([this](const FString& Text) { SetSearchText(Text); })] +
	                           SVerticalBox::Slot().FillHeight(1.0f).Padding(FMargin(
	                               6.0f, 2.0f))[SNew(SScrollBox) +
	                                            SScrollBox::Slot()[SAssignNew(Tiles, SWrapBox)
	                                                                   .UseAllottedSize(true)
	                                                                   .InnerSlotPadding(FVector2(8.0f, 8.0f))]] +
	                           SVerticalBox::Slot().AutoHeight().Padding(FMargin(
	                               8.0f, 3.0f))[SNew(STextBlock)
	                                                .TextStyle(Small)
	                                                .Text(
	                                                    [this]
	                                                    {
		                                                    return FString::Format(
		                                                        "{} item{}{}{}", Items.size(),
		                                                        Items.size() == 1 ? "" : "s",
		                                                        SelectedPath.IsEmpty() ? "" : " (1 selected)",
		                                                        Registry && !Registry->GetIssues().empty()
		                                                            ? FTextLocalization::Format(
		                                                                  " — {} content issues",
		                                                                  Registry->GetIssues().size())
		                                                            : FString());
	                                                    })]]]];
	Tree->SetItemExpansion(&TreeRoot, true);
	Navigate(Args._InitialPath.IsEmpty() && !Roots.empty() ? Roots.front().Path : Args._InitialPath, false);
}

SContentBrowser::~SContentBrowser()
{
	bAlive = false;
}

FReply SContentBrowser::OnMouseButtonDown(const FGeometry&, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() != EMouseButton::Right)
	{
		return FReply::Unhandled();
	}
	FSlateApplication& App = FSlateApplication::Get();
	const TSharedPtr<SWindow> Window = App.FindWindowForWidget(*this);
	const FVector2 Position = Event.GetAbsolutePosition() / (Window ? Window->GetScale() : 1.0f);
	App.PushMenu(SharedThis(this), MakeAddMenu(), Position);
	return FReply::Handled();
}

EContentAssetType SContentBrowser::GetAssetType(const FString& FilePath) const
{
	return DescribeItem(FilePath).Type;
}

FContentBrowserItem SContentBrowser::DescribeItem(const FString& FilePath) const
{
	FContentBrowserItem Item;
	Item.Path = FilePath;
	Item.Name = FromPath(ToPath(FilePath).stem());
	const FString Extension = FromPath(ToPath(FilePath).extension()).ToLower();
	if (Extension == ".gmap" || Extension == ".asset")
	{
		FAssetPackageMetadata Metadata;
		if (const FAssetData* Data = Registry ? Registry->FindByFile(FilePath) : nullptr)
		{
			Metadata = Data->Metadata;
			Item.PackagePath = Data->PackagePath;
			Item.Error = Registry->GetConflict(*Data);
		}
		else
			FAssetPackageStore::ReadMetadata(FilePath, Metadata, Item.Error);
		Item.PackageId = Metadata.Id;
		Item.Schema = Metadata.Schema;
		if (!Item.Error.IsEmpty())
			Item.Type = EContentAssetType::InvalidPackage;
		else if (Metadata.Schema == "Genesis.World" && (Metadata.SchemaVersion >= 1 && Metadata.SchemaVersion <= 3) &&
		    Metadata.Kind == EAssetPackageKind::Map)
			Item.Type = EContentAssetType::Map;
		else if (Metadata.Schema == "Genesis.Prefab" && (Metadata.SchemaVersion >= 1 && Metadata.SchemaVersion <= 3) &&
		    Metadata.Kind == EAssetPackageKind::Asset)
			Item.Type = EContentAssetType::Prefab;
		else if (Metadata.Kind == EAssetPackageKind::Asset &&
		    (Metadata.SchemaVersion == 1 || Metadata.SchemaVersion == 2) && Metadata.Schema == "Genesis.StaticMesh")
			Item.Type = EContentAssetType::StaticMesh;
		else if (Metadata.Kind == EAssetPackageKind::Asset && Metadata.SchemaVersion == 1 &&
		    Metadata.Schema == "Genesis.Texture2D")
			Item.Type = EContentAssetType::Texture;
		else if (IsMaterialAsset(Metadata))
			Item.Type = EContentAssetType::Material;
		else if (Metadata.Schema == "Genesis.MaterialFunction")
			Item.Type = EContentAssetType::MaterialFunction;
		else if (Metadata.Schema == "Genesis.MaterialParameterCollection")
			Item.Type = EContentAssetType::MaterialParameterCollection;
		else
			Item.Type = EContentAssetType::Package;
		return Item;
	}
	const TFunction<EContentAssetType()> LooseType = [&]() -> EContentAssetType
	{
		if (Extension == ".qle")
		{
			for (const FString& Folder : PrefabFolders)
			{
				if (IsUnder(FilePath, Folder))
				{
					return EContentAssetType::Prefab;
				}
			}
			return EContentAssetType::Map;
		}
		if (Extension == ".map")
			return EContentAssetType::QuakeMap;
		if (Extension == ".ent" || Extension == ".def" || Extension == ".fgd")
			return EContentAssetType::EntityList;
		if (Extension == ".png" || Extension == ".tga" || Extension == ".bmp" || Extension == ".jpg" ||
		    Extension == ".dds" || Extension == ".wal")
			return EContentAssetType::Texture;
		if (Extension == ".ttf" || Extension == ".otf")
			return EContentAssetType::Font;
		if (Extension == ".txt" || Extension == ".md" || Extension == ".json" || Extension == ".ini")
			return EContentAssetType::Text;
		return EContentAssetType::File;
	};
	Item.Type = LooseType();
	return Item;
}

void SContentBrowser::ScanChildren(FFolderNode& Node)
{
	if (Node.bScanned)
	{
		return;
	}
	Node.bScanned = true;
	std::error_code Error;
	TArray<fs::path> Folders;
	for (fs::directory_iterator It(ToPath(Node.Path), Error), End; !Error && It != End; It.increment(Error))
	{
		if (It->is_directory(Error))
		{
			Folders.push_back(It->path());
		}
	}
	std::sort(Folders.begin(), Folders.end());
	for (const fs::path& Folder : Folders)
	{
		TUniquePtr<SContentBrowser::FFolderNode> Child = MakeUnique<FFolderNode>();
		Child->Name = FromPath(Folder.filename());
		Child->Path = FromPath(Folder);
		Child->Parent = &Node;
		Node.Children.push_back(std::move(Child));
	}
}

SContentBrowser::FFolderNode* SContentBrowser::FindNode(const FString& FolderPath)
{
	if (FolderPath.IsEmpty())
	{
		return &TreeRoot;
	}
	// Down from the root that holds it, one folder at a time.
	for (const TUniquePtr<FFolderNode>& Root : TreeRoot.Children)
	{
		if (!IsUnder(FolderPath, Root->Path))
		{
			continue;
		}
		FFolderNode* Node = Root.get();
		while (Node && Normalized(Node->Path) != Normalized(FolderPath))
		{
			ScanChildren(*Node);
			FFolderNode* Next = nullptr;
			for (const TUniquePtr<FFolderNode>& Child : Node->Children)
			{
				if (IsUnder(FolderPath, Child->Path))
				{
					Next = Child.get();
					break;
				}
			}
			Node = Next;
		}
		return Node;
	}
	return nullptr;
}

FString SContentBrowser::GetDisplayPath(const FString& FolderPath, TArray<TPair<FString, FString>>* OutParts) const
{
	TArray<TPair<FString, FString>> Parts = {{"All", FString()}};
	for (const FContentBrowserRoot& Root : Roots)
	{
		if (!FolderPath.IsEmpty() && IsUnder(FolderPath, Root.Path))
		{
			Parts.push_back({Root.Name, Root.Path});
			const fs::path Relative =
			    ToPath(FolderPath).lexically_normal().lexically_relative(ToPath(Root.Path).lexically_normal());
			fs::path Walk = ToPath(Root.Path);
			for (const fs::path& Part : Relative)
			{
				if (Part == "." || Part.empty())
				{
					continue;
				}
				Walk /= Part;
				Parts.push_back({FromPath(Part), FromPath(Walk)});
			}
			break;
		}
	}
	// The folder's own name ("All" for the roots).
	const FString Name = Parts.back().first;
	if (OutParts)
	{
		*OutParts = std::move(Parts);
	}
	return Name;
}

void SContentBrowser::ScanItems()
{
	Items.clear();
	if (Path.IsEmpty())
	{
		for (const FContentBrowserRoot& Root : Roots)
		{
			Items.push_back({Root.Name, Root.Path, EContentAssetType::Folder});
		}
		return;
	}
	std::error_code Error;
	const FString Needle = SearchText.ToLower();
	const TFunction<void(const fs::directory_entry&)> Consider = [&](const fs::directory_entry& Entry)
	{
		const bool bFolder = Entry.is_directory(Error);
		const std::filesystem::path Ext = Entry.path().extension();
		if (!bFolder && (Ext == L".gexp" || Ext == L".gbulk" || Ext == L".tmp"))
			return;
		FContentBrowserItem Item = bFolder ? FContentBrowserItem() : DescribeItem(FromPath(Entry.path()));
		Item.Path = FromPath(Entry.path());
		if (bFolder)
			Item.Type = EContentAssetType::Folder;
		Item.Name = FromPath(bFolder ? Entry.path().filename() : Entry.path().stem());
		if (!Needle.IsEmpty() && !Item.Name.ToLower().Contains(Needle.View()))
		{
			return;
		}
		Items.push_back(std::move(Item));
	};
	if (Needle.IsEmpty())
	{
		for (fs::directory_iterator It(ToPath(Path), Error), End; !Error && It != End; It.increment(Error))
		{
			Consider(*It);
		}
	}
	else
	{
		// A search looks below the folder too, up to a limit.
		for (fs::recursive_directory_iterator It(ToPath(Path), Error), End; !Error && It != End && Items.size() < 2000;
		    It.increment(Error))
		{
			Consider(*It);
		}
	}
	// Folders first, then by name.
	std::sort(Items.begin(), Items.end(), [](const FContentBrowserItem& A, const FContentBrowserItem& B)
	{
		const bool AFolder = A.Type == EContentAssetType::Folder;
		const bool BFolder = B.Type == EContentAssetType::Folder;
		return AFolder != BFolder ? AFolder : A.Name.ToLower().View() < B.Name.ToLower().View();
	});
}

void SContentBrowser::Navigate(const FString& NewPath, bool bRecord)
{
	if (bRecord && Normalized(NewPath) != Normalized(Path))
	{
		BackStack.push_back(Path);
		ForwardStack.clear();
	}
	Path = NewPath;
	SelectedPath.Empty();
	ScanItems();
	QueueRebuild();
}

void SContentBrowser::SetPath(const FString& NewPath)
{
	Navigate(NewPath, true);
}

void SContentBrowser::GoBack()
{
	if (BackStack.empty())
	{
		return;
	}
	ForwardStack.push_back(Path);
	const FString Previous = BackStack.back();
	BackStack.pop_back();
	Navigate(Previous, false);
}

void SContentBrowser::GoForward()
{
	if (ForwardStack.empty())
	{
		return;
	}
	BackStack.push_back(Path);
	const FString Next = ForwardStack.back();
	ForwardStack.pop_back();
	Navigate(Next, false);
}

void SContentBrowser::SetSearchText(const FString& Text)
{
	SearchText = Text.TrimStartAndEnd();
	ScanItems();
	QueueRebuild();
}

void SContentBrowser::Refresh(bool bRescanRegistry)
{
	if (Registry && bRescanRegistry)
	{
		Registry->Scan();
		RegistryRevision = Registry->GetRevision();
		for (size_t I = 0; I < Registry->GetIssues().size() && I < 20; ++I)
		{
			FAssetRegistryIssue const& Issue = Registry->GetIssues()[I];
			GENESIS_LOG(LogMapEditor, Warning, "Content issue in {}: {}", Issue.FilePath, Issue.Message);
		}
	}
	if (Registry)
		RegistryRevision = Registry->GetRevision();
	if (FFolderNode* Node = FindNode(Path))
	{
		Node->Children.clear();
		Node->bScanned = false;
	}
	ScanItems();
	QueueRebuild();
}

void SContentBrowser::Tick(const FGeometry&, double, float)
{
	if (Registry && RegistryRevision != Registry->GetRevision())
	{
		RegistryRevision = Registry->GetRevision();
		// Another browser or a save refreshed the shared registry. Rebuild the
		// view on the next frame without recursively rescanning mounted content.
		Refresh(false);
	}
}

void SContentBrowser::Activate(const FContentBrowserItem& Item)
{
	if (Item.Type == EContentAssetType::Folder)
	{
		SearchText.Empty();
		SetPath(Item.Path);
		return;
	}
	if (Item.Type == EContentAssetType::InvalidPackage)
	{
		GENESIS_LOG(LogMapEditor, Warning, "Cannot open {}: {}", Item.Path, Item.Error);
		return;
	}
	OnAssetActivated.ExecuteIfBound(Item);
}

FString SContentBrowser::CreateFolder()
{
	if (Path.IsEmpty())
	{
		return FString();
	}
	std::error_code Error;
	for (int32 Number = 1; Number < 1000; ++Number)
	{
		const fs::path Folder =
		    ToPath(Path) / (Number == 1 ? fs::path(L"NewFolder") : fs::path(L"NewFolder" + std::to_wstring(Number)));
		if (!fs::exists(Folder, Error) && fs::create_directory(Folder, Error))
		{
			Refresh();
			SelectedPath = FromPath(Folder);
			GENESIS_LOG(LogMapEditor, Display, "Created the folder {}", SelectedPath);
			return SelectedPath;
		}
	}
	return FString();
}

bool SContentBrowser::ImportFile(const FString& Source)
{
	if (Path.IsEmpty())
	{
		return false;
	}
	std::error_code Error;
	const fs::path From = ToPath(Source);
	const FString Extension = FromPath(From.extension()).ToLower();
	const bool bImage = Extension == ".png" || Extension == ".bmp" || Extension == ".jpg" || Extension == ".jpeg" ||
	    Extension == ".tif" || Extension == ".tiff";
	const bool bMesh = Extension == ".obj";
	const std::filesystem::path TargetExtension = bImage || bMesh ? fs::path(L".asset") : From.extension();
	fs::path To = ToPath(Path) / (From.stem().wstring() + TargetExtension.wstring());
	for (int32 Number = 2; fs::exists(To, Error) && Number < 1000; ++Number)
	{
		To = ToPath(Path) / (From.stem().wstring() + std::to_wstring(Number) + TargetExtension.wstring());
	}
	if (fs::exists(To, Error))
	{
		GENESIS_LOG(LogMapEditor, Warning, "No free import filename in {}", Path);
		return false;
	}
	bool bCopied = false;
	if (bImage || bMesh)
	{
		FString ImportError;
		bCopied = bImage ? FMapAssetImport::ImportTexture(Source, FromPath(To), ImportError)
		                 : FMapAssetImport::ImportMesh(Source, FromPath(To), ImportError);
		if (!bCopied)
			GENESIS_LOG(LogMapEditor, Warning, "{}", ImportError);
	}
	else if (Extension == ".gmap" || Extension == ".asset")
	{
		FAssetPackage Package;
		FString PackageError;
		if (FAssetPackageStore::Load(Source, Package, PackageError))
		{
			Package.Id.Empty(); // imported copy is a new asset
			bCopied = FAssetPackageStore::Save(FromPath(To), Package, PackageError);
		}
		if (!bCopied)
			GENESIS_LOG(LogMapEditor, Warning, "{}", PackageError);
	}
	else
		bCopied = fs::copy_file(From, To, Error);
	if (!bCopied)
	{
		GENESIS_LOG(LogMapEditor, Warning, "Could not import {}: {}", Source, Error.message());
		return false;
	}
	GENESIS_LOG(LogMapEditor, Display, "Imported {} as {}", Source, FromPath(To));
	Refresh();
	SelectedPath = FromPath(To);
	return true;
}

TSharedRef<SWidget> SContentBrowser::MakeAddMenu()
{
	return SNew(SVerticalBox) +
	    SVerticalBox::Slot().AutoHeight()[SNew(SMenuEntry)
	            .Text(GENESIS_TEXT("New Folder"))
	            .ToolTipText(GENESIS_TEXT("Make a folder here"))
	            .IsEnabled([this] { return !Path.IsEmpty(); })
	            .OnSelected([this] { CreateFolder(); })] +
	    SVerticalBox::Slot().AutoHeight()[SNew(SMenuEntry)
	            .Text(GENESIS_TEXT("Material"))
	            .IsEnabled([this] { return !Path.IsEmpty(); })
	            .OnSelected([this] { CreateMaterial(); })] +
	    SVerticalBox::Slot().AutoHeight()[SNew(SMenuEntry)
	            .Text(GENESIS_TEXT("Material Parameter Collection"))
	            .IsEnabled([this] { return !Path.IsEmpty(); })
	            .OnSelected([this] { CreateMaterialParameterCollection(); })] +
	    SVerticalBox::Slot().AutoHeight()[SNew(SMenuEntry)
	            .Text(GENESIS_TEXT("Material Function"))
	            .IsEnabled([this] { return !Path.IsEmpty(); })
	            .OnSelected([this] { CreateMaterialFunction(); })] +
	    SVerticalBox::Slot().AutoHeight()[SNew(SMenuEntry)
	            .Text(GENESIS_TEXT("Material Instance from Selection"))
	            .ToolTipText(GENESIS_TEXT("Select a saved graph material or instance as the parent"))
	            .IsEnabled(
	                [this] { return !Path.IsEmpty() && GetAssetType(SelectedPath) == EContentAssetType::Material; })
	            .OnSelected([this] { CreateMaterialInstance(); })] +
	    SVerticalBox::Slot().AutoHeight()[SNew(SMenuEntry)
	            .Text(GENESIS_TEXT("Static Mesh from Selection"))
	            .IsEnabled([this] {
		return !Path.IsEmpty() && OnCreateMesh.IsBound();
	}).OnSelected([this] { CreateMeshFromSelection(); })];
}

FString SContentBrowser::CreateMaterial()
{
	if (Path.IsEmpty())
		return {};
	for (int Number = 1; Number < 1000; ++Number)
	{
		const FString File = FromPath(
		    ToPath(Path) / (L"Material" + (Number == 1 ? std::wstring() : std::to_wstring(Number)) + L".asset"));
		if (fs::exists(ToPath(File)))
			continue;
		FMaterialAsset Material;
		for (const FContentBrowserItem& Item : Items)
			if (Item.Path == SelectedPath && Item.Schema == "Genesis.Texture2D" && !Item.PackagePath.IsEmpty())
				Material.BaseColorTexture = {Item.PackageId, Item.PackagePath};
		FAssetPackage P;
		FString Error;
		if (!FRenderAssetCodec::EncodeMaterial(Material, P, Error) || !FAssetPackageStore::Save(File, P, Error))
		{
			GENESIS_LOG(LogMapEditor, Warning, "{}", Error);
			return {};
		}
		Refresh();
		SelectedPath = File;
		return File;
	}
	return {};
}
FString SContentBrowser::CreateMaterialInstance()
{
	if (Path.IsEmpty())
		return {};
	FMaterialInstanceAsset Instance;
	for (const FContentBrowserItem& Item : Items)
		if (Item.Path == SelectedPath && Item.Type == EContentAssetType::Material)
			Instance.Parent = {Item.PackageId, Item.PackagePath};
	if (Instance.Parent.IsNull())
		return {};
	FString Error;
	FMaterialAsset Parent;
	if (!Registry || !FRenderAssetLoader(*Registry).LoadMaterial(Instance.Parent, Parent, Error) || !Parent.Graph)
	{
		GENESIS_LOG(LogMapEditor, Warning,
		    "Save the parent in the graph material editor before creating an instance. {}", Error);
		return {};
	}
	for (int Number = 1; Number < 1000; ++Number)
	{
		const FString File = FromPath(ToPath(Path) /
		    (L"MaterialInstance" + (Number == 1 ? std::wstring() : std::to_wstring(Number)) + L".asset"));
		if (fs::exists(ToPath(File)))
			continue;
		FAssetPackage P;
		if (!FRenderAssetCodec::EncodeMaterialInstance(Instance, P, Error) || !FAssetPackageStore::Save(File, P, Error))
		{
			GENESIS_LOG(LogMapEditor, Warning, "{}", Error);
			return {};
		}
		Refresh();
		SelectedPath = File;
		return File;
	}
	return {};
}
FString SContentBrowser::CreateMeshFromSelection()
{
	if (Path.IsEmpty() || !OnCreateMesh.IsBound())
		return {};
	for (int Number = 1; Number < 1000; ++Number)
	{
		const FString File = FromPath(
		    ToPath(Path) / (L"StaticMesh" + (Number == 1 ? std::wstring() : std::to_wstring(Number)) + L".asset"));
		if (fs::exists(ToPath(File)))
			continue;
		FString Error;
		if (!OnCreateMesh.Execute(File, Error))
		{
			GENESIS_LOG(LogMapEditor, Warning, "{}", Error);
			return {};
		}
		Refresh();
		SelectedPath = File;
		return File;
	}
	return {};
}

void SContentBrowser::QueueRebuild()
{
	// Next frame: tiles and path buttons navigate when clicked, and the
	// rebuild replaces them.
	if (bRebuildQueued)
	{
		return;
	}
	bRebuildQueued = true;
	RegisterActiveTimer(0.0f, [this](double, float)
	{
		bRebuildQueued = false;
		RebuildWidgets();
		return EActiveTimerReturn::Stop;
	});
}

TSharedRef<SWidget> SContentBrowser::MakeBreadcrumbs()
{
	TArray<TPair<FString, FString>> Parts;
	GetDisplayPath(Path, &Parts);
	const TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	for (SIZE_T Index = 0; Index < Parts.size(); ++Index)
	{
		const FString PartPath = Parts[Index].second;
		if (Index > 0)
		{
			Row->AddSlot().AutoWidth().VAlign(VAlign_Center)[SNew(SMapViewportIcon).Icon(EIcon::Forward).Size(12.0f)];
		}
		Row->AddSlot().AutoWidth().VAlign(VAlign_Center)[SNew(SButton)
		        .ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("FlatButton"))
		        .IsFocusable(false)
		        .ContentPadding(FMargin(5.0f, 1.0f))
		        .Text(Parts[Index].first)
		        .OnClicked([this, PartPath]
		{
			SearchText.Empty();
			SetPath(PartPath);
			return FReply::Handled();
		})];
	}
	return Row;
}

TSharedRef<SWidget> SContentBrowser::MakeTile(const FContentBrowserItem& Item)
{
	return SNew(SAssetTile, Item).IsSelected([this, ItemPath = Item.Path] {
		return SelectedPath == ItemPath;
	}).OnSelect([this, ItemPath = Item.Path] {
		SelectedPath = ItemPath;
	}).OnActivate([this, Item] { Activate(Item); });
}

void SContentBrowser::RebuildWidgets()
{
	BreadcrumbHost->SetContent(MakeBreadcrumbs());
	Tiles->ClearChildren();
	for (const FContentBrowserItem& Item : Items)
	{
		Tiles->AddSlot()[MakeTile(Item)];
	}
	// The tree shows where we are.
	bSyncingTree = true;
	if (FFolderNode* Node = FindNode(Path))
	{
		for (FFolderNode* Up = Node->Parent; Up; Up = Up->Parent)
		{
			Tree->SetItemExpansion(Up, true);
		}
		Tree->RequestTreeRefresh();
		Tree->SetSelection(Node);
	}
	else
	{
		Tree->ClearSelection();
	}
	bSyncingTree = false;
}

FString SContentBrowser::CreateMaterialFunction()
{
	if (Path.IsEmpty())
		return {};
	for (int Number = 1; Number < 1000; ++Number)
	{
		const FString File = FromPath(ToPath(Path) /
		    (L"MaterialFunction" + (Number == 1 ? std::wstring() : std::to_wstring(Number)) + L".asset"));
		if (fs::exists(ToPath(File)))
			continue;
		FMaterialGraph G;
		G.bFunction = true;
		G.Outputs[0] = G.Add(EMaterialExpression::FunctionInput);
		G.Find(G.Outputs[0])->Value[0] = 0.5f;
		FAssetPackage P;
		FString Error;
		if (!FMaterialFunctionCodec::Encode(G, P, Error) || !FAssetPackageStore::Save(File, P, Error))
		{
			GENESIS_LOG(LogMapEditor, Warning, "{}", Error);
			return {};
		}
		Refresh();
		SelectedPath = File;
		return File;
	}
	return {};
}

FString SContentBrowser::CreateMaterialParameterCollection()
{
	if (Path.IsEmpty())
		return {};
	for (int Number = 1; Number < 1000; ++Number)
	{
		const FString File = FromPath(ToPath(Path) /
		    (L"MaterialParameterCollection" + (Number == 1 ? std::wstring() : std::to_wstring(Number)) + L".asset"));
		if (fs::exists(ToPath(File)))
			continue;
		FMaterialParameterCollection C;
		C.Parameters = {{"Scalar", 1, {0, 0, 0, 0}}, {"Vector", 4, {1, 1, 1, 1}}};
		FAssetPackage P;
		FString Error;
		if (!FMaterialParameterCollectionCodec::Encode(C, P, Error) || !FAssetPackageStore::Save(File, P, Error))
		{
			GENESIS_LOG(LogMapEditor, Warning, "{}", Error);
			return {};
		}
		Refresh();
		SelectedPath = File;
		return File;
	}
	return {};
}
