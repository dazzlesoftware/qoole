// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "MapMaterialTextures.h"
#include <cmath>
#include <algorithm>

namespace
{
float Linear(float V)
{
	return V <= 0.04045f ? V / 12.92f : std::pow((V + 0.055f) / 1.055f, 2.4f);
}
float Display(float V)
{
	return V <= 0.0031308f ? V * 12.92f : 1.055f * std::pow(V, 1 / 2.4f) - 0.055f;
}
uint8 Byte(float V)
{
	return static_cast<uint8>(std::clamp(V * 255.0f + 0.5f, 0.0f, 255.0f));
}
} // namespace
bool FMapMaterialMipChain::Build(const FTextureAsset& Image, bool Normal, FMapMaterialMipChain& Out, FString& Error)
{
	if (Image.Width <= 0 || Image.Height <= 0 || Image.Width > 8192 || Image.Height > 8192 ||
		Image.Pixels.size() != static_cast<size_t>(Image.Width) * Image.Height * 4)
	{
		Error = "Invalid material texture dimensions or bytes.";
		return false;
	}
	FMapMaterialMipChain M;
	M.Pixels = Image.Pixels;
	M.Levels = 1;
	int W = Image.Width, H = Image.Height;
	size_t Offset = 0;
	while (W > 1 || H > 1)
	{
		const int NW = std::max(W / 2, 1), NH = std::max(H / 2, 1);
		const size_t Next = M.Pixels.size();
		M.Pixels.resize(Next + static_cast<size_t>(NW) * NH * 4);
		for (int Y = 0; Y < NH; ++Y)
			for (int X = 0; X < NW; ++X)
			{
				float Sum[4] = {};
				int Count = 0;
				for (int SY = Y * H / NH; SY < (Y + 1) * H / NH; ++SY)
					for (int SX = X * W / NW; SX < (X + 1) * W / NW; ++SX)
					{
						const auto* P = &M.Pixels[Offset + (static_cast<size_t>(SY) * W + SX) * 4];
						for (int C = 0; C < 4; ++C)
						{
							float V = P[C] / 255.0f;
							if (C < 3)
								V = Normal ? V * 2 - 1 : Image.bSRGB ? Linear(V) : V;
							Sum[C] += V;
						}
						++Count;
					}
				for (float& V : Sum)
					V /= Count;
				if (Normal)
				{
					const float L = std::sqrt(Sum[0] * Sum[0] + Sum[1] * Sum[1] + Sum[2] * Sum[2]);
					if (L < 1e-8f)
					{
						Sum[0] = Sum[1] = 0;
						Sum[2] = 1;
					}
					else
						for (int C = 0; C < 3; ++C)
							Sum[C] /= L;
				}
				for (int C = 0; C < 4; ++C)
				{
					float V = Sum[C];
					if (C < 3)
						V = Normal ? V * 0.5f + 0.5f : Image.bSRGB ? Display(V) : V;
					M.Pixels[Next + (static_cast<size_t>(Y) * NW + X) * 4 + C] = Byte(V);
				}
			}
		Offset = Next;
		W = NW;
		H = NH;
		++M.Levels;
	}
	Out = std::move(M);
	Error.Empty();
	return true;
}
