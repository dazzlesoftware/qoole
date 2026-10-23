// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "Materials/MaterialGraph.h"
#include "MaterialShaderTemplate.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <sstream>
#include <locale>
#include <cstring>
#include <stdexcept>

namespace
{
void Require(bool B, const char* Message)
{
	if (!B)
		throw std::runtime_error(Message);
}
FString Number(float V)
{
	std::ostringstream S;
	S.imbue(std::locale::classic());
	S.precision(9);
	S << std::scientific << V;
	return S.str();
}
FString Type(int Width)
{
	if (Width == 8)
		return "GraphMaterialAttributes";
	return Width == 1 ? FString("float") : FString::Format("float{}", Width);
}
int Integer(const FJsonValue& V, int Limit)
{
	double N = V.AsNumber(-1);
	Require(V.GetType() == EJsonType::Number && std::isfinite(N) && N >= 0 && N <= Limit &&
				N == std::floor(N),
			"Invalid graph integer.");
	return static_cast<int>(N);
}
float Real(const FJsonValue& V)
{
	double N = V.AsNumber();
	Require(V.GetType() == EJsonType::Number && std::isfinite(N) && std::abs(N) <= 1e6,
			"Invalid graph value or layout.");
	return static_cast<float>(N);
}
void Replace(std::string& S, const char* Marker, const FString& Value)
{
	auto P = S.find(Marker);
	Require(P != std::string::npos, "Invalid shader template.");
	S.replace(P, std::strlen(Marker), Value.View());
}
const char* UnaryFunction(EMaterialExpression E)
{
	switch (E)
	{
	case EMaterialExpression::Abs:
		return "abs";
	case EMaterialExpression::Ceil:
		return "ceil";
	case EMaterialExpression::Floor:
		return "floor";
	case EMaterialExpression::Frac:
		return "frac";
	case EMaterialExpression::Sine:
		return "sin";
	case EMaterialExpression::Cosine:
		return "cos";
	case EMaterialExpression::Tangent:
		return "tan";
	case EMaterialExpression::SquareRoot:
		return "sqrt";
	case EMaterialExpression::Saturate:
		return "saturate";
	case EMaterialExpression::Normalize:
		return "normalize";
	case EMaterialExpression::Length:
		return "length";
	case EMaterialExpression::Sign:
		return "sign";
	case EMaterialExpression::Exp:
		return "exp";
	case EMaterialExpression::Log:
		return "log";
	default:
		return nullptr;
	}
}
thread_local int GraphCodecDepth = 0, GraphCodecNodes = 0, GraphCodecGraphs = 0;
struct FGraphCodecGuard
{
	FGraphCodecGuard()
	{
		if (GraphCodecDepth == 0)
		{
			GraphCodecNodes = 0;
			GraphCodecGraphs = 0;
		}
		Require(GraphCodecDepth < 16, "Function snapshot nesting exceeds 16.");
		Require(++GraphCodecGraphs <= 65, "Too many embedded function snapshots.");
		++GraphCodecDepth;
	}
	void AddNodes(int N)
	{
		Require(N >= 0 && N <= 256 && (GraphCodecNodes += N) <= 4096,
				"Embedded function snapshot node budget exceeded.");
	}
	~FGraphCodecGuard()
	{
		--GraphCodecDepth;
	}
};
struct FValue
{
	FString Text;
	int Width = 1;
};
} // namespace
const char* MaterialExpressionName(EMaterialExpression E)
{
	static const char* Names[] = {"Constant",
								  "Constant2Vector",
								  "Constant3Vector",
								  "Constant4Vector",
								  "TextureCoordinate",
								  "TextureSample",
								  "VertexColor",
								  "Add",
								  "Subtract",
								  "Multiply",
								  "Divide",
								  "LinearInterpolate",
								  "OneMinus",
								  "Clamp",
								  "ComponentMask",
								  "NormalDecode",
								  "Reroute",
								  "Comment",
								  "Abs",
								  "Ceil",
								  "Floor",
								  "Frac",
								  "Sine",
								  "Cosine",
								  "Tangent",
								  "SquareRoot",
								  "Power",
								  "Min",
								  "Max",
								  "Saturate",
								  "Normalize",
								  "Length",
								  "DotProduct",
								  "CrossProduct",
								  "AppendVector",
								  "Rotator",
								  "Distance",
								  "Step",
								  "SmoothStep",
								  "Sign",
								  "Fmod",
								  "Exp",
								  "Log",
								  "ScalarParameter",
								  "VectorParameter",
								  "TextureSampleParameter2D",
								  "StaticSwitchParameter",
								  "QualitySwitch",
								  "PlatformSwitch",
								  "FunctionInput",
								  "MaterialFunctionCall",
								  "ScalarCollectionParameter",
								  "VectorCollectionParameter",
								  "MakeMaterialAttributes",
								  "BreakMaterialAttributes",
								  "BlendMaterialAttributes"};
	auto I = static_cast<unsigned>(E);
	static_assert(std::size(Names) == static_cast<size_t>(EMaterialExpression::Count));
	return I < std::size(Names) ? Names[I] : "Unknown";
}
const char* MaterialOutputName(EMaterialOutput E)
{
	static const char* Names[] = {"Base Color", "Roughness", "Metallic",
								  "Opacity",	"Normal",	 "Emissive Color"};
	return Names[static_cast<int>(E)];
}
int MaterialInputCount(EMaterialExpression E)
{
	if (UnaryFunction(E) || E == EMaterialExpression::Reroute)
		return 1;
	switch (E)
	{
	case EMaterialExpression::MakeMaterialAttributes:
		return 6;
	case EMaterialExpression::BreakMaterialAttributes:
		return 1;
	case EMaterialExpression::BlendMaterialAttributes:
		return 3;
	case EMaterialExpression::TextureSample:
	case EMaterialExpression::TextureSampleParameter2D:
	case EMaterialExpression::OneMinus:
	case EMaterialExpression::ComponentMask:
	case EMaterialExpression::NormalDecode:
	case EMaterialExpression::FunctionInput:
		return 1;
	case EMaterialExpression::Add:
	case EMaterialExpression::Subtract:
	case EMaterialExpression::Multiply:
	case EMaterialExpression::Divide:
	case EMaterialExpression::Power:
	case EMaterialExpression::Min:
	case EMaterialExpression::Max:
	case EMaterialExpression::DotProduct:
	case EMaterialExpression::CrossProduct:
	case EMaterialExpression::AppendVector:
	case EMaterialExpression::Distance:
	case EMaterialExpression::Step:
	case EMaterialExpression::Fmod:
	case EMaterialExpression::StaticSwitchParameter:
	case EMaterialExpression::PlatformSwitch:
		return 2;
	case EMaterialExpression::Lerp:
	case EMaterialExpression::Clamp:
	case EMaterialExpression::Rotator:
	case EMaterialExpression::SmoothStep:
	case EMaterialExpression::QualitySwitch:
		return 3;
	case EMaterialExpression::MaterialFunctionCall:
		return 8;
	default:
		return 0;
	}
}
const char* MaterialInputName(EMaterialExpression E, int I)
{
	if (E == EMaterialExpression::TextureSample || E == EMaterialExpression::TextureSampleParameter2D)
		return "UVs";
	if (E == EMaterialExpression::MakeMaterialAttributes)
	{
		static const char* Names[] = {"Base Color", "Roughness", "Metallic", "Opacity", "Normal", "Emissive"};
		return Names[I];
	}
	if (E == EMaterialExpression::BreakMaterialAttributes)
		return "Attributes";
	if (E == EMaterialExpression::BlendMaterialAttributes)
		return I == 0 ? "Base Layer" : I == 1 ? "Top Layer" : "Alpha";
	if (E == EMaterialExpression::FunctionInput)
		return "Default";
	if (E == EMaterialExpression::MaterialFunctionCall)
	{
		static const char* Names[] = {"Input 0", "Input 1", "Input 2", "Input 3",
									  "Input 4", "Input 5", "Input 6", "Input 7"};
		return Names[I];
	}
	if (E == EMaterialExpression::StaticSwitchParameter)
		return I == 0 ? "True" : "False";
	if (E == EMaterialExpression::QualitySwitch)
		return I == 0 ? "Low" : I == 1 ? "Medium" : "High";
	if (E == EMaterialExpression::PlatformSwitch)
		return I == 0 ? "D3D12" : "Vulkan";
	if (E == EMaterialExpression::Rotator)
		return I == 0 ? "UVs" : I == 1 ? "Angle (radians)" : "Center";
	if (E == EMaterialExpression::SmoothStep)
		return I == 0 ? "Min" : I == 1 ? "Max" : "Value";
	if (E == EMaterialExpression::Clamp)
		return I == 0 ? "Input" : I == 1 ? "Min" : "Max";
	if (MaterialInputCount(E) == 1)
		return "Input";
	return I == 0 ? "A" : I == 1 ? "B" : "Alpha";
}
FMaterialNode* FMaterialGraph::Find(uint32 Id)
{
	for (auto& N : Nodes)
		if (N.Id == Id)
			return &N;
	return nullptr;
}
const FMaterialNode* FMaterialGraph::Find(uint32 Id) const
{
	for (const auto& N : Nodes)
		if (N.Id == Id)
			return &N;
	return nullptr;
}
uint32 FMaterialGraph::Add(EMaterialExpression E, float X, float Y)
{
	uint32 Id = 1;
	for (const auto& N : Nodes)
		Id = std::max(Id, N.Id + 1);
	if (Nodes.size() >= FMaterialGraphCompiler::MaxNodes || Id > 1000000)
		return 0;
	FMaterialNode N;
	if (E == EMaterialExpression::FunctionInput)
		N.Mask = 1;
	if (E == EMaterialExpression::BreakMaterialAttributes)
		N.Mask = 0;
	if (E == EMaterialExpression::BlendMaterialAttributes)
		N.Value[2] = .5f;
	N.Id = Id;
	N.Expression = E;
	N.X = X;
	N.Y = Y;
	if (E == EMaterialExpression::Multiply || E == EMaterialExpression::Divide ||
		E == EMaterialExpression::Add)
		N.Value[1] = 1;
	if (E == EMaterialExpression::Lerp)
	{
		N.Value[1] = 1;
		N.Value[2] = .5f;
	}
	if (E == EMaterialExpression::Clamp)
		N.Value[2] = 1;
	if (E == EMaterialExpression::Comment)
	{
		N.Value[0] = 400;
		N.Value[1] = 220;
		N.Description = "Comment";
	}
	if (E == EMaterialExpression::Power)
		N.Value[1] = 2;
	if (E == EMaterialExpression::Rotator)
		N.Value[2] = .5f;
	if (E == EMaterialExpression::SmoothStep)
	{
		N.Value[1] = 1;
		N.Value[2] = .5f;
	}
	if (E == EMaterialExpression::ScalarParameter || E == EMaterialExpression::VectorParameter ||
		E == EMaterialExpression::TextureSampleParameter2D ||
		E == EMaterialExpression::StaticSwitchParameter ||
		E == EMaterialExpression::ScalarCollectionParameter ||
		E == EMaterialExpression::VectorCollectionParameter)
		N.ParameterName = FString::Format("Parameter{}", Id);
	Nodes.push_back(N);
	return Id;
}

bool FMaterialGraphCompiler::Compile(const FMaterialGraph& G, FMaterialGraphProgram& Out, FString& Error)
{
	if (!G.Functions.empty() || std::any_of(G.Nodes.begin(), G.Nodes.end(), [](const auto& N) {
			return N.Expression == EMaterialExpression::MaterialFunctionCall;
		}))
	{
		FMaterialGraph Expanded;
		if (!ExpandFunctions(G, Expanded, Error))
			return false;
		return Compile(Expanded, Out, Error);
	}
	uint32 Current = 0;
	try
	{
		Require(G.QualityLevel <= 2, "Invalid material quality level.");
		Require(G.Nodes.size() <= MaxNodes, "Too many material nodes.");
		std::map<uint32, const FMaterialNode*> Nodes;
		std::map<FString, EMaterialExpression> ParameterTypes;
		for (const auto& N : G.Nodes)
		{
			Current = N.DiagnosticOwner ? N.DiagnosticOwner : N.Id;
			Require(N.Id > 0 && N.Id <= 1000000 && Nodes.emplace(N.Id, &N).second,
					"Duplicate or invalid node ID.");
			Require(static_cast<int>(N.Expression) < static_cast<int>(EMaterialExpression::Count),
					"Unknown expression.");
			if (N.Expression == EMaterialExpression::ScalarParameter ||
				N.Expression == EMaterialExpression::VectorParameter ||
				N.Expression == EMaterialExpression::TextureSampleParameter2D ||
				N.Expression == EMaterialExpression::StaticSwitchParameter)
				Require(!N.ParameterName.IsEmpty() && N.ParameterName.Len() <= 128,
						"Parameter requires a name of at most 128 characters.");
			if (N.Expression == EMaterialExpression::ScalarParameter ||
				N.Expression == EMaterialExpression::VectorParameter ||
				N.Expression == EMaterialExpression::TextureSampleParameter2D ||
				N.Expression == EMaterialExpression::StaticSwitchParameter)
			{
				auto [It, Inserted] = ParameterTypes.emplace(N.ParameterName, N.Expression);
				Require(Inserted || It->second == N.Expression, "Parameter name has incompatible types.");
			}
			if (N.Expression == EMaterialExpression::StaticSwitchParameter)
			{
				Require(N.Value[2] == 0 || N.Value[2] == 1, "Static switch default must be zero or one.");
				for (const auto& Other : G.Nodes)
					if (Other.Id != N.Id && Other.Expression == N.Expression &&
						Other.ParameterName == N.ParameterName)
						Require(Other.Value[2] == N.Value[2], "Static switch defaults disagree.");
			}
			Require(N.ParameterGroup.Len() <= 128, "Parameter group name is too long.");
			Require(N.Description.Len() <= 4096, "Node description is too long.");
			for (float V : N.Value)
				Require(std::isfinite(V) && std::abs(V) <= 1e6f, "Invalid constant.");
			Require(std::isfinite(N.X) && std::isfinite(N.Y) && std::abs(N.X) <= 1e6 && std::abs(N.Y) <= 1e6,
					"Invalid layout.");
			for (int I = MaterialInputCount(N.Expression); I < 8; ++I)
				Require(N.Inputs[I] == 0, "Expression has an unused input link.");
		}
		FMaterialGraphProgram Program;
		FString Code, Declarations;
		const auto AttributeTypes = [&] {
			if (!Declarations.Contains("struct GraphMaterialAttributes"))
				Declarations += R"HLSL(
struct GraphMaterialAttributes {float3 BaseColor;float Roughness;float Metallic;float Opacity;float3 Normal;float3 Emissive;};
GraphMaterialAttributes MakeGraphAttributes(float3 C,float R,float M,float O,float3 N,float3 E){GraphMaterialAttributes A;A.BaseColor=C;A.Roughness=R;A.Metallic=M;A.Opacity=O;A.Normal=N;A.Emissive=E;return A;}
GraphMaterialAttributes BlendGraphAttributes(GraphMaterialAttributes A,GraphMaterialAttributes B,float T){T=saturate(T);float3 N=lerp(A.Normal,B.Normal,T);return MakeGraphAttributes(lerp(A.BaseColor,B.BaseColor,T),lerp(A.Roughness,B.Roughness,T),lerp(A.Metallic,B.Metallic,T),lerp(A.Opacity,B.Opacity,T),N/max(length(N),1e-8),lerp(A.Emissive,B.Emissive,T));}
)HLSL";
		};
		const auto DefaultAttributes = [&]() {
			AttributeTypes();
			return FValue{"MakeGraphAttributes(float3(1,1,1),0.5,0.0,1.0,float3(0,0,1),float3(0,0,0))", 8};
		};

		std::map<uint32, int> State;
		std::map<uint32, FValue> Values;
		std::function<FValue(uint32)> Visit = [&](uint32 Id) -> FValue {
			Current = Nodes.contains(Id) && Nodes[Id]->DiagnosticOwner ? Nodes[Id]->DiagnosticOwner : Id;
			Require(Nodes.contains(Id), "Dangling material link.");
			Require(State[Id] != 1, "Material graph contains a cycle.");
			if (State[Id] == 2)
				return Values[Id];
			State[Id] = 1;
			const auto& N = *Nodes[Id];
			auto Input = [&](int I) {
				auto V = N.Inputs[I] ? Visit(N.Inputs[I]) : FValue{Number(N.Value[I]), 1};
				Current = Nodes.contains(Id) && Nodes[Id]->DiagnosticOwner ? Nodes[Id]->DiagnosticOwner : Id;
				if (V.Width == 8)
					Require(N.Expression == EMaterialExpression::FunctionInput ||
								N.Expression == EMaterialExpression::Reroute ||
								N.Expression == EMaterialExpression::StaticSwitchParameter ||
								N.Expression == EMaterialExpression::QualitySwitch ||
								N.Expression == EMaterialExpression::PlatformSwitch ||
								N.Expression == EMaterialExpression::BreakMaterialAttributes ||
								N.Expression == EMaterialExpression::BlendMaterialAttributes,
							"Use BreakMaterialAttributes before arithmetic or channel inputs.");
				return V;
			};
			FValue V;
			const int E = static_cast<int>(N.Expression);
			if (E <= static_cast<int>(EMaterialExpression::Constant4Vector))
			{
				V.Width = E + 1;
				V.Text = Type(V.Width) + "(";
				for (int I = 0; I < V.Width; ++I)
				{
					if (I)
						V.Text += ",";
					V.Text += Number(N.Value[I]);
				}
				V.Text += ")";
			}
			else if (N.Expression == EMaterialExpression::FunctionInput)
			{
				Require(N.InputIndex < 8 && ((N.Mask >= 1 && N.Mask <= 4) || N.Mask == 8),
						"Invalid function input interface.");
				if (N.Inputs[0])
				{
					V = Input(0);
					Require(V.Width == N.Mask, "Function input type mismatch.");
				}
				else if (N.Mask == 8)
					V = DefaultAttributes();
				else
				{
					V.Width = N.Mask;
					V.Text = Type(V.Width) + "(";
					for (int I = 0; I < V.Width; ++I)
					{
						if (I)
							V.Text += ",";
						V.Text += Number(N.Value[I]);
					}
					V.Text += ")";
				}
			}

			else if (N.Expression == EMaterialExpression::MakeMaterialAttributes)
			{
				AttributeTypes();
				static const char* Defaults[] = {"float3(1,1,1)", "0.5",		   "0.0",
												 "1.0",			  "float3(0,0,1)", "float3(0,0,0)"};
				static const int Widths[] = {3, 1, 1, 1, 3, 3};
				V.Width = 8;
				V.Text = "MakeGraphAttributes(";
				for (int I = 0; I < 6; ++I)
				{
					const auto P = N.Inputs[I] ? Input(I) : FValue{Defaults[I], Widths[I]};
					Require(P.Width == Widths[I] || P.Width == 1, "Material attribute input type mismatch.");
					if (I)
						V.Text += ",";
					V.Text += P.Text;
				}
				V.Text += ")";
			}
			else if (N.Expression == EMaterialExpression::BreakMaterialAttributes)
			{
				AttributeTypes();
				const auto A = N.Inputs[0] ? Input(0) : DefaultAttributes();
				Require(A.Width == 8 && N.Mask < 6, "Choose a valid material attribute from a bundle.");
				static const char* Fields[] = {"BaseColor", "Roughness", "Metallic",
											   "Opacity",	"Normal",	 "Emissive"};
				static const int Widths[] = {3, 1, 1, 1, 3, 3};
				V = {A.Text + "." + Fields[N.Mask], Widths[N.Mask]};
			}
			else if (N.Expression == EMaterialExpression::BlendMaterialAttributes)
			{
				AttributeTypes();
				const auto A = N.Inputs[0] ? Input(0) : DefaultAttributes(),
						   B = N.Inputs[1] ? Input(1) : DefaultAttributes(), T = Input(2);
				Require(A.Width == 8 && B.Width == 8 && T.Width == 1,
						"Layer blending requires two attribute bundles and scalar alpha.");
				V = {"BlendGraphAttributes(" + A.Text + "," + B.Text + "," + T.Text + ")", 8};
			}
			else if (N.Expression == EMaterialExpression::StaticSwitchParameter)
				V = Input(N.Value[2] != 0 ? 0 : 1);
			else if (N.Expression == EMaterialExpression::QualitySwitch)
				V = Input(G.QualityLevel);
			else if (N.Expression == EMaterialExpression::PlatformSwitch)
			{
				const auto A = Input(0), B = Input(1);
				Require(A.Width == B.Width ||
							(A.Width <= 4 && B.Width <= 4 && (A.Width == 1 || B.Width == 1)),
						"Platform switch widths differ.");
				V.Width = std::max(A.Width, B.Width);
				if (!Declarations.Contains("#define GENESIS_PLATFORM"))
					Declarations += "#ifdef MAP_SPIRV\n#define GENESIS_PLATFORM(D,V) (V)\n#else\n#define "
									"GENESIS_PLATFORM(D,V) (D)\n#endif\n";
				V.Text = "GENESIS_PLATFORM(" + A.Text + "," + B.Text + ")";
			}
			else if (N.Expression == EMaterialExpression::ScalarParameter ||
					 N.Expression == EMaterialExpression::VectorParameter ||
					 N.Expression == EMaterialExpression::ScalarCollectionParameter ||
					 N.Expression == EMaterialExpression::VectorCollectionParameter)
			{
				const bool IsCollection = N.Expression == EMaterialExpression::ScalarCollectionParameter ||
										  N.Expression == EMaterialExpression::VectorCollectionParameter;
				Require(!IsCollection || (!N.Collection.IsNull() && !N.ParameterName.IsEmpty()),
						"Choose a collection and parameter.");
				const auto ParameterKey =
					IsCollection ? (N.Collection.Id.IsEmpty() ? N.Collection.Path : N.Collection.Id) + "/" +
									   N.ParameterName
								 : N.ParameterName;
				V.Width = N.Expression == EMaterialExpression::ScalarParameter ||
								  N.Expression == EMaterialExpression::ScalarCollectionParameter
							  ? 1
							  : 4;
				size_t Slot = 0;
				for (; Slot < Program.Parameters.size(); ++Slot)
					if (Program.Parameters[Slot].Name == ParameterKey)
						break;
				if (Slot == Program.Parameters.size())
				{
					FMaterialGraphParameter P;
					P.Name = ParameterKey;
					P.Group = N.ParameterGroup;
					if (IsCollection)
					{
						P.Collection = N.Collection;
						P.CollectionParameter = N.ParameterName;
					}
					P.Width = static_cast<uint8>(V.Width);
					for (int I = 0; I < 4; ++I)
						P.Value[I] = N.Value[I];
					Program.Parameters.push_back(P);
				}
				else
				{
					const auto& P = Program.Parameters[Slot];
					Require(P.Width == V.Width, "Parameter name has incompatible types.");
					for (int I = 0; I < V.Width; ++I)
						Require(P.Value[I] == N.Value[I], "Shared parameter defaults must agree.");
				}
				V.Text = FString::Format("GraphParameters[{}]{}", Slot, V.Width == 1 ? ".x" : "");
			}
			else if (N.Expression == EMaterialExpression::TextureCoordinate)
				V = {"i.uv", 2};
			else if (N.Expression == EMaterialExpression::VertexColor)
				V = {"float4(Linear(i.color.rgb),i.color.a)", 4};
			else if (N.Expression == EMaterialExpression::TextureSample ||
					 N.Expression == EMaterialExpression::TextureSampleParameter2D)
			{
				FString RefError;
				Require(!N.Texture.IsNull() &&
							FAssetReference::FromJson(N.Texture.ToJson(), RefError).has_value(),
						"Choose a texture asset for TextureSample.");
				const auto UV = N.Inputs[0] ? Input(0) : FValue{"i.uv", 2};
				Require(UV.Width == 2, "Texture UVs require float2.");
				size_t Slot = Program.Textures.size();
				if (N.Expression == EMaterialExpression::TextureSampleParameter2D)
					for (size_t I = 0; I < Program.Textures.size(); ++I)
						if (Program.Textures[I].ParameterName == N.ParameterName)
						{
							Slot = I;
							break;
						}
				if (Slot == Program.Textures.size())
				{
					Require(Program.Textures.size() < MaxTextures, "Material exceeds texture limit.");
					Program.Textures.push_back({N.Texture, N.bNormalTexture,
												N.Expression == EMaterialExpression::TextureSampleParameter2D
													? N.ParameterName
													: FString()});
					Declarations += FString::Format(
						"BINDING(0,{}) Texture2D GraphTexture{} : register(t{});\n", Slot, Slot, Slot);
				}
				else
				{
					const auto& Existing = Program.Textures[Slot];
					Require(Existing.Reference.Id == N.Texture.Id &&
								Existing.Reference.Path == N.Texture.Path &&
								Existing.bNormal == N.bNormalTexture,
							"Shared texture parameter defaults and sampling roles must agree.");
				}
				Program.Textures[Slot].Group = N.ParameterGroup;
				V = {FString::Format("GraphTexture{}.Sample(RepeatSampler,{})", Slot, UV.Text), 4};
			}
			else
			{
				auto A = Input(0);
				Require(N.Expression == EMaterialExpression::Reroute || A.Width <= 4,
						"Use BreakMaterialAttributes to access a bundle value.");
				V.Width = A.Width;
				if (N.Expression == EMaterialExpression::ComponentMask)
				{
					Require(N.Mask > 0 && N.Mask < 16, "Choose channels in ComponentMask.");
					FString Channels;
					V.Width = 0;
					for (int I = 0; I < 4; ++I)
						if (N.Mask & (1 << I))
						{
							Require(I < A.Width, "Mask channel exceeds input width.");
							Channels += FString(std::string(1, "xyzw"[I]));
							++V.Width;
						}
					V.Text = A.Width == 1 ? A.Text : "(" + A.Text + ")." + Channels;
				}
				else if (N.Expression == EMaterialExpression::NormalDecode)
				{
					Require(A.Width >= 3, "NormalDecode requires RGB.");
					V = {"Unit((" + A.Text + ").xyz*2.0-1.0)", 3};
				}
				else if (N.Expression == EMaterialExpression::Reroute)
					V = A;
				else if (N.Expression == EMaterialExpression::Comment)
					Require(false, "Comments cannot be wired to shader inputs.");
				else if (const auto* Unary = UnaryFunction(N.Expression))
				{
					FString Argument = A.Text;
					if (N.Expression == EMaterialExpression::SquareRoot)
						Argument = "max(" + Argument + ",0.0)";
					if (N.Expression == EMaterialExpression::Log)
						Argument = "max(" + Argument + ",1e-8)";
					if (N.Expression == EMaterialExpression::Normalize)
						V.Text = "(" + Argument + "/max(length(" + Argument + "),1e-8))";
					else
						V.Text = FString(Unary) + "(" + Argument + ")";
					if (N.Expression == EMaterialExpression::Length)
						V.Width = 1;
				}
				else if (N.Expression == EMaterialExpression::AppendVector)
				{
					auto B = Input(1);
					V.Width = A.Width + B.Width;
					Require(V.Width <= 4, "AppendVector exceeds float4.");
					V.Text = Type(V.Width) + "(" + A.Text + "," + B.Text + ")";
				}
				else if (N.Expression == EMaterialExpression::Rotator)
				{
					auto B = Input(1), C = Input(2);
					Require(A.Width == 2 && B.Width == 1 && (C.Width == 1 || C.Width == 2),
							"Rotator requires UV float2, scalar radians and center float2.");
					const auto X = "(" + A.Text + "-" + C.Text + ")";
					V = {"(float2(cos(" + B.Text + ")*" + X + ".x-sin(" + B.Text + ")*" + X + ".y,sin(" +
							 B.Text + ")*" + X + ".x+cos(" + B.Text + ")*" + X + ".y)+" + C.Text + ")",
						 2};
				}
				else if (N.Expression == EMaterialExpression::SmoothStep)
				{
					auto B = Input(1), C = Input(2);
					V.Width = std::max({A.Width, B.Width, C.Width});
					Require((A.Width == 1 || A.Width == V.Width) && (B.Width == 1 || B.Width == V.Width) &&
								(C.Width == 1 || C.Width == V.Width),
							"SmoothStep widths differ.");
					V.Text = "smoothstep(" + A.Text + "," + B.Text + "," + C.Text + ")";
				}
				else if (N.Expression == EMaterialExpression::OneMinus)
					V.Text = "(1.0-" + A.Text + ")";
				else if (N.Expression == EMaterialExpression::Clamp)
				{
					auto B = Input(1), C = Input(2);
					Require((B.Width == 1 || B.Width == A.Width) && (C.Width == 1 || C.Width == A.Width),
							"Clamp bounds width mismatch.");
					V.Text = "clamp(" + A.Text + "," + B.Text + "," + C.Text + ")";
				}
				else
				{
					auto B = Input(1);
					Require(A.Width == B.Width || A.Width == 1 || B.Width == 1,
							"Incompatible arithmetic widths.");
					V.Width = std::max(A.Width, B.Width);
					if (N.Expression == EMaterialExpression::DotProduct ||
						N.Expression == EMaterialExpression::CrossProduct ||
						N.Expression == EMaterialExpression::Distance)
					{
						Require(A.Width == B.Width, "Vector operation requires matching widths.");
						if (N.Expression == EMaterialExpression::CrossProduct)
							Require(A.Width == 3, "CrossProduct requires float3.");
						const char* Function = N.Expression == EMaterialExpression::DotProduct ? "dot"
											   : N.Expression == EMaterialExpression::CrossProduct
												   ? "cross"
												   : "distance";
						V.Text = FString(Function) + "(" + A.Text + "," + B.Text + ")";
						V.Width = N.Expression == EMaterialExpression::CrossProduct ? 3 : 1;
					}
					else if (N.Expression == EMaterialExpression::Power ||
							 N.Expression == EMaterialExpression::Min ||
							 N.Expression == EMaterialExpression::Max ||
							 N.Expression == EMaterialExpression::Step ||
							 N.Expression == EMaterialExpression::Fmod)
					{
						const char* Function = N.Expression == EMaterialExpression::Power  ? "pow"
											   : N.Expression == EMaterialExpression::Min  ? "min"
											   : N.Expression == EMaterialExpression::Max  ? "max"
											   : N.Expression == EMaterialExpression::Step ? "step"
																						   : "fmod";
						auto AT = A.Text, BT = B.Text;
						if (N.Expression == EMaterialExpression::Power)
							AT = "max(" + AT + ",1e-8)";
						if (N.Expression == EMaterialExpression::Fmod)
							BT = "max(abs(" + BT + "),1e-8)";
						V.Text = FString(Function) + "(" + AT + "," + BT + ")";
					}
					else if (N.Expression == EMaterialExpression::Lerp)
					{
						auto C = Input(2);
						Require(C.Width == 1 || C.Width == V.Width, "Lerp alpha width mismatch.");
						V.Text = "lerp(" + A.Text + "," + B.Text + "," + C.Text + ")";
					}
					else
					{
						const char* Op = N.Expression == EMaterialExpression::Add		 ? "+"
										 : N.Expression == EMaterialExpression::Subtract ? "-"
										 : N.Expression == EMaterialExpression::Multiply ? "*"
																						 : "/";
						// Signed epsilon protects divisions in procedural materials.
						if (N.Expression == EMaterialExpression::Divide)
							B.Text =
								"(lerp(-1.0,1.0,step(0.0," + B.Text + "))*max(abs(" + B.Text + "),1e-8))";
						V.Text = "(" + A.Text + Op + B.Text + ")";
					}
				}
			}
			FString Name = FString::Format("node{}", Id);
			Code += FString::Format("\t{} {} = {};\n", Type(V.Width), Name, V.Text);
			V.Text = Name;
			State[Id] = 2;
			Values[Id] = V;
			return V;
		};
		// Validate structural links even in disconnected editing fragments, but
		// only emit expressions reachable from an output (as shader editors do).
		std::map<uint32, int> Links;
		std::function<void(uint32)> CheckLinks = [&](uint32 Id) {
			Current = Nodes.contains(Id) && Nodes[Id]->DiagnosticOwner ? Nodes[Id]->DiagnosticOwner : Id;
			Require(Nodes.contains(Id), "Dangling material link.");
			Require(Links[Id] != 1, "Material graph contains a cycle.");
			if (Links[Id] == 2)
				return;
			Links[Id] = 1;
			for (auto Input : Nodes[Id]->Inputs)
				if (Input)
					CheckLinks(Input);
			Links[Id] = 2;
		};
		for (const auto& [Id, N] : Nodes)
			CheckLinks(Id);
		static const char* Names[] = {"surfaceColor",	"surfaceRoughness", "surfaceMetallic",
									  "surfaceOpacity", "surfaceNormal",	"surfaceEmissive"};
		static const int Widths[] = {3, 1, 1, 1, 3, 3};
		static const char* Defaults[] = {"float3(1,1,1)", "0.5",		   "0.0",
										 "1.0",			  "float3(0,0,1)", "float3(0,0,0)"};
		TOptional<FValue> Attributes;
		if (G.bUseMaterialAttributes || (G.bFunction && G.Outputs[0]))
		{
			const auto A = G.Outputs[0] ? Visit(G.Outputs[0]) : DefaultAttributes();
			if (A.Width == 8)
				Attributes = A;
			else
				Require(!G.bUseMaterialAttributes, "Material output requires an attribute bundle.");
		}
		if (Attributes)
			for (int I = 1; I < 6; ++I)
				Require(G.Outputs[I] == 0,
						"Attribute output cannot be mixed with individual material outputs.");
		static const char* AttributeFields[] = {"BaseColor", "Roughness", "Metallic",
												"Opacity",	 "Normal",	  "Emissive"};
		for (int I = 0; I < 6; ++I)
		{
			FValue V = Attributes	  ? FValue{Attributes->Text + "." + AttributeFields[I], Widths[I]}
					   : G.Outputs[I] ? Visit(G.Outputs[I])
									  : FValue{Defaults[I], Widths[I]};
			if (G.bFunction && I == 0 && V.Width != 1 && V.Width != 3)
			{
				V.Text = V.Width == 4 ? "(" + V.Text + ").xyz" : "float3(" + V.Text + ",0.0)";
				V.Width = 3;
			}
			Require(V.Width == Widths[I] || V.Width == 1,
					"Material output type mismatch; use ComponentMask for RGB or alpha.");
			Code += FString::Format("\t{} {} = {};\n", Type(Widths[I]), Names[I], V.Text);
		}
		if (!Program.Parameters.empty())
			Declarations +=
				FString::Format("BINDING(0,{}) cbuffer MaterialParameters : register(b1) {{ float4 "
								"GraphParameters[{}]; }};\n",
								std::max(size_t(1), Program.Textures.size()), Program.Parameters.size());
		std::string Source = MaterialGraphShaderTemplate;
		Replace(Source, "/*GRAPH_TEXTURES*/", Declarations);
		Replace(Source, "/*GRAPH_EVALUATE*/", Code);
		Replace(Source, "/*GRAPH_NORMAL*/", (G.Outputs[4] || Attributes.has_value()) ? "true" : "false");
		Program.PixelShaderSource = Source;
		Out = std::move(Program);
		Error.Empty();
		return true;
	}
	catch (const std::exception& E)
	{
		Error = FString::Format("Node {}: {}", Current, E.what());
		return false;
	}
}

FJsonValue FMaterialGraphCompiler::Encode(const FMaterialGraph& G)
{
	FGraphCodecGuard Guard;
	Guard.AddNodes(static_cast<int>(G.Nodes.size()));
	auto J = FJsonValue::MakeObject();
	J["Version"] = 6;
	J["UseMaterialAttributes"] = G.bUseMaterialAttributes;
	J["FunctionGraph"] = G.bFunction;
	J["Functions"] = FJsonValue::MakeArray();
	for (const auto& F : G.Functions)
	{
		Require(F.Graph != nullptr, "Missing function snapshot.");
		auto V = FJsonValue::MakeObject();
		V["Reference"] = F.Reference.ToJson();
		V["Graph"] = Encode(*F.Graph);
		J["Functions"].Push(std::move(V));
	}
	J["QualityLevel"] = G.QualityLevel;
	J["Nodes"] = FJsonValue::MakeArray();
	J["Outputs"] = FJsonValue::MakeArray();
	for (const auto& N : G.Nodes)
	{
		auto V = FJsonValue::MakeObject();
		V["Id"] = static_cast<int>(N.Id);
		V["Expression"] = static_cast<int>(N.Expression);
		V["Inputs"] = FJsonValue::MakeArray();
		for (auto I : N.Inputs)
			V["Inputs"].Push(static_cast<int>(I));
		V["Value"] = FJsonValue::MakeArray();
		for (auto F : N.Value)
			V["Value"].Push(F);
		V["Texture"] = N.Texture.ToJson();
		V["Collection"] = N.Collection.ToJson();
		V["Function"] = N.Function.ToJson();
		V["InputIndex"] = N.InputIndex;
		V["Description"] = N.Description;
		V["ParameterName"] = N.ParameterName;
		V["ParameterGroup"] = N.ParameterGroup;
		V["NormalTexture"] = N.bNormalTexture;
		V["Mask"] = N.Mask;
		V["X"] = N.X;
		V["Y"] = N.Y;
		J["Nodes"].Push(V);
	}
	for (auto O : G.Outputs)
		J["Outputs"].Push(static_cast<int>(O));
	return J;
}
bool FMaterialGraphCompiler::Decode(const FJsonValue& J, FMaterialGraph& Out, FString& Error,
									bool ValidateSemantics)
{
	try
	{
		FGraphCodecGuard Guard;
		Require(J.IsObject() && Integer(J.Get("Version"), 6) >= 1, "Unsupported graph version.");
		const auto& Ns = J.Get("Nodes");
		const auto& Os = J.Get("Outputs");
		Require(Ns.IsArray() && Ns.Num() <= MaxNodes && Os.IsArray() && Os.Num() == 6,
				"Invalid graph arrays.");
		Guard.AddNodes(Ns.Num());
		FMaterialGraph G;
		if (J.Contains("UseMaterialAttributes"))
		{
			Require(J.Get("UseMaterialAttributes").GetType() == EJsonType::Bool, "Invalid attributes flag.");
			G.bUseMaterialAttributes = J.Get("UseMaterialAttributes").AsBool();
		}
		if (J.Contains("QualityLevel"))
			G.QualityLevel = static_cast<uint8>(Integer(J.Get("QualityLevel"), 2));
		if (J.Contains("FunctionGraph"))
		{
			Require(J.Get("FunctionGraph").GetType() == EJsonType::Bool, "Invalid function graph flag.");
			G.bFunction = J.Get("FunctionGraph").AsBool();
		}
		if (J.Contains("Functions"))
		{
			const auto& Fs = J.Get("Functions");
			Require(Fs.IsArray() && Fs.Num() <= 64, "Invalid function snapshot list.");
			for (const auto& V : Fs.GetItems())
			{
				auto Ref = FAssetReference::FromJson(V.Get("Reference"), Error);
				Require(Ref.has_value() && !Ref->IsNull(), "Invalid function snapshot reference.");
				FMaterialGraph Definition;
				Require(Decode(V.Get("Graph"), Definition, Error), *Error);
				Require(Definition.bFunction, "Snapshot is not a function graph.");
				G.Functions.push_back({*Ref, std::make_shared<const FMaterialGraph>(std::move(Definition))});
			}
		}
		for (const auto& V : Ns.GetItems())
		{
			Require(V.IsObject(), "Invalid graph node.");
			FMaterialNode N;
			N.Id = Integer(V.Get("Id"), 1000000);
			N.Expression = static_cast<EMaterialExpression>(
				Integer(V.Get("Expression"), static_cast<int>(EMaterialExpression::Count) - 1));
			const auto& Is = V.Get("Inputs");
			const auto& Vs = V.Get("Value");
			Require(Is.IsArray() && (Is.Num() == 3 || Is.Num() == 8) && Vs.IsArray() &&
						(Vs.Num() == 4 || Vs.Num() == 8),
					"Invalid node arrays.");
			for (int I = 0; I < Is.Num(); ++I)
				N.Inputs[I] = Integer(Is[I], 1000000);
			for (int I = 0; I < Vs.Num(); ++I)
				N.Value[I] = Real(Vs[I]);
			auto Ref = FAssetReference::FromJson(V.Get("Texture"), Error);
			Require(Ref.has_value(), "Invalid graph texture reference.");
			N.Texture = *Ref;
			if (V.Contains("Collection"))
			{
				auto C = FAssetReference::FromJson(V.Get("Collection"), Error);
				Require(C.has_value(), "Invalid collection reference.");
				N.Collection = *C;
			}
			if (V.Contains("Function"))
			{
				auto F = FAssetReference::FromJson(V.Get("Function"), Error);
				Require(F.has_value(), "Invalid function reference.");
				N.Function = *F;
			}
			if (V.Contains("InputIndex"))
				N.InputIndex = static_cast<uint8>(Integer(V.Get("InputIndex"), 7));
			if (V.Contains("ParameterGroup"))
			{
				Require(V.Get("ParameterGroup").GetType() == EJsonType::String, "Invalid parameter group.");
				N.ParameterGroup = V.Get("ParameterGroup").AsString();
			}
			if (V.Contains("ParameterName"))
			{
				Require(V.Get("ParameterName").GetType() == EJsonType::String, "Invalid parameter name.");
				N.ParameterName = V.Get("ParameterName").AsString();
			}
			if (V.Contains("Description"))
			{
				Require(V.Get("Description").GetType() == EJsonType::String, "Invalid node description.");
				N.Description = V.Get("Description").AsString();
			}
			Require(V.Get("NormalTexture").GetType() == EJsonType::Bool, "Invalid sampler type.");
			N.bNormalTexture = V.Get("NormalTexture").AsBool();
			N.Mask = static_cast<uint8>(Integer(V.Get("Mask"), 15));
			N.X = Real(V.Get("X"));
			N.Y = Real(V.Get("Y"));
			G.Nodes.push_back(N);
		}
		for (int I = 0; I < 6; ++I)
			G.Outputs[I] = Integer(Os[I], 1000000);
		FMaterialGraphProgram Program;
		if (ValidateSemantics && !Compile(G, Program, Error))
			return false;
		Out = std::move(G);
		Error.Empty();
		return true;
	}
	catch (const std::exception& E)
	{
		Error = E.what();
		return false;
	}
}
