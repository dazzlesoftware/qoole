// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "MaterialParameterCollectionEditor.h"
#include "Materials/MaterialParameterCollection.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Text/SEditableText.h"
#include "Widgets/Text/STextBlock.h"
#include "Services/Services.h"
#include "Services/MessageDialog.h"
namespace
{
class SCollectionEditor final : public SCompoundWidget
{
  public:
	SLATE_BEGIN_ARGS(SCollectionEditor)
	SLATE_END_ARGS() void Construct(const FArguments&, FMaterialEditorArgs InArgs,
									FMaterialParameterCollection InCollection)
	{
		Args = std::move(InArgs);
		Collection = std::move(InCollection);
		auto Toolbar = SNew(SHorizontalBox);
		Toolbar->AddSlot().AutoWidth()[SNew(SButton).Text("Add Scalar").OnClicked([this] {
			Add(1);
			return FReply::Handled();
		})];
		Toolbar->AddSlot().AutoWidth()[SNew(SButton).Text("Add Vector").OnClicked([this] {
			Add(4);
			return FReply::Handled();
		})];
		Toolbar->AddSlot().AutoWidth()[SNew(SButton).Text("Undo").OnClicked([this] {
			History(true);
			return FReply::Handled();
		})];
		Toolbar->AddSlot().AutoWidth()[SNew(SButton).Text("Redo").OnClicked([this] {
			History(false);
			return FReply::Handled();
		})];
		Toolbar->AddSlot().AutoWidth()[SNew(SButton).Text("Save").OnClicked([this] {
			Save();
			return FReply::Handled();
		})];
		ChildSlot[SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight()[Toolbar] +
				  SVerticalBox::Slot().FillHeight(
					  1)[SNew(SScrollBox) + SScrollBox::Slot()[SAssignNew(Rows, SVerticalBox)]] +
				  SVerticalBox::Slot()
					  .AutoHeight()[SNew(STextBlock).Text([this] { return Status; }).AutoWrapText(true)]];
		Rebuild();
	}
	bool IsDirty() const
	{
		return Dirty;
	}

  private:
	void Remember()
	{
		if (Undo.size() >= 100)
			Undo.erase(Undo.begin());
		Undo.push_back(Collection);
		Redo.clear();
		Dirty = true;
		Status = "Edited shared defaults; save to update dependent materials.";
	}
	void History(bool Back)
	{
		auto& Source = Back ? Undo : Redo;
		auto& Target = Back ? Redo : Undo;
		if (Source.empty())
			return;
		Target.push_back(Collection);
		Collection = std::move(Source.back());
		Source.pop_back();
		Dirty = true;
		Rebuild();
	}
	void Add(uint8 Width)
	{
		if (Collection.Parameters.size() >= 256)
		{
			Status = "Collection parameter limit reached.";
			return;
		}
		Remember();
		FMaterialCollectionParameter P;
		P.Width = Width;
		int I = 1;
		do
		{
			P.Name = FString::Format("{}{}", Width == 1 ? "Scalar" : "Vector", I++);
		} while (std::any_of(Collection.Parameters.begin(), Collection.Parameters.end(),
							 [&](const auto& V) { return V.Name == P.Name; }));
		Collection.Parameters.push_back(P);
		Rebuild();
	}
	void Rebuild()
	{
		Rows->ClearChildren();
		for (size_t I = 0; I < Collection.Parameters.size(); ++I)
		{
			auto Row = SNew(SHorizontalBox);
			Row->AddSlot().AutoWidth()[SNew(STextBlock)
										   .Text(Collection.Parameters[I].Width == 1 ? "Scalar" : "Vector")];
			Row->AddSlot().FillWidth(1)[SNew(SEditableTextBox)
											.Text(Collection.Parameters[I].Name)
											.OnTextChanged([this, I](const FString& S) {
												if (Collection.Parameters[I].Name == S)
													return;
												Remember();
												Collection.Parameters[I].Name = S;
											})];
			for (int C = 0; C < Collection.Parameters[I].Width; ++C)
				Row->AddSlot()
					.AutoWidth()[SNew(SSpinBox<float>)
									 .MinValue(-1000000.f)
									 .MaxValue(1000000.f)
									 .Value([this, I, C] { return Collection.Parameters[I].Value[C]; })
									 .OnValueChanged([this, I, C](float V) {
										 if (Collection.Parameters[I].Value[C] == V)
											 return;
										 Remember();
										 Collection.Parameters[I].Value[C] = V;
									 })];
			Row->AddSlot().AutoWidth()[SNew(SButton).Text("Remove").OnClicked([this, I] {
				Remember();
				Collection.Parameters.erase(Collection.Parameters.begin() + I);
				Rebuild();
				return FReply::Handled();
			})];
			Rows->AddSlot().AutoHeight()[Row];
		}
	}
	void Save()
	{
		FAssetPackage P;
		const auto* A = Args.Registry->Resolve(Args.Material, Status, "Genesis.MaterialParameterCollection");
		if (!A)
			return;
		const auto File = A->FilePath;
		if (!Args.Registry->Load(Args.Material, P, Status, "Genesis.MaterialParameterCollection") ||
			!FMaterialParameterCollectionCodec::Encode(Collection, P, Status) ||
			!FAssetPackageStore::Save(File, P, Status))
			return;
		Dirty = false;
		Status = "Saved shared parameter defaults.";
		Args.Registry->Scan();
		Args.OnSaved.ExecuteIfBound();
		Args.OnLog.ExecuteIfBound(Status);
	}
	FMaterialEditorArgs Args;
	FMaterialParameterCollection Collection;
	TArray<FMaterialParameterCollection> Undo, Redo;
	TSharedPtr<SVerticalBox> Rows;
	bool Dirty = false;
	FString Status = "Shared defaults use uniforms; value changes do not require new shaders.";
};
} // namespace
TSharedPtr<SWindow> FMaterialParameterCollectionEditor::Open(const FMaterialEditorArgs& Args, FString& Error)
{
	if (!Args.Registry)
	{
		Error = "Collection editor requires an asset registry.";
		return {};
	}
	FAssetPackage P;
	FMaterialParameterCollection Collection;
	if (!Args.Registry->Load(Args.Material, P, Error, "Genesis.MaterialParameterCollection") ||
		!FMaterialParameterCollectionCodec::Decode(P, Collection, Error))
		return {};
	const auto Panel = SNew(SCollectionEditor, Args, std::move(Collection));
	const auto Window =
		SNew(SWindow)
			.Title("Material Parameter Collection: " + Args.Name)
			.ClientSize(FVector2(880, 520))
			.OnRequestClose([Panel] {
				return !Panel->IsDirty() || FServices::GetMessageDialog().Show(
												EMessageType::Question, EMessageButtons::YesNo,
												"Discard unsaved collection changes?",
												"Material Parameter Collection") == EMessageResult::Yes;
			})[Panel];
	FSlateApplication::Get().AddWindow(Window);
	Error.Empty();
	return Window.ToSharedPtr();
}
