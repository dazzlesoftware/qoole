// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "GameViewportClient.h"
#include "Renderer/WorldSceneDrawList.h"
#include "Framework/Application/SlateApplication.h"
#include "Internationalization/TextLocalization.h"
#include "Widgets/Layout/SOverlay.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SViewport.h"
#include "Widgets/Text/STextBlock.h"
#include <cmath>

namespace
{
	constexpr float DegreesToRadians = 0.01745329252f;
	constexpr float MouseDegreesPerPixel = 0.15f;
	// Sunlight from the north-west, high in the sky.
	constexpr float SunX = -0.35f, SunY = 0.45f, SunZ = 0.82f;
	const FLinearColor SkyColor(0.46f, 0.62f, 0.84f, 1.0f);

	bool Held(const TSet<uint16>& Keys, EKey Key)
	{
		return Keys.contains(static_cast<uint16>(Key));
	}
	bool IsMovementKey(EKey Key)
	{
		switch (Key)
		{
		case EKey::W:
		case EKey::A:
		case EKey::S:
		case EKey::D:
		case EKey::Q:
		case EKey::E:
		case EKey::Up:
		case EKey::Down:
		case EKey::Left:
		case EKey::Right:
		case EKey::SpaceBar:
		case EKey::LeftShift:
		case EKey::RightShift:
			return true;
		default:
			return false;
		}
	}

	// The HUD's translucent backing, so its text reads over any terrain or sky.
	class SGameHudPanel final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SGameHudPanel)
		SLATE_DEFAULT_SLOT(Content)
		SLATE_END_ARGS()
		void Construct(const FArguments& InArgs)
		{
			if (InArgs._Content.Widget)
				ChildSlot.Padding(FMargin(10, 6))[ToSharedRef(InArgs._Content.Widget)];
		}

	protected:
		int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FRect& CullingRect,
		    FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style) const override
		{
			Out.AddSolidBox(Layer, Geometry.ToPaintGeometry(), FColor(12, 14, 18, 170));
			return SCompoundWidget::OnPaint(Args, Geometry, CullingRect, Out, Layer + 1, Style);
		}
	};
} // namespace

FGameViewportClient::FGameViewportClient(
    FRHIDevice& InDevice, FSlateRHIRenderingPolicy& InRenderer, FGameInstance& InGame)
    : FRenderTargetViewportClient(InDevice, InRenderer), Game(InGame), Scene(InDevice), Meshes(InDevice)
{
}

FGameViewportClient::~FGameViewportClient()
{
	if (Depth)
		Device.DeferRelease(Depth);
}

FReply FGameViewportClient::OnMouseButtonDown(const FGeometry&, const FPointerEvent& Event)
{
	const TSharedPtr<SViewport> Widget = GetViewport();
	if (Event.GetEffectingButton() != EMouseButton::Left || !Widget)
		return FReply::Unhandled();
	bMouseCaptured = true;
	return FReply::Handled().UseHighPrecisionMouseMovement(ToSharedRef(Widget)).SetUserFocus(ToSharedRef(Widget));
}

FReply FGameViewportClient::OnRawMouseMove(FVector2 Delta)
{
	if (!bMouseCaptured)
		return FReply::Unhandled();
	Game.Player.Look(-Delta.X * MouseDegreesPerPixel, -Delta.Y * MouseDegreesPerPixel);
	return FReply::Handled();
}

FReply FGameViewportClient::OnKeyDown(const FGeometry&, const FKeyEvent& Event)
{
	const EKey Key = Event.GetKey();
	FString Error;
	switch (Key)
	{
	case EKey::Escape:
		if (bMouseCaptured)
			ReleaseMouse();
		else if (OnEscape.IsBound())
			OnEscape.Execute();
		else
			return FReply::Unhandled();
		return FReply::Handled();
	case EKey::F:
		if (!Event.IsRepeat())
			Game.ToggleFlying();
		return FReply::Handled();
	case EKey::F5:
		Game.QuickSave(Error);
		return FReply::Handled();
	case EKey::F9:
		Game.QuickLoad(Error);
		return FReply::Handled();
	case EKey::F10:
		if (!OnQuit.IsBound())
			return FReply::Unhandled();
		OnQuit.Execute();
		return FReply::Handled();
	default:
		if (!IsMovementKey(Key))
			return FReply::Unhandled();
		HeldKeys.insert(static_cast<uint16>(Key));
		UpdateInput();
		return FReply::Handled();
	}
}

FReply FGameViewportClient::OnKeyUp(const FGeometry&, const FKeyEvent& Event)
{
	if (!IsMovementKey(Event.GetKey()))
		return FReply::Unhandled();
	HeldKeys.erase(static_cast<uint16>(Event.GetKey()));
	UpdateInput();
	return FReply::Handled();
}

void FGameViewportClient::OnFocusLost(const FFocusEvent&)
{
	// Keys released while another widget had focus never arrive here.
	HeldKeys.clear();
	UpdateInput();
}

void FGameViewportClient::UpdateInput()
{
	FCharacterInput& Input = Game.Input;
	Input.Forward = (Held(HeldKeys, EKey::W) || Held(HeldKeys, EKey::Up) ? 1.0f : 0.0f) -
	    (Held(HeldKeys, EKey::S) || Held(HeldKeys, EKey::Down) ? 1.0f : 0.0f);
	Input.Right = (Held(HeldKeys, EKey::D) || Held(HeldKeys, EKey::Right) ? 1.0f : 0.0f) -
	    (Held(HeldKeys, EKey::A) || Held(HeldKeys, EKey::Left) ? 1.0f : 0.0f);
	Input.Up = (Held(HeldKeys, EKey::E) ? 1.0f : 0.0f) - (Held(HeldKeys, EKey::Q) ? 1.0f : 0.0f);
	Input.bRun = Held(HeldKeys, EKey::LeftShift) || Held(HeldKeys, EKey::RightShift);
	Input.bJump = Held(HeldKeys, EKey::SpaceBar);
}

TSharedRef<SWidget> FGameViewportClient::MakeGameView(
    const TSharedRef<FGameViewportClient>& Client, TFunction<FString()> HudText, TSharedPtr<SViewport>& OutViewport)
{
	const TSharedPtr<IViewportClient> Input = Client.ToSharedPtr();
	return SNew(SOverlay) + SOverlay::Slot()[SAssignNew(OutViewport, SViewport).ViewportClient(Input)] +
	    SOverlay::Slot()
	        .HAlign(HAlign_Left)
	        .VAlign(VAlign_Top)
	        .Padding(FMargin(12))[SNew(SGameHudPanel)[SNew(STextBlock).Text(std::move(HudText))]];
}

void FGameViewportClient::ReleaseMouse()
{
	bMouseCaptured = false;
	FSlateApplication::Get().SetMouseCapture(nullptr);
}

void FGameViewportClient::Draw(FRHICommandList& Cmd, FSlateRenderTargetRHI& RenderTarget)
{
	const uint32 Width = RenderTarget.GetTexture().GetWidth(), Height = RenderTarget.GetTexture().GetHeight();
	if (!Depth || Depth->GetWidth() != Width || Depth->GetHeight() != Height)
	{
		if (Depth)
			Device.DeferRelease(Depth);
		FRHITextureDesc Desc;
		Desc.Width = Width;
		Desc.Height = Height;
		Desc.Format = EPixelFormat::D32_FLOAT;
		Desc.bRenderTarget = true;
		Depth = Device.CreateTexture(Desc);
	}
	if (!Depth || Width == 0 || Height == 0)
		return;

	// The camera: view X right, Y forward, Z up, from the eye in render space.
	Vector3d Eye;
	const FGameWorld& World = Game.GetWorld();
	const FCharacterMovement& Player = Game.Player;
	World.ToRender(Player.GetEyeLocation(), Eye);
	const float Yaw = Player.Yaw * DegreesToRadians, Pitch = Player.Pitch * DegreesToRadians;
	const float Forward[3] = {std::cos(Pitch) * std::cos(Yaw), std::cos(Pitch) * std::sin(Yaw), std::sin(Pitch)};
	const float Right[3] = {std::sin(Yaw), -std::cos(Yaw), 0.0f};
	const float Up[3] = {Right[1] * Forward[2] - Right[2] * Forward[1], Right[2] * Forward[0] - Right[0] * Forward[2],
	    Right[0] * Forward[1] - Right[1] * Forward[0]};
	const float* Rows[3] = {Right, Forward, Up};
	const float EyeAt[3] = {Eye.GetX(), Eye.GetY(), Eye.GetZ()};
	Matrix44 WorldToView;
	WorldToView.SetIdentity();
	for (int32 Row = 0; Row < 3; ++Row)
	{
		float Offset = 0;
		for (int32 Column = 0; Column < 3; ++Column)
		{
			WorldToView.SetMatrix(Row, Column, Rows[Row][Column]);
			Offset -= Rows[Row][Column] * EyeAt[Column];
		}
		WorldToView.SetMatrix(Row, 3, Offset);
	}

	FProjectedSceneView View;
	View.Width = Width;
	View.Height = Height;
	View.CenterX = Width * 0.5f;
	View.CenterY = Height * 0.5f;
	View.ProjectionScale = (Height * 0.5f) / std::tan(FieldOfViewDegrees * 0.5f * DegreesToRadians);
	View.bLit = true;
	const float Sun[3] = {SunX, SunY, SunZ}, WorldUp[3] = {0, 0, 1};
	for (int32 Row = 0; Row < 3; ++Row)
	{
		View.Light[Row] = Rows[Row][0] * Sun[0] + Rows[Row][1] * Sun[1] + Rows[Row][2] * Sun[2];
		View.Up[Row] = Rows[Row][0] * WorldUp[0] + Rows[Row][1] * WorldUp[1] + Rows[Row][2] * WorldUp[2];
	}
	const float SlopeX = View.CenterX / View.ProjectionScale, SlopeZ = View.CenterY / View.ProjectionScale;
	const float FarDistance = static_cast<float>(World.GetComposition().GetCellSize() * FarCells);
	const FSceneViewVolume Volume{
	    Vector3d(-SlopeX, NearDistance, -SlopeZ), Vector3d(SlopeX, FarDistance, SlopeZ), true};

	TArray<FGameRegion> Regions;
	World.GetLoadedRegions(Regions);
	FWorldSceneDrawList List;
	RegionsDrawn = 0;
	for (const FGameRegion& Region : Regions)
	{
		FString Error;
		if (Region.Scene &&
		    List.Append(*Region.Scene, Region.ToRender, WorldToView, Volume, View, TerrainDetailPixels, Error))
			++RegionsDrawn;
		else if (!Error.IsEmpty())
			LastError = Error;
	}
	TerrainTriangles = List.TerrainTriangles;

	FString Error;
	TUniquePtr<FProjectedSceneFrame> Geometry =
	    Scene.Prepare(List.Vertices, List.Batches, List.Instances, RenderTarget.GetTexture().GetFormat(), Error);
	TUniquePtr<FNativeMeshFrame> Materials = List.Meshes.Vertices.empty()
	    ? nullptr
	    : Meshes.Prepare(List.Meshes, RenderTarget.GetTexture().GetFormat(), Error);
	if (!Error.IsEmpty())
		LastError = Error;
	Cmd.BeginRenderPass(RenderTarget.GetTexture(), *Depth, SkyColor, 1.0f);
	Cmd.SetViewport(0, 0, static_cast<float>(Width), static_cast<float>(Height));
	Cmd.SetScissor(FIntRect(0, 0, static_cast<int32>(Width), static_cast<int32>(Height)));
	if (Geometry)
		Scene.DrawGeometry(Cmd, *Geometry, View);
	if (Materials)
		Meshes.Draw(Cmd, *Materials, View, false);
	if (Geometry)
		Scene.DrawInstances(Cmd, *Geometry, View);
	if (Materials)
		Meshes.Draw(Cmd, *Materials, View, true);
	Cmd.EndRenderPass();
	Scene.Trim();
	Meshes.Trim();
	++FramesDrawn;
}
