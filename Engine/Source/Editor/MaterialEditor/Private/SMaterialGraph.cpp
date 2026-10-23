// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "SMaterialGraph.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialParameterCollection.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Styling/StarshipCoreStyle.h"
#include "Rendering/DrawElements.h"
#include "Widgets/Text/SEditableText.h"
#include <algorithm>
#include <cmath>
#include <map>
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
namespace
{
constexpr float Width = 210, OutputX = 1020, OutputY = 40;
float Height(const FMaterialGraph& G, const FMaterialNode& N)
{
	int Count = 0;
	for (int I = 0; I < MaterialNodeInputCount(G, N); ++I)
		if (MaterialNodeHasInput(G, N, I))
			++Count;
	return 62.f + 32.f * std::max(1, Count);
}
bool Near(FVector2 A, FVector2 B)
{
	const auto D = A - B;
	return D.X * D.X + D.Y * D.Y < 100;
}
} // namespace
void SMaterialGraph::Construct(const FArguments& A)
{
	Material = A._Material;
	Registry = A._Registry;
	check(Material);
	if (!Material->Graph)
		Material->Graph = FMaterialEditingLibrary::CreateGraph(*Material);
	OnChanged = A._OnChanged;
	OnSelectionChanged = A._OnSelectionChanged;
	SetClipping(EWidgetClipping::ClipToBounds);
	OnError = A._OnError;
}
void SMaterialGraph::Remember()
{
	if (UndoStack.size() >= 100)
		UndoStack.erase(UndoStack.begin());
	UndoStack.push_back({Graph(), Material->BlendMode, Material->OpacityMaskClip});
	RedoStack.clear();
}
void SMaterialGraph::Changed()
{
	Invalidate(EInvalidateWidgetReason::Paint);
	OnChanged.ExecuteIfBound();
}
uint32 SMaterialGraph::AddNode(EMaterialExpression E, FVector2 P)
{
	Remember();
	auto Id = Graph().Add(E, P.X, P.Y);
	Selected = Id;
	Selection = Id ? TArray<uint32>{Id} : TArray<uint32>{};
	Changed();
	OnSelectionChanged.ExecuteIfBound();
	return Id;
}
bool SMaterialGraph::Connect(uint32 From, uint32 To, int Input, FString& Error)
{
	if (!Graph().Find(From) || Input < 0 ||
		(To == 0 ? Input >= ((Graph().bFunction || Graph().bUseMaterialAttributes) ? 1 : 6)
				 : !Graph().Find(To) || !MaterialNodeHasInput(Graph(), *Graph().Find(To), Input)))
	{
		Error = "Invalid graph pin.";
		return false;
	}
	auto Previous = Graph();
	auto& Link = To ? Graph().Find(To)->Inputs[Input] : Graph().Outputs[Input];
	Link = From;
	FMaterialGraphProgram P;
	if (!FMaterialGraphCompiler::Compile(Graph(), P, Error))
	{
		Graph() = std::move(Previous);
		return false;
	}
	if (UndoStack.size() >= 100)
		UndoStack.erase(UndoStack.begin());
	UndoStack.push_back({std::move(Previous), Material->BlendMode, Material->OpacityMaskClip});
	RedoStack.clear();
	Changed();
	return true;
}
void SMaterialGraph::Disconnect(uint32 To, int Input)
{
	if (Input < 0 ||
		(To == 0 ? Input >= ((Graph().bFunction || Graph().bUseMaterialAttributes) ? 1 : 6)
				 : !Graph().Find(To) || !MaterialNodeHasInput(Graph(), *Graph().Find(To), Input)))
		return;
	Remember();
	(To ? Graph().Find(To)->Inputs[Input] : Graph().Outputs[Input]) = 0;
	Changed();
}
bool SMaterialGraph::IsNodeSelected(uint32 Id) const
{
	return std::find(Selection.begin(), Selection.end(), Id) != Selection.end();
}
void SMaterialGraph::SetNodeSelection(const TArray<uint32>& Nodes)
{
	Selection.clear();
	for (auto Id : Nodes)
		if (Graph().Find(Id) && !IsNodeSelected(Id))
			Selection.push_back(Id);
	Selected = Selection.empty() ? 0 : Selection.back();
	Invalidate(EInvalidateWidgetReason::Paint);
	OnSelectionChanged.ExecuteIfBound();
}
FString SMaterialGraph::CopySelection() const
{
	FMaterialGraph Fragment;
	Fragment.Functions = Graph().Functions;
	Fragment.QualityLevel = Graph().QualityLevel;
	for (const auto& N : Graph().Nodes)
		if (IsNodeSelected(N.Id))
		{
			auto Copy = N;
			for (auto& I : Copy.Inputs)
				if (!IsNodeSelected(I))
					I = 0;
			Fragment.Nodes.push_back(Copy);
		}
	auto J = FJsonValue::MakeObject();
	J["Schema"] = "Genesis.MaterialGraphClipboard";
	J["Graph"] = FMaterialGraphCompiler::Encode(Fragment);
	return J.ToString(false);
}
bool SMaterialGraph::PasteNodes(const FString& Text, FVector2 Position, FString& Error)
{
	if (Text.Len() > 1024 * 1024)
	{
		Error = "Clipboard graph is too large.";
		return false;
	}
	const auto J = FJsonValue::Parse(Text.View(), &Error);
	if (!J || J->Get("Schema").AsString() != "Genesis.MaterialGraphClipboard")
	{
		Error = "Clipboard does not contain material nodes.";
		return false;
	}
	FMaterialGraph Fragment;
	if (!FMaterialGraphCompiler::Decode(J->Get("Graph"), Fragment, Error))
		return false;
	if (Fragment.Nodes.empty())
	{
		Error = "No nodes to paste.";
		return false;
	}
	if (Graph().Nodes.size() + Fragment.Nodes.size() > FMaterialGraphCompiler::MaxNodes)
	{
		Error = "Pasting exceeds the material node limit.";
		return false;
	}
	auto Next = Graph();
	for (const auto& F : Fragment.Functions)
		if (std::none_of(Next.Functions.begin(), Next.Functions.end(), [&](const auto& Existing) {
				return Existing.Reference.Id == F.Reference.Id && Existing.Reference.Path == F.Reference.Path;
			}))
			Next.Functions.push_back(F);
	std::map<uint32, uint32> Remap;
	TArray<uint32> Added;
	FVector2 Low(Fragment.Nodes[0].X, Fragment.Nodes[0].Y);
	for (const auto& N : Fragment.Nodes)
	{
		Low.X = std::min(Low.X, N.X);
		Low.Y = std::min(Low.Y, N.Y);
	}
	for (const auto& N : Fragment.Nodes)
	{
		auto Id = Next.Add(N.Expression);
		if (!Id)
		{
			Error = "Cannot allocate node IDs.";
			return false;
		}
		Remap[N.Id] = Id;
		Added.push_back(Id);
	}
	for (const auto& N : Fragment.Nodes)
	{
		auto* Copy = Next.Find(Remap[N.Id]);
		*Copy = N;
		Copy->Id = Remap[N.Id];
		Copy->X += Position.X - Low.X;
		Copy->Y += Position.Y - Low.Y;
		for (auto& I : Copy->Inputs)
			I = I && Remap.contains(I) ? Remap[I] : 0;
	}
	FMaterialGraphProgram Program;
	if (!FMaterialGraphCompiler::Compile(Next, Program, Error))
		return false;
	Remember();
	Graph() = std::move(Next);
	SetNodeSelection(Added);
	Changed();
	Error.Empty();
	return true;
}
void SMaterialGraph::DuplicateSelected()
{
	if (Selection.empty())
		return;
	FVector2 Low(1e6f, 1e6f);
	for (auto Id : Selection)
		if (const auto* N = Graph().Find(Id))
		{
			Low.X = std::min(Low.X, N->X);
			Low.Y = std::min(Low.Y, N->Y);
		}
	FString Error;
	if (!PasteNodes(CopySelection(), Low + FVector2(40, 40), Error))
		OnError.ExecuteIfBound(Error);
}
void SMaterialGraph::DeleteSelected()
{
	if (Selection.empty())
		return;
	Remember();
	std::erase_if(Graph().Nodes, [&](const auto& N) { return IsNodeSelected(N.Id); });
	for (auto& N : Graph().Nodes)
		for (auto& I : N.Inputs)
			if (IsNodeSelected(I))
				I = 0;
	for (auto& O : Graph().Outputs)
		if (IsNodeSelected(O))
			O = 0;
	Selection.clear();
	Selected = 0;
	Changed();
	OnSelectionChanged.ExecuteIfBound();
}
void SMaterialGraph::Undo()
{
	if (UndoStack.empty())
		return;
	RedoStack.push_back({Graph(), Material->BlendMode, Material->OpacityMaskClip});
	Graph() = UndoStack.back().Graph;
	Material->BlendMode = UndoStack.back().Blend;
	Material->OpacityMaskClip = UndoStack.back().MaskCutoff;
	UndoStack.pop_back();
	Selected = 0;
	Selection.clear();
	Changed();
	OnSelectionChanged.ExecuteIfBound();
}
void SMaterialGraph::Redo()
{
	if (RedoStack.empty())
		return;
	UndoStack.push_back({Graph(), Material->BlendMode, Material->OpacityMaskClip});
	Graph() = RedoStack.back().Graph;
	Material->BlendMode = RedoStack.back().Blend;
	Material->OpacityMaskClip = RedoStack.back().MaskCutoff;
	RedoStack.pop_back();
	Selected = 0;
	Selection.clear();
	Changed();
	OnSelectionChanged.ExecuteIfBound();
}
void SMaterialGraph::EditNode(const TFunction<void(FMaterialNode&)>& Edit)
{
	if (auto* N = Graph().Find(Selected))
	{
		Remember();
		Edit(*N);
		Changed();
	}
}
void SMaterialGraph::FrameAll()
{
	FVector2 Low(0, 0), High(OutputX + Width, OutputY + 260);
	for (const auto& N : Graph().Nodes)
	{
		Low.X = std::min(Low.X, N.X);
		Low.Y = std::min(Low.Y, N.Y);
		High.X = std::max(High.X, N.X + (N.Expression == EMaterialExpression::Comment
											 ? FMath::Clamp(N.Value[0], 100.f, 2000.f)
											 : Width));
		High.Y = std::max(High.Y, N.Y + (N.Expression == EMaterialExpression::Comment
											 ? FMath::Clamp(N.Value[1], 60.f, 2000.f)
											 : Height(Graph(), N)));
	}
	const auto Size = GetCachedGeometry().GetLocalSize();
	Zoom = FMath::Clamp(std::min((Size.X - 60) / (High.X - Low.X), (Size.Y - 60) / (High.Y - Low.Y)), .25f,
						1.25f);
	Pan = (Size - (High - Low) * Zoom) * .5f - Low * Zoom;
	Invalidate(EInvalidateWidgetReason::Paint);
}
void SMaterialGraph::Tick(const FGeometry& G, double, float)
{
	if (bInitialFrame && G.Size.X > 100 && G.Size.Y > 100)
	{
		bInitialFrame = false;
		FrameAll();
	}
}
FVector2 SMaterialGraph::GetPinPosition(uint32 Id, int Input, bool Output) const
{
	FVector2 P(OutputX, OutputY);
	if (Id)
	{
		const auto* N = Graph().Find(Id);
		if (!N)
			return {};
		P = FVector2(N->X, N->Y);
	}
	int Row = Input;
	if (Id && !Output)
		if (const auto* N = Graph().Find(Id); N && N->Expression == EMaterialExpression::MaterialFunctionCall)
		{
			Row = 0;
			for (int I = 0; I < Input; ++I)
				if (MaterialNodeHasInput(Graph(), *N, I))
					++Row;
		}
	P += FVector2(Output ? Width : 0, 52 + 32.f * Row);
	return P * Zoom + Pan;
}
bool SMaterialGraph::HitPin(FVector2 P, uint32& Id, int& Pin, bool& Output) const
{
	for (int I = 0; I < ((Graph().bFunction || Graph().bUseMaterialAttributes) ? 1 : 6); ++I)
		if (Near(P, GetPinPosition(0, I, false)))
		{
			Id = 0;
			Pin = I;
			Output = false;
			return true;
		}
	for (auto It = Graph().Nodes.rbegin(); It != Graph().Nodes.rend(); ++It)
	{
		if (It->Expression == EMaterialExpression::Comment)
			continue;
		if (Near(P, GetPinPosition(It->Id, 0, true)))
		{
			Id = It->Id;
			Pin = 0;
			Output = true;
			return true;
		}
		for (int I = 0; I < MaterialNodeInputCount(Graph(), *It); ++I)
			if (MaterialNodeHasInput(Graph(), *It, I) && Near(P, GetPinPosition(It->Id, I, false)))
			{
				Id = It->Id;
				Pin = I;
				Output = false;
				return true;
			}
	}
	return false;
}
FReply SMaterialGraph::OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E)
{
	Last = Cursor = G.AbsoluteToLocal(E.GetAbsolutePosition());
	if (E.GetEffectingButton() == EMouseButton::Right)
	{
		MenuPosition = ToCanvas(Last);
		const auto Entries = SNew(SVerticalBox);
		auto Filter = [this, Entries](const FString& Search) {
			Entries->ClearChildren();
			for (int I = 0; I < static_cast<int>(EMaterialExpression::Count); ++I)
			{
				auto Type = static_cast<EMaterialExpression>(I);
				if (!FString(MaterialExpressionName(Type)).ToLower().Contains(Search.ToLower().View()))
					continue;
				Entries->AddSlot().AutoHeight()
					[SNew(SMenuEntry).Text(MaterialExpressionName(Type)).OnSelected([this, Type] {
						AddNode(Type, MenuPosition);
					})];
			}
		};
		Filter({});
		const auto Menu =
			SNew(SVerticalBox) +
			SVerticalBox::Slot().AutoHeight()
				[SNew(SEditableTextBox).HintText("Search material nodes").OnTextChanged(Filter)] +
			SVerticalBox::Slot().AutoHeight()[SNew(SBox).MaxDesiredHeight(
				450.f)[SNew(SScrollBox) + SScrollBox::Slot()[Entries]]];
		auto& App = FSlateApplication::Get();
		auto W = App.FindWindowForWidget(*this);
		App.PushMenu(SharedThis(this), Menu, E.GetAbsolutePosition() / (W ? W->GetScale() : 1.f));
		return FReply::Handled();
	}
	if (E.GetEffectingButton() == EMouseButton::Middle)
	{
		bPanning = true;
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}
	if (E.GetEffectingButton() != EMouseButton::Left)
		return FReply::Unhandled();
	uint32 Id;
	int Pin;
	bool Output;
	if (HitPin(Last, Id, Pin, Output))
	{
		if (E.IsAltDown() && !Output)
			Disconnect(Id, Pin);
		else if (Output)
			WireSource = Id;
		else if (Id ? Graph().Find(Id)->Inputs[Pin] : Graph().Outputs[Pin])
		{
			WireSource = Id ? Graph().Find(Id)->Inputs[Pin] : Graph().Outputs[Pin];
			Disconnect(Id, Pin);
		}
		return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this));
	}
	uint32 Hit = 0;
	auto P = ToCanvas(Last);
	for (auto It = Graph().Nodes.rbegin(); It != Graph().Nodes.rend(); ++It)
		if (P.X >= It->X && P.X <= It->X + Width && P.Y >= It->Y &&
			P.Y <= It->Y + (It->Expression == EMaterialExpression::Comment ? 30 : Height(Graph(), *It)))
		{
			Hit = It->Id;
			break;
		}
	const bool Additive = E.IsControlDown() || E.IsShiftDown();
	if (Hit)
	{
		if (Additive)
		{
			if (IsNodeSelected(Hit))
				std::erase(Selection, Hit);
			else
				Selection.push_back(Hit);
		}
		else if (!IsNodeSelected(Hit))
			Selection = {Hit};
		Selected = IsNodeSelected(Hit) ? Hit : (Selection.empty() ? 0 : Selection.back());
	}
	else
	{
		if (!Additive)
			Selection.clear();
		Selected = Selection.empty() ? 0 : Selection.back();
		bMarquee = true;
		MarqueeStart = Last;
	}
	bDragging = Hit && IsNodeSelected(Hit);
	bMoved = false;
	OnSelectionChanged.ExecuteIfBound();
	Invalidate(EInvalidateWidgetReason::Paint);
	return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this));
}
FReply SMaterialGraph::OnMouseMove(const FGeometry& G, const FPointerEvent& E)
{
	Cursor = G.AbsoluteToLocal(E.GetAbsolutePosition());
	const auto Delta = Cursor - Last;
	Last = Cursor;
	if (bPanning)
		Pan += Delta;
	if (bDragging && (Delta.X != 0 || Delta.Y != 0))
	{
		if (!bMoved)
		{
			Remember();
			bMoved = true;
		}
		for (auto Id : Selection)
			if (auto* N = Graph().Find(Id))
			{
				N->X += Delta.X / Zoom;
				N->Y += Delta.Y / Zoom;
			}
	}
	if (bPanning || bDragging || WireSource || bMarquee)
	{
		Invalidate(EInvalidateWidgetReason::Paint);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}
FReply SMaterialGraph::OnMouseButtonUp(const FGeometry& G, const FPointerEvent& E)
{
	if (E.GetEffectingButton() != EMouseButton::Left && E.GetEffectingButton() != EMouseButton::Middle)
		return FReply::Unhandled();
	if (bMarquee)
	{
		const auto A = ToCanvas(MarqueeStart), B = ToCanvas(G.AbsoluteToLocal(E.GetAbsolutePosition()));
		const float X0 = std::min(A.X, B.X), Y0 = std::min(A.Y, B.Y), X1 = std::max(A.X, B.X),
					Y1 = std::max(A.Y, B.Y);
		if (X1 - X0 > 2 || Y1 - Y0 > 2)
			for (const auto& N : Graph().Nodes)
				if (N.X <= X1 && N.X + Width >= X0 && N.Y <= Y1 && N.Y + Height(Graph(), N) >= Y0 &&
					!IsNodeSelected(N.Id))
					Selection.push_back(N.Id);
		Selected = Selection.empty() ? 0 : Selection.back();
		OnSelectionChanged.ExecuteIfBound();
	}
	if (WireSource)
	{
		uint32 Id;
		int Pin;
		bool Output;
		FString Error;
		if (HitPin(G.AbsoluteToLocal(E.GetAbsolutePosition()), Id, Pin, Output) && !Output)
			if (!Connect(WireSource, Id, Pin, Error))
			{
				SetToolTipText(Error);
				OnError.ExecuteIfBound(Error);
			}
	}
	if (bMoved)
		OnChanged.ExecuteIfBound();
	OnMouseCaptureLost();
	return FReply::Handled().ReleaseMouseCapture();
}
void SMaterialGraph::OnMouseCaptureLost()
{
	bDragging = bPanning = bMoved = bMarquee = false;
	WireSource = 0;
	Invalidate(EInvalidateWidgetReason::Paint);
}
FReply SMaterialGraph::OnMouseWheel(const FGeometry& G, const FPointerEvent& E)
{
	const auto P = G.AbsoluteToLocal(E.GetAbsolutePosition()), Before = ToCanvas(P);
	Zoom = FMath::Clamp(Zoom * std::pow(1.15f, E.GetWheelDelta()), .25f, 1.75f);
	Pan = P - Before * Zoom;
	Invalidate(EInvalidateWidgetReason::Paint);
	return FReply::Handled();
}
FReply SMaterialGraph::OnKeyDown(const FGeometry&, const FKeyEvent& E)
{
	auto& Platform = FSlateApplication::Get().GetPlatformApplication();
	if (E.IsControlDown() && E.GetKey() == EKey::A)
	{
		TArray<uint32> All;
		for (const auto& N : Graph().Nodes)
			All.push_back(N.Id);
		SetNodeSelection(All);
		return FReply::Handled();
	}
	if (E.IsControlDown() && (E.GetKey() == EKey::C || E.GetKey() == EKey::X))
	{
		if (!Selection.empty())
		{
			Platform.ClipboardCopy(CopySelection());
			if (E.GetKey() == EKey::X)
				DeleteSelected();
		}
		return FReply::Handled();
	}
	if (E.IsControlDown() && E.GetKey() == EKey::V)
	{
		FString Error;
		if (!PasteNodes(Platform.ClipboardPaste(), ToCanvas(Cursor), Error))
			OnError.ExecuteIfBound(Error);
		return FReply::Handled();
	}
	if (E.IsControlDown() && E.GetKey() == EKey::D)
	{
		DuplicateSelected();
		return FReply::Handled();
	}
	if (E.GetKey() == EKey::Escape)
	{
		OnMouseCaptureLost();
		return FReply::Handled().ReleaseMouseCapture();
	}
	if (E.IsControlDown() && E.GetKey() == EKey::Z)
	{
		Undo();
		return FReply::Handled();
	}
	if (E.IsControlDown() && E.GetKey() == EKey::Y)
	{
		Redo();
		return FReply::Handled();
	}
	if (E.GetKey() == EKey::Delete)
	{
		DeleteSelected();
		return FReply::Handled();
	}
	if (E.GetKey() == EKey::F)
	{
		FrameAll();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}
int32 SMaterialGraph::OnPaint(const FPaintArgs&, const FGeometry& G, const FRect&,
							  FSlateWindowElementList& Out, int32 L, const FWidgetStyle&) const
{
	Out.AddSolidBox(L, G.ToPaintGeometry(), FColor(22, 24, 28));
	const auto Area = G.GetLocalSize();
	const float Step = 32 * Zoom;
	for (float X = std::fmod(Pan.X, Step); X < Area.X; X += Step)
		Out.AddLines(L + 1, G.ToPaintGeometry(), {FVector2(X, 0), FVector2(X, Area.Y)}, FColor(32, 35, 40));
	for (float Y = std::fmod(Pan.Y, Step); Y < Area.Y; Y += Step)
		Out.AddLines(L + 1, G.ToPaintGeometry(), {FVector2(0, Y), FVector2(Area.X, Y)}, FColor(32, 35, 40));
	for (const auto& N : Graph().Nodes)
		if (N.Expression == EMaterialExpression::Comment)
		{
			const auto Size =
				FVector2(FMath::Clamp(N.Value[0], 210.f, 4000.f), FMath::Clamp(N.Value[1], 80.f, 4000.f));
			Out.AddRoundedBox(L + 1, G.ToPaintGeometry(FVector2(N.X, N.Y) * Zoom + Pan, Size * Zoom),
							  FColor(65, 56, 30, 190), 5,
							  IsNodeSelected(N.Id) ? FColor(240, 170, 45) : FColor(120, 105, 60), 1);
		}
	auto Wire = [&](FVector2 A, FVector2 B) {
		TArray<FVector2> Points;
		float Span = std::max(40.f, std::abs(B.X - A.X) * .5f);
		for (int I = 0; I <= 24; ++I)
		{
			float T = I / 24.f, U = 1 - T;
			Points.push_back(A * (U * U * U) + (A + FVector2(Span, 0)) * (3 * U * U * T) +
							 (B - FVector2(Span, 0)) * (3 * U * T * T) + B * (T * T * T));
		}
		Out.AddLines(L + 2, G.ToPaintGeometry(), std::move(Points), FColor(180, 190, 205), 2);
	};
	for (const auto& N : Graph().Nodes)
		for (int I = 0; I < MaterialNodeInputCount(Graph(), N); ++I)
			if (N.Inputs[I])
				Wire(GetPinPosition(N.Inputs[I], 0, true), GetPinPosition(N.Id, I, false));
	for (int I = 0; I < ((Graph().bFunction || Graph().bUseMaterialAttributes) ? 1 : 6); ++I)
		if (Graph().Outputs[I])
			Wire(GetPinPosition(Graph().Outputs[I], 0, true), GetPinPosition(0, I, false));
	if (WireSource)
		Wire(GetPinPosition(WireSource, 0, true), Cursor);
	auto Font = FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText").Font;
	Font.Size = 14;
	auto Text = [&](FVector2 P, const FString& S, FColor C) {
		auto Run = FSlateApplication::Get().GetFontCache().ShapeText(S.View(), Font, G.Scale * Zoom);
		Out.AddText(L + 5,
					G.ToPaintGeometry(P, FVector2(Width, 24)).WithLocalTransform(FTransform2D(Zoom, {})), Run,
					C);
	};
	auto Node = [&](uint32 Id, FVector2 Origin, const FString& Name, int Inputs, float H) {
		auto P = Origin * Zoom + Pan;
		Out.AddRoundedBox(L + 3, G.ToPaintGeometry(P, FVector2(Width, H) * Zoom), FColor(47, 50, 57), 5,
						  FColor(Id && IsNodeSelected(Id) ? 240 : 75, Id && IsNodeSelected(Id) ? 170 : 80,
								 Id && IsNodeSelected(Id) ? 45 : 90),
						  Id && IsNodeSelected(Id) ? 2.f : 1.f);
		Out.AddSolidBox(L + 4, G.ToPaintGeometry(P, FVector2(Width, 30) * Zoom),
						Id ? FColor(45, 80, 110) : FColor(95, 47, 45));
		Text(P + FVector2(10, 6) * Zoom, Name, FColor::White());
		for (int I = 0; I < Inputs; ++I)
		{
			if (Id && !MaterialNodeHasInput(Graph(), *Graph().Find(Id), I))
				continue;
			const auto Pin = GetPinPosition(Id, I, false);
			Out.AddRoundedBox(L + 5, G.ToPaintGeometry(Pin - FVector2(5, 5), FVector2(10, 10)),
							  FColor(180, 190, 210), 5);
			Text(Pin + FVector2(12, -8) * Zoom,
				 Id								  ? MaterialNodeInputName(Graph(), *Graph().Find(Id), I)
				 : Graph().bFunction			  ? "Function Result"
				 : Graph().bUseMaterialAttributes ? "Material Attributes"
												  : MaterialOutputName(static_cast<EMaterialOutput>(I)),
				 FColor(220, 225, 235));
		}
		if (Id)
		{
			const auto Pin = GetPinPosition(Id, 0, true);
			Out.AddRoundedBox(L + 5, G.ToPaintGeometry(Pin - FVector2(5, 5), FVector2(10, 10)),
							  FColor(220, 220, 220), 5);
		}
	};
	for (const auto& N : Graph().Nodes)
		if (N.Expression != EMaterialExpression::Comment)
			Node(N.Id, FVector2(N.X, N.Y), MaterialExpressionName(N.Expression),
				 MaterialNodeInputCount(Graph(), N), Height(Graph(), N));
	for (const auto& N : Graph().Nodes)
		if (static_cast<int>(N.Expression) <= 3)
		{
			FString Values;
			for (int I = 0; I <= static_cast<int>(N.Expression); ++I)
			{
				if (I)
					Values += ", ";
				Values += FString::Format("{:.3f}", N.Value[I]);
			}
			Text(FVector2(N.X + 12, N.Y + 48) * Zoom + Pan, Values, FColor(205, 215, 225));
		}
	for (const auto& N : Graph().Nodes)
		if (!N.Description.IsEmpty())
			Text(FVector2(N.X + 8, N.Y + (N.Expression == EMaterialExpression::Comment ? 6 : -22)) * Zoom +
					 Pan,
				 N.Description.Left(60), FColor(210, 195, 140));
	if (bMarquee)
	{
		const FVector2 A(std::min(MarqueeStart.X, Cursor.X), std::min(MarqueeStart.Y, Cursor.Y)),
			B(std::max(MarqueeStart.X, Cursor.X), std::max(MarqueeStart.Y, Cursor.Y));
		Out.AddRoundedBox(L + 6, G.ToPaintGeometry(A, B - A), FColor(80, 150, 230, 35), 0,
						  FColor(100, 170, 240), 1);
	}
	Node(0, FVector2(OutputX, OutputY),
		 Graph().bFunction				  ? "Function Result"
		 : Graph().bUseMaterialAttributes ? "Material Attributes"
										  : "Material",
		 (Graph().bFunction || Graph().bUseMaterialAttributes) ? 1 : 6,
		 (Graph().bFunction || Graph().bUseMaterialAttributes) ? 94.f : 260.f);
	return L + 6;
}

void SMaterialGraph::SetQualityLevel(uint8 Quality)
{
	if (Quality > 2 || Graph().QualityLevel == Quality)
		return;
	Remember();
	Graph().QualityLevel = Quality;
	Changed();
}

bool SMaterialGraph::DropAsset(const FAssetReference& Ref, const FString& Schema, FVector2 Position,
							   FString& Error)
{
	if (!Registry)
	{
		Error = "Asset drops require an asset registry.";
		return false;
	}
	if (!Registry->Resolve(Ref, Error, Schema.View()))
		return false;
	auto Candidate = Graph();
	EMaterialExpression Type;
	if (Schema == "Genesis.Texture2D")
		Type = EMaterialExpression::TextureSample;
	else if (Schema == "Genesis.MaterialFunction")
		Type = EMaterialExpression::MaterialFunctionCall;
	else if (Schema == "Genesis.MaterialParameterCollection")
		Type = EMaterialExpression::ScalarCollectionParameter;
	else
	{
		Error = "This asset is not a material graph input.";
		return false;
	}
	const auto Id = Candidate.Add(Type, Position.X, Position.Y);
	if (!Id)
	{
		Error = "Material node limit reached.";
		return false;
	}
	auto* Node = Candidate.Find(Id);
	if (Type == EMaterialExpression::TextureSample)
		Node->Texture = Ref;
	else if (Type == EMaterialExpression::MaterialFunctionCall)
		Node->Function = Ref;
	else
	{
		FAssetPackage P;
		FMaterialParameterCollection C;
		if (!Registry->Load(Ref, P, Error) || !FMaterialParameterCollectionCodec::Decode(P, C, Error))
			return false;
		if (C.Parameters.empty())
		{
			Error = "Choose a collection with a parameter.";
			return false;
		}
		Node->Collection = Ref;
		Node->ParameterName = C.Parameters[0].Name;
		if (C.Parameters[0].Width == 4)
			Node->Expression = EMaterialExpression::VectorCollectionParameter;
	}
	bool DependenciesChanged = false;
	if (!FMaterialFunctionCodec::Refresh(Candidate, *Registry, DependenciesChanged, Error))
		return false;
	Remember();
	Graph() = std::move(Candidate);
	SetNodeSelection({Id});
	Changed();
	Error.Empty();
	return true;
}
FReply SMaterialGraph::OnDragOver(const FGeometry&, const FDragDropEvent& Event)
{
	auto Op = Event.GetOperationAs<FAssetDragDropOp>();
	return Op && (Op->Schema == "Genesis.Texture2D" || Op->Schema == "Genesis.MaterialFunction" ||
				  Op->Schema == "Genesis.MaterialParameterCollection")
			   ? FReply::Handled()
			   : FReply::Unhandled();
}
FReply SMaterialGraph::OnDrop(const FGeometry& G, const FDragDropEvent& Event)
{
	auto Op = Event.GetOperationAs<FAssetDragDropOp>();
	if (!Op)
		return FReply::Unhandled();
	FString Error;
	if (!DropAsset(Op->Asset, Op->Schema, ToCanvas(G.AbsoluteToLocal(Event.GetAbsolutePosition())), Error))
	{
		OnError.ExecuteIfBound(Error);
		return FReply::Unhandled();
	}
	return FReply::Handled();
}

void SMaterialGraph::SetUseMaterialAttributes(bool Use)
{
	if (Graph().bUseMaterialAttributes == Use)
		return;
	Remember();
	Graph().bUseMaterialAttributes = Use;
	for (auto& Output : Graph().Outputs)
		Output = 0;
	Changed();
}

void SMaterialGraph::SetMaterialSettings(EMaterialBlendMode Mode, float Clip)
{
	if (static_cast<int>(Mode) < 0 || static_cast<int>(Mode) > 2 || !std::isfinite(Clip) || Clip < 0 ||
		Clip > 1)
		return;
	if (Material->BlendMode == Mode && Material->OpacityMaskClip == Clip)
		return;
	Remember();
	Material->BlendMode = Mode;
	Material->OpacityMaskClip = Clip;
	Changed();
}
