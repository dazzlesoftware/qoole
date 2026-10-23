// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "AssetRegistry.h"

enum class EMaterialExpression : uint8
{
	Constant,
	Constant2Vector,
	Constant3Vector,
	Constant4Vector,
	TextureCoordinate,
	TextureSample,
	VertexColor,
	Add,
	Subtract,
	Multiply,
	Divide,
	Lerp,
	OneMinus,
	Clamp,
	ComponentMask,
	NormalDecode,
	Reroute,
	Comment,
	Abs,
	Ceil,
	Floor,
	Frac,
	Sine,
	Cosine,
	Tangent,
	SquareRoot,
	Power,
	Min,
	Max,
	Saturate,
	Normalize,
	Length,
	DotProduct,
	CrossProduct,
	AppendVector,
	Rotator,
	Distance,
	Step,
	SmoothStep,
	Sign,
	Fmod,
	Exp,
	Log,
	ScalarParameter,
	VectorParameter,
	TextureSampleParameter2D,
	Count
};
enum class EMaterialOutput : uint8
{
	BaseColor,
	Roughness,
	Metallic,
	Opacity,
	Normal,
	Emissive,
	Count
};
// A zero ID means the input uses its default. IDs survive reorder and layout edits.
struct FMaterialNode
{
	uint32 Id = 0;
	EMaterialExpression Expression = EMaterialExpression::Constant;
	uint32 Inputs[3] = {};
	float Value[4] = {0, 0, 0, 1};
	FAssetReference Texture;
	FString ParameterName;
	FString Description; // editor annotation; excluded from shader semantics
	uint8 Mask = 7;		 // RGBA bits
	bool bNormalTexture = false;
	float X = 0, Y = 0; // editor-only layout; excluded from shader source
};
struct FMaterialGraph
{
	TArray<FMaterialNode> Nodes;
	uint32 Outputs[static_cast<int>(EMaterialOutput::Count)] = {};
	FMaterialNode* Find(uint32 Id);
	const FMaterialNode* Find(uint32 Id) const;
	uint32 Add(EMaterialExpression Type, float X = 0, float Y = 0);
};
struct FMaterialGraphTexture
{
	FAssetReference Reference;
	bool bNormal = false;
	FString ParameterName; // empty for ordinary texture samples
};
struct FMaterialGraphParameter
{
	FString Name;
	uint8 Width = 1;
	float Value[4] = {};
};
struct FMaterialGraphProgram
{
	FString PixelShaderSource;
	TArray<FMaterialGraphTexture> Textures;
	TArray<FMaterialGraphParameter> Parameters;
};
const char* MaterialExpressionName(EMaterialExpression);
const char* MaterialOutputName(EMaterialOutput);
int MaterialInputCount(EMaterialExpression);
const char* MaterialInputName(EMaterialExpression, int);
// Platform-independent validation and HLSL lowering. Shader binaries are cooked
// by editor/toolchain adapters, never compiled by the shipped renderer.
class FMaterialGraphCompiler
{
  public:
	static constexpr int MaxNodes = 256, MaxTextures = 12;
	static bool Compile(const FMaterialGraph&, FMaterialGraphProgram&, FString& Error);
	static FJsonValue Encode(const FMaterialGraph&);
	static bool Decode(const FJsonValue&, FMaterialGraph&, FString& Error);
};
