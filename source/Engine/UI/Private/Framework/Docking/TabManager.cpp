// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Framework/Docking/TabManager.h"

#include "Application/UIApplication.h"
#include "Framework/Docking/SDockingArea.h"

// ---------------------------------------------------------------------------
// Layout description

TSharedRef<FDockStackLayout> FDockStackLayout::AddTab(FName TabId, ETabState State)
{
	Tabs.push_back({ TabId, State });
	if (State == ETabState::Opened && ForegroundTab.IsNone())
	{
		ForegroundTab = TabId;
	}
	return TSharedRef<FDockStackLayout>(std::static_pointer_cast<FDockStackLayout>(shared_from_this()));
}

TSharedRef<FDockStackLayout> FDockStackLayout::SetForegroundTab(FName TabId)
{
	ForegroundTab = TabId;
	return TSharedRef<FDockStackLayout>(std::static_pointer_cast<FDockStackLayout>(shared_from_this()));
}

TSharedRef<FDockStackLayout> FDockStackLayout::SetSizeCoefficient(float Coefficient)
{
	SizeCoefficient = Coefficient;
	return TSharedRef<FDockStackLayout>(std::static_pointer_cast<FDockStackLayout>(shared_from_this()));
}

TSharedRef<FDockSplitterLayout> FDockSplitterLayout::Split(const TSharedRef<FDockLayoutNode>& Child)
{
	Children.push_back(Child);
	return TSharedRef<FDockSplitterLayout>(std::static_pointer_cast<FDockSplitterLayout>(shared_from_this()));
}

TSharedRef<FDockSplitterLayout> FDockSplitterLayout::SetOrientation(EOrientation InOrientation)
{
	Orientation = InOrientation;
	return TSharedRef<FDockSplitterLayout>(std::static_pointer_cast<FDockSplitterLayout>(shared_from_this()));
}

TSharedRef<FDockSplitterLayout> FDockSplitterLayout::SetSizeCoefficient(float Coefficient)
{
	SizeCoefficient = Coefficient;
	return TSharedRef<FDockSplitterLayout>(std::static_pointer_cast<FDockSplitterLayout>(shared_from_this()));
}

TSharedRef<FDockAreaLayout> FDockAreaLayout::Split(const TSharedRef<FDockLayoutNode>& Child)
{
	Children.push_back(Child);
	return TSharedRef<FDockAreaLayout>(std::static_pointer_cast<FDockAreaLayout>(shared_from_this()));
}

TSharedRef<FDockAreaLayout> FDockAreaLayout::SetOrientation(EOrientation InOrientation)
{
	Orientation = InOrientation;
	return TSharedRef<FDockAreaLayout>(std::static_pointer_cast<FDockAreaLayout>(shared_from_this()));
}

TSharedRef<FDockAreaLayout> FDockAreaLayout::SetWindow(TOptional<FIntPoint> Position, FVector2 Size)
{
	WindowPosition = Position;
	WindowSize = Size;
	return TSharedRef<FDockAreaLayout>(std::static_pointer_cast<FDockAreaLayout>(shared_from_this()));
}

TSharedRef<FDockLayout> FDockLayout::New(FName Name, int32 Version)
{
	auto Layout = MakeShared<FDockLayout>();
	Layout->Name = Name;
	Layout->Version = Version;
	return Layout;
}

TSharedRef<FDockAreaLayout> FDockLayout::NewPrimaryArea()
{
	auto Area = MakeShared<FDockAreaLayout>();
	Area->bPrimary = true;
	return Area;
}

TSharedRef<FDockAreaLayout> FDockLayout::NewFloatingArea()
{
	return MakeShared<FDockAreaLayout>();
}

TSharedRef<FDockSplitterLayout> FDockLayout::NewSplitter()
{
	return MakeShared<FDockSplitterLayout>();
}

TSharedRef<FDockStackLayout> FDockLayout::NewStack()
{
	return MakeShared<FDockStackLayout>();
}

TSharedRef<FDockLayout> FDockLayout::AddArea(const TSharedRef<FDockAreaLayout>& Area)
{
	Areas.push_back(Area);
	return TSharedRef<FDockLayout>(shared_from_this());
}

TSharedPtr<FDockAreaLayout> FDockLayout::GetPrimaryArea() const
{
	for (const TSharedRef<FDockAreaLayout>& Area : Areas)
	{
		if (Area->bPrimary)
		{
			return Area.ToSharedPtr();
		}
	}
	return nullptr;
}

namespace
{
	FJsonValue NodeToJson(const FDockLayoutNode& Node)
	{
		FJsonValue Json = FJsonValue::MakeObject();
		Json["Size"] = Node.SizeCoefficient;
		if (Node.IsStack())
		{
			const FDockStackLayout& Stack = static_cast<const FDockStackLayout&>(Node);
			Json["Type"] = "Stack";
			FJsonValue& Tabs = Json["Tabs"] = FJsonValue::MakeArray();
			for (const FDockStackLayout::FTab& Tab : Stack.Tabs)
			{
				FJsonValue TabJson = FJsonValue::MakeObject();
				TabJson["Id"] = FString(Tab.TabId.View());
				TabJson["Open"] = Tab.State == ETabState::Opened;
				Tabs.Push(TabJson);
			}
			Json["Foreground"] = FString(Stack.ForegroundTab.View());
			return Json;
		}
		const FDockSplitterLayout& Splitter = static_cast<const FDockSplitterLayout&>(Node);
		Json["Type"] = "Splitter";
		Json["Orientation"] = Splitter.Orientation == EOrientation::Horizontal ? "Horizontal" : "Vertical";
		FJsonValue& Children = Json["Children"] = FJsonValue::MakeArray();
		for (const TSharedRef<FDockLayoutNode>& Child : Splitter.Children)
		{
			Children.Push(NodeToJson(*Child));
		}
		return Json;
	}

	void SplitterFromJson(FDockSplitterLayout& Out, const FJsonValue& Json);

	TSharedPtr<FDockLayoutNode> NodeFromJson(const FJsonValue& Json)
	{
		if (!Json.IsObject())
		{
			return nullptr;
		}
		const FString Type = Json.Get("Type").AsString();
		TSharedPtr<FDockLayoutNode> Node;
		if (Type == "Stack")
		{
			auto Stack = std::make_shared<FDockStackLayout>();
			const FJsonValue& Tabs = Json.Get("Tabs");
			for (int32 I = 0; I < Tabs.Num() && Tabs.IsArray(); ++I)
			{
				const FString Id = Tabs[I].Get("Id").AsString();
				if (!Id.IsEmpty())
				{
					Stack->Tabs.push_back({ FName(Id), Tabs[I].Get("Open").AsBool(true) ? ETabState::Opened : ETabState::Closed });
				}
			}
			Stack->ForegroundTab = FName(Json.Get("Foreground").AsString());
			Node = Stack;
		}
		else if (Type == "Splitter")
		{
			auto Splitter = std::make_shared<FDockSplitterLayout>();
			SplitterFromJson(*Splitter, Json);
			Node = Splitter;
		}
		else
		{
			return nullptr;
		}
		Node->SizeCoefficient = FMath::Max(0.01f, Json.Get("Size").AsFloat(1.0f));
		return Node;
	}

	void SplitterFromJson(FDockSplitterLayout& Out, const FJsonValue& Json)
	{
		Out.Orientation = Json.Get("Orientation").AsString() == "Vertical" ? EOrientation::Vertical : EOrientation::Horizontal;
		const FJsonValue& Children = Json.Get("Children");
		for (int32 I = 0; I < Children.Num() && Children.IsArray(); ++I)
		{
			if (const TSharedPtr<FDockLayoutNode> Child = NodeFromJson(Children[I]))
			{
				Out.Children.push_back(ToSharedRef(Child));
			}
		}
	}
}

FJsonValue FDockLayout::ToJson() const
{
	FJsonValue Json = FJsonValue::MakeObject();
	Json["Name"] = FString(Name.View());
	Json["Version"] = Version;
	FJsonValue& AreaArray = Json["Areas"] = FJsonValue::MakeArray();
	for (const TSharedRef<FDockAreaLayout>& Area : Areas)
	{
		FJsonValue AreaJson = NodeToJson(*Area);
		AreaJson["Type"] = "Area";
		AreaJson["Primary"] = Area->bPrimary;
		if (Area->WindowPosition)
		{
			AreaJson["X"] = Area->WindowPosition->X;
			AreaJson["Y"] = Area->WindowPosition->Y;
		}
		AreaJson["Width"] = Area->WindowSize.X;
		AreaJson["Height"] = Area->WindowSize.Y;
		AreaArray.Push(AreaJson);
	}
	return Json;
}

TSharedPtr<FDockLayout> FDockLayout::FromJson(const FJsonValue& Json)
{
	if (!Json.IsObject() || !Json.Get("Areas").IsArray())
	{
		return nullptr;
	}
	auto Layout = std::make_shared<FDockLayout>();
	Layout->Name = FName(Json.Get("Name").AsString());
	Layout->Version = Json.Get("Version").AsInt(-1);
	const FJsonValue& AreaArray = Json.Get("Areas");
	for (int32 I = 0; I < AreaArray.Num(); ++I)
	{
		const FJsonValue& AreaJson = AreaArray[I];
		auto Area = std::make_shared<FDockAreaLayout>();
		SplitterFromJson(*Area, AreaJson);
		Area->bPrimary = AreaJson.Get("Primary").AsBool();
		if (AreaJson.Contains("X") && AreaJson.Contains("Y"))
		{
			Area->WindowPosition = FIntPoint(AreaJson.Get("X").AsInt(), AreaJson.Get("Y").AsInt());
		}
		Area->WindowSize = FVector2(AreaJson.Get("Width").AsFloat(600.0f), AreaJson.Get("Height").AsFloat(400.0f));
		Layout->Areas.push_back(TSharedRef<FDockAreaLayout>(Area));
	}
	return Layout;
}

// ---------------------------------------------------------------------------
// SDockTab

void SDockTab::Construct(const FArguments& Args)
{
	Label.Assign(Args._Label);
	Role = Args._Role;
	bClosable = Args._Closable;
	OnTabClosed = Args._OnTabClosed;
	CanClose = Args._CanClose;
	if (Args._Content.Widget)
	{
		ChildSlot[ToSharedRef(Args._Content.Widget)];
	}
}

bool SDockTab::IsForeground() const
{
	const TSharedPtr<SDockTabStack> Stack = ParentStack.lock();
	return Stack && Stack->GetForegroundTab().get() == this;
}

bool SDockTab::IsActive() const
{
	const TSharedPtr<FTabManager> Manager = TabManager.lock();
	return Manager && Manager->GetActiveTab().get() == this;
}

void SDockTab::ActivateInParent()
{
	if (const TSharedPtr<SDockTabStack> Stack = ParentStack.lock())
	{
		Stack->BringToFront(*this);
	}
	if (const TSharedPtr<FTabManager> Manager = TabManager.lock())
	{
		Manager->SetActiveTab(SharedThis(this));
	}
}

bool SDockTab::RequestCloseTab()
{
	if (!bClosable || (CanClose.IsBound() && !CanClose.Execute()))
	{
		return false;
	}
	const TSharedPtr<SDockTabStack> Stack = ParentStack.lock();
	const TSharedPtr<FTabManager> Manager = TabManager.lock();
	const TSharedRef<SDockTab> Self = SharedThis(this);
	if (Stack)
	{
		Stack->RemoveTab(*this);
	}
	if (Manager && Stack)
	{
		Manager->OnTabClosed(Self, ToSharedRef(Stack));
	}
	else
	{
		NotifyClosed();
	}
	return true;
}

void SDockTab::NotifyClosed()
{
	ParentStack.reset();
	OnTabClosed.ExecuteIfBound(SharedThis(this));
}

// ---------------------------------------------------------------------------
// FTabManager

TSharedRef<FTabManager> FTabManager::Create()
{
	return TSharedRef<FTabManager>(std::make_shared<FTabManager>());
}

FTabSpawnerEntry& FTabManager::RegisterTabSpawner(FName TabId, FOnSpawnTab Spawn)
{
	UnregisterTabSpawner(TabId);
	return Spawners.emplace_back(TabId, std::move(Spawn));
}

void FTabManager::UnregisterTabSpawner(FName TabId)
{
	std::erase_if(Spawners, [TabId](const FTabSpawnerEntry& E) { return E.TabId == TabId; });
}

const FTabSpawnerEntry* FTabManager::FindSpawner(FName TabId) const
{
	for (const FTabSpawnerEntry& Entry : Spawners)
	{
		if (Entry.TabId == TabId)
		{
			return &Entry;
		}
	}
	return nullptr;
}

TSharedRef<SDockTab> FTabManager::SpawnTab(FName TabId, const TSharedPtr<SWindow>& Window)
{
	const FTabSpawnerEntry* Spawner = FindSpawner(TabId);
	check(Spawner);
	FSpawnTabArgs Args;
	Args.TabId = TabId;
	Args.OwnerWindow = Window;
	const TSharedRef<SDockTab> Tab = Spawner->Spawn(Args);
	Tab->SetTabId(TabId);
	Tab->SetTabManager(shared_from_this());
	if (Tab->GetLabel().IsEmpty())
	{
		Tab->SetLabel(Spawner->DisplayName);
	}
	return Tab;
}

TSharedRef<SWidget> FTabManager::RestoreNode(const TSharedRef<FDockLayoutNode>& Node, const TSharedPtr<SWindow>& Window)
{
	if (Node->IsStack())
	{
		const FDockStackLayout& Layout = static_cast<const FDockStackLayout&>(Node.Get());
		const TSharedRef<SDockTabStack> Stack = SNew(SDockTabStack, TSharedRef<FTabManager>(shared_from_this()));
		for (const FDockStackLayout::FTab& Tab : Layout.Tabs)
		{
			if (Tab.State == ETabState::Opened && FindSpawner(Tab.TabId) && !FindExistingLiveTab(Tab.TabId))
			{
				Stack->AddTab(SpawnTab(Tab.TabId, Window), INDEX_NONE, false);
			}
			else
			{
				Stack->ClosedTabIds.push_back(Tab.TabId);
			}
		}
		for (const TSharedRef<SDockTab>& Tab : Stack->GetTabs())
		{
			if (Tab->GetTabId() == Layout.ForegroundTab)
			{
				Stack->BringToFront(Tab.Get());
			}
		}
		return Stack;
	}
	const FDockSplitterLayout& Layout = static_cast<const FDockSplitterLayout&>(Node.Get());
	const TSharedRef<SDockingSplitter> Splitter = SNew(SDockingSplitter).Orientation(Layout.Orientation);
	for (const TSharedRef<FDockLayoutNode>& Child : Layout.Children)
	{
		const TSharedRef<SWidget> Widget = RestoreNode(Child, Window);
		const TSharedRef<SDockingNode> ChildNode(std::static_pointer_cast<SDockingNode>(Widget.ToSharedPtr()));
		Splitter->AddChildNode(ChildNode, Child->SizeCoefficient);
	}
	return Splitter;
}

TSharedRef<SDockingArea> FTabManager::RestoreArea(const TSharedRef<FDockAreaLayout>& Layout, const TSharedPtr<SWindow>& Window)
{
	const TSharedRef<SDockingArea> Area = SNew(SDockingArea, TSharedRef<FTabManager>(shared_from_this()), Layout->bPrimary, Window).Orientation(Layout->Orientation);
	for (const TSharedRef<FDockLayoutNode>& Child : Layout->Children)
	{
		const TSharedRef<SWidget> Widget = RestoreNode(Child, Window);
		Area->AddChildNode(TSharedRef<SDockingNode>(std::static_pointer_cast<SDockingNode>(Widget.ToSharedPtr())), Child->SizeCoefficient);
	}
	// Stacks whose tabs were all closed are left out (their tabs reopen elsewhere).
	TArray<TSharedRef<SDockTabStack>> Stacks;
	Area->GatherStacks(Stacks);
	for (const TSharedRef<SDockTabStack>& Stack : Stacks)
	{
		if (Stack->NumTabs() == 0 && Stack->GetParentSplitter())
		{
			Stack->GetParentSplitter()->RemoveChildNode(Stack.Get());
		}
	}
	Area->Simplify();
	Areas.push_back(Area.ToSharedPtr());
	if (Layout->bPrimary)
	{
		PrimaryArea = Area.ToSharedPtr();
	}
	return Area;
}

TSharedRef<SDockingArea> FTabManager::OpenFloatingWindow(const TSharedRef<FDockAreaLayout>& Layout)
{
	const TSharedRef<SWindow> Window = SNew(SWindow)
		.Title("Genesis")
		.ClientSize(Layout->WindowSize)
		.ScreenPosition(Layout->WindowPosition)
		.AppearsInTaskbar(false);
	const TSharedRef<SDockingArea> Area = RestoreArea(Layout, Window.ToSharedPtr());
	Window->SetContent(Area);
	const TWeakPtr<SDockingArea> WeakArea = Area.ToSharedPtr();
	const TWeakPtr<FTabManager> WeakManager = weak_from_this();
	// Closing the window closes its tabs (any of them may refuse).
	Window->SetOnRequestClose(FRequestWindowClose::CreateLambda([WeakArea, WeakManager]()
	{
		const TSharedPtr<SDockingArea> Pinned = WeakArea.lock();
		if (!Pinned)
		{
			return true;
		}
		TArray<TSharedRef<SDockTabStack>> Stacks;
		Pinned->GatherStacks(Stacks);
		bool bAllClosed = true;
		for (const TSharedRef<SDockTabStack>& Stack : Stacks)
		{
			for (const TSharedRef<SDockTab>& Tab : TArray<TSharedRef<SDockTab>>(Stack->GetTabs()))
			{
				bAllClosed &= Tab->RequestCloseTab();
			}
		}
		if (const TSharedPtr<FTabManager> Manager = WeakManager.lock())
		{
			std::erase_if(Manager->Areas, [&Pinned](const TWeakPtr<SDockingArea>& A) { return A.lock() == Pinned || A.expired(); });
		}
		return bAllClosed;
	}));
	if (FUIApplication::IsInitialized())
	{
		FUIApplication::Get().AddWindow(Window);
	}
	TArray<TSharedRef<SDockTabStack>> Stacks;
	Area->GatherStacks(Stacks);
	if (!Stacks.empty() && Stacks.front()->GetForegroundTab())
	{
		Window->SetTitle(Stacks.front()->GetForegroundTab()->GetLabel());
	}
	return Area;
}

TSharedRef<SWidget> FTabManager::RestoreFrom(const TSharedRef<FDockLayout>& Layout, const TSharedPtr<SWindow>& PrimaryWindow)
{
	LayoutName = Layout->Name;
	LayoutVersion = Layout->Version;
	bRestoring = true;
	TSharedPtr<SDockingArea> Primary;
	for (const TSharedRef<FDockAreaLayout>& Area : Layout->Areas)
	{
		if (Area->bPrimary && !Primary)
		{
			Primary = RestoreArea(Area, PrimaryWindow).ToSharedPtr();
		}
	}
	if (!Primary)
	{
		Primary = RestoreArea(FDockLayout::NewPrimaryArea(), PrimaryWindow).ToSharedPtr();
	}
	for (const TSharedRef<FDockAreaLayout>& Area : Layout->Areas)
	{
		if (!Area->bPrimary)
		{
			auto Floating = std::make_shared<FDockAreaLayout>(*Area);
			OpenFloatingWindow(TSharedRef<FDockAreaLayout>(Floating));
		}
	}
	bRestoring = false;
	OnLayoutChanged.Broadcast();
	return ToSharedRef(StaticCastSharedPtr<SWidget>(Primary));
}

namespace
{
	TSharedRef<FDockLayoutNode> PersistNode(const SDockingNode& Node)
	{
		if (Node.IsStack())
		{
			const SDockTabStack& Stack = static_cast<const SDockTabStack&>(Node);
			auto Layout = FDockLayout::NewStack();
			for (const TSharedRef<SDockTab>& Tab : Stack.GetTabs())
			{
				Layout->AddTab(Tab->GetTabId());
			}
			for (const FName Closed : Stack.ClosedTabIds)
			{
				Layout->AddTab(Closed, ETabState::Closed);
			}
			if (const TSharedPtr<SDockTab> Foreground = Stack.GetForegroundTab())
			{
				Layout->SetForegroundTab(Foreground->GetTabId());
			}
			Layout->SizeCoefficient = Node.GetSizeCoefficient();
			return Layout;
		}
		const SDockingSplitter& Splitter = static_cast<const SDockingSplitter&>(Node);
		auto Layout = FDockLayout::NewSplitter()->SetOrientation(Splitter.GetOrientation());
		for (const TSharedRef<SDockingNode>& Child : Splitter.GetChildNodes())
		{
			Layout->Split(PersistNode(Child.Get()));
		}
		Layout->SizeCoefficient = Node.GetSizeCoefficient();
		return Layout;
	}
}

TSharedRef<FDockLayout> FTabManager::PersistLayout() const
{
	const TSharedRef<FDockLayout> Layout = FDockLayout::New(LayoutName, LayoutVersion);
	for (const TWeakPtr<SDockingArea>& Weak : Areas)
	{
		const TSharedPtr<SDockingArea> Area = Weak.lock();
		if (!Area)
		{
			continue;
		}
		auto AreaLayout = Area->IsPrimary() ? FDockLayout::NewPrimaryArea() : FDockLayout::NewFloatingArea();
		AreaLayout->SetOrientation(Area->GetOrientation());
		for (const TSharedRef<SDockingNode>& Child : Area->GetChildNodes())
		{
			AreaLayout->Split(PersistNode(Child.Get()));
		}
		if (const TSharedPtr<SWindow> Window = Area->GetWindow(); Window && !Area->IsPrimary() && Window->GetNativeWindow())
		{
			AreaLayout->SetWindow(Window->GetNativeWindow()->GetClientPosition(), Window->GetLogicalSize());
		}
		if (!Area->IsPrimary() && Area->GetChildNodes().empty())
		{
			continue;
		}
		Layout->AddArea(AreaLayout);
	}
	return Layout;
}

TSharedRef<FDockLayout> FTabManager::LoadLayout(const FString& SavedJson, const TSharedRef<FDockLayout>& Default)
{
	if (SavedJson.IsEmpty())
	{
		return Default;
	}
	FString Error;
	const TOptional<FJsonValue> Json = FJsonValue::Parse(SavedJson.View(), &Error);
	if (!Json)
	{
		GE_LOG(LogUI, Warning, "Saved layout ignored: {}", Error);
		return Default;
	}
	const TSharedPtr<FDockLayout> Saved = FDockLayout::FromJson(*Json);
	if (!Saved || Saved->Name != Default->Name || Saved->Version != Default->Version || !Saved->GetPrimaryArea())
	{
		GE_LOG(LogUI, Log, "Saved layout ignored (another name or version); using the default \"{}\" v{}", Default->Name, Default->Version);
		return Default;
	}
	return ToSharedRef(Saved);
}

void FTabManager::CloseAllAreas()
{
	for (const TWeakPtr<SDockingArea>& Weak : TArray<TWeakPtr<SDockingArea>>(Areas))
	{
		const TSharedPtr<SDockingArea> Area = Weak.lock();
		if (!Area)
		{
			continue;
		}
		TArray<TSharedRef<SDockTabStack>> Stacks;
		Area->GatherStacks(Stacks);
		for (const TSharedRef<SDockTabStack>& Stack : Stacks)
		{
			for (const TSharedRef<SDockTab>& Tab : TArray<TSharedRef<SDockTab>>(Stack->GetTabs()))
			{
				Stack->RemoveTab(Tab.Get());
				Tab->NotifyClosed();
			}
		}
		if (!Area->IsPrimary())
		{
			if (const TSharedPtr<SWindow> Window = Area->GetWindow(); Window && FUIApplication::IsInitialized())
			{
				Window->SetOnRequestClose(FRequestWindowClose());
				FUIApplication::Get().RequestDestroyWindow(ToSharedRef(Window));
			}
		}
	}
	Areas.clear();
	PrimaryArea.reset();
	ActiveTab.reset();
}

TSharedPtr<SDockTab> FTabManager::FindExistingLiveTab(FName TabId) const
{
	for (const TSharedRef<SDockTab>& Tab : GetLiveTabs())
	{
		if (Tab->GetTabId() == TabId)
		{
			return Tab.ToSharedPtr();
		}
	}
	return nullptr;
}

TArray<TSharedRef<SDockTab>> FTabManager::GetLiveTabs() const
{
	TArray<TSharedRef<SDockTab>> Tabs;
	for (const TWeakPtr<SDockingArea>& Weak : Areas)
	{
		if (const TSharedPtr<SDockingArea> Area = Weak.lock())
		{
			TArray<TSharedRef<SDockTabStack>> Stacks;
			Area->GatherStacks(Stacks);
			for (const TSharedRef<SDockTabStack>& Stack : Stacks)
			{
				Tabs.insert(Tabs.end(), Stack->GetTabs().begin(), Stack->GetTabs().end());
			}
		}
	}
	return Tabs;
}

TSharedPtr<SDockTabStack> FTabManager::FindStackRemembering(FName TabId) const
{
	for (const TWeakPtr<SDockingArea>& Weak : Areas)
	{
		if (const TSharedPtr<SDockingArea> Area = Weak.lock())
		{
			TArray<TSharedRef<SDockTabStack>> Stacks;
			Area->GatherStacks(Stacks);
			for (const TSharedRef<SDockTabStack>& Stack : Stacks)
			{
				if (ArrayUtil::Contains(Stack->ClosedTabIds, TabId))
				{
					return Stack.ToSharedPtr();
				}
			}
		}
	}
	return nullptr;
}

TSharedPtr<SDockTabStack> FTabManager::FindAnyStack() const
{
	// The active tab's stack if it is in the primary area, else the primary
	// area's first stack.
	if (const TSharedPtr<SDockTab> Active = ActiveTab.lock())
	{
		const TSharedPtr<SDockTabStack> Stack = Active->GetParentStack();
		if (Stack && Stack->GetDockArea() && Stack->GetDockArea()->IsPrimary())
		{
			return Stack;
		}
	}
	if (const TSharedPtr<SDockingArea> Primary = PrimaryArea.lock())
	{
		TArray<TSharedRef<SDockTabStack>> Stacks;
		Primary->GatherStacks(Stacks);
		if (!Stacks.empty())
		{
			return Stacks.front().ToSharedPtr();
		}
	}
	return nullptr;
}

TSharedPtr<SDockTab> FTabManager::TryInvokeTab(FName TabId)
{
	if (const TSharedPtr<SDockTab> Existing = FindExistingLiveTab(TabId))
	{
		Existing->ActivateInParent();
		if (const TSharedPtr<SDockTabStack> Stack = Existing->GetParentStack())
		{
			if (SDockingArea* Area = Stack->GetDockArea(); Area && Area->GetWindow() && Area->GetWindow()->GetNativeWindow())
			{
				Area->GetWindow()->GetNativeWindow()->BringToFront();
			}
		}
		return Existing;
	}
	if (!FindSpawner(TabId))
	{
		return nullptr;
	}
	TSharedPtr<SDockTabStack> Stack = FindStackRemembering(TabId);
	if (!Stack)
	{
		Stack = FindAnyStack();
	}
	if (!Stack)
	{
		const TSharedPtr<SDockingArea> Primary = PrimaryArea.lock();
		if (!Primary)
		{
			return nullptr;
		}
		SAssignNew(Stack, SDockTabStack, TSharedRef<FTabManager>(shared_from_this()));
		Primary->AddChildNode(ToSharedRef(StaticCastSharedPtr<SDockingNode>(Stack)), 1.0f);
	}
	ArrayUtil::Remove(Stack->ClosedTabIds, TabId);
	SDockingArea* Area = Stack->GetDockArea();
	const TSharedRef<SDockTab> Tab = SpawnTab(TabId, Area ? Area->GetWindow() : nullptr);
	Stack->AddTab(Tab);
	Tab->ActivateInParent();
	OnLayoutChanged.Broadcast();
	return Tab.ToSharedPtr();
}

void FTabManager::SetActiveTab(const TSharedPtr<SDockTab>& Tab)
{
	const TSharedPtr<SDockTab> Old = ActiveTab.lock();
	if (Old == Tab)
	{
		return;
	}
	ActiveTab = Tab;
	for (const TSharedPtr<SDockTab>& Changed : { Old, Tab })
	{
		if (Changed)
		{
			if (const TSharedPtr<SDockTabStack> Stack = Changed->GetParentStack())
			{
				Stack->Invalidate(EInvalidateWidgetReason::Paint);
			}
		}
	}
	OnActiveTabChanged.Broadcast(Tab);
}

void FTabManager::OnTabClosed(const TSharedRef<SDockTab>& Tab, const TSharedRef<SDockTabStack>& Stack)
{
	if (!ArrayUtil::Contains(Stack->ClosedTabIds, Tab->GetTabId()))
	{
		Stack->ClosedTabIds.push_back(Tab->GetTabId());
	}
	if (ActiveTab.lock() == Tab.ToSharedPtr())
	{
		SetActiveTab(Stack->GetForegroundTab());
	}
	Tab->NotifyClosed();
	if (Stack->NumTabs() == 0)
	{
		OnStackEmptied(Stack);
	}
	OnLayoutChanged.Broadcast();
}

void FTabManager::OnStackEmptied(const TSharedRef<SDockTabStack>& Stack)
{
	SDockingSplitter* Parent = Stack->GetParentSplitter();
	SDockingArea* Area = Stack->GetDockArea();
	if (!Parent || !Area)
	{
		return;
	}
	// Keep what the stack remembered with a neighbour.
	const TArray<FName> Remembered = Stack->ClosedTabIds;
	Parent->RemoveChildNode(Stack.Get());
	Area->Simplify();
	TArray<TSharedRef<SDockTabStack>> Others;
	Area->GatherStacks(Others);
	if (!Others.empty())
	{
		for (const FName Id : Remembered)
		{
			if (!ArrayUtil::Contains(Others.front()->ClosedTabIds, Id))
			{
				Others.front()->ClosedTabIds.push_back(Id);
			}
		}
	}
	if (!Area->IsPrimary() && Area->GetChildNodes().empty())
	{
		const TSharedPtr<SDockingArea> AreaPtr = StaticCastSharedPtr<SDockingArea>(Area->shared_from_this());
		std::erase_if(Areas, [&AreaPtr](const TWeakPtr<SDockingArea>& A) { return A.lock() == AreaPtr || A.expired(); });
		if (const TSharedPtr<SWindow> Window = Area->GetWindow(); Window && FUIApplication::IsInitialized())
		{
			Window->SetOnRequestClose(FRequestWindowClose());
			FUIApplication::Get().RequestDestroyWindow(ToSharedRef(Window));
		}
	}
}

void FTabManager::RemoveTabFromStack(const TSharedRef<SDockTab>& Tab)
{
	const TSharedPtr<SDockTabStack> Stack = Tab->GetParentStack();
	if (!Stack)
	{
		return;
	}
	Stack->RemoveTab(Tab.Get());
	if (Stack->NumTabs() == 0)
	{
		OnStackEmptied(ToSharedRef(Stack));
	}
}

void FTabManager::MoveTabToStack(const TSharedRef<SDockTab>& Tab, const TSharedRef<SDockTabStack>& Target, int32 Index)
{
	const TSharedPtr<SDockTabStack> Source = Tab->GetParentStack();
	if (Source == Target.ToSharedPtr())
	{
		const int32 From = Target->IndexOfTab(Tab.Get());
		Target->RemoveTab(Tab.Get());
		Target->AddTab(Tab, Index > From ? Index - 1 : Index);
	}
	else
	{
		RemoveTabFromStack(Tab);
		Target->AddTab(Tab, Index);
	}
	Tab->ActivateInParent();
	OnLayoutChanged.Broadcast();
}

void FTabManager::MoveTabToNewStack(const TSharedRef<SDockTab>& Tab, const TSharedRef<SDockTabStack>& Beside, EUINavigation Side)
{
	if (Tab->GetParentStack() == Beside.ToSharedPtr() && Beside->NumTabs() == 1)
	{
		return; // splitting a stack with only this tab beside itself
	}
	RemoveTabFromStack(Tab);
	SDockingSplitter* Parent = Beside->GetParentSplitter();
	if (!Parent)
	{
		return;
	}
	const TSharedRef<SDockTabStack> NewStack = SNew(SDockTabStack, TSharedRef<FTabManager>(shared_from_this()));
	const EOrientation Orientation = Side == EUINavigation::Left || Side == EUINavigation::Right ? EOrientation::Horizontal : EOrientation::Vertical;
	const bool bAfter = Side == EUINavigation::Right || Side == EUINavigation::Down;
	if (Parent->GetOrientation() == Orientation)
	{
		const int32 Index = Parent->IndexOf(Beside.Get());
		const float Coefficient = Parent->GetCoefficient(Beside.Get());
		Parent->SetCoefficient(Beside.Get(), Coefficient * 0.5f);
		Parent->AddChildNode(NewStack, Coefficient * 0.5f, bAfter ? Index + 1 : Index);
	}
	else
	{
		const TSharedRef<SDockingSplitter> Split = SNew(SDockingSplitter).Orientation(Orientation);
		const TSharedRef<SDockingNode> BesideNode(std::static_pointer_cast<SDockingNode>(Beside.ToSharedPtr()));
		Parent->ReplaceChildNode(Beside.Get(), Split);
		Split->AddChildNode(bAfter ? BesideNode : TSharedRef<SDockingNode>(NewStack), 1.0f);
		Split->AddChildNode(bAfter ? TSharedRef<SDockingNode>(NewStack) : BesideNode, 1.0f);
	}
	NewStack->AddTab(Tab);
	Tab->ActivateInParent();
	OnLayoutChanged.Broadcast();
}

void FTabManager::MoveTabToNewWindow(const TSharedRef<SDockTab>& Tab, FIntPoint ScreenPosition, FVector2 LogicalSize)
{
	// The only tab of a floating window: move the window instead.
	if (const TSharedPtr<SDockTabStack> Stack = Tab->GetParentStack())
	{
		SDockingArea* Area = Stack->GetDockArea();
		if (Area && !Area->IsPrimary() && Stack->NumTabs() == 1 && Area->GetChildNodes().size() == 1 && Area->GetWindow())
		{
			Area->GetWindow()->GetNativeWindow()->MoveWindowTo(ScreenPosition.X, ScreenPosition.Y);
			return;
		}
	}
	RemoveTabFromStack(Tab);
	const TSharedRef<SDockingArea> Area = OpenFloatingWindow(FDockLayout::NewFloatingArea()->SetWindow(ScreenPosition, LogicalSize));
	const TSharedRef<SDockTabStack> Stack = SNew(SDockTabStack, TSharedRef<FTabManager>(shared_from_this()));
	Area->AddChildNode(Stack, 1.0f);
	Stack->AddTab(Tab);
	if (const TSharedPtr<SWindow> Window = Area->GetWindow())
	{
		Window->SetTitle(Tab->GetLabel());
	}
	Tab->ActivateInParent();
	OnLayoutChanged.Broadcast();
}

void FTabManager::PopulateTabSpawnerMenu(FMenuSection& Section)
{
	const TWeakPtr<FTabManager> Weak = weak_from_this();
	for (const FTabSpawnerEntry& Spawner : Spawners)
	{
		if (Spawner.bMenuHidden)
		{
			continue;
		}
		const FName TabId = Spawner.TabId;
		FUIAction Action;
		Action.Execute = FSimpleDelegate::CreateLambda([Weak, TabId]()
		{
			const TSharedPtr<FTabManager> Manager = Weak.lock();
			if (!Manager)
			{
				return;
			}
			if (const TSharedPtr<SDockTab> Tab = Manager->FindExistingLiveTab(TabId))
			{
				Tab->RequestCloseTab();
			}
			else
			{
				Manager->TryInvokeTab(TabId);
			}
		});
		Action.GetCheckState = TDelegate<ECheckBoxState()>::CreateLambda([Weak, TabId]()
		{
			const TSharedPtr<FTabManager> Manager = Weak.lock();
			return Manager && Manager->FindExistingLiveTab(TabId) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		});
		Section.AddEntry(TabId, Spawner.DisplayName, Spawner.ToolTip, Action, EUserInterfaceActionType::Check);
	}
}
