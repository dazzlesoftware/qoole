// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "MaterialInstanceEditor.h"
#include "Templates/SharedPointer.h"
#include "Internationalization/TextLocalization.h"
#include "MaterialEditingLibrary.h"
#include <set>
#include <functional>
#include "MaterialEditorViewportClient.h"
#include "SlateRHIRenderer.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Text/STextBlock.h"
#include "Services/Services.h"
#include "Services/MessageDialog.h"
#include <algorithm>
namespace
{
	class SMaterialInstanceEditorPanel final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SMaterialInstanceEditorPanel)
		SLATE_ARGUMENT(FMaterialEditorArgs, EditorArgs, {})
		SLATE_ARGUMENT(FMaterialInstanceAsset, Instance, {})
		SLATE_ARGUMENT(TSharedPtr<FMaterialEditorViewportClient>, Preview, nullptr)
		SLATE_END_ARGS()
		void Construct(const FArguments& A)
		{
			Args = A._EditorArgs;
			RegistryRevision = Args.Registry->GetRevision();
			Instance = A._Instance;
			Preview = A._Preview;
			const TSharedRef<SVerticalBox> Main = SNew(SVerticalBox);
			if (Preview)
			{
				Main->AddSlot().FillHeight(1)[SNew(SViewport).ViewportClient(Preview)];
				Main->AddSlot().AutoHeight()[MakeMaterialPreviewControls(
				    ToSharedRef(Preview), Args.Registry, [this] { Update(); })];
			}
			const TSharedRef<SVerticalBox> Controls = SNew(SVerticalBox);
			Controls->AddSlot().AutoHeight()[SNew(STextBlock).Text(GENESIS_TEXT("Parent Material"))];
			Controls->AddSlot().AutoHeight()[SNew(SComboButton)
			        .Text([this] { return Instance.Parent.Path; })
			        .OnGetMenuContent([this]
			{
				const TSharedRef<SVerticalBox> Choices = SNew(SVerticalBox);
				for (const FAssetData& Asset : Args.Registry->GetAssets())
					if ((Asset.Metadata.Schema == "Genesis.Material" ||
					        Asset.Metadata.Schema == "Genesis.MaterialInstance") &&
					    Asset.Metadata.Id != Args.Material.Id)
						Choices->AddSlot().AutoHeight()[SNew(SMenuEntry)
						        .Text(Asset.PackagePath)
						        .OnSelected([this, Ref = Asset.GetReference()]
						{
							Remember();
							Instance.Parent = Ref;
							Rebuild();
						})];
				return SNew(SScrollBox) + SScrollBox::Slot()[Choices];
			})];
			Controls->AddSlot().AutoHeight()[SNew(STextBlock)
			        .Text(GENESIS_TEXT("Inherited parameters use parent values. Enable Override "
			                           "to edit; Reset restores inheritance."))
			        .AutoWrapText(true)];
			Controls->AddSlot().FillHeight(
			    1)[SNew(SScrollBox) + SScrollBox::Slot()[SAssignNew(Parameters, SVerticalBox)]];
			Controls->AddSlot().AutoHeight()[SNew(SHorizontalBox) +
			    SHorizontalBox::Slot().AutoWidth()[SNew(SButton)
			            .Text(GENESIS_TEXT("Undo"))
			            .OnClicked(
			                [this]
			{
				Undo();
				return FReply::Handled();
			})] +
			    SHorizontalBox::Slot().AutoWidth()[SNew(SButton)
			            .Text(GENESIS_TEXT("Redo"))
			            .OnClicked([this]
			{
				Redo();
				return FReply::Handled();
			})]];
			Controls->AddSlot().AutoHeight()[SNew(SHorizontalBox) +
			    SHorizontalBox::Slot().AutoWidth()[SNew(SButton)
			            .Text(GENESIS_TEXT("Save"))
			            .OnClicked(
			                [this]
			{
				Save(false);
				return FReply::Handled();
			})] +
			    SHorizontalBox::Slot().AutoWidth()[SNew(SButton)
			            .Text(GENESIS_TEXT("Save and Apply"))
			            .IsEnabled(Args.OnApply.IsBound())
			            .OnClicked(
			                [this]
			{
				Save(true);
				return FReply::Handled();
			})] +
			    SHorizontalBox::Slot()
			        .AutoWidth()[SNew(SSpinBox<int32>).MinValue(0).MaxValue(63).Value([this] {
				return ApplySlot;
			}).OnValueChanged([this](int32 Slot) { ApplySlot = Slot; })]];
			Controls->AddSlot().AutoHeight()[SNew(STextBlock).Text([this] { return Status; }).AutoWrapText(true)];
			ChildSlot[SNew(SSplitter) + SSplitter::Slot().Value(.5f)[Main] + SSplitter::Slot().Value(.5f)[Controls]];
			Rebuild();
		}
		void Tick(const FGeometry& G, double T, float D) override
		{
			SCompoundWidget::Tick(G, T, D);
			if (Args.Registry && RegistryRevision != Args.Registry->GetRevision())
			{
				RegistryRevision = Args.Registry->GetRevision();
				if (!bDirty)
				{
					FAssetPackage P;
					FMaterialInstanceAsset Current;
					FString Error;
					if (Args.Registry->Load(Args.Material, P, Error, "Genesis.MaterialInstance") &&
					    FRenderAssetCodec::DecodeMaterialInstance(P, Current, Error))
						Instance = std::move(Current);
				}
				Rebuild();
			}
		}
		bool IsDirty() const
		{
			return bDirty;
		}

	private:
		bool LoadParent()
		{
			// Reject cycles involving the edited asset before writing anything.
			FAssetReference Ref = Instance.Parent;
			for (int Depth = 0; Depth < 31; ++Depth)
			{
				FAssetPackage P;
				if (!Args.Registry->Load(Ref, P, Status))
					return false;
				if (P.Id == Args.Material.Id)
				{
					Status = GENESIS_TEXT("Parent selection would create a material instance cycle.");
					return false;
				}
				if (P.Schema == "Genesis.Material")
					return FRenderAssetLoader(*Args.Registry).LoadMaterial(Instance.Parent, Parent, Status);
				FMaterialInstanceAsset Next;
				if (!FRenderAssetCodec::DecodeMaterialInstance(P, Next, Status))
					return false;
				Ref = Next.Parent;
			}
			Status = GENESIS_TEXT("Material instance parent chain is cyclic or too deep.");
			return false;
		}
		bool Update()
		{
			FMaterialAsset Resolved;
			if (!LoadParent() || !FMaterialEditingLibrary::CompileMaterialInstance(Parent, Instance, Status) ||
			    !FMaterialInstance::Resolve(Parent, Instance, Resolved, Status))
				return false;
			if (Preview && !Preview->Update(Resolved, *Args.Registry, Status))
				return false;
			Status = bDirty ? (Instance.StaticParameters.empty() && !Instance.bOverrideQuality
			                          ? "Modified uniforms - shader compilation not required"
			                          : "Modified static/quality permutation - cooked for both backends")
			                : "Inherited material preview";
			return true;
		}
		void Remember()
		{
			UndoStack.push_back(Instance);
			if (UndoStack.size() > 100)
				UndoStack.erase(UndoStack.begin());
			RedoStack.clear();
			bDirty = true;
		}
		void Undo()
		{
			if (UndoStack.empty())
				return;
			RedoStack.push_back(Instance);
			Instance = UndoStack.back();
			UndoStack.pop_back();
			bDirty = true;
			Rebuild();
		}
		void Redo()
		{
			if (RedoStack.empty())
				return;
			UndoStack.push_back(Instance);
			Instance = RedoStack.back();
			RedoStack.pop_back();
			bDirty = true;
			Rebuild();
		}
		FMaterialParameterOverride* Find(const FString& Name)
		{
			for (FMaterialParameterOverride& P : Instance.Parameters)
				if (P.Name == Name)
					return &P;
			return nullptr;
		}
		FMaterialTextureParameterOverride* FindTexture(const FString& Name)
		{
			for (FMaterialTextureParameterOverride& T : Instance.Textures)
				if (T.Name == Name)
					return &T;
			return nullptr;
		}
		FMaterialStaticParameterOverride* FindStatic(const FString& Name)
		{
			for (FMaterialStaticParameterOverride& P : Instance.StaticParameters)
				if (P.Name == Name)
					return &P;
			return nullptr;
		}
		void Rebuild()
		{
			Parameters->ClearChildren();
			if (!LoadParent())
				return;
			FMaterialGraphProgram Program;
			if (!Parent.Graph)
			{
				Status = GENESIS_TEXT("Save the parent as a graph material before authoring an instance.");
				return;
			}
			FMaterialAsset Variant;
			if (!FMaterialInstance::BuildStaticPermutation(Parent, Instance, Variant, Status))
				Variant = Parent;
			if (!FMaterialGraphCompiler::Compile(*Variant.Graph, Program, Status))
				return;
			Parameters->AddSlot().AutoHeight()[SNew(SComboButton)
			        .Text(
			            [this]
			{
				return Instance.bOverrideQuality ? FString::Format("Quality: {}",
				                                       Instance.QualityLevel == 0       ? "Low"
				                                           : Instance.QualityLevel == 1 ? "Medium"
				                                                                        : "High")
				                                 : FString("Quality: Inherit");
			})
			        .OnGetMenuContent([this]
			{
				const TSharedRef<SVerticalBox> Choices = SNew(SVerticalBox);
				for (int I = -1; I < 3; ++I)
					Choices->AddSlot().AutoHeight()[SNew(SMenuEntry)
					        .Text(I < 0      ? "Inherit"
					                : I == 0 ? "Low"
					                : I == 1 ? "Medium"
					                         : "High")
					        .OnSelected([this, I]
					{
						Remember();
						Instance.bOverrideQuality = I >= 0;
						if (I >= 0)
							Instance.QualityLevel = static_cast<uint8>(I);
						Rebuild();
					})];
				return Choices;
			})];
			std::set<FString> StaticNames;
			std::function<void(const FMaterialGraph&)> StaticControls = [&](const FMaterialGraph& Graph)
			{
				for (const FMaterialNode& Node : Graph.Nodes)
					if (Node.Expression == EMaterialExpression::StaticSwitchParameter &&
					    StaticNames.insert(Node.ParameterName).second)
					{
						const FString Name = Node.ParameterName;
						const bool Default = Node.Value[2] != 0;
						Parameters->AddSlot().AutoHeight()[SNew(STextBlock).Text(Name + " (Static Switch)")];
						Parameters->AddSlot().AutoHeight()[SNew(SButton)
						        .Text(FindStatic(Name) ? "Reset Static to Parent" : "Override Static")
						        .OnClicked([this, Name, Default]
						{
							Remember();
							if (FindStatic(Name))
								std::erase_if(Instance.StaticParameters,
								    [&](const TArray<FMaterialStaticParameterOverride>::value_type& P)
								{ return P.Name == Name; });
							else
								Instance.StaticParameters.push_back({Name, Default});
							Rebuild();
							return FReply::Handled();
						})];
						Parameters->AddSlot().AutoHeight()[SNew(SButton)
						        .IsEnabled(FindStatic(Name) != nullptr)
						        .Text(
						            [this, Name, Default]
						{
							const FMaterialStaticParameterOverride* P = FindStatic(Name);
							return (P ? P->Value : Default) ? FString("Static: True") : FString("Static: False");
						})
						        .OnClicked([this, Name]
						{
							if (FMaterialStaticParameterOverride* P = FindStatic(Name))
							{
								Remember();
								FindStatic(Name)->Value = !P->Value;
								Rebuild();
							}
							return FReply::Handled();
						})];
					}
				for (const FMaterialFunctionSnapshot& F : Graph.Functions)
					if (F.Graph)
						StaticControls(*F.Graph);
			};
			StaticControls(*Parent.Graph);
			for (const FMaterialStaticParameterOverride& P : Instance.StaticParameters)
				if (!StaticNames.contains(P.Name))
					Parameters->AddSlot().AutoHeight()[SNew(SButton)
					        .Text("Remove missing static parameter: " + P.Name)
					        .OnClicked([this, Name = P.Name]
					{
						Remember();
						std::erase_if(Instance.StaticParameters,
						    [&](const TArray<FMaterialStaticParameterOverride>::value_type& V)
						{ return V.Name == Name; });
						Rebuild();
						return FReply::Handled();
					})];

			// All overrides remain visible, including stale entries after a parent edit.
			// Removing those entries lets an instance recover without external file edits.
			for (const FMaterialParameterOverride& P : Instance.Parameters)
				if (std::none_of(Program.Parameters.begin(), Program.Parameters.end(),
				        [&](const TArray<FMaterialGraphParameter>::value_type& V) { return V.Name == P.Name; }))
					Parameters->AddSlot().AutoHeight()[SNew(SButton)
					        .Text("Remove missing parameter: " + P.Name)
					        .OnClicked([this, Name = P.Name]
					{
						Remember();
						std::erase_if(Instance.Parameters,
						    [&](const TArray<FMaterialParameterOverride>::value_type& V) { return V.Name == Name; });
						Rebuild();
						return FReply::Handled();
					})];
			for (const FMaterialTextureParameterOverride& T : Instance.Textures)
				if (std::none_of(Program.Textures.begin(), Program.Textures.end(),
				        [&](const TArray<FMaterialGraphTexture>::value_type& V) { return V.ParameterName == T.Name; }))
					Parameters->AddSlot().AutoHeight()[SNew(SButton)
					        .Text("Remove missing texture: " + T.Name)
					        .OnClicked([this, Name = T.Name]
					{
						Remember();
						std::erase_if(Instance.Textures,
						    [&](const TArray<FMaterialTextureParameterOverride>::value_type& V)
						{ return V.Name == Name; });
						Rebuild();
						return FReply::Handled();
					})];
			std::stable_sort(Program.Parameters.begin(), Program.Parameters.end(),
			    [](const TArray<FMaterialGraphParameter>::value_type& A,
			        const TArray<FMaterialGraphParameter>::value_type& B)
			{ return A.Group == B.Group ? A.Name < B.Name : A.Group < B.Group; });
			FString CurrentGroup;
			for (const FMaterialGraphParameter& P : Program.Parameters)
			{
				if (!P.Collection.IsNull())
					continue;
				const FString Group = P.Group.IsEmpty() ? FString("General") : P.Group;
				if (CurrentGroup != Group)
				{
					CurrentGroup = Group;
					Parameters->AddSlot().AutoHeight()[SNew(STextBlock).Text("Group: " + Group)];
				}
				Parameters->AddSlot()
				    .AutoHeight()[SNew(STextBlock).Text(P.Name + (P.Width == 1 ? " (Scalar)" : " (Vector)"))];
				Parameters->AddSlot().AutoHeight()[SNew(SButton)
				        .Text(Find(P.Name) ? "Reset to Parent" : "Override")
				        .OnClicked([this, P]
				{
					Remember();
					if (Find(P.Name))
						std::erase_if(Instance.Parameters,
						    [&](const TArray<FMaterialParameterOverride>::value_type& V) { return V.Name == P.Name; });
					else
					{
						FMaterialParameterOverride V;
						V.Name = P.Name;
						V.Width = P.Width;
						for (int I = 0; I < 4; ++I)
							V.Value[I] = P.Value[I];
						Instance.Parameters.push_back(V);
					}
					Rebuild();
					return FReply::Handled();
				})];
				const TSharedRef<SHorizontalBox> Values = SNew(SHorizontalBox);
				for (int I = 0; I < P.Width; ++I)
					Values->AddSlot().FillWidth(1)[SNew(SSpinBox<float>)
					        .MinValue(-1000000.f)
					        .MaxValue(1000000.f)
					        .Delta(.01f)
					        .IsEnabled(Find(P.Name) != nullptr)
					        .Value(
					            [this, P, I]
					{
						FMaterialParameterOverride* V = Find(P.Name);
						return V ? V->Value[I] : P.Value[I];
					})
					        .OnValueChanged([this, Name = P.Name, I](float Value)
					{
						if (FMaterialParameterOverride* V = Find(Name))
						{
							if (V->Value[I] == Value)
								return;
							Remember();
							Find(Name)->Value[I] = Value;
							Update();
						}
					})];
				Parameters->AddSlot().AutoHeight()[Values];
			}
			for (const FMaterialGraphTexture& T : Program.Textures)
				if (!T.ParameterName.IsEmpty())
				{
					Parameters->AddSlot().AutoHeight()[SNew(STextBlock).Text(T.ParameterName + " (Texture2D)")];
					Parameters->AddSlot().AutoHeight()[SNew(SButton)
					        .Text(FindTexture(T.ParameterName) ? "Reset to Parent" : "Override")
					        .OnClicked([this, T]
					{
						Remember();
						if (FindTexture(T.ParameterName))
							std::erase_if(Instance.Textures,
							    [&](const TArray<FMaterialTextureParameterOverride>::value_type& V)
							{ return V.Name == T.ParameterName; });
						else
							Instance.Textures.push_back({T.ParameterName, T.Reference});
						Rebuild();
						return FReply::Handled();
					})];
					Parameters->AddSlot().AutoHeight()[SNew(SComboButton)
					        .Text(
					            [this, T]
					{
						FMaterialTextureParameterOverride* V = FindTexture(T.ParameterName);
						return V ? V->Texture.Path : T.Reference.Path;
					})
					        .IsEnabled(FindTexture(T.ParameterName) != nullptr)
					        .OnGetMenuContent([this, Name = T.ParameterName]
					{
						const TSharedRef<SVerticalBox> Choices = SNew(SVerticalBox);
						for (const FAssetData& Asset : Args.Registry->GetAssets())
							if (Asset.Metadata.Schema == "Genesis.Texture2D")
								Choices->AddSlot().AutoHeight()[SNew(SMenuEntry)
								        .Text(Asset.PackagePath)
								        .OnSelected([this, Name, Ref = Asset.GetReference()]
								{
									if (FindTexture(Name))
									{
										Remember();
										FindTexture(Name)->Texture = Ref;
										Update();
									}
								})];
						return SNew(SScrollBox) + SScrollBox::Slot()[Choices];
					})];
				}
			Update();
		}
		void Save(bool Apply)
		{
			if (!Update())
				return;
			FAssetPackage P;
			if (!Args.Registry->Load(Args.Material, P, Status, "Genesis.MaterialInstance"))
				return;
			const FAssetData* Data = Args.Registry->Resolve(Args.Material, Status);
			if (!Data)
				return;
			const FString File = Data->FilePath;
			if (!FRenderAssetCodec::EncodeMaterialInstance(Instance, P, Status) ||
			    !FAssetPackageStore::Save(File, P, Status))
				return;
			Args.Registry->Scan();
			bDirty = false;
			Status = GENESIS_TEXT("Saved material instance");
			Args.OnSaved.ExecuteIfBound();
			if (Apply)
				Args.OnApply.ExecuteIfBound(ApplySlot);
			Args.OnLog.ExecuteIfBound(Status);
		}
		uint64 RegistryRevision = 0;
		FMaterialEditorArgs Args;
		FMaterialInstanceAsset Instance;
		FMaterialAsset Parent;
		TSharedPtr<FMaterialEditorViewportClient> Preview;
		TSharedPtr<SVerticalBox> Parameters;
		TArray<FMaterialInstanceAsset> UndoStack, RedoStack;
		FString Status;
		bool bDirty = false, bCube = false;
		int32 ApplySlot = 0;
	};
} // namespace
TSharedPtr<SWindow> FMaterialInstanceEditor::Open(const FMaterialEditorArgs& Args, FString& Error)
{
	if (!Args.Registry)
	{
		Error = GENESIS_TEXT("Material instance editor requires an asset registry.");
		return {};
	}
	FAssetPackage P;
	FMaterialInstanceAsset Instance;
	if (!Args.Registry->Load(Args.Material, P, Error, "Genesis.MaterialInstance") ||
	    !FRenderAssetCodec::DecodeMaterialInstance(P, Instance, Error))
		return {};
	TSharedPtr<FMaterialEditorViewportClient> Preview;
	if (Args.Device && Args.Renderer && Args.WindowRenderer)
	{
		Preview = MakeShared<FMaterialEditorViewportClient>(*Args.Device, *Args.Renderer);
		Args.WindowRenderer->AddViewportClient(ToSharedRef(Preview));
	}
	FMaterialEditorArgs CanonicalArgs = Args;
	CanonicalArgs.Material.Id = P.Id;
	const TSharedRef<SMaterialInstanceEditorPanel> Panel =
	    SNew(SMaterialInstanceEditorPanel).EditorArgs(CanonicalArgs).Instance(Instance).Preview(Preview);
	const TSharedRef<SWindow> Window = SNew(SWindow)
	                                       .Title("Material Instance: " + Args.Name)
	                                       .ClientSize(FVector2(1150, 760))
	                                       .OnRequestClose([Panel]
	{
		return !Panel->IsDirty() ||
		    FServices::GetMessageDialog().Show(EMessageType::Question, EMessageButtons::YesNo,
		        "Discard unsaved material instance changes?", "Material Instance Editor") == EMessageResult::Yes;
	})[Panel];
	FSlateApplication::Get().AddWindow(Window);
	Error.Empty();
	return Window.ToSharedPtr();
}
