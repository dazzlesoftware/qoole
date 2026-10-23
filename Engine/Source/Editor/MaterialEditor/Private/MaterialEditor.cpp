// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "MaterialEditor.h"
#include "Containers/ContainerAliases.h"
#include "Templates/SharedPointer.h"
#include "Internationalization/TextLocalization.h"
#include "MaterialParameterCollectionEditor.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialFunction.h"
#include "MaterialInstanceEditor.h"
#include "MaterialCompilationManager.h"
#include "SMaterialGraph.h"
#include "MaterialEditorViewportClient.h"
#include "Geometry/StaticMesh.h"
#include "RenderTargetViewportClient.h"
#include "SlateRHIRenderer.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/Layout/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Text/SEditableText.h"
#include "Services/Services.h"
#include "Services/MessageDialog.h"
#include <map>

namespace
{
	class SMaterialEditorPanel final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SMaterialEditorPanel)
		SLATE_ARGUMENT(std::shared_ptr<FMaterialAsset>, Material, nullptr)
		SLATE_ARGUMENT(std::shared_ptr<FAssetRegistry>, Registry, nullptr)
		SLATE_ARGUMENT(TSharedPtr<FMaterialEditorViewportClient>, Preview, nullptr)
		SLATE_ARGUMENT(FString, OwnerId, "")
		SLATE_END_ARGS()
		void Construct(const FArguments& A)
		{
			Material = A._Material;
			Registry = A._Registry;
			OwnerId = A._OwnerId;
			Preview = A._Preview;
			const bool WasGraph = Material->Graph.has_value();
			Material->Graph = FMaterialEditingLibrary::CreateGraph(*Material);
			bDirty = !WasGraph;
			const TSharedRef<SVerticalBox> Left = SNew(SVerticalBox);
			if (Preview)
			{
				Left->AddSlot().FillHeight(.5f)[SNew(SViewport).ViewportClient(Preview)];
				Left->AddSlot().AutoHeight()[MakeMaterialPreviewControls(
				    ToSharedRef(Preview), Registry, [this] { RequestCompile(); })];
			}
			Left->AddSlot().AutoHeight()[SNew(STextBlock).Text(GENESIS_TEXT("Selected Node"))];
			Left->AddSlot().FillHeight(.5f)[SNew(SScrollBox) + SScrollBox::Slot()[SAssignNew(Details, SVerticalBox)]];
			const TSharedRef<SHorizontalBox> Toolbar = SNew(SHorizontalBox) +
			    SHorizontalBox::Slot().AutoWidth()[SNew(SButton)
			            .Text(GENESIS_TEXT("Compile / Preview"))
			            .OnClicked(
			                [this]
			{
				RequestCompile();
				return FReply::Handled();
			})] +
			    SHorizontalBox::Slot().AutoWidth()[SNew(SButton)
			            .Text(GENESIS_TEXT("Undo"))
			            .OnClicked(
			                [this]
			{
				Canvas->Undo();
				return FReply::Handled();
			})] +
			    SHorizontalBox::Slot().AutoWidth()[SNew(SButton)
			            .Text(GENESIS_TEXT("Redo"))
			            .OnClicked(
			                [this]
			{
				Canvas->Redo();
				return FReply::Handled();
			})] +
			    SHorizontalBox::Slot().AutoWidth()[SNew(SButton)
			            .Text(GENESIS_TEXT("Frame"))
			            .OnClicked([this]
			{
				Canvas->FrameAll();
				return FReply::Handled();
			})];
			Toolbar->AddSlot().AutoWidth()[SNew(SButton)
			        .Text([this]
			{ return bNodePreview ? FString("Preview: Selected Node") : FString("Preview: Material"); })
			        .IsEnabled(
			            [this]
			{
				const FMaterialNode* N = Material->Graph->Find(Canvas ? Canvas->GetSelectedNode() : 0);
				return !PendingSave && N && N->Expression != EMaterialExpression::Comment;
			})
			        .OnClicked([this]
			{
				bNodePreview = !bNodePreview;
				++Revision;
				CompiledRevision = 0;
				RequestCompile();
				return FReply::Handled();
			})];
			Toolbar->AddSlot().AutoWidth()[SNew(SButton)
			        .Text([this] { return bLive ? FString("Live Preview: On") : FString("Live Preview: Off"); })
			        .OnClicked([this]
			{
				bLive = !bLive;
				if (bLive && CompiledRevision != Revision)
					RequestCompile();
				else if (!bLive && !PendingSave)
					CompileAt = 0;
				return FReply::Handled();
			})];
			ChildSlot[SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight()[Toolbar] +
			    SVerticalBox::Slot().FillHeight(1)[SNew(SSplitter) + SSplitter::Slot().Value(.24f).MinSize(240)[Left] +
			        SSplitter::Slot().Value(.76f)[SAssignNew(Canvas, SMaterialGraph)
			                .Material(Material)
			                .Registry(Registry)
			                .OnChanged([this] { Edited(); })
			                .OnError([this](const FString& E) { Status = E; })
			                .OnSelectionChanged(
			                    [this]
			{
				RebuildDetails();
				if (bNodePreview)
				{
					const FMaterialNode* N = Material->Graph->Find(Canvas->GetSelectedNode());
					if (!N || N->Expression == EMaterialExpression::Comment)
						bNodePreview = false;
					++Revision;
					CompiledRevision = 0;
					RequestCompile();
				}
			})]] +
			    SVerticalBox::Slot().AutoHeight()[SNew(STextBlock)
			            .Text(GENESIS_TEXT("Right-click: add node | Drag output pin to input | Alt-click input: "
			                               "disconnect | Middle drag: pan | Wheel: zoom | Ctrl+click: multi-select | "
			                               "Ctrl+C/V/D | Delete | Ctrl+Z/Y"))
			            .AutoWrapText(true)] +
			    SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text([this] { return Status; }).AutoWrapText(true)]];
			RebuildDetails();
			RequestCompile();
		}
		void SetBlendMode(EMaterialBlendMode Mode)
		{
			Canvas->SetMaterialSettings(Mode, Material->OpacityMaskClip);
		}
		void SetMaskCutoff(float V)
		{
			Canvas->SetMaterialSettings(Material->BlendMode, V);
		}
		void ToggleMaterialAttributes()
		{
			Canvas->SetUseMaterialAttributes(!Material->Graph->bUseMaterialAttributes);
		}
		void RequestCompile()
		{
			bool Changed = false;
			if (!FMaterialFunctionCodec::Refresh(*Material->Graph, *Registry, Changed, Status, OwnerId))
			{
				CompileAt = 0;
				PendingSave = {};
				CompiledRevision = 0;
				++Revision;
				Status += GENESIS_TEXT(" - preview remains at the last successful compile");
				return;
			}
			RegistryRevision = Registry->GetRevision();
			if (Changed)
			{
				++Revision;
				bDirty = true;
			}
			CompileAt = 0;
			if (CompiledRevision == Revision)
			{
				PublishPreview();
				return;
			}
			SubmitCompile();
			Status = GENESIS_TEXT("Compiling in background...");
			EnsureCompileTimer();
		}
		void Tick(const FGeometry& G, double T, float D) override
		{
			SCompoundWidget::Tick(G, T, D);
			if (Registry && RegistryRevision != Registry->GetRevision())
			{
				RegistryRevision = Registry->GetRevision();
				bool Changed = false;
				FString DependencyError;
				if (!FMaterialFunctionCodec::Refresh(*Material->Graph, *Registry, Changed, DependencyError, OwnerId))
				{
					CompileAt = 0;
					PendingSave = {};
					CompiledRevision = 0;
					++Revision;
					Status = DependencyError + " - preview remains at the last successful compile";
					return;
				}
				if (Changed)
					Edited();
				if (bLive && (Changed || CompiledRevision != Revision))
					RequestCompile();
			}
		}
		void SubmitCompile()
		{
			FMaterialAsset Snapshot = *Material;
			if (bNodePreview)
			{
				for (uint32& Output : Snapshot.Graph->Outputs)
					Output = 0;
				Snapshot.Graph->Outputs[0] = Canvas->GetSelectedNode();
				Snapshot.Graph->bFunction = true;
				Snapshot.Graph->bUseMaterialAttributes = false;
			}
			Compiler.Submit(Snapshot, Revision);
		}
		void SaveWhenReady(TFunction<void()> Save)
		{
			PendingSave = std::move(Save);
			if (bNodePreview)
			{
				bNodePreview = false;
				++Revision;
				CompiledRevision = 0;
			}
			if (CompiledRevision == Revision)
			{
				if (PublishPreview())
				{
					TFunction<void()> Callback = std::move(PendingSave);
					PendingSave = {};
					Callback();
				}
				else
					PendingSave = {};
				return;
			}
			RequestCompile();
		}
		void Edited()
		{
			bDirty = true;
			++Revision;
			Status = GENESIS_TEXT("Edited - preview is showing the last successful compile");
			if (bLive || PendingSave)
			{
				CompileAt = FPlatformTime::Seconds() + .25;
				EnsureCompileTimer();
			}
		}
		bool PublishPreview()
		{
			const FMaterialAsset& Display = bNodePreview && CompiledPreview ? *CompiledPreview : *Material;
			FLoadedMaterialSlot Resources;
			if (!FRenderAssetLoader(*Registry).LoadMaterialResources(Display, Resources, Status))
				return false;
			if (Preview && !Preview->Update(Display, *Registry, Status))
				return false;
			Status =
			    bNodePreview ? "Node preview compiled for DirectX 12 and Vulkan" : "Compiled for DirectX 12 and Vulkan";
			return true;
		}
		void EnsureCompileTimer()
		{
			if (bCompileTimer)
				return;
			bCompileTimer = true;
			RegisterActiveTimer(.03f, [this](double, float)
			{
				if (TOptional<FMaterialCompilationResult> Result = Compiler.TakeCompleted())
					if (Result->Revision == Revision)
					{
						if (Result->bSuccess)
						{
							if (bNodePreview)
								CompiledPreview = std::move(Result->Material);
							else
							{
								Material->GraphDXIL = std::move(Result->Material.GraphDXIL);
								Material->GraphSPIRV = std::move(Result->Material.GraphSPIRV);
								Material->CookedGraphSource = std::move(Result->Material.CookedGraphSource);
							}
							if (PublishPreview())
							{
								CompiledRevision = Revision;
								if (PendingSave)
								{
									TFunction<void()> Save = std::move(PendingSave);
									PendingSave = {};
									Save();
								}
							}
							else
								PendingSave = {};
						}
						else
						{
							Status = Result->Error + " - preview remains at the last successful compile";
							PendingSave = {};
						}
					}
				if (CompileAt > 0 && FPlatformTime::Seconds() >= CompileAt)
				{
					CompileAt = 0;
					SubmitCompile();
					Status = GENESIS_TEXT("Compiling in background...");
				}
				if (Compiler.IsBusy() || CompileAt > 0)
					return EActiveTimerReturn::Continue;
				bCompileTimer = false;
				return EActiveTimerReturn::Stop;
			});
		}
		void Saved()
		{
			bDirty = false;
			Status = GENESIS_TEXT("Saved");
		}
		bool IsDirty() const
		{
			return bDirty;
		}

	private:
		void RebuildDetails()
		{
			Details->ClearChildren();
			const FMaterialNode* N = Material->Graph->Find(Canvas->GetSelectedNode());
			if (!N)
			{
				Details->AddSlot().AutoHeight()[SNew(STextBlock)
				        .Text(GENESIS_TEXT("Select a node to edit its properties. Material "
				                           "outputs use linear values; normal is tangent space."))
				        .AutoWrapText(true)];
				return;
			}
			const uint32 Id = N->Id;
			Details->AddSlot().AutoHeight()[SNew(STextBlock).Text(MaterialExpressionName(N->Expression))];
			Details->AddSlot().AutoHeight()[SNew(STextBlock).Text(GENESIS_TEXT("Description / Comment"))];
			Details->AddSlot()
			    .AutoHeight()[SNew(SEditableTextBox).Text(N->Description).OnTextChanged([this](const FString& Text) {
				Canvas->EditNode([&](FMaterialNode& Node) { Node.Description = Text; });
			})];

			if (N->Expression == EMaterialExpression::ScalarParameter ||
			    N->Expression == EMaterialExpression::VectorParameter ||
			    N->Expression == EMaterialExpression::TextureSampleParameter2D ||
			    N->Expression == EMaterialExpression::StaticSwitchParameter ||
			    N->Expression == EMaterialExpression::FunctionInput)
			{
				Details->AddSlot().AutoHeight()[SNew(STextBlock).Text(GENESIS_TEXT("Parameter Name"))];
				Details->AddSlot().AutoHeight()
				    [SNew(SEditableTextBox).Text(N->ParameterName).OnTextChanged([this](const FString& Text) {
					Canvas->EditNode([&](FMaterialNode& Node) { Node.ParameterName = Text; });
				})];
			}
			if (N->Expression == EMaterialExpression::StaticSwitchParameter)
			{
				Details->AddSlot().AutoHeight()[SNew(SButton)
				        .Text(
				            [this, Id]
				{
					const FMaterialNode* V = Material->Graph->Find(Id);
					return V && V->Value[2] != 0 ? FString("Static Value: True") : FString("Static Value: False");
				})
				        .OnClicked([this]
				{
					Canvas->EditNode([](FMaterialNode& Node) { Node.Value[2] = Node.Value[2] == 0 ? 1.f : 0.f; });
					return FReply::Handled();
				})];
			}
			if (N->Expression == EMaterialExpression::QualitySwitch)
			{
				Details->AddSlot()
				    .AutoHeight()[SNew(STextBlock).Text(GENESIS_TEXT("Cook quality (0 Low, 1 Medium, 2 High)"))];
				Details->AddSlot().AutoHeight()[SNew(SSpinBox<int>)
				        .MinValue(0)
				        .MaxValue(2)
				        .Value([this] {
					return static_cast<int>(Material->Graph->QualityLevel);
				}).OnValueChanged([this](int V) { Canvas->SetQualityLevel(static_cast<uint8>(V)); })];
			}
			if (N->Expression == EMaterialExpression::ScalarParameter ||
			    N->Expression == EMaterialExpression::VectorParameter ||
			    N->Expression == EMaterialExpression::TextureSampleParameter2D ||
			    N->Expression == EMaterialExpression::StaticSwitchParameter)
			{
				Details->AddSlot()
				    .AutoHeight()[SNew(STextBlock).Text(GENESIS_TEXT("Parameter Group (empty = General)"))];
				Details->AddSlot().AutoHeight()
				    [SNew(SEditableTextBox).Text(N->ParameterGroup).OnTextChanged([this](const FString& S) {
					Canvas->EditNode([&](FMaterialNode& Node) { Node.ParameterGroup = S; });
				})];
			}
			if (N->Expression == EMaterialExpression::BreakMaterialAttributes)
			{
				Details->AddSlot().AutoHeight()[SNew(STextBlock)
				        .Text(GENESIS_TEXT("Attribute: 0 Base Color, 1 Roughness, 2 Metallic, 3 "
				                           "Opacity, 4 Normal, 5 Emissive"))];
				Details->AddSlot().AutoHeight()[SNew(SSpinBox<int>)
				        .MinValue(0)
				        .MaxValue(5)
				        .Value([this, Id]
				{
					const FMaterialNode* V = Material->Graph->Find(Id);
					return V ? int(V->Mask) : 0;
				}).OnValueChanged([this](int V) {
					Canvas->EditNode([&](FMaterialNode& Node) { Node.Mask = static_cast<uint8>(V); });
				})];
			}
			const int Values = N->Expression == EMaterialExpression::MaterialFunctionCall ||
			        N->Expression == EMaterialExpression::MakeMaterialAttributes ||
			        N->Expression == EMaterialExpression::BreakMaterialAttributes
			    ? 0
			    : N->Expression == EMaterialExpression::FunctionInput   ? (N->Mask == 8 ? 0 : N->Mask)
			    : N->Expression == EMaterialExpression::VectorParameter ? 4
			    : N->Expression == EMaterialExpression::ScalarParameter ? 1
			    : N->Expression == EMaterialExpression::Comment         ? 2
			    : static_cast<int>(N->Expression) <= 3                  ? static_cast<int>(N->Expression) + 1
			                                                            : MaterialInputCount(N->Expression);
			for (int I = 0; I < Values; ++I)
			{
				if (N->Expression == EMaterialExpression::BlendMaterialAttributes && I < 2)
					continue;
				Details->AddSlot().AutoHeight()[SNew(STextBlock)
				        .Text(FString::Format(
				            "{} {}", static_cast<int>(N->Expression) <= 3 ? "Value" : "Default", I + 1))];
				Details->AddSlot().AutoHeight()[SNew(SSpinBox<float>)
				        .MinValue(-10000.f)
				        .MaxValue(10000.f)
				        .Delta(.01f)
				        .Value([this, Id, I]
				{
					const FMaterialNode* V = Material->Graph->Find(Id);
					return V ? V->Value[I] : 0.f;
				}).OnValueChanged([this, I](float V) {
					Canvas->EditNode([&](FMaterialNode& Node) { Node.Value[I] = V; });
				})];
			}
			if (N->Expression == EMaterialExpression::FunctionInput)
			{
				Details->AddSlot()
				    .AutoHeight()[SNew(STextBlock).Text(GENESIS_TEXT("Function input index (0..7) and type"))];
				Details->AddSlot().AutoHeight()[SNew(SSpinBox<int>)
				        .MinValue(0)
				        .MaxValue(7)
				        .Value([this, Id]
				{
					FMaterialNode* V = Material->Graph->Find(Id);
					return V ? int(V->InputIndex) : 0;
				}).OnValueChanged([this](int V) {
					Canvas->EditNode([&](FMaterialNode& Node) { Node.InputIndex = static_cast<uint8>(V); });
				})];
				Details->AddSlot().AutoHeight()[SNew(SComboButton)
				        .Text(N->Mask == 8 ? "Type: Material Attributes" : FString::Format("Type: float{}", N->Mask))
				        .OnGetMenuContent([this]
				{
					const TSharedRef<SVerticalBox> Choices = SNew(SVerticalBox);
					for (int Width : {1, 2, 3, 4, 8})
						Choices->AddSlot().AutoHeight()[SNew(SMenuEntry)
						        .Text(Width == 8 ? FString("Material Attributes") : FString::Format("float{}", Width))
						        .OnSelected([this, Width]
						{
							Canvas->EditNode([&](FMaterialNode& Node) { Node.Mask = static_cast<uint8>(Width); });
							RebuildDetails();
						})];
					return Choices;
				})];
			}
			if (N->Expression == EMaterialExpression::ScalarCollectionParameter ||
			    N->Expression == EMaterialExpression::VectorCollectionParameter)
			{
				const int Width = N->Expression == EMaterialExpression::ScalarCollectionParameter ? 1 : 4;
				const TSharedRef<SComboButton> Menu = SNew(SComboButton)
				                                          .Text(GENESIS_TEXT("Choose Collection Parameter"))
				                                          .OnGetMenuContent([this, Width]
				{
					const TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
					for (const FAssetData& Asset : Registry->GetAssets())
						if (Asset.Metadata.Schema == "Genesis.MaterialParameterCollection")
						{
							FAssetPackage P;
							FMaterialParameterCollection C;
							FString E;
							if (!Registry->Load(Asset.GetReference(), P, E) ||
							    !FMaterialParameterCollectionCodec::Decode(P, C, E))
								continue;
							for (const FMaterialCollectionParameter& Parameter : C.Parameters)
								if (Parameter.Width == Width)
								{
									const FAssetReference Ref = Asset.GetReference();
									const FString Name = Parameter.Name;
									List->AddSlot().AutoHeight()[SNew(SMenuEntry)
									        .Text(Asset.PackagePath + " / " + Name)
									        .OnSelected([this, Ref, Name]
									{
										Canvas->EditNode([&](FMaterialNode& Node)
										{
											Node.Collection = Ref;
											Node.ParameterName = Name;
										});
										RequestCompile();
										RebuildDetails();
									})];
								}
						}
					return SNew(SScrollBox) + SScrollBox::Slot()[List];
				});
				Details->AddSlot().AutoHeight()[Menu];
				Details->AddSlot().AutoHeight()[SNew(STextBlock).Text(N->Collection.Path + " / " + N->ParameterName)];
			}
			if (N->Expression == EMaterialExpression::MaterialFunctionCall)
			{
				const TSharedRef<SComboButton> Menu = SNew(SComboButton)
				                                          .Text(GENESIS_TEXT("Choose Material Function"))
				                                          .OnGetMenuContent([this]
				{
					const TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
					for (const FAssetData& Asset : Registry->GetAssets())
						if (Asset.Metadata.Schema == "Genesis.MaterialFunction")
						{
							const FAssetReference Ref = Asset.GetReference();
							List->AddSlot().AutoHeight()[SNew(SMenuEntry)
							        .Text(Asset.PackagePath)
							        .OnSelected([this, Ref]
							{
								Canvas->EditNode([&](FMaterialNode& Node) { Node.Function = Ref; });
								RequestCompile();
								RebuildDetails();
							})];
						}
					return SNew(SScrollBox) + SScrollBox::Slot()[List];
				});
				Details->AddSlot().AutoHeight()[Menu];
				Details->AddSlot().AutoHeight()[SNew(STextBlock).Text(N->Function.Path)];
			}
			if (N->Expression == EMaterialExpression::ComponentMask)
			{
				Details->AddSlot()
				    .AutoHeight()[SNew(STextBlock).Text(GENESIS_TEXT("Channels (R=1 G=2 B=4 A=8, RGB=7)"))];
				Details->AddSlot().AutoHeight()[SNew(SSpinBox<int>)
				        .MinValue(1)
				        .MaxValue(15)
				        .Value([this, Id]
				{
					const FMaterialNode* V = Material->Graph->Find(Id);
					return V ? int(V->Mask) : 7;
				}).OnValueChanged([this](int V) {
					Canvas->EditNode([&](FMaterialNode& Node) { Node.Mask = static_cast<uint8>(V); });
				})];
			}
			if (N->Expression == EMaterialExpression::TextureSample ||
			    N->Expression == EMaterialExpression::TextureSampleParameter2D)
			{
				const TSharedRef<SComboButton> Menu = SNew(SComboButton)
				                                          .Text(GENESIS_TEXT("Choose Texture"))
				                                          .OnGetMenuContent([this]
				{
					const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
					for (const FAssetData& T : Registry->GetAssets())
						if (T.Metadata.Schema == "Genesis.Texture2D")
							Box->AddSlot().AutoHeight()[SNew(SMenuEntry)
							        .Text(T.PackagePath)
							        .OnSelected([this, Ref = T.GetReference()]
							{
								Canvas->EditNode([&](FMaterialNode& Node) { Node.Texture = Ref; });
								RebuildDetails();
							})];
					return SNew(SScrollBox) + SScrollBox::Slot()[Box];
				});
				Details->AddSlot().AutoHeight()[Menu];
				Details->AddSlot().AutoHeight()[SNew(STextBlock).Text(N->Texture.Path).AutoWrapText(true)];
				Details->AddSlot().AutoHeight()[SNew(SButton)
				        .Text(N->bNormalTexture ? "Sampling: Normal (linear)" : "Sampling: Color / Data")
				        .OnClicked([this]
				{
					Canvas->EditNode([](FMaterialNode& Node) { Node.bNormalTexture = !Node.bNormalTexture; });
					RebuildDetails();
					return FReply::Handled();
				})];
			}
		}
		std::shared_ptr<FMaterialAsset> Material;
		std::shared_ptr<FAssetRegistry> Registry;
		TSharedPtr<FMaterialEditorViewportClient> Preview;
		TSharedPtr<SMaterialGraph> Canvas;
		TSharedPtr<SVerticalBox> Details;
		FString Status, OwnerId;
		uint64 RegistryRevision = 0;
		FMaterialCompilationManager Compiler;
		uint64 Revision = 1, CompiledRevision = 0;
		double CompileAt = 0;
		bool bCompileTimer = false, bLive = true;
		TFunction<void()> PendingSave;
		bool bDirty = false;
		bool bNodePreview = false;
		TOptional<FMaterialAsset> CompiledPreview;
	};
} // namespace
TSharedPtr<SWindow> FMaterialEditor::Open(const FMaterialEditorArgs& Args, FString& Error)
{
	if (!Args.Registry)
	{
		Error = GENESIS_TEXT("Material editor requires an asset registry.");
		return {};
	}
	FAssetPackage Package;
	if (!Args.Registry->Load({Args.Material.Id, Args.Material.Path}, Package, Error))
	{
		return {};
	}
	using FEditorKey = std::pair<const FAssetRegistry*, FString>;
	static std::map<FEditorKey, TWeakPtr<SWindow>> OpenEditors;
	for (std::map<FEditorKey, TWeakPtr<SWindow>>::iterator It = OpenEditors.begin(); It != OpenEditors.end();)
	{
		TSharedPtr<SWindow> Window = It->second.lock();
		if (!Window || Window->IsClosed())
			It = OpenEditors.erase(It);
		else
			++It;
	}
	const FEditorKey Key{Args.Registry.get(), Package.Id};
	if (std::map<FEditorKey, TWeakPtr<SWindow>>::iterator It = OpenEditors.find(Key); It != OpenEditors.end())
		if (TSharedPtr<SWindow> Window = It->second.lock())
		{
			if (Window->GetNativeWindow())
				Window->GetNativeWindow()->BringToFront();
			Error.Empty();
			return Window;
		}
	if (Package.Schema == "Genesis.MaterialParameterCollection" || Package.Schema == "Genesis.MaterialInstance")
	{
		TSharedPtr<SWindow> Window = Package.Schema == "Genesis.MaterialInstance"
		    ? FMaterialInstanceEditor::Open(Args, Error)
		    : FMaterialParameterCollectionEditor::Open(Args, Error);
		if (Window)
			OpenEditors[Key] = Window;
		return Window;
	}
	TSharedPtr<FMaterialAsset> Material = std::make_shared<FMaterialAsset>();
	if (Package.Schema == "Genesis.MaterialFunction")
	{
		FMaterialGraph Graph;
		if (!FMaterialFunctionCodec::Decode(Package, Graph, Error))
			return {};
		Material->Graph = std::move(Graph);
	}
	else if (!FRenderAssetCodec::DecodeMaterial(Package, *Material, Error))
	{
		return {};
	}
	TSharedPtr<FMaterialEditorViewportClient> Preview;
	if (Args.Device && Args.Renderer && Args.WindowRenderer)
	{
		Preview = MakeShared<FMaterialEditorViewportClient>(*Args.Device, *Args.Renderer);
		Args.WindowRenderer->AddViewportClient(ToSharedRef(Preview));
	}
	const TSharedRef<SMaterialEditorPanel> Panel =
	    SNew(SMaterialEditorPanel).Material(Material).Registry(Args.Registry).Preview(Preview).OwnerId(Package.Id);
	TSharedPtr<int> ApplySlot = std::make_shared<int32>(0);
	const TSharedRef<SHorizontalBox> Settings = SNew(SHorizontalBox);
	if (!Material->Graph->bFunction)
	{
		Settings->AddSlot().AutoWidth()[SNew(SButton)
		        .Text(
		            [Material]
		{
			return Material->Graph->bUseMaterialAttributes ? FString("Outputs: Attributes")
			                                               : FString("Outputs: Individual");
		})
		        .OnClicked([Panel]
		{
			Panel->ToggleMaterialAttributes();
			return FReply::Handled();
		})];
		TSharedPtr<std::vector<FString, std::allocator<FString>>> BlendModes =
		    std::make_shared<TArray<FString>>(TArray<FString>{"Opaque", "Masked", "Translucent"});
		Settings->AddSlot().AutoWidth()[SNew(SComboBox<FString>)
		        .OptionsSource(BlendModes.get())
		        .OnGenerateWidget([](const FString& S) -> TSharedRef<SWidget> { return SNew(STextBlock).Text(S); })
		        .OnSelectionChanged([Material, BlendModes, Panel](const FString& S, ESelectInfo)
		{
			for (int I = 0; I < 3; ++I)
				if ((*BlendModes)[I] == S)
					Panel->SetBlendMode(static_cast<EMaterialBlendMode>(I));
		})[SNew(STextBlock).Text([Material, BlendModes] {
			return (*BlendModes)[static_cast<int>(Material->BlendMode)];
		})]];
		Settings->AddSlot().AutoWidth()[SNew(STextBlock).Text(GENESIS_TEXT("Mask cutoff"))];
		Settings->AddSlot().AutoWidth()[SNew(SSpinBox<float>)
		        .MinValue(0.f)
		        .MaxValue(1.f)
		        .Delta(.01f)
		        .Value([Material] {
			return Material->OpacityMaskClip;
		}).OnValueChanged([Material, Panel](float V) { Panel->SetMaskCutoff(V); })];
		Settings->AddSlot().AutoWidth()[SNew(STextBlock).Text(GENESIS_TEXT("Apply slot"))];
		Settings->AddSlot()
		    .AutoWidth()[SNew(SSpinBox<int32>).MinValue(0).MaxValue(63).Value([ApplySlot] {
			return *ApplySlot;
		}).OnValueChanged([ApplySlot](int V) { *ApplySlot = V; })];
	}
	const TWeakPtr<SMaterialEditorPanel> WeakPanel = Panel.ToSharedPtr();
	TFunction<FReply(bool)> Save = [Args, Material, WeakPanel, ApplySlot](bool Apply)
	{
		if (TSharedPtr<SMaterialEditorPanel> Panel = WeakPanel.lock())
			Panel->SaveWhenReady([Args, Material, WeakPanel, ApplySlot, Apply]
			{
				FString E;
				FAssetPackage P;
				const FAssetData* Current = Args.Registry->Resolve({Args.Material.Id, Args.Material.Path}, E,
				    Material->Graph->bFunction ? "Genesis.MaterialFunction" : "Genesis.Material");
				if (!Current)
				{
					Args.OnLog.ExecuteIfBound(E);
					return;
				}
				const FString File = Current->FilePath;
				if (!Args.Registry->Load({Args.Material.Id, Args.Material.Path}, P, E,
				        Material->Graph->bFunction ? "Genesis.MaterialFunction" : "Genesis.Material") ||
				    !(Material->Graph->bFunction ? FMaterialFunctionCodec::Encode(*Material->Graph, P, E)
				                                 : FRenderAssetCodec::EncodeMaterial(*Material, P, E)) ||
				    !FAssetPackageStore::Save(File, P, E))
				{
					Args.OnLog.ExecuteIfBound(E);
					return;
				}
				Args.Registry->Scan();
				{
					int32 Updated = 0;
					FString DependencyError;
					if (!FMaterialEditingLibrary::RecompileDependents(
					        *Args.Registry, Args.Material, Updated, DependencyError))
						Args.OnLog.ExecuteIfBound("Saved; dependent recook failures: " + DependencyError);
					else
						Args.OnLog.ExecuteIfBound(
						    FTextLocalization::Format("Updated {} dependent material assets.", Updated));
				}
				Args.OnSaved.ExecuteIfBound();
				if (TSharedPtr<SMaterialEditorPanel> Panel = WeakPanel.lock())
					Panel->Saved();
				if (Apply)
					Args.OnApply.ExecuteIfBound(*ApplySlot);
				Args.OnLog.ExecuteIfBound("Saved material " + Args.Name);
			});
		return FReply::Handled();
	};
	Settings->AddSlot().AutoWidth()[SNew(SButton).Text(GENESIS_TEXT("Save")).OnClicked([Save] { return Save(false); })];
	Settings->AddSlot().AutoWidth()[SNew(SButton)
	        .Text(GENESIS_TEXT("Save and Apply to Selection"))
	        .IsEnabled(Args.OnApply.IsBound() && !Material->Graph->bFunction)
	        .OnClicked([Save] { return Save(true); })];
	const TSharedRef<SWindow> Window =
	    SNew(SWindow)
	        .Title((Material->Graph->bFunction ? FString("Material Function: ") : FString("Material: ")) + Args.Name)
	        .ClientSize(FVector2(1380, 860))
	        .OnRequestClose([Panel]
	{
		return !Panel->IsDirty() ||
		    FServices::GetMessageDialog().Show(EMessageType::Question, EMessageButtons::YesNo,
		        "Discard unsaved material changes?", "Material Editor") == EMessageResult::Yes;
	})[SNew(SVerticalBox) + SVerticalBox::Slot().FillHeight(1)[Panel] + SVerticalBox::Slot().AutoHeight()[Settings]];
	FSlateApplication::Get().AddWindow(Window);
	Error.Empty();
	OpenEditors[Key] = Window.ToSharedPtr();
	return Window.ToSharedPtr();
}
