// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "MaterialEditor.h"
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

namespace
{
class SMaterialEditorPanel final : public SCompoundWidget
{
  public:
	SLATE_BEGIN_ARGS(SMaterialEditorPanel)
	SLATE_ARGUMENT(std::shared_ptr<FMaterialAsset>, Material, nullptr)
	SLATE_ARGUMENT(std::shared_ptr<FAssetRegistry>, Registry, nullptr)
	SLATE_ARGUMENT(TSharedPtr<FMaterialEditorViewportClient>, Preview, nullptr)
	SLATE_END_ARGS()
	void Construct(const FArguments& A)
	{
		Material = A._Material;
		Registry = A._Registry;
		Preview = A._Preview;
		const bool WasGraph = Material->Graph.has_value();
		Material->Graph = FMaterialEditingLibrary::CreateGraph(*Material);
		bDirty = !WasGraph;
		const auto Left = SNew(SVerticalBox);
		if (Preview)
		{
			Left->AddSlot().FillHeight(.5f)[SNew(SViewport).ViewportClient(Preview)];
			Left->AddSlot().AutoHeight()
				[SNew(SHorizontalBox) +
				 SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text("Sphere").OnClicked([this] {
					 Preview->SetShape("Sphere");
					 RequestCompile();
					 return FReply::Handled();
				 })] +
				 SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text("Cube").OnClicked([this] {
					 Preview->SetShape("Cube");
					 RequestCompile();
					 return FReply::Handled();
				 })] +
				 SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text("Lit / Unlit").OnClicked([this] {
					 Preview->ToggleLit();
					 return FReply::Handled();
				 })]];
		}
		Left->AddSlot().AutoHeight()[SNew(STextBlock).Text("Selected Node")];
		Left->AddSlot().FillHeight(
			.5f)[SNew(SScrollBox) + SScrollBox::Slot()[SAssignNew(Details, SVerticalBox)]];
		const auto Toolbar =
			SNew(SHorizontalBox) +
			SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text("Compile / Preview").OnClicked([this] {
				RequestCompile();
				return FReply::Handled();
			})] +
			SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text("Undo").OnClicked([this] {
				Canvas->Undo();
				return FReply::Handled();
			})] +
			SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text("Redo").OnClicked([this] {
				Canvas->Redo();
				return FReply::Handled();
			})] +
			SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text("Frame").OnClicked([this] {
				Canvas->FrameAll();
				return FReply::Handled();
			})];
		Toolbar->AddSlot().AutoWidth()[SNew(SButton)
										   .Text([this] {
											   return bLive ? FString("Live Preview: On")
															: FString("Live Preview: Off");
										   })
										   .OnClicked([this] {
											   bLive = !bLive;
											   if (bLive && CompiledRevision != Revision)
												   RequestCompile();
											   else if (!bLive && !PendingSave)
												   CompileAt = 0;
											   return FReply::Handled();
										   })];
		ChildSlot[SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight()[Toolbar] +
				  SVerticalBox::Slot().FillHeight(1)
					  [SNew(SSplitter) + SSplitter::Slot().Value(.24f).MinSize(240)[Left] +
					   SSplitter::Slot().Value(.76f)[SAssignNew(Canvas, SMaterialGraph)
														 .Material(Material)
														 .OnChanged([this] { Edited(); })
														 .OnError([this](const FString& E) { Status = E; })
														 .OnSelectionChanged([this] { RebuildDetails(); })]] +
				  SVerticalBox::Slot().AutoHeight()
					  [SNew(STextBlock)
						   .Text("Right-click: add node | Drag output pin to input | Alt-click input: "
								 "disconnect | Middle drag: pan | Wheel: zoom | Ctrl+click: multi-select | "
								 "Ctrl+C/V/D | Delete | Ctrl+Z/Y")
						   .AutoWrapText(true)] +
				  SVerticalBox::Slot()
					  .AutoHeight()[SNew(STextBlock).Text([this] { return Status; }).AutoWrapText(true)]];
		RebuildDetails();
		RequestCompile();
	}
	void RequestCompile()
	{
		CompileAt = 0;
		if (CompiledRevision == Revision)
		{
			PublishPreview();
			return;
		}
		Compiler.Submit(*Material, Revision);
		Status = "Compiling in background...";
		EnsureCompileTimer();
	}
	void SaveWhenReady(TFunction<void()> Save)
	{
		PendingSave = std::move(Save);
		if (CompiledRevision == Revision)
		{
			if (PublishPreview())
			{
				auto Callback = std::move(PendingSave);
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
		Status = "Edited - preview is showing the last successful compile";
		if (bLive || PendingSave)
		{
			CompileAt = FPlatformTime::Seconds() + .25;
			EnsureCompileTimer();
		}
	}
	bool PublishPreview()
	{
		FLoadedMaterialSlot Resources;
		if (!FRenderAssetLoader(*Registry).LoadMaterialResources(*Material, Resources, Status))
			return false;
		if (Preview && !Preview->Update(*Material, *Registry, Status))
			return false;
		Status = "Compiled for DirectX 12 and Vulkan";
		return true;
	}
	void EnsureCompileTimer()
	{
		if (bCompileTimer)
			return;
		bCompileTimer = true;
		RegisterActiveTimer(.03f, [this](double, float) {
			if (auto Result = Compiler.TakeCompleted())
				if (Result->Revision == Revision)
				{
					if (Result->bSuccess)
					{
						Material->GraphDXIL = std::move(Result->Material.GraphDXIL);
						Material->GraphSPIRV = std::move(Result->Material.GraphSPIRV);
						Material->CookedGraphSource = std::move(Result->Material.CookedGraphSource);
						if (PublishPreview())
						{
							CompiledRevision = Revision;
							if (PendingSave)
							{
								auto Save = std::move(PendingSave);
								PendingSave = {};
								Save();
							}
						}
						else
							PendingSave = {};
					}
					else
					{
						Status = Result->Error;
						PendingSave = {};
					}
				}
			if (CompileAt > 0 && FPlatformTime::Seconds() >= CompileAt)
			{
				CompileAt = 0;
				Compiler.Submit(*Material, Revision);
				Status = "Compiling in background...";
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
		Status = "Saved";
	}
	bool IsDirty() const
	{
		return bDirty;
	}

  private:
	void RebuildDetails()
	{
		Details->ClearChildren();
		const auto* N = Material->Graph->Find(Canvas->GetSelectedNode());
		if (!N)
		{
			Details->AddSlot().AutoHeight()[SNew(STextBlock)
												.Text("Select a node to edit its properties. Material "
													  "outputs use linear values; normal is tangent space.")
												.AutoWrapText(true)];
			return;
		}
		const auto Id = N->Id;
		Details->AddSlot().AutoHeight()[SNew(STextBlock).Text(MaterialExpressionName(N->Expression))];
		Details->AddSlot().AutoHeight()[SNew(STextBlock).Text("Description / Comment")];
		Details->AddSlot().AutoHeight()
			[SNew(SEditableTextBox).Text(N->Description).OnTextChanged([this](const FString& Text) {
				Canvas->EditNode([&](auto& Node) { Node.Description = Text; });
			})];

		if (N->Expression == EMaterialExpression::ScalarParameter ||
			N->Expression == EMaterialExpression::VectorParameter ||
			N->Expression == EMaterialExpression::TextureSampleParameter2D)
		{
			Details->AddSlot().AutoHeight()[SNew(STextBlock).Text("Parameter Name")];
			Details->AddSlot().AutoHeight()
				[SNew(SEditableTextBox).Text(N->ParameterName).OnTextChanged([this](const FString& Text) {
					Canvas->EditNode([&](auto& Node) { Node.ParameterName = Text; });
				})];
		}
		const int Values = N->Expression == EMaterialExpression::VectorParameter   ? 4
						   : N->Expression == EMaterialExpression::ScalarParameter ? 1
						   : N->Expression == EMaterialExpression::Comment		   ? 2
						   : static_cast<int>(N->Expression) <= 3 ? static_cast<int>(N->Expression) + 1
																  : MaterialInputCount(N->Expression);
		for (int I = 0; I < Values; ++I)
		{
			Details->AddSlot()
				.AutoHeight()[SNew(STextBlock)
								  .Text(FString::Format(
									  "{} {}", static_cast<int>(N->Expression) <= 3 ? "Value" : "Default",
									  I + 1))];
			Details->AddSlot().AutoHeight()[SNew(SSpinBox<float>)
												.MinValue(-10000.f)
												.MaxValue(10000.f)
												.Delta(.01f)
												.Value([this, Id, I] {
													const auto* V = Material->Graph->Find(Id);
													return V ? V->Value[I] : 0.f;
												})
												.OnValueChanged([this, I](float V) {
													Canvas->EditNode([&](auto& Node) { Node.Value[I] = V; });
												})];
		}
		if (N->Expression == EMaterialExpression::ComponentMask)
		{
			Details->AddSlot().AutoHeight()[SNew(STextBlock).Text("Channels (R=1 G=2 B=4 A=8, RGB=7)")];
			Details->AddSlot().AutoHeight()[SNew(SSpinBox<int>)
												.MinValue(1)
												.MaxValue(15)
												.Value([this, Id] {
													const auto* V = Material->Graph->Find(Id);
													return V ? int(V->Mask) : 7;
												})
												.OnValueChanged([this](int V) {
													Canvas->EditNode([&](auto& Node) {
														Node.Mask = static_cast<uint8>(V);
													});
												})];
		}
		if (N->Expression == EMaterialExpression::TextureSample ||
			N->Expression == EMaterialExpression::TextureSampleParameter2D)
		{
			const auto Menu = SNew(SComboButton).Text("Choose Texture").OnGetMenuContent([this] {
				const auto Box = SNew(SVerticalBox);
				for (const auto& T : Registry->GetAssets())
					if (T.Metadata.Schema == "Genesis.Texture2D")
						Box->AddSlot().AutoHeight()
							[SNew(SMenuEntry).Text(T.PackagePath).OnSelected([this, Ref = T.GetReference()] {
								Canvas->EditNode([&](auto& Node) { Node.Texture = Ref; });
								RebuildDetails();
							})];
				return SNew(SScrollBox) + SScrollBox::Slot()[Box];
			});
			Details->AddSlot().AutoHeight()[Menu];
			Details->AddSlot().AutoHeight()[SNew(STextBlock).Text(N->Texture.Path).AutoWrapText(true)];
			Details->AddSlot().AutoHeight()[SNew(SButton)
												.Text(N->bNormalTexture ? "Sampling: Normal (linear)"
																		: "Sampling: Color / Data")
												.OnClicked([this] {
													Canvas->EditNode([](auto& Node) {
														Node.bNormalTexture = !Node.bNormalTexture;
													});
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
	FString Status;
	FMaterialCompilationManager Compiler;
	uint64 Revision = 1, CompiledRevision = 0;
	double CompileAt = 0;
	bool bCompileTimer = false, bLive = true;
	TFunction<void()> PendingSave;
	bool bDirty = false;
};
} // namespace
TSharedPtr<SWindow> FMaterialEditor::Open(const FMaterialEditorArgs& Args, FString& Error)
{
	if (!Args.Registry)
	{
		Error = "Material editor requires an asset registry.";
		return {};
	}
	FAssetPackage Package;
	if (!Args.Registry->Load({Args.Material.Id, Args.Material.Path}, Package, Error))
	{
		return {};
	}
	if (Package.Schema == "Genesis.MaterialInstance")
		return FMaterialInstanceEditor::Open(Args, Error);
	auto Material = std::make_shared<FMaterialAsset>();
	if (!FRenderAssetCodec::DecodeMaterial(Package, *Material, Error))
	{
		return {};
	}
	TSharedPtr<FMaterialEditorViewportClient> Preview;
	if (Args.Device && Args.Renderer && Args.WindowRenderer)
	{
		Preview = MakeShared<FMaterialEditorViewportClient>(*Args.Device, *Args.Renderer);
		Args.WindowRenderer->AddViewportClient(ToSharedRef(Preview));
	}
	const auto Panel = SNew(SMaterialEditorPanel).Material(Material).Registry(Args.Registry).Preview(Preview);
	auto ApplySlot = std::make_shared<int32>(0);
	const auto Settings = SNew(SHorizontalBox);
	auto BlendModes = std::make_shared<TArray<FString>>(TArray<FString>{"Opaque", "Masked", "Translucent"});
	Settings->AddSlot()
		.AutoWidth()[SNew(SComboBox<FString>)
						 .OptionsSource(BlendModes.get())
						 .OnGenerateWidget(
							 [](const FString& S) -> TSharedRef<SWidget> { return SNew(STextBlock).Text(S); })
						 .OnSelectionChanged([Material, BlendModes, Panel](const FString& S, ESelectInfo) {
							 Panel->Edited();
							 for (int I = 0; I < 3; ++I)
								 if ((*BlendModes)[I] == S)
									 Material->BlendMode = static_cast<EMaterialBlendMode>(I);
						 })[SNew(STextBlock).Text([Material, BlendModes] {
							 return (*BlendModes)[static_cast<int>(Material->BlendMode)];
						 })]];
	Settings->AddSlot().AutoWidth()[SNew(STextBlock).Text("Mask cutoff")];
	Settings->AddSlot().AutoWidth()[SNew(SSpinBox<float>)
										.MinValue(0.f)
										.MaxValue(1.f)
										.Delta(.01f)
										.Value([Material] { return Material->OpacityMaskClip; })
										.OnValueChanged([Material, Panel](float V) {
											Material->OpacityMaskClip = V;
											Panel->Edited();
										})];
	Settings->AddSlot().AutoWidth()[SNew(STextBlock).Text("Apply slot")];
	Settings->AddSlot().AutoWidth()[SNew(SSpinBox<int32>)
										.MinValue(0)
										.MaxValue(63)
										.Value([ApplySlot] { return *ApplySlot; })
										.OnValueChanged([ApplySlot](int V) { *ApplySlot = V; })];
	const TWeakPtr<SMaterialEditorPanel> WeakPanel = Panel.ToSharedPtr();
	auto Save = [Args, Material, WeakPanel, ApplySlot](bool Apply) {
		if (auto Panel = WeakPanel.lock())
			Panel->SaveWhenReady([Args, Material, WeakPanel, ApplySlot, Apply] {
				FString E;
				FAssetPackage P;
				const auto* Current =
					Args.Registry->Resolve({Args.Material.Id, Args.Material.Path}, E, "Genesis.Material");
				if (!Current)
				{
					Args.OnLog.ExecuteIfBound(E);
					return;
				}
				const auto File = Current->FilePath;
				if (!Args.Registry->Load({Args.Material.Id, Args.Material.Path}, P, E, "Genesis.Material") ||
					!FRenderAssetCodec::EncodeMaterial(*Material, P, E) ||
					!FAssetPackageStore::Save(File, P, E))
				{
					Args.OnLog.ExecuteIfBound(E);
					return;
				}
				Args.Registry->Scan();
				Args.OnSaved.ExecuteIfBound();
				if (auto Panel = WeakPanel.lock())
					Panel->Saved();
				if (Apply)
					Args.OnApply.ExecuteIfBound(*ApplySlot);
				Args.OnLog.ExecuteIfBound("Saved material " + Args.Name);
			});
		return FReply::Handled();
	};
	Settings->AddSlot().AutoWidth()[SNew(SButton).Text("Save").OnClicked([Save] { return Save(false); })];
	Settings->AddSlot().AutoWidth()[SNew(SButton)
										.Text("Save and Apply to Selection")
										.IsEnabled(Args.OnApply.IsBound())
										.OnClicked([Save] { return Save(true); })];
	const auto Window =
		SNew(SWindow)
			.Title("Material: " + Args.Name)
			.ClientSize(FVector2(1380, 860))
			.OnRequestClose([Panel] {
				return !Panel->IsDirty() ||
					   FServices::GetMessageDialog().Show(EMessageType::Question, EMessageButtons::YesNo,
														  "Discard unsaved material changes?",
														  "Material Editor") == EMessageResult::Yes;
			})[SNew(SVerticalBox) + SVerticalBox::Slot().FillHeight(1)[Panel] +
			   SVerticalBox::Slot().AutoHeight()[Settings]];
	FSlateApplication::Get().AddWindow(Window);
	Error.Empty();
	return Window.ToSharedPtr();
}
