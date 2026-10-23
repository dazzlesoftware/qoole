// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// Icons of the map editor's panels and toolbars, drawn with lines and
// rounded boxes in the foreground color (the style has no images yet).
// SMapViewportIcon draws one at any size (the drawings are on a 16 x 16 grid).

#pragma once

#include "Widgets/SLeafWidget.h"

#include <cmath>
#include <numbers>

namespace MapEditorPrivate
{
	enum class EIcon : uint8
	{
		None,
		Select,
		Rotate,
		Scale,
		Clip,
		Vertex,
		Edge,
		Face,
		GridSnap,
		RotationSnap,
		Perspective,
		Ortho,
		CameraSpeed,
		Lit,
		Unlit,
		Wireframe,
		Show,
		Settings,
		Options, // ⋮
		Maximize,
		Restore,
		// Panels.
		Recent,
		Basic,
		Light,
		Geometry,
		All,
		Folder,
		FolderOpen,
		Prefab,
		Map,
		Entity,
		File,
		Add,
		Import,
		Save,
		Back,
		Forward,
		Search,
	};

	inline TArray<FVector2> Arc(FVector2 Center, float Radius, float FromDegrees, float ToDegrees, int32 Segments)
	{
		TArray<FVector2> Points;
		for (int32 Index = 0; Index <= Segments; ++Index)
		{
			const float Angle =
			    (FromDegrees + (ToDegrees - FromDegrees) * static_cast<float>(Index) / static_cast<float>(Segments)) *
			    std::numbers::pi_v<float> / 180.0f;
			Points.push_back(Center + FVector2(std::cos(Angle), -std::sin(Angle)) * Radius);
		}
		return Points;
	}

	// A 16 x 16 line drawing in the foreground color (scaled to the widget).
	class SMapViewportIcon final : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SMapViewportIcon)
		SLATE_ATTRIBUTE(EIcon, Icon, EIcon::None)
		SLATE_ARGUMENT(float, Size, 16.0f)
		SLATE_END_ARGS()

		void Construct(const FArguments& Args)
		{
			Icon.Assign(Args._Icon);
			Size = Args._Size;
		}

	protected:
		FVector2 ComputeDesiredSize(float) const override
		{
			return FVector2(Size, Size);
		}

		int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FRect&, FSlateWindowElementList& Out,
		    int32 LayerId, const FWidgetStyle& Style) const override
		{
			const FColor Color = Style.GetForegroundColor();
			const float K = FMath::Min(Geometry.Size.X, Geometry.Size.Y) / 16.0f;
			const FVector2 Offset = (Geometry.Size - FVector2(16.0f * K, 16.0f * K)) * 0.5f;
			const FPaintGeometry Paint = Geometry.ToPaintGeometry();
			const TFunction<FVector2(float, float)> P = [&](float X, float Y) { return Offset + FVector2(X, Y) * K; };
			struct FIconLineBuilder
			{
				const FVector2& Offset;
				float Scale;
				FSlateWindowElementList& Out;
				int32 LayerId;
				const FPaintGeometry& Paint;
				FColor Color;
				void operator()(TArray<FVector2> Points, bool bClosed = false, float Thickness = 1.25f) const
				{
					for (FVector2& Point : Points)
					{
						Point = Offset + Point * Scale;
					}
					Out.AddLines(LayerId, Paint, std::move(Points), Color, Thickness * Scale, bClosed);
				}
			};
			const FIconLineBuilder Lines{Offset, K, Out, LayerId, Paint, Color};
			const TFunction<void(float, float, float, float, FColor)> Box =
			    [&](float L, float T, float R, float B, FColor Fill)
			{ Out.AddSolidBox(LayerId, Geometry.ToPaintGeometry(FRect(P(L, T), P(R, B))), Fill); };
			const TFunction<void(float, float, float, FColor)> Disc = [&](float X, float Y, float Radius, FColor Fill)
			{
				Out.AddRoundedBox(LayerId,
				    Geometry.ToPaintGeometry(FRect(P(X - Radius, Y - Radius), P(X + Radius, Y + Radius))), Fill,
				    Radius * K);
			};
			const TFunction<FColor(uint8)> Faded = [&](uint8 Alpha)
			{ return FColor(Color.R, Color.G, Color.B, static_cast<uint8>(Color.A * Alpha / 255)); };

			switch (Icon.Get())
			{
			case EIcon::Select:
				Lines({{4.0f, 2.0f}, {4.0f, 13.0f}, {6.8f, 10.4f}, {8.9f, 14.2f}, {10.6f, 13.3f}, {8.6f, 9.6f},
				          {12.2f, 9.6f}},
				    true);
				break;
			case EIcon::Rotate:
			{
				Lines(Arc({8.0f, 8.0f}, 5.5f, 60.0f, 350.0f, 20));
				const FVector2 Tip(8.0f + 5.5f * 0.5f, 8.0f - 5.5f * 0.866f);
				Lines({Tip + FVector2(-3.0f, -1.2f), Tip, Tip + FVector2(-0.6f, 3.0f)});
				break;
			}
			case EIcon::Scale:
				Lines({{2.5f, 6.5f}, {9.5f, 6.5f}, {9.5f, 13.5f}, {2.5f, 13.5f}}, true);
				Lines({{6.5f, 9.5f}, {13.5f, 2.5f}});
				Lines({{9.5f, 2.5f}, {13.5f, 2.5f}, {13.5f, 6.5f}});
				break;
			case EIcon::Clip:
				Lines({{3.0f, 3.0f}, {13.0f, 3.0f}, {13.0f, 13.0f}, {3.0f, 13.0f}}, true, 1.0f);
				Lines({{1.0f, 12.0f}, {15.0f, 4.0f}}, false, 1.75f);
				Box(3.0f, 8.5f, 13.0f, 13.0f, Faded(70));
				break;
			case EIcon::Vertex:
				Lines({{4.0f, 4.0f}, {12.0f, 4.0f}, {12.0f, 12.0f}, {4.0f, 12.0f}}, true, 1.0f);
				for (const FVector2 Corner :
				    {FVector2(4.0f, 4.0f), FVector2(12.0f, 4.0f), FVector2(12.0f, 12.0f), FVector2(4.0f, 12.0f)})
				{
					Box(Corner.X - 1.8f, Corner.Y - 1.8f, Corner.X + 1.8f, Corner.Y + 1.8f, Color);
				}
				break;
			case EIcon::Edge:
				Lines({{4.0f, 4.0f}, {12.0f, 4.0f}, {12.0f, 12.0f}, {4.0f, 12.0f}}, true, 1.0f);
				Lines({{2.5f, 4.0f}, {13.5f, 4.0f}}, false, 3.0f);
				break;
			case EIcon::Face:
				Lines({{4.0f, 4.0f}, {12.0f, 4.0f}, {12.0f, 12.0f}, {4.0f, 12.0f}}, true, 1.0f);
				Box(4.0f, 4.0f, 12.0f, 12.0f, Faded(150));
				break;
			case EIcon::GridSnap:
				Lines({{2.5f, 2.5f}, {13.5f, 2.5f}, {13.5f, 13.5f}, {2.5f, 13.5f}}, true, 1.0f);
				Lines({{6.2f, 2.5f}, {6.2f, 13.5f}}, false, 1.0f);
				Lines({{9.8f, 2.5f}, {9.8f, 13.5f}}, false, 1.0f);
				Lines({{2.5f, 6.2f}, {13.5f, 6.2f}}, false, 1.0f);
				Lines({{2.5f, 9.8f}, {13.5f, 9.8f}}, false, 1.0f);
				break;
			case EIcon::RotationSnap:
				Lines({{13.5f, 13.0f}, {2.5f, 13.0f}, {11.0f, 4.5f}});
				Lines(Arc({2.5f, 13.0f}, 6.5f, 0.0f, 45.0f, 8), false, 1.0f);
				break;
			case EIcon::Perspective:
				Lines({{1.5f, 5.0f}, {10.5f, 5.0f}, {10.5f, 12.0f}, {1.5f, 12.0f}}, true);
				Lines({{10.5f, 8.5f}, {14.5f, 5.5f}, {14.5f, 11.5f}}, true);
				break;
			case EIcon::Ortho:
				Lines({{2.5f, 2.5f}, {13.5f, 2.5f}, {13.5f, 13.5f}, {2.5f, 13.5f}}, true);
				Box(5.5f, 5.5f, 10.5f, 10.5f, Faded(170));
				break;
			case EIcon::CameraSpeed:
				Lines({{3.0f, 4.0f}, {7.0f, 8.0f}, {3.0f, 12.0f}}, false, 1.5f);
				Lines({{8.0f, 4.0f}, {12.0f, 8.0f}, {8.0f, 12.0f}}, false, 1.5f);
				break;
			case EIcon::Lit:
				Disc(8.0f, 8.0f, 5.75f, Faded(110));
				Disc(6.5f, 6.5f, 2.5f, Color);
				break;
			case EIcon::Unlit:
				Disc(8.0f, 8.0f, 5.75f, Color);
				break;
			case EIcon::Wireframe:
			{
				TArray<FVector2> Circle = Arc({8.0f, 8.0f}, 5.75f, 0.0f, 360.0f, 24);
				Lines(Circle, false, 1.0f);
				TArray<FVector2> Meridian;
				TArray<FVector2> Parallel;
				for (int32 Index = 0; Index <= 24; ++Index)
				{
					const float Angle = static_cast<float>(Index) / 24.0f * 2.0f * std::numbers::pi_v<float>;
					Meridian.push_back(FVector2(8.0f + std::cos(Angle) * 2.4f, 8.0f + std::sin(Angle) * 5.75f));
					Parallel.push_back(FVector2(8.0f + std::cos(Angle) * 5.75f, 8.0f + std::sin(Angle) * 2.4f));
				}
				Lines(Meridian, false, 1.0f);
				Lines(Parallel, false, 1.0f);
				break;
			}
			case EIcon::Show:
			{
				// Arcs of radius R about points K above and below the middle
				// meet at the eye's corners when K = R sin(35 degrees).
				TArray<FVector2> Eye = Arc({8.0f, 12.9f}, 8.5f, 35.0f, 145.0f, 12);
				TArray<FVector2> Lower = Arc({8.0f, 3.1f}, 8.5f, 215.0f, 325.0f, 12);
				Eye.insert(Eye.end(), Lower.begin(), Lower.end());
				Lines(Eye, true);
				Disc(8.0f, 8.0f, 2.2f, Color);
				break;
			}
			case EIcon::Settings:
				Lines(Arc({8.0f, 8.0f}, 3.4f, 0.0f, 360.0f, 16));
				for (int32 Tooth = 0; Tooth < 8; ++Tooth)
				{
					const float Angle = static_cast<float>(Tooth) * std::numbers::pi_v<float> / 4.0f;
					const FVector2 Dir(std::cos(Angle), std::sin(Angle));
					Lines({FVector2(8.0f, 8.0f) + Dir * 4.6f, FVector2(8.0f, 8.0f) + Dir * 6.6f}, false, 2.0f);
				}
				break;
			case EIcon::Options:
				for (const float Y : {3.5f, 8.0f, 12.5f})
				{
					Disc(8.0f, Y, 1.4f, Color);
				}
				break;
			case EIcon::Maximize:
				Lines({{2.5f, 2.5f}, {13.5f, 2.5f}, {13.5f, 13.5f}, {2.5f, 13.5f}}, true);
				break;
			case EIcon::Restore:
				Lines({{2.5f, 2.5f}, {13.5f, 2.5f}, {13.5f, 13.5f}, {2.5f, 13.5f}}, true);
				Lines({{8.0f, 2.5f}, {8.0f, 13.5f}});
				Lines({{2.5f, 8.0f}, {13.5f, 8.0f}});
				break;
			case EIcon::Recent:
				Lines(Arc({8.0f, 8.0f}, 6.0f, 0.0f, 360.0f, 24));
				Lines({{8.0f, 4.0f}, {8.0f, 8.0f}, {11.0f, 10.0f}});
				break;
			case EIcon::Basic:
				Disc(8.0f, 4.5f, 2.6f, Color);
				Lines(
				    {{3.0f, 14.5f}, {3.5f, 10.5f}, {6.0f, 8.5f}, {10.0f, 8.5f}, {12.5f, 10.5f}, {13.0f, 14.5f}}, true);
				break;
			case EIcon::Light:
				Lines(Arc({8.0f, 6.5f}, 4.5f, -40.0f, 220.0f, 18));
				Lines({{5.5f, 11.0f}, {10.5f, 11.0f}});
				Lines({{6.0f, 13.5f}, {10.0f, 13.5f}});
				break;
			case EIcon::Geometry:
				Lines({{3.0f, 5.0f}, {8.0f, 2.5f}, {13.0f, 5.0f}, {13.0f, 11.0f}, {8.0f, 13.5f}, {3.0f, 11.0f}}, true);
				Lines({{3.0f, 5.0f}, {8.0f, 7.5f}, {13.0f, 5.0f}});
				Lines({{8.0f, 7.5f}, {8.0f, 13.5f}});
				break;
			case EIcon::All:
				Lines({{2.5f, 3.5f}, {13.5f, 3.5f}}, false, 1.5f);
				Lines({{2.5f, 8.0f}, {13.5f, 8.0f}}, false, 1.5f);
				Lines({{2.5f, 12.5f}, {13.5f, 12.5f}}, false, 1.5f);
				break;
			case EIcon::Folder:
			case EIcon::FolderOpen:
			{
				const FColor Tan(197, 158, 92, Color.A);
				const FColor Dark(150, 116, 62, Color.A);
				Out.AddRoundedBox(
				    LayerId, Geometry.ToPaintGeometry(FRect(P(1.5f, 3.0f), P(7.5f, 6.0f))), Dark, 1.0f * K);
				Out.AddRoundedBox(LayerId, Geometry.ToPaintGeometry(FRect(P(1.5f, 4.5f), P(14.5f, 13.5f))),
				    Icon.Get() == EIcon::FolderOpen ? Dark : Tan, 1.2f * K);
				if (Icon.Get() == EIcon::FolderOpen)
				{
					Out.AddRoundedBox(
					    LayerId, Geometry.ToPaintGeometry(FRect(P(3.0f, 7.0f), P(15.0f, 13.5f))), Tan, 1.2f * K);
				}
				break;
			}
			case EIcon::Prefab:
			case EIcon::Map:
			case EIcon::Entity:
			case EIcon::File:
			{
				// A page with the type's color along its foot, as asset tiles have.
				const FColor Page(56, 60, 70, Color.A);
				FColor Strip(150, 158, 172, Color.A);
				if (Icon.Get() == EIcon::Prefab)
					Strip = FColor(96, 168, 255, Color.A);
				if (Icon.Get() == EIcon::Map)
					Strip = FColor(232, 150, 60, Color.A);
				if (Icon.Get() == EIcon::Entity)
					Strip = FColor(120, 200, 110, Color.A);
				Out.AddRoundedBox(
				    LayerId, Geometry.ToPaintGeometry(FRect(P(2.5f, 1.5f), P(13.5f, 14.5f))), Page, 1.2f * K);
				Box(2.5f, 12.5f, 13.5f, 14.5f, Strip);
				if (Icon.Get() == EIcon::Prefab)
				{
					Lines({{5.0f, 6.0f}, {8.0f, 4.5f}, {11.0f, 6.0f}, {11.0f, 9.5f}, {8.0f, 11.0f}, {5.0f, 9.5f}}, true,
					    1.0f);
				}
				else if (Icon.Get() == EIcon::Map)
				{
					Lines({{4.5f, 10.5f}, {7.0f, 5.0f}, {9.0f, 8.5f}, {10.5f, 6.5f}, {12.0f, 10.5f}}, true, 1.0f);
				}
				else if (Icon.Get() == EIcon::Entity)
				{
					Disc(8.0f, 7.5f, 2.6f, Strip);
				}
				else
				{
					for (const float Y : {4.5f, 7.0f, 9.5f})
					{
						Lines({{4.5f, Y}, {11.5f, Y}}, false, 1.0f);
					}
				}
				break;
			}
			case EIcon::Add:
				Lines({{8.0f, 3.0f}, {8.0f, 13.0f}}, false, 1.75f);
				Lines({{3.0f, 8.0f}, {13.0f, 8.0f}}, false, 1.75f);
				break;
			case EIcon::Import:
				Lines({{8.0f, 2.5f}, {8.0f, 10.0f}});
				Lines({{5.0f, 7.0f}, {8.0f, 10.0f}, {11.0f, 7.0f}});
				Lines({{2.5f, 10.5f}, {2.5f, 13.5f}, {13.5f, 13.5f}, {13.5f, 10.5f}});
				break;
			case EIcon::Save:
				Lines({{2.5f, 2.5f}, {11.5f, 2.5f}, {13.5f, 4.5f}, {13.5f, 13.5f}, {2.5f, 13.5f}}, true);
				Box(5.0f, 2.5f, 10.5f, 6.0f, Faded(160));
				Box(5.0f, 9.0f, 11.0f, 13.5f, Faded(160));
				break;
			case EIcon::Back:
				Lines({{10.0f, 3.5f}, {5.5f, 8.0f}, {10.0f, 12.5f}}, false, 1.6f);
				break;
			case EIcon::Forward:
				Lines({{6.0f, 3.5f}, {10.5f, 8.0f}, {6.0f, 12.5f}}, false, 1.6f);
				break;
			case EIcon::Search:
				Lines(Arc({6.8f, 6.8f}, 4.3f, 0.0f, 360.0f, 20));
				Lines({{10.0f, 10.0f}, {14.0f, 14.0f}}, false, 1.75f);
				break;
			case EIcon::None:
				break;
			}
			return LayerId;
		}

	private:
		TWidgetAttribute<EIcon, EInvalidateWidgetReason::Paint> Icon{*this, EIcon::None};
		float Size = 16.0f;
	};

} // namespace MapEditorPrivate
