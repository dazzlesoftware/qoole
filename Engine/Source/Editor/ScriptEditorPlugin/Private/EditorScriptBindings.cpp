// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "EditorScriptBindings.h"
#include "MapCoreHeaders.h"
#include "MapDocument.h"
#include "World/World.h"
#include "AssetRegistry.h"
#include "Landscape/Landscape.h"
#include "Foliage/Foliage.h"
#include "MapLandscapeMode.h"
#include "MapFoliageMode.h"
#include "Framework/Commands/UICommands.h"
#include <cmath>
#include <stdexcept>
namespace
{
using J = FJsonValue;
void Require(bool B, const char* E)
{
	if (!B)
		throw std::runtime_error(E);
}
float Number(const J& V, float Min = -10000000, float Max = 10000000)
{
	const auto N = V.AsNumber();
	Require(V.GetType() == EJsonType::Number && std::isfinite(N) && N >= Min && N <= Max,
			"Number is outside its supported range.");
	return static_cast<float>(N);
}
int Integer(const J& V, int Min, int Max)
{
	const auto N = Number(V, static_cast<float>(Min), static_cast<float>(Max));
	Require(std::floor(N) == N, "Expected an integer.");
	return static_cast<int>(N);
}
Vector3d Vector(const J& V)
{
	Require(V.IsArray() && V.Num() == 3, "Expected a three-number vector.");
	return Vector3d(Number(V[0]), Number(V[1]), Number(V[2]));
}
Object* Find(Object& Root, const FString& Id)
{
	if (Root.GetIdentity().Actor == Id)
		return &Root;
	for (int I = 0; I < Root.GetNumChildren(); ++I)
		if (auto* Found = Find(Root.GetChild(I), Id))
			return Found;
	return nullptr;
}
Object& Actor(FMapDocument& D, const J& A)
{
	auto* O = Find(D.GetRoot(), A.Get("ActorId").AsString());
	Require(O != nullptr, "Editor actor is missing.");
	return *O;
}
J Describe(Object& O)
{
	auto R = J::MakeObject();
	R["ActorId"] = O.GetIdentity().Actor;
	R["Name"] = O.GetObjName();
	R["Children"] = O.GetNumChildren();
	R["Kind"] = O.HasLandscape() ? "Landscape"
				: O.HasFoliage() ? "Foliage"
				: O.HasMesh()	 ? "Mesh"
				: O.HasBrush()	 ? "Brush"
				: O.HasEntity()	 ? "Entity"
								 : "Group";
	return R;
}
J Ids(const TArray<FString>& Values)
{
	auto R = J::MakeArray();
	for (const auto& V : Values)
		R.Push(V);
	return R;
}
std::shared_ptr<FEditorScriptContext> Context(const std::weak_ptr<FEditorScriptContext>& W)
{
	auto C = W.lock();
	Require(C && C->GetDocument && C->GetDocument(), "Editor script document has expired.");
	return C;
}
bool Owns(const FEditorScriptContext& C, FMapDocument& D)
{
	return C.TransactionDocument == &D && C.TransactionId != 0 &&
		   C.TransactionId == D.GetActiveTransactionId();
}
} // namespace
void CancelEditorScriptTransaction(FEditorScriptContext& C)
{
	auto* D = C.GetDocument ? C.GetDocument() : nullptr;
	if (D && Owns(C, *D))
		D->CancelTransaction();
	C.TransactionDocument = nullptr;
	C.TransactionId = 0;
}
bool RegisterEditorScriptBindings(FScriptBindingRegistry& Registry, std::weak_ptr<FEditorScriptContext> Weak,
								  FString& Error)
{
	TArray<FScriptBinding> Batch;
	const auto Add = [&](const char* Name, bool Write, TArray<FScriptArgument> Args, EJsonType Type, auto Fn,
						 bool Transactional = true) {
		Batch.push_back(
			{Name, Write ? "Editor.Write" : "Editor.Read", true, std::move(Args), Type,
			 [Weak, Write, Fn, Transactional, Name](const J& A, J& R, FString& E) {
				 auto C = Context(Weak);
				 auto& D = *C->GetDocument();
				 const bool Edit = Write && Transactional;
				 if (Write && D.IsInTransaction() && !Owns(*C, D))
				 {
					 E = "An editor transaction is already owned by another operation.";
					 return false;
				 }
				 const bool Begin = Edit && !D.IsInTransaction();
				 if (Begin)
				 {
					 D.BeginTransaction(Name);
					 C->TransactionDocument = &D;
					 C->TransactionId = D.GetActiveTransactionId();
				 }
				 try
				 {
					 if (!Fn(*C, D, A, R, E) || !ValidateScriptValue(R, E))
					 {
						 if (Edit)
							 CancelEditorScriptTransaction(*C);
						 return false;
					 }
					 if (Edit && !Owns(*C, D))
					 {
						 C->TransactionDocument = nullptr;
						 C->TransactionId = 0;
						 E = "Native operation canceled the script transaction; its edits were rolled back.";
						 return false;
					 }
					 if (Begin && Owns(*C, D))
					 {
						 D.EndTransaction();
						 C->TransactionDocument = nullptr;
						 C->TransactionId = 0;
					 }
					 return true;
				 }
				 catch (...)
				 {
					 if (Edit)
						 CancelEditorScriptTransaction(*C);
					 throw;
				 }
			 }});
	};
	Add("Editor.GetDocument", false, {}, EJsonType::Object, [](auto&, auto& D, const J&, J& R, auto&) {
		R = J::MakeObject();
		R["Path"] = D.GetPath();
		R["Modified"] = D.IsModified();
		R["Scope"] = D.GetScope().GetIdentity().Actor;
		R["Brushes"] = D.CountBrushes();
		R["Entities"] = D.CountEntities();
		R["Meshes"] = D.CountMeshes();
		return true;
	});
	Add("Editor.GetObjects", false, {}, EJsonType::Array, [](auto&, auto& D, const J&, J& R, auto&) {
		R = J::MakeArray();
		for (auto* O : D.GetObjects())
			R.Push(Describe(*O));
		return true;
	});
	Add("Editor.FindActor", false, {{"ActorId", EJsonType::String}}, EJsonType::Object,
		[](auto&, auto& D, const J& A, J& R, auto&) {
			R = Describe(Actor(D, A));
			return true;
		});
	Add("Editor.GetSelection", false, {}, EJsonType::Array, [](auto&, auto& D, const J&, J& R, auto&) {
		R = Ids(D.GetSelectedActorIds());
		return true;
	});
	Add(
		"Editor.SetSelection", true, {{"ActorIds", EJsonType::Array}}, EJsonType::Null,
		[](auto&, auto& D, const J& A, J& R, auto&) {
			TArray<FString> Values;
			for (const auto& V : A.Get("ActorIds").GetItems())
			{
				Require(V.GetType() == EJsonType::String, "ActorIds must contain strings.");
				auto* O = Find(D.GetRoot(), V.AsString());
				Require(O != nullptr, "Selection actor is missing.");
				Values.push_back(V.AsString());
			}
			D.SetSelectedActorIds(Values);
			R = {};
			return true;
		},
		false);
	Add(
		"Editor.SetScope", true, {{"ActorId", EJsonType::String}}, EJsonType::Bool,
		[](auto&, auto& D, const J& A, J& R, auto&) {
			R = D.SetScope(Actor(D, A));
			return true;
		},
		false);
	Add(
		"Editor.ScopeUp", true, {}, EJsonType::Null,
		[](auto&, auto& D, const J&, J& R, auto&) {
			D.ScopeUp();
			R = {};
			return true;
		},
		false);
	Add("Editor.SetName", true, {{"ActorId", EJsonType::String}, {"Name", EJsonType::String}},
		EJsonType::Null, [](auto&, auto& D, const J& A, J& R, auto&) {
			D.SetObjectName(Actor(D, A), A.Get("Name").AsString());
			R = {};
			return true;
		});
	Add("Editor.AddMesh", true, {{"Mesh", EJsonType::String}, {"Position", EJsonType::Array}},
		EJsonType::Object, [](auto&, auto& D, const J& A, J& R, auto&) {
			auto* O = D.AddMesh(A.Get("Mesh").AsString(), Vector(A.Get("Position")));
			Require(O != nullptr, "Mesh is missing.");
			R = Describe(*O);
			return true;
		});
	Add("Editor.AddEntity", true, {{"Class", EJsonType::String}, {"Position", EJsonType::Array}},
		EJsonType::Object, [](auto&, auto& D, const J& A, J& R, auto&) {
			auto* O = D.AddEntity(A.Get("Class").AsString(), Vector(A.Get("Position")));
			Require(O != nullptr, "Entity class is unavailable.");
			R = Describe(*O);
			return true;
		});
	Add("Editor.MoveSelection", true, {{"Delta", EJsonType::Array}}, EJsonType::Null,
		[](auto&, auto& D, const J& A, J& R, auto&) {
			D.MoveObjects(D.GetSelection(), Vector(A.Get("Delta")));
			R = {};
			return true;
		});
	Add("Editor.DuplicateSelection", true, {{"Delta", EJsonType::Array}}, EJsonType::Array,
		[](auto&, auto& D, const J& A, J& R, auto&) {
			auto Objects = D.DuplicateObjects(D.GetSelection(), Vector(A.Get("Delta")));
			R = J::MakeArray();
			for (auto* O : Objects)
				R.Push(Describe(*O));
			return true;
		});
	Add("Editor.DeleteSelection", true, {}, EJsonType::Null, [](auto&, auto& D, const J&, J& R, auto&) {
		D.DeleteObjects(D.GetSelection());
		R = {};
		return true;
	});
	Add("Editor.GroupSelection", true, {}, EJsonType::Object, [](auto&, auto& D, const J&, J& R, auto&) {
		auto* O = D.GroupObjects(D.GetSelection());
		Require(O != nullptr, "Selection cannot be grouped.");
		R = Describe(*O);
		return true;
	});
	Add("Editor.UngroupSelection", true, {}, EJsonType::Bool, [](auto&, auto& D, const J&, J& R, auto&) {
		R = D.UngroupObjects(D.GetSelection());
		return true;
	});
	Add("Editor.GetActorTransform", false, {{"ActorId", EJsonType::String}}, EJsonType::Object,
		[](auto&, auto& D, const J& A, J& R, auto&) {
			FTransform3D T;
			Require(D.GetActorTransform(A.Get("ActorId").AsString(), T), "Actor has no native transform.");
			R = T.ToJson();
			return true;
		});
	Add("Editor.SetActorTransform", true, {{"ActorId", EJsonType::String}, {"Transform", EJsonType::Object}},
		EJsonType::Bool, [](auto&, auto& D, const J& A, J& R, FString& E) {
			auto T = FTransform3D::FromJson(A.Get("Transform"), E);
			if (!T)
				return false;
			Require(D.SetActorTransform(A.Get("ActorId").AsString(), *T),
					"Actor has no editable native transform.");
			R = true;
			return true;
		});
	Add("Editor.SetEntityKey", true, {{"Key", EJsonType::String}, {"Value", EJsonType::String}},
		EJsonType::Null, [](auto&, auto& D, const J& A, J& R, auto&) {
			D.SetEntityKey(D.GetSelection(), A.Get("Key").AsString(), A.Get("Value").AsString());
			R = {};
			return true;
		});
	Add("Editor.SetTexture", true, {{"Texture", EJsonType::String}}, EJsonType::Null,
		[](auto&, auto& D, const J& A, J& R, auto&) {
			D.SetTexture(D.GetSelection(), A.Get("Texture").AsString());
			R = {};
			return true;
		});
	Add(
		"Editor.BeginTransaction", true, {{"Description", EJsonType::String}}, EJsonType::Null,
		[](auto& C, auto& D, const J& A, J& R, auto&) {
			Require(!D.IsInTransaction(), "Script transaction is already open.");
			D.BeginTransaction(A.Get("Description").AsString());
			C.TransactionDocument = &D;
			C.TransactionId = D.GetActiveTransactionId();
			R = {};
			return true;
		},
		false);
	Add(
		"Editor.EndTransaction", true, {}, EJsonType::Null,
		[](auto& C, auto& D, const J&, J& R, auto&) {
			Require(Owns(C, D), "No script transaction is open.");
			D.EndTransaction();
			C.TransactionDocument = nullptr;
			C.TransactionId = 0;
			R = {};
			return true;
		},
		false);
	Add(
		"Editor.CancelTransaction", true, {}, EJsonType::Null,
		[](auto& C, auto& D, const J&, J& R, auto&) {
			Require(Owns(C, D), "No script transaction is open.");
			CancelEditorScriptTransaction(C);
			R = {};
			return true;
		},
		false);
	Add("Editor.GetUndoState", false, {}, EJsonType::Object, [](auto&, auto& D, const J&, J& R, auto&) {
		R = J::MakeObject();
		R["CanUndo"] = D.CanUndo();
		R["CanRedo"] = D.CanRedo();
		R["Undo"] = D.GetUndoDescription();
		R["Redo"] = D.GetRedoDescription();
		R["InTransaction"] = D.IsInTransaction();
		return true;
	});
	Add(
		"Editor.Undo", true, {}, EJsonType::Null,
		[](auto&, auto& D, const J&, J& R, auto&) {
			Require(!D.IsInTransaction(), "End the script transaction before undo.");
			D.Undo();
			R = {};
			return true;
		},
		false);
	Add(
		"Editor.Redo", true, {}, EJsonType::Null,
		[](auto&, auto& D, const J&, J& R, auto&) {
			Require(!D.IsInTransaction(), "End the script transaction before redo.");
			D.Redo();
			R = {};
			return true;
		},
		false);
	Add("Landscape.Create", true,
		{{"QuadsX", EJsonType::Number},
		 {"QuadsY", EJsonType::Number},
		 {"QuadSize", EJsonType::Number},
		 {"Position", EJsonType::Array}},
		EJsonType::Object, [](auto&, auto& D, const J& A, J& R, auto&) {
			const auto X = Integer(A.Get("QuadsX"), 1, Landscape::MaxQuads),
					   Y = Integer(A.Get("QuadsY"), 1, Landscape::MaxQuads);
			const auto Size = Number(A.Get("QuadSize"), 1, 8192);
			auto* O = D.AddLandscape(X, Y, Size, Vector(A.Get("Position")));
			Require(O != nullptr, "Landscape creation failed.");
			R = Describe(*O);
			return true;
		});
	Add("Landscape.GetInfo", false, {{"ActorId", EJsonType::String}}, EJsonType::Object,
		[](auto&, auto& D, const J& A, J& R, auto&) {
			auto& O = Actor(D, A);
			Require(O.HasLandscape(), "Actor is not a landscape.");
			const auto& L = O.GetLandscape();
			R = J::MakeObject();
			R["QuadsX"] = L.GetQuadsX();
			R["QuadsY"] = L.GetQuadsY();
			R["QuadSize"] = L.GetQuadSize();
			R["MinHeight"] = L.GetMinHeight();
			R["MaxHeight"] = L.GetMaxHeight();
			R["Layers"] = L.GetNumLayers();
			return true;
		});
	Add("Landscape.GetHeights", false,
		{{"ActorId", EJsonType::String}, {"Offset", EJsonType::Number}, {"Count", EJsonType::Number}},
		EJsonType::Array, [](auto&, auto& D, const J& A, J& R, auto&) {
			auto& O = Actor(D, A);
			Require(O.HasLandscape(), "Actor is not a landscape.");
			const auto& H = O.GetLandscape().GetHeights();
			int Start = Integer(A.Get("Offset"), 0, static_cast<int>(H.size())),
				Count = Integer(A.Get("Count"), 0, 16384);
			Require(Start + Count <= static_cast<int>(H.size()), "Height range exceeds the grid.");
			R = J::MakeArray();
			for (int I = 0; I < Count; ++I)
				R.Push(H[Start + I]);
			return true;
		});
	Add("Landscape.SetHeights", true,
		{{"ActorId", EJsonType::String}, {"Offset", EJsonType::Number}, {"Heights", EJsonType::Array}},
		EJsonType::Null, [](auto&, auto& D, const J& A, J& R, auto&) {
			auto& O = Actor(D, A);
			Require(O.HasLandscape(), "Actor is not a landscape.");
			auto H = O.GetLandscape().GetHeights();
			int Start = Integer(A.Get("Offset"), 0, static_cast<int>(H.size()));
			const auto& Values = A.Get("Heights");
			Require(Values.Num() <= 16384 && Start + Values.Num() <= static_cast<int>(H.size()),
					"Height range exceeds the grid or chunk limit.");
			for (int I = 0; I < Values.Num(); ++I)
				H[Start + I] = Number(Values[I], -65536, 65536);
			auto New = O.GetLandscape().WithHeights(std::move(H));
			Require(New != nullptr, "Landscape update failed.");
			D.SetLandscape(O, std::move(New));
			R = {};
			return true;
		});

	Add("Landscape.ApplyBrush", true,
		{{"ActorId", EJsonType::String},
		 {"Tool", EJsonType::String},
		 {"Position", EJsonType::Array},
		 {"Radius", EJsonType::Number},
		 {"Strength", EJsonType::Number},
		 {"Falloff", EJsonType::Number},
		 {"Seconds", EJsonType::Number},
		 {"Invert", EJsonType::Bool, false},
		 {"Layer", EJsonType::Number, false},
		 {"Target", EJsonType::Number, false}},
		EJsonType::Bool, [](auto&, auto& D, const J& A, J& R, auto&) {
			auto& O = Actor(D, A);
			Require(O.HasLandscape(), "Actor is not a landscape.");
			FMapLandscapeMode Mode;
			Mode.SetDocument(&D);
			const auto Tool = A.Get("Tool").AsString();
			bool Found = false;
			for (auto T : {ELandscapeTool::Sculpt, ELandscapeTool::Smooth, ELandscapeTool::Flatten,
						   ELandscapeTool::Erosion, ELandscapeTool::Noise, ELandscapeTool::Paint})
				if (Tool == ToString(T))
				{
					Mode.Tool = T;
					Found = true;
					break;
				}
			Require(Found, "Unsupported brush tool.");
			Mode.Brush.Radius = Number(A.Get("Radius"), 8, 8192);
			Mode.Brush.Strength = Number(A.Get("Strength"), 0, 1);
			Mode.Brush.Falloff = Number(A.Get("Falloff"), 0, 1);
			if (A.Contains("Layer"))
				Mode.SelectedLayer = Integer(A.Get("Layer"), 0, O.GetLandscape().GetNumLayers() - 1);
			if (A.Contains("Target"))
			{
				Mode.bFlattenToTarget = true;
				Mode.FlattenTarget = Number(A.Get("Target"), -65536, 65536);
			}
			const auto P = Vector(A.Get("Position"));
			float Remaining = Number(A.Get("Seconds"), 1.0f / 60, 10);
			const auto Invert = A.Get("Invert").AsBool();
			Require(Mode.BeginStroke(O, P, Invert), "Cannot begin landscape stroke.");
			try
			{
				Remaining -= 1.0f / 60;
				while (Remaining > 0)
				{
					const auto Step = std::min(Remaining, 1.0f / 60);
					Mode.ApplyAt(O, P, Step, Invert);
					Remaining -= Step;
				}
				Mode.EndStroke();
			}
			catch (...)
			{
				Mode.EndStroke();
				throw;
			}
			R = true;
			return true;
		});
	Add("Landscape.AddLayer", true,
		{{"ActorId", EJsonType::String},
		 {"Name", EJsonType::String},
		 {"Color", EJsonType::Array},
		 {"Texture", EJsonType::String, false}},
		EJsonType::Number, [](auto&, auto& D, const J& A, J& R, auto&) {
			auto& O = Actor(D, A);
			Require(O.HasLandscape(), "Actor is not a landscape.");
			const auto& L = O.GetLandscape();
			Require(L.GetNumLayers() < 64, "Landscape layer limit reached.");
			auto Layers = L.GetLayers();
			auto Color = Vector(A.Get("Color"));
			const auto Byte = [](float V) {
				Require(V >= 0 && V <= 255 && std::floor(V) == V, "Layer color must contain integer bytes.");
				return static_cast<unsigned char>(V);
			};
			const auto Name = A.Get("Name").AsString();
			Require(!Name.IsEmpty(), "Layer name must not be empty.");
			for (const auto& Layer : Layers)
				Require(Layer.name != Name.ToAnsi(), "Layer name already exists.");
			auto Layer = L.MakeLayer(*Name, Byte(Color.GetX()), Byte(Color.GetY()), Byte(Color.GetZ()));
			Layer.texture = A.Get("Texture").AsString().ToAnsi();
			Layers.push_back(std::move(Layer));
			auto New = L.WithLayers(std::move(Layers));
			Require(New != nullptr, "Layer creation failed.");
			const int Index = L.GetNumLayers();
			D.SetLandscape(O, std::move(New), "Add Landscape Layer");
			R = Index;
			return true;
		});
	Add("Foliage.AddPreset", true, {{"Preset", EJsonType::String}}, EJsonType::Object,
		[](auto&, auto& D, const J& A, J& R, auto&) {
			const auto Name = A.Get("Preset").AsString();
			const auto& Names = FMapFoliageMode::GetPresetNames();
			Require(std::find(Names.begin(), Names.end(), Name) != Names.end(), "Unknown foliage preset.");
			FMapFoliageMode Mode;
			Mode.SetDocument(&D);
			Require(Mode.AddType(Name), "Foliage type creation failed.");
			auto* O = Mode.GetFoliageObject();
			Require(O != nullptr, "Foliage actor is missing.");
			R = Describe(*O);
			R["Type"] = O->GetFoliage().GetNumTypes() - 1;
			return true;
		});
	Add("Foliage.GetInfo", false, {{"ActorId", EJsonType::String}}, EJsonType::Object,
		[](auto&, auto& D, const J& A, J& R, auto&) {
			auto& O = Actor(D, A);
			Require(O.HasFoliage(), "Actor is not foliage.");
			R = J::MakeObject();
			R["Types"] = O.GetFoliage().GetNumTypes();
			R["Instances"] = O.GetFoliage().GetNumInstances();
			return true;
		});
	Add("Foliage.Place", true, {{"Type", EJsonType::Number}, {"Position", EJsonType::Array}}, EJsonType::Bool,
		[](auto&, auto& D, const J& A, J& R, auto&) {
			FMapFoliageMode Mode;
			Mode.SetDocument(&D);
			auto* O = Mode.GetFoliageObject();
			Require(O != nullptr, "Scope has no foliage actor.");
			const auto Type = Integer(A.Get("Type"), 0, O->GetFoliage().GetNumTypes() - 1);
			R = Mode.PlaceAt(Type, Vector(A.Get("Position")));
			return true;
		});
	Add("Foliage.ClearType", true, {{"Type", EJsonType::Number}}, EJsonType::Bool,
		[](auto&, auto& D, const J& A, J& R, auto&) {
			FMapFoliageMode Mode;
			Mode.SetDocument(&D);
			auto* O = Mode.GetFoliageObject();
			Require(O != nullptr, "Scope has no foliage actor.");
			R = Mode.ClearType(Integer(A.Get("Type"), 0, O->GetFoliage().GetNumTypes() - 1));
			return true;
		});
	Add("Foliage.ApplyBrush", true,
		{{"Types", EJsonType::Array},
		 {"Position", EJsonType::Array},
		 {"Radius", EJsonType::Number},
		 {"Strength", EJsonType::Number},
		 {"Falloff", EJsonType::Number},
		 {"Seconds", EJsonType::Number},
		 {"Erase", EJsonType::Bool}},
		EJsonType::Bool, [](auto&, auto& D, const J& A, J& R, auto&) {
			FMapFoliageMode Mode;
			Mode.SetDocument(&D);
			auto* O = Mode.GetFoliageObject();
			Require(O != nullptr, "Scope has no foliage actor.");
			const auto Count = O->GetFoliage().GetNumTypes();
			for (int I = 0; I < Count; ++I)
				Mode.SetActive(I, false);
			for (const auto& V : A.Get("Types").GetItems())
				Mode.SetActive(Integer(V, 0, Count - 1), true);
			Mode.Brush.Radius = Number(A.Get("Radius"), 8, 8192);
			Mode.Brush.Strength = Number(A.Get("Strength"), 0, 1);
			Mode.Brush.Falloff = Number(A.Get("Falloff"), 0, 1);
			const auto P = Vector(A.Get("Position"));
			float Remaining = Number(A.Get("Seconds"), 1.0f / 60, 10);
			const auto Erase = A.Get("Erase").AsBool();
			Require(Mode.BeginStroke(P, Erase), "Cannot begin foliage stroke.");
			try
			{
				Remaining -= 1.0f / 60;
				while (Remaining > 0)
				{
					const auto Step = std::min(Remaining, 1.0f / 60);
					Mode.ApplyAt(P, Step, Erase);
					Remaining -= Step;
				}
				Mode.EndStroke();
			}
			catch (...)
			{
				Mode.EndStroke();
				throw;
			}
			R = true;
			return true;
		});

	Add("Editor.RotateSelection", true,
		{{"Axis", EJsonType::Number}, {"Degrees", EJsonType::Number}, {"Pivot", EJsonType::Array}},
		EJsonType::Null, [](auto&, auto& D, const J& A, J& R, auto&) {
			D.RotateObjects(D.GetSelection(), Integer(A.Get("Axis"), 0, 2),
							Number(A.Get("Degrees"), -360000, 360000), Vector(A.Get("Pivot")));
			R = {};
			return true;
		});
	Add("Editor.ScaleSelection", true, {{"Factors", EJsonType::Array}, {"Pivot", EJsonType::Array}},
		EJsonType::Null, [](auto&, auto& D, const J& A, J& R, auto&) {
			const auto S = Vector(A.Get("Factors"));
			Require(std::abs(S.GetX()) >= 0.0001 && std::abs(S.GetY()) >= 0.0001 &&
						std::abs(S.GetZ()) >= 0.0001,
					"Scale factors must not be zero.");
			D.ScaleObjects(D.GetSelection(), S, Vector(A.Get("Pivot")));
			R = {};
			return true;
		});
	Add("Editor.FlipSelection", true, {{"Axis", EJsonType::Number}, {"Pivot", EJsonType::Array}},
		EJsonType::Null, [](auto&, auto& D, const J& A, J& R, auto&) {
			D.FlipObjects(D.GetSelection(), Integer(A.Get("Axis"), 0, 2), Vector(A.Get("Pivot")));
			R = {};
			return true;
		});
	Add("Editor.HollowSelection", true, {{"Thickness", EJsonType::Number}, {"Inward", EJsonType::Bool}},
		EJsonType::Bool, [](auto&, auto& D, const J& A, J& R, auto&) {
			R = D.HollowObjects(D.GetSelection(), Number(A.Get("Thickness"), 0.01f, 65536),
								A.Get("Inward").AsBool());
			return true;
		});
	Add("Editor.SubtractSelection", true, {}, EJsonType::Bool, [](auto&, auto& D, const J&, J& R, auto&) {
		R = D.SubtractSelection();
		return true;
	});
	Add("Editor.IntersectSelection", true, {}, EJsonType::Bool, [](auto&, auto& D, const J&, J& R, auto&) {
		R = D.IntersectSelection();
		return true;
	});
	Add("Editor.ClipSelection", true,
		{{"Normal", EJsonType::Array}, {"Point", EJsonType::Array}, {"Mode", EJsonType::String}},
		EJsonType::Bool, [](auto&, auto& D, const J& A, J& R, auto&) {
			auto N = Vector(A.Get("Normal"));
			Require(N.GetX() * N.GetX() + N.GetY() * N.GetY() + N.GetZ() * N.GetZ() > 0.000001,
					"Plane normal must not be zero.");
			const auto Name = A.Get("Mode").AsString();
			Require(Name == "Front" || Name == "Back" || Name == "Split",
					"Clip mode must be Front, Back or Split.");
			R = D.ClipObjects(D.GetSelection(), Plane(N, Vector(A.Get("Point"))),
							  Name == "Front"  ? EMapClip::KeepFront
							  : Name == "Back" ? EMapClip::KeepBack
											   : EMapClip::Split);
			return true;
		});

	Add("Editor.AddMeshAsset", true, {{"Reference", EJsonType::Object}, {"Position", EJsonType::Array}},
		EJsonType::Object, [](auto&, auto& D, const J& A, J& R, FString& E) {
			auto Ref = FAssetReference::FromJson(A.Get("Reference"), E);
			if (!Ref)
				return false;
			auto* O = D.AddMeshAsset(*Ref, Vector(A.Get("Position")), E);
			if (!O)
				return false;
			R = Describe(*O);
			return true;
		});
	Add("Editor.SetMeshMaterial", true,
		{{"ActorId", EJsonType::String},
		 {"Reference", EJsonType::Object},
		 {"Slot", EJsonType::Number, false}},
		EJsonType::Bool, [](auto&, auto& D, const J& A, J& R, FString& E) {
			auto Ref = FAssetReference::FromJson(A.Get("Reference"), E);
			if (!Ref)
				return false;
			const auto Slot = A.Contains("Slot") ? Integer(A.Get("Slot"), 0, 63) : 0;
			if (!D.SetMeshMaterial(Actor(D, A), *Ref, E, Slot))
				return false;
			R = true;
			return true;
		});
	Add("Editor.AddPrefab", true,
		{{"Path", EJsonType::String},
		 {"Position", EJsonType::Array},
		 {"ExpectedId", EJsonType::String, false}},
		EJsonType::Object, [](auto&, auto& D, const J& A, J& R, FString& E) {
			auto* O = D.AddPrefab(A.Get("Path").AsString(), Vector(A.Get("Position")), &E,
								  A.Get("ExpectedId").AsString());
			if (!O)
				return false;
			R = Describe(*O);
			return true;
		});
	Add("Editor.GetCommands", false, {}, EJsonType::Array, [](auto& C, auto&, const J&, J& R, auto&) {
		const auto Commands = C.GetCommands ? C.GetCommands() : nullptr;
		Require(Commands != nullptr, "Editor command host is unavailable.");
		R = J::MakeArray();
		for (const auto& Context : FInputBindingManager::Get().GetContexts())
			for (const auto& Command : Context->GetCommands())
				if (Commands->IsActionMapped(Command))
				{
					auto V = J::MakeObject();
					V["Context"] = Command->GetContext().ToString();
					V["Name"] = Command->GetName().ToString();
					V["Label"] = Command->GetLabel();
					V["Description"] = Command->GetDescription();
					V["CanExecute"] = Commands->CanExecuteAction(Command.Get());
					V["Visible"] = Commands->IsVisible(Command.Get());
					R.Push(std::move(V));
				}
		return true;
	});
	Batch.push_back({"Editor.ExecuteCommand",
					 "Editor.Commands",
					 true,
					 {{"Context", EJsonType::String}, {"Name", EJsonType::String}},
					 EJsonType::Bool,
					 [Weak](const J& A, J& R, FString&) {
						 auto C = Context(Weak);
						 Require(!C->GetDocument()->IsInTransaction(),
								 "End the transaction before executing editor commands.");
						 const auto Commands = C->GetCommands ? C->GetCommands() : nullptr;
						 Require(Commands != nullptr, "Editor command host is unavailable.");
						 const auto Context =
							 FInputBindingManager::Get().FindContext(FName(*A.Get("Context").AsString()));
						 Require(Context != nullptr, "Command context is missing.");
						 const auto Command = Context->FindCommand(FName(*A.Get("Name").AsString()));
						 Require(Command != nullptr && Commands->IsActionMapped(ToSharedRef(Command)),
								 "Editor command is not mapped.");
						 Require(Commands->CanExecuteAction(*Command) && Commands->IsVisible(*Command),
								 "Editor command is disabled or hidden.");
						 R = Commands->ExecuteAction(*Command);
						 return true;
					 }});
	Batch.push_back({"Editor.Save",
					 "Editor.Files",
					 true,
					 {{"Path", EJsonType::String}},
					 EJsonType::Bool,
					 [Weak](const J& A, J& R, FString& E) {
						 auto C = Context(Weak);
						 auto& D = *C->GetDocument();
						 Require(!D.IsInTransaction(), "End the transaction before saving.");
						 if (!D.Save(A.Get("Path").AsString(), E))
							 return false;
						 R = true;
						 return true;
					 }});
	return Registry.RegisterBatch(std::move(Batch), Error);
}
