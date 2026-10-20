// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// SContentBrowser: Unreal's content browser, over folders on disk. The
// editor shows up to four (Content Browser 1 to 4, as in Unreal).
//
//   toolbar      + Add (new folder), Import (copies files into the folder),
//                Save All, back and forward, and the path ("All > Content >
//                Editor"), each part clickable
//   left         the folder tree, from the roots (Content, Prefabs)
//   right        a search box (it searches the folder and those below it),
//                the folder's subfolders and assets as tiles, and a count
//
// A tile's color says what it is: prefabs (.qle in a prefab folder), maps
// (.qle and .map), entity lists, textures, fonts and other files.
// Double-click a folder to open it, or an asset to use it (OnAssetActivated:
// the editor places prefabs and opens maps); drag a prefab into a viewport
// to place it under the cursor.

#pragma once

#include "MapPlacement.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class SBox;
class SWrapBox;

enum class EContentAssetType : uint8
{
	Folder,
	Prefab,
	Map,
	QuakeMap,
	EntityList,
	Texture,
	Font,
	Text,
	File,
};

const char* ToString(EContentAssetType Type);

struct FContentBrowserRoot
{
	FString Name; // as shown ("Content")
	FString Path; // a folder on disk
};

struct FContentBrowserItem
{
	FString Name; // the file name, without the extension for assets
	FString Path;
	EContentAssetType Type = EContentAssetType::File;
};

class SContentBrowser final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SContentBrowser)
		SLATE_ARGUMENT(TArray<FContentBrowserRoot>, Roots, {})
		// .qle files in these folders (and below) are prefabs.
		SLATE_ARGUMENT(TArray<FString>, PrefabFolders, {})
		// The folder shown first; empty: the first root.
		SLATE_ARGUMENT(FString, InitialPath, FString())
		SLATE_EVENT(TDelegate<void(const FContentBrowserItem&)>, OnAssetActivated)
		SLATE_EVENT(FSimpleDelegate, OnSaveAll)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);
	~SContentBrowser() override;

	// The folder shown ("" is All, the roots). The tiles follow next frame.
	void SetPath(const FString& Path);
	const FString& GetPath() const { return Path; }
	void GoBack();
	void GoForward();
	void SetSearchText(const FString& Text);
	// Scans the folder again (after files changed).
	void Refresh();

	// What the tiles show (after the search), for tests.
	const TArray<FContentBrowserItem>& GetItems() const { return Items; }
	const FString& GetSelectedPath() const { return SelectedPath; }
	void SetSelectedPath(const FString& InPath) { SelectedPath = InPath; }
	// Opens a folder or uses an asset, as a double-click does.
	void Activate(const FContentBrowserItem& Item);

	FString CreateFolder(); // "NewFolder", "NewFolder2"... in the folder shown; the new path, or empty
	bool ImportFile(const FString& Source); // copies it into the folder shown

	EContentAssetType GetAssetType(const FString& FilePath) const;

private:
	struct FFolderNode
	{
		FString Name;
		FString Path;
		FFolderNode* Parent = nullptr;
		TArray<TUniquePtr<FFolderNode>> Children;
		bool bScanned = false;
	};

	void ScanChildren(FFolderNode& Node);
	FFolderNode* FindNode(const FString& FolderPath);
	// The folder's name as shown, and the path from All down to it (name, folder).
	FString GetDisplayPath(const FString& FolderPath, TArray<TPair<FString, FString>>* OutParts = nullptr) const;
	void ScanItems();
	void QueueRebuild();
	void RebuildWidgets();
	TSharedRef<SWidget> MakeBreadcrumbs();
	TSharedRef<SWidget> MakeTile(const FContentBrowserItem& Item);
	TSharedRef<SWidget> MakeAddMenu();
	void Navigate(const FString& NewPath, bool bRecord);

	TArray<FContentBrowserRoot> Roots;
	TArray<FString> PrefabFolders;
	TDelegate<void(const FContentBrowserItem&)> OnAssetActivated;
	FSimpleDelegate OnSaveAll;

	FFolderNode TreeRoot; // "All"
	TArray<FFolderNode*> TreeRoots;
	TSharedPtr<STreeView<FFolderNode*>> Tree;
	bool bSyncingTree = false;

	FString Path;
	FString SearchText;
	FString SelectedPath;
	TArray<FString> BackStack;
	TArray<FString> ForwardStack;
	TArray<FContentBrowserItem> Items;

	TSharedPtr<SBox> BreadcrumbHost;
	TSharedPtr<SWrapBox> Tiles;
	bool bRebuildQueued = false;
	bool bAlive = true;
};
