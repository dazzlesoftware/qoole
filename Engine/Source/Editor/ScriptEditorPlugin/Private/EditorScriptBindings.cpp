// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "EditorScriptBindings.h"
#include "Containers/ContainerAliases.h"
#include "Templates/SharedPointer.h"
#include "Misc/ScopeExit.h"
#include "Internationalization/TextLocalization.h"
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
#include "Misc/ValidationContext.h"
namespace
{
	using J = FJsonValue;
	float Number(FValidationContext& Validation, const J& V, float Min = -10000000, float Max = 10000000)
	{
		const double N = V.AsNumber();
		if (!Validation.Check(V.GetType() == EJsonType::Number && std::isfinite(N) && N >= Min && N <= Max,
		        GENESIS_TEXT("Number is outside its supported range.")))
		{
			return {};
		}
		return static_cast<float>(N);
	}
	int Integer(FValidationContext& Validation, const J& V, int Min, int Max)
	{
		const double N = V.AsNumber();
		if (!Validation.Check(
		        V.GetType() == EJsonType::Number && std::isfinite(N) && N >= Min && N <= Max && std::floor(N) == N,
		        GENESIS_TEXT("Expected an integer in its supported range.")))
		{
			return {};
		}
		return static_cast<int>(N);
	}

	Vector3d Vector(FValidationContext& Validation, const J& V)
	{
		if (!Validation.Check(V.IsArray() && V.Num() == 3, GENESIS_TEXT("Expected a three-number vector.")))
		{
			return {};
		}
		return Vector3d(Number(Validation, V[0]), Number(Validation, V[1]), Number(Validation, V[2]));
	}
	Object* Find(Object& Root, const FString& Id)
	{
		if (Root.GetIdentity().Actor == Id)
			return &Root;
		for (int I = 0; I < Root.GetNumChildren(); ++I)
			if (Object* Found = Find(Root.GetChild(I), Id))
				return Found;
		return nullptr;
	}
	Object* Actor(FValidationContext& Validation, FMapDocument& D, const J& A)
	{
		Object* O = Find(D.GetRoot(), A.Get("ActorId").AsString());
		if (!Validation.Check(O != nullptr, GENESIS_TEXT("Editor actor is missing.")))
		{
			return {};
		}
		return O;
	}
	J Describe(Object& O)
	{
		FJsonValue R = J::MakeObject();
		R["ActorId"] = O.GetIdentity().Actor;
		R["Name"] = O.GetObjName();
		R["Children"] = O.GetNumChildren();
		R["Kind"] = O.HasLandscape() ? "Landscape"
		    : O.HasFoliage()         ? "Foliage"
		    : O.HasMesh()            ? "Mesh"
		    : O.HasBrush()           ? "Brush"
		    : O.HasEntity()          ? "Entity"
		                             : "Group";
		return R;
	}
	J Ids(const TArray<FString>& Values)
	{
		FJsonValue R = J::MakeArray();
		for (const FString& V : Values)
			R.Push(V);
		return R;
	}
	std::shared_ptr<FEditorScriptContext> Context(
	    FValidationContext& Validation, const std::weak_ptr<FEditorScriptContext>& W)
	{
		TSharedPtr<FEditorScriptContext> C = W.lock();
		if (!Validation.Check(
		        C && C->GetDocument && C->GetDocument(), GENESIS_TEXT("Editor script document has expired.")))
		{
			return {};
		}
		return C;
	}
	bool Owns(const FEditorScriptContext& C, FMapDocument& D)
	{
		return C.TransactionDocument == &D && C.TransactionId != 0 && C.TransactionId == D.GetActiveTransactionId();
	}
} // namespace
void CancelEditorScriptTransaction(FEditorScriptContext& C)
{
	FMapDocument* D = C.GetDocument ? C.GetDocument() : nullptr;
	if (D && Owns(C, *D))
		D->CancelTransaction();
	C.TransactionDocument = nullptr;
	C.TransactionId = 0;
}
using FEditorBindingCallback = TFunction<bool(FEditorScriptContext&, FMapDocument&, const J&, J&, FString&)>;
void AddEditorBinding(TArray<FScriptBinding>& Batch, const std::weak_ptr<FEditorScriptContext>& Weak, const char* Name,
    bool Write, TArray<FScriptArgument> Args, EJsonType Type, const FEditorBindingCallback& Fn,
    bool Transactional = true)
{
	Batch.push_back({Name, Write ? "Editor.Write" : "Editor.Read", true, std::move(Args), Type,
	    [Weak, Write, Fn, Transactional, Name](const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		std::shared_ptr<FEditorScriptContext> C = Context(Validation, Weak);
		if (!Validation.IsValid())
		{
			return false;
		}
		FMapDocument& D = *C->GetDocument();
		const bool Edit = Write && Transactional;
		if (Write && D.IsInTransaction() && !Owns(*C, D))
		{
			E = GENESIS_TEXT("An editor transaction is already owned by another operation.");
			return false;
		}
		const bool Begin = Edit && !D.IsInTransaction();
		if (Begin)
		{
			D.BeginTransaction(Name);
			C->TransactionDocument = &D;
			C->TransactionId = D.GetActiveTransactionId();
		}
		FScopeExit Rollback([C, Edit]
		{
			if (Edit)
			{
				CancelEditorScriptTransaction(*C);
			}
		});
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
			E = GENESIS_TEXT("Native operation canceled the script transaction; its edits were rolled back.");
			return false;
		}
		if (Begin && Owns(*C, D))
		{
			D.EndTransaction();
			C->TransactionDocument = nullptr;
			C->TransactionId = 0;
		}
		Rollback.Release();
		return true;
	}});
}
bool RegisterEditorScriptBindings(
    FScriptBindingRegistry& Registry, std::weak_ptr<FEditorScriptContext> Weak, FString& Error)
{
	TArray<FScriptBinding> Batch;
	AddEditorBinding(Batch, Weak, "Editor.GetDocument", false, {}, EJsonType::Object,
	    [](FEditorScriptContext&, FMapDocument& D, const J&, J& R, FString&) -> bool
	{
		R = J::MakeObject();
		R["Path"] = D.GetPath();
		R["Modified"] = D.IsModified();
		R["Scope"] = D.GetScope().GetIdentity().Actor;
		R["Brushes"] = D.CountBrushes();
		R["Entities"] = D.CountEntities();
		R["Meshes"] = D.CountMeshes();
		return true;
	});
	AddEditorBinding(Batch, Weak, "Editor.GetObjects", false, {}, EJsonType::Array,
	    [](FEditorScriptContext&, FMapDocument& D, const J&, J& R, FString&) -> bool
	{
		R = J::MakeArray();
		for (Object* O : D.GetObjects())
			R.Push(Describe(*O));
		return true;
	});
	AddEditorBinding(Batch, Weak, "Editor.FindActor", false, {{"ActorId", EJsonType::String}}, EJsonType::Object,
	    [](FEditorScriptContext&, FMapDocument& D, const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		Object* ActorObject = Actor(Validation, D, A);
		if (!Validation.IsValid())
		{
			return false;
		}
		R = Describe((*ActorObject));
		return true;
	});
	AddEditorBinding(Batch, Weak, "Editor.GetSelection", false, {}, EJsonType::Array,
	    [](FEditorScriptContext&, FMapDocument& D, const J&, J& R, FString&) -> bool
	{
		R = Ids(D.GetSelectedActorIds());
		return true;
	});
	AddEditorBinding(Batch, Weak, "Editor.SetSelection", true, {{"ActorIds", EJsonType::Array}}, EJsonType::Null,
	    [](FEditorScriptContext&, FMapDocument& D, const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		TArray<FString> Values;
		for (const FJsonValue& V : A.Get("ActorIds").GetItems())
		{
			if (!Validation.Check(V.GetType() == EJsonType::String, GENESIS_TEXT("ActorIds must contain strings.")))
			{
				return false;
			}

			Object* O = Find(D.GetRoot(), V.AsString());
			if (!Validation.Check(O != nullptr, GENESIS_TEXT("Selection actor is missing.")))
			{
				return false;
			}

			Values.push_back(V.AsString());
		}
		D.SetSelectedActorIds(Values);
		R = {};
		return true;
	}, false);
	AddEditorBinding(Batch, Weak, "Editor.SetScope", true, {{"ActorId", EJsonType::String}}, EJsonType::Bool,
	    [](FEditorScriptContext&, FMapDocument& D, const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		Object* ActorObject = Actor(Validation, D, A);
		if (!Validation.IsValid())
		{
			return false;
		}
		R = D.SetScope((*ActorObject));
		return true;
	}, false);
	AddEditorBinding(Batch, Weak, "Editor.ScopeUp", true, {}, EJsonType::Null,
	    [](FEditorScriptContext&, FMapDocument& D, const J&, J& R, FString&) -> bool
	{
		D.ScopeUp();
		R = {};
		return true;
	}, false);
	AddEditorBinding(Batch, Weak, "Editor.SetName", true, {{"ActorId", EJsonType::String}, {"Name", EJsonType::String}},
	    EJsonType::Null, [](FEditorScriptContext&, FMapDocument& D, const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		Object* ActorObject = Actor(Validation, D, A);
		if (!Validation.IsValid())
		{
			return false;
		}
		D.SetObjectName((*ActorObject), A.Get("Name").AsString());
		R = {};
		return true;
	});
	AddEditorBinding(Batch, Weak, "Editor.AddMesh", true, {{"Mesh", EJsonType::String}, {"Position", EJsonType::Array}},
	    EJsonType::Object, [](FEditorScriptContext&, FMapDocument& D, const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		const Vector3d Position = Vector(Validation, A.Get("Position"));
		if (!Validation.IsValid())
		{
			return false;
		}
		Object* O = D.AddMesh(A.Get("Mesh").AsString(), Position);

		if (!Validation.Check(O != nullptr, GENESIS_TEXT("Mesh is missing.")))
		{
			return false;
		}

		R = Describe(*O);
		return true;
	});
	AddEditorBinding(Batch, Weak, "Editor.AddEntity", true,
	    {{"Class", EJsonType::String}, {"Position", EJsonType::Array}}, EJsonType::Object,
	    [](FEditorScriptContext&, FMapDocument& D, const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		const Vector3d Position = Vector(Validation, A.Get("Position"));
		if (!Validation.IsValid())
		{
			return false;
		}
		Object* O = D.AddEntity(A.Get("Class").AsString(), Position);

		if (!Validation.Check(O != nullptr, GENESIS_TEXT("Entity class is unavailable.")))
		{
			return false;
		}

		R = Describe(*O);
		return true;
	});
	AddEditorBinding(Batch, Weak, "Editor.MoveSelection", true, {{"Delta", EJsonType::Array}}, EJsonType::Null,
	    [](FEditorScriptContext&, FMapDocument& D, const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		const Vector3d Delta = Vector(Validation, A.Get("Delta"));
		if (!Validation.IsValid())
		{
			return false;
		}
		D.MoveObjects(D.GetSelection(), Delta);
		if (!Validation.IsValid())
		{
			return false;
		}
		R = {};
		return true;
	});
	AddEditorBinding(Batch, Weak, "Editor.DuplicateSelection", true, {{"Delta", EJsonType::Array}}, EJsonType::Array,
	    [](FEditorScriptContext&, FMapDocument& D, const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		const Vector3d Delta = Vector(Validation, A.Get("Delta"));
		if (!Validation.IsValid())
		{
			return false;
		}
		TArray<Object*> Objects = D.DuplicateObjects(D.GetSelection(), Delta);

		R = J::MakeArray();
		for (Object* O : Objects)
			R.Push(Describe(*O));
		return true;
	});
	AddEditorBinding(Batch, Weak, "Editor.DeleteSelection", true, {}, EJsonType::Null,
	    [](FEditorScriptContext&, FMapDocument& D, const J&, J& R, FString&) -> bool
	{
		D.DeleteObjects(D.GetSelection());
		R = {};
		return true;
	});
	AddEditorBinding(Batch, Weak, "Editor.GroupSelection", true, {}, EJsonType::Object,
	    [](FEditorScriptContext&, FMapDocument& D, const J&, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		Object* O = D.GroupObjects(D.GetSelection());
		if (!Validation.Check(O != nullptr, GENESIS_TEXT("Selection cannot be grouped.")))
		{
			return false;
		}

		R = Describe(*O);
		return true;
	});
	AddEditorBinding(Batch, Weak, "Editor.UngroupSelection", true, {}, EJsonType::Bool,
	    [](FEditorScriptContext&, FMapDocument& D, const J&, J& R, FString&) -> bool
	{
		R = D.UngroupObjects(D.GetSelection());
		return true;
	});
	AddEditorBinding(Batch, Weak, "Editor.GetActorTransform", false, {{"ActorId", EJsonType::String}},
	    EJsonType::Object, [](FEditorScriptContext&, FMapDocument& D, const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		FTransform3D T;
		if (!Validation.Check(
		        D.GetActorTransform(A.Get("ActorId").AsString(), T), GENESIS_TEXT("Actor has no native transform.")))
		{
			return false;
		}

		R = T.ToJson();
		return true;
	});
	AddEditorBinding(Batch, Weak, "Editor.SetActorTransform", true,
	    {{"ActorId", EJsonType::String}, {"Transform", EJsonType::Object}}, EJsonType::Bool,
	    [](FEditorScriptContext&, FMapDocument& D, const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		TOptional<FTransform3D> T = FTransform3D::FromJson(A.Get("Transform"), E);
		if (!T)
			return false;
		if (!Validation.Check(D.SetActorTransform(A.Get("ActorId").AsString(), *T),
		        GENESIS_TEXT("Actor has no editable native transform.")))
		{
			return false;
		}

		R = true;
		return true;
	});
	AddEditorBinding(Batch, Weak, "Editor.SetEntityKey", true,
	    {{"Key", EJsonType::String}, {"Value", EJsonType::String}}, EJsonType::Null,
	    [](FEditorScriptContext&, FMapDocument& D, const J& A, J& R, FString&) -> bool
	{
		D.SetEntityKey(D.GetSelection(), A.Get("Key").AsString(), A.Get("Value").AsString());
		R = {};
		return true;
	});
	AddEditorBinding(Batch, Weak, "Editor.SetTexture", true, {{"Texture", EJsonType::String}}, EJsonType::Null,
	    [](FEditorScriptContext&, FMapDocument& D, const J& A, J& R, FString&) -> bool
	{
		D.SetTexture(D.GetSelection(), A.Get("Texture").AsString());
		R = {};
		return true;
	});
	AddEditorBinding(Batch, Weak, "Editor.BeginTransaction", true, {{"Description", EJsonType::String}},
	    EJsonType::Null, [](FEditorScriptContext& C, FMapDocument& D, const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		if (!Validation.Check(!D.IsInTransaction(), GENESIS_TEXT("Script transaction is already open.")))
		{
			return false;
		}

		D.BeginTransaction(A.Get("Description").AsString());
		C.TransactionDocument = &D;
		C.TransactionId = D.GetActiveTransactionId();
		R = {};
		return true;
	}, false);
	AddEditorBinding(Batch, Weak, "Editor.EndTransaction", true, {}, EJsonType::Null,
	    [](FEditorScriptContext& C, FMapDocument& D, const J&, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		if (!Validation.Check(Owns(C, D), GENESIS_TEXT("No script transaction is open.")))
		{
			return false;
		}

		D.EndTransaction();
		C.TransactionDocument = nullptr;
		C.TransactionId = 0;
		R = {};
		return true;
	}, false);
	AddEditorBinding(Batch, Weak, "Editor.CancelTransaction", true, {}, EJsonType::Null,
	    [](FEditorScriptContext& C, FMapDocument& D, const J&, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		if (!Validation.Check(Owns(C, D), GENESIS_TEXT("No script transaction is open.")))
		{
			return false;
		}

		CancelEditorScriptTransaction(C);
		R = {};
		return true;
	}, false);
	AddEditorBinding(Batch, Weak, "Editor.GetUndoState", false, {}, EJsonType::Object,
	    [](FEditorScriptContext&, FMapDocument& D, const J&, J& R, FString&) -> bool
	{
		R = J::MakeObject();
		R["CanUndo"] = D.CanUndo();
		R["CanRedo"] = D.CanRedo();
		R["Undo"] = D.GetUndoDescription();
		R["Redo"] = D.GetRedoDescription();
		R["InTransaction"] = D.IsInTransaction();
		return true;
	});
	AddEditorBinding(Batch, Weak, "Editor.Undo", true, {}, EJsonType::Null,
	    [](FEditorScriptContext&, FMapDocument& D, const J&, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		if (!Validation.Check(!D.IsInTransaction(), GENESIS_TEXT("End the script transaction before undo.")))
		{
			return false;
		}

		D.Undo();
		R = {};
		return true;
	}, false);
	AddEditorBinding(Batch, Weak, "Editor.Redo", true, {}, EJsonType::Null,
	    [](FEditorScriptContext&, FMapDocument& D, const J&, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		if (!Validation.Check(!D.IsInTransaction(), GENESIS_TEXT("End the script transaction before redo.")))
		{
			return false;
		}

		D.Redo();
		R = {};
		return true;
	}, false);
	AddEditorBinding(Batch, Weak, "Landscape.Create", true,
	    {{"QuadsX", EJsonType::Number}, {"QuadsY", EJsonType::Number}, {"QuadSize", EJsonType::Number},
	        {"Position", EJsonType::Array}},
	    EJsonType::Object, [](FEditorScriptContext&, FMapDocument& D, const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		const int X = Integer(Validation, A.Get("QuadsX"), 1, Landscape::MaxQuads),
		          Y = Integer(Validation, A.Get("QuadsY"), 1, Landscape::MaxQuads);
		if (!Validation.IsValid())
		{
			return false;
		}
		const float Size = Number(Validation, A.Get("QuadSize"), 1, 8192);
		if (!Validation.IsValid())
		{
			return false;
		}
		const Vector3d Position = Vector(Validation, A.Get("Position"));
		if (!Validation.IsValid())
		{
			return false;
		}
		Object* O = D.AddLandscape(X, Y, Size, Position);

		if (!Validation.Check(O != nullptr, GENESIS_TEXT("Landscape creation failed.")))
		{
			return false;
		}

		R = Describe(*O);
		return true;
	});
	AddEditorBinding(Batch, Weak, "Landscape.GetInfo", false, {{"ActorId", EJsonType::String}}, EJsonType::Object,
	    [](FEditorScriptContext&, FMapDocument& D, const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		Object* ActorObject = Actor(Validation, D, A);
		if (!Validation.IsValid())
		{
			return false;
		}
		Object& O = (*ActorObject);
		if (!Validation.Check(O.HasLandscape(), GENESIS_TEXT("Actor is not a landscape.")))
		{
			return false;
		}

		const Landscape& L = O.GetLandscape();
		R = J::MakeObject();
		R["QuadsX"] = L.GetQuadsX();
		R["QuadsY"] = L.GetQuadsY();
		R["QuadSize"] = L.GetQuadSize();
		R["MinHeight"] = L.GetMinHeight();
		R["MaxHeight"] = L.GetMaxHeight();
		R["Layers"] = L.GetNumLayers();
		return true;
	});
	AddEditorBinding(Batch, Weak, "Landscape.GetHeights", false,
	    {{"ActorId", EJsonType::String}, {"Offset", EJsonType::Number}, {"Count", EJsonType::Number}}, EJsonType::Array,
	    [](FEditorScriptContext&, FMapDocument& D, const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		Object* ActorObject = Actor(Validation, D, A);
		if (!Validation.IsValid())
		{
			return false;
		}
		Object& O = (*ActorObject);
		if (!Validation.Check(O.HasLandscape(), GENESIS_TEXT("Actor is not a landscape.")))
		{
			return false;
		}

		const std::vector<float>& H = O.GetLandscape().GetHeights();
		int Start = Integer(Validation, A.Get("Offset"), 0, static_cast<int>(H.size())),
		    Count = Integer(Validation, A.Get("Count"), 0, 16384);
		if (!Validation.IsValid())
		{
			return false;
		}
		if (!Validation.Check(
		        Start + Count <= static_cast<int>(H.size()), GENESIS_TEXT("Height range exceeds the grid.")))
		{
			return false;
		}

		R = J::MakeArray();
		for (int I = 0; I < Count; ++I)
			R.Push(H[Start + I]);
		return true;
	});
	AddEditorBinding(Batch, Weak, "Landscape.SetHeights", true,
	    {{"ActorId", EJsonType::String}, {"Offset", EJsonType::Number}, {"Heights", EJsonType::Array}}, EJsonType::Null,
	    [](FEditorScriptContext&, FMapDocument& D, const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		Object* ActorObject = Actor(Validation, D, A);
		if (!Validation.IsValid())
		{
			return false;
		}
		Object& O = (*ActorObject);
		if (!Validation.Check(O.HasLandscape(), GENESIS_TEXT("Actor is not a landscape.")))
		{
			return false;
		}

		std::vector<float> H = O.GetLandscape().GetHeights();
		int Start = Integer(Validation, A.Get("Offset"), 0, static_cast<int>(H.size()));
		if (!Validation.IsValid())
		{
			return false;
		}
		const FJsonValue& Values = A.Get("Heights");
		if (!Validation.Check(Values.Num() <= 16384 && Start + Values.Num() <= static_cast<int>(H.size()),
		        GENESIS_TEXT("Height range exceeds the grid or chunk limit.")))
		{
			return false;
		}

		for (int I = 0; I < Values.Num(); ++I)
		{
			H[Start + I] = Number(Validation, Values[I], -65536, 65536);
			if (!Validation.IsValid())
			{
				return false;
			}
		}
		if (!Validation.IsValid())
		{
			return false;
		}
		std::shared_ptr<const Landscape> New = O.GetLandscape().WithHeights(std::move(H));
		if (!Validation.Check(New != nullptr, GENESIS_TEXT("Landscape update failed.")))
		{
			return false;
		}

		D.SetLandscape(O, std::move(New));
		R = {};
		return true;
	});
	AddEditorBinding(Batch, Weak, "Landscape.ApplyBrush", true,
	    {{"ActorId", EJsonType::String}, {"Tool", EJsonType::String}, {"Position", EJsonType::Array},
	        {"Radius", EJsonType::Number}, {"Strength", EJsonType::Number}, {"Falloff", EJsonType::Number},
	        {"Seconds", EJsonType::Number}, {"Invert", EJsonType::Bool, false}, {"Layer", EJsonType::Number, false},
	        {"Target", EJsonType::Number, false}},
	    EJsonType::Bool, [](FEditorScriptContext&, FMapDocument& D, const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		Object* ActorObject = Actor(Validation, D, A);
		if (!Validation.IsValid())
		{
			return false;
		}
		Object& O = (*ActorObject);
		if (!Validation.Check(O.HasLandscape(), GENESIS_TEXT("Actor is not a landscape.")))
		{
			return false;
		}

		FMapLandscapeMode Mode;
		Mode.SetDocument(&D);
		const FString Tool = A.Get("Tool").AsString();
		bool Found = false;
		for (ELandscapeTool T : {ELandscapeTool::Sculpt, ELandscapeTool::Smooth, ELandscapeTool::Flatten,
		         ELandscapeTool::Erosion, ELandscapeTool::Noise, ELandscapeTool::Paint})
			if (Tool == ToString(T))
			{
				Mode.Tool = T;
				Found = true;
				break;
			}
		if (!Validation.Check(Found, GENESIS_TEXT("Unsupported brush tool.")))
		{
			return false;
		}

		Mode.Brush.Radius = Number(Validation, A.Get("Radius"), 8, 8192);
		if (!Validation.IsValid())
		{
			return false;
		}
		Mode.Brush.Strength = Number(Validation, A.Get("Strength"), 0, 1);
		if (!Validation.IsValid())
		{
			return false;
		}
		Mode.Brush.Falloff = Number(Validation, A.Get("Falloff"), 0, 1);
		if (!Validation.IsValid())
		{
			return false;
		}
		if (A.Contains("Layer"))
			Mode.SelectedLayer = Integer(Validation, A.Get("Layer"), 0, O.GetLandscape().GetNumLayers() - 1);
		if (!Validation.IsValid())
		{
			return false;
		}
		if (A.Contains("Target"))
		{
			Mode.bFlattenToTarget = true;
			Mode.FlattenTarget = Number(Validation, A.Get("Target"), -65536, 65536);
			if (!Validation.IsValid())
			{
				return false;
			}
		}
		const Vector3d P = Vector(Validation, A.Get("Position"));
		if (!Validation.IsValid())
		{
			return false;
		}
		float Remaining = Number(Validation, A.Get("Seconds"), 1.0f / 60, 10);
		if (!Validation.IsValid())
		{
			return false;
		}
		const bool Invert = A.Get("Invert").AsBool();
		if (!Validation.Check(Mode.BeginStroke(O, P, Invert), GENESIS_TEXT("Cannot begin landscape stroke.")))
		{
			return false;
		}

		FScopeExit EndStroke([&Mode] { Mode.EndStroke(); });
		Remaining -= 1.0f / 60;
		while (Remaining > 0)
		{
			float const Step = std::min(Remaining, 1.0f / 60);
			Mode.ApplyAt(O, P, Step, Invert);
			Remaining -= Step;
		}
		R = true;
		return true;
	});
	AddEditorBinding(Batch, Weak, "Landscape.AddLayer", true,
	    {{"ActorId", EJsonType::String}, {"Name", EJsonType::String}, {"Color", EJsonType::Array},
	        {"Texture", EJsonType::String, false}},
	    EJsonType::Number, [](FEditorScriptContext&, FMapDocument& D, const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		Object* ActorObject = Actor(Validation, D, A);
		if (!Validation.IsValid())
		{
			return false;
		}
		Object& O = (*ActorObject);
		if (!Validation.Check(O.HasLandscape(), GENESIS_TEXT("Actor is not a landscape.")))
		{
			return false;
		}

		const Landscape& L = O.GetLandscape();
		if (!Validation.Check(L.GetNumLayers() < 64, GENESIS_TEXT("Landscape layer limit reached.")))
		{
			return false;
		}

		std::vector<LandscapeLayer> Layers = L.GetLayers();
		Vector3d Color = Vector(Validation, A.Get("Color"));
		if (!Validation.IsValid())
		{
			return false;
		}
		const TFunction<unsigned char(float)> Byte = [&Validation](float V) -> unsigned char
		{
			if (!Validation.Check(
			        V >= 0 && V <= 255 && std::floor(V) == V, GENESIS_TEXT("Layer color must contain integer bytes.")))
			{
				return {};
			}
			return static_cast<unsigned char>(V);
		};
		const FString Name = A.Get("Name").AsString();
		if (!Validation.Check(!Name.IsEmpty(), GENESIS_TEXT("Layer name must not be empty.")))
		{
			return false;
		}

		for (const LandscapeLayer& Layer : Layers)
		{
			if (!Validation.Check(Layer.name != Name.ToAnsi(), GENESIS_TEXT("Layer name already exists.")))
			{
				return false;
			}
		}
		LandscapeLayer Layer = L.MakeLayer(*Name, Byte(Color.GetX()), Byte(Color.GetY()), Byte(Color.GetZ()));
		if (!Validation.IsValid())
		{
			return false;
		}
		Layer.texture = A.Get("Texture").AsString().ToAnsi();
		Layers.push_back(std::move(Layer));
		std::shared_ptr<const Landscape> New = L.WithLayers(std::move(Layers));
		if (!Validation.Check(New != nullptr, GENESIS_TEXT("Layer creation failed.")))
		{
			return false;
		}

		const int Index = L.GetNumLayers();
		D.SetLandscape(O, std::move(New), "Add Landscape Layer");
		R = Index;
		return true;
	});
	AddEditorBinding(Batch, Weak, "Foliage.AddPreset", true, {{"Preset", EJsonType::String}}, EJsonType::Object,
	    [](FEditorScriptContext&, FMapDocument& D, const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		const FString Name = A.Get("Preset").AsString();
		const TArray<FString>& Names = FMapFoliageMode::GetPresetNames();
		if (!Validation.Check(
		        std::find(Names.begin(), Names.end(), Name) != Names.end(), GENESIS_TEXT("Unknown foliage preset.")))
		{
			return false;
		}

		FMapFoliageMode Mode;
		Mode.SetDocument(&D);
		if (!Validation.Check(Mode.AddType(Name), GENESIS_TEXT("Foliage type creation failed.")))
		{
			return false;
		}

		Object* O = Mode.GetFoliageObject();
		if (!Validation.Check(O != nullptr, GENESIS_TEXT("Foliage actor is missing.")))
		{
			return false;
		}

		R = Describe(*O);
		R["Type"] = O->GetFoliage().GetNumTypes() - 1;
		return true;
	});
	AddEditorBinding(Batch, Weak, "Foliage.GetInfo", false, {{"ActorId", EJsonType::String}}, EJsonType::Object,
	    [](FEditorScriptContext&, FMapDocument& D, const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		Object* ActorObject = Actor(Validation, D, A);
		if (!Validation.IsValid())
		{
			return false;
		}
		Object& O = (*ActorObject);
		if (!Validation.Check(O.HasFoliage(), GENESIS_TEXT("Actor is not foliage.")))
		{
			return false;
		}

		R = J::MakeObject();
		R["Types"] = O.GetFoliage().GetNumTypes();
		R["Instances"] = O.GetFoliage().GetNumInstances();
		return true;
	});
	AddEditorBinding(Batch, Weak, "Foliage.Place", true, {{"Type", EJsonType::Number}, {"Position", EJsonType::Array}},
	    EJsonType::Bool, [](FEditorScriptContext&, FMapDocument& D, const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		FMapFoliageMode Mode;
		Mode.SetDocument(&D);
		Object* O = Mode.GetFoliageObject();
		if (!Validation.Check(O != nullptr, GENESIS_TEXT("Scope has no foliage actor.")))
		{
			return false;
		}

		const int Type = Integer(Validation, A.Get("Type"), 0, O->GetFoliage().GetNumTypes() - 1);
		if (!Validation.IsValid())
		{
			return false;
		}
		const Vector3d Position = Vector(Validation, A.Get("Position"));
		if (!Validation.IsValid())
		{
			return false;
		}
		R = Mode.PlaceAt(Type, Position);
		if (!Validation.IsValid())
		{
			return false;
		}
		return true;
	});
	AddEditorBinding(Batch, Weak, "Foliage.ClearType", true, {{"Type", EJsonType::Number}}, EJsonType::Bool,
	    [](FEditorScriptContext&, FMapDocument& D, const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		FMapFoliageMode Mode;
		Mode.SetDocument(&D);
		Object* O = Mode.GetFoliageObject();
		if (!Validation.Check(O != nullptr, GENESIS_TEXT("Scope has no foliage actor.")))
		{
			return false;
		}

		const int Type = Integer(Validation, A.Get("Type"), 0, O->GetFoliage().GetNumTypes() - 1);
		if (!Validation.IsValid())
		{
			return false;
		}
		R = Mode.ClearType(Type);
		if (!Validation.IsValid())
		{
			return false;
		}
		return true;
	});
	AddEditorBinding(Batch, Weak, "Foliage.ApplyBrush", true,
	    {{"Types", EJsonType::Array}, {"Position", EJsonType::Array}, {"Radius", EJsonType::Number},
	        {"Strength", EJsonType::Number}, {"Falloff", EJsonType::Number}, {"Seconds", EJsonType::Number},
	        {"Erase", EJsonType::Bool}},
	    EJsonType::Bool, [](FEditorScriptContext&, FMapDocument& D, const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		FMapFoliageMode Mode;
		Mode.SetDocument(&D);
		Object* O = Mode.GetFoliageObject();
		if (!Validation.Check(O != nullptr, GENESIS_TEXT("Scope has no foliage actor.")))
		{
			return false;
		}

		const int Count = O->GetFoliage().GetNumTypes();
		for (int I = 0; I < Count; ++I)
			Mode.SetActive(I, false);
		for (const FJsonValue& V : A.Get("Types").GetItems())
		{
			const int TypeIndex = Integer(Validation, V, 0, Count - 1);
			if (!Validation.IsValid())
			{
				return false;
			}
			Mode.SetActive(TypeIndex, true);
		}
		if (!Validation.IsValid())
		{
			return false;
		}
		Mode.Brush.Radius = Number(Validation, A.Get("Radius"), 8, 8192);
		if (!Validation.IsValid())
		{
			return false;
		}
		Mode.Brush.Strength = Number(Validation, A.Get("Strength"), 0, 1);
		if (!Validation.IsValid())
		{
			return false;
		}
		Mode.Brush.Falloff = Number(Validation, A.Get("Falloff"), 0, 1);
		if (!Validation.IsValid())
		{
			return false;
		}
		const Vector3d P = Vector(Validation, A.Get("Position"));
		if (!Validation.IsValid())
		{
			return false;
		}
		float Remaining = Number(Validation, A.Get("Seconds"), 1.0f / 60, 10);
		if (!Validation.IsValid())
		{
			return false;
		}
		const bool Erase = A.Get("Erase").AsBool();
		if (!Validation.Check(Mode.BeginStroke(P, Erase), GENESIS_TEXT("Cannot begin foliage stroke.")))
		{
			return false;
		}

		FScopeExit EndStroke([&Mode] { Mode.EndStroke(); });
		Remaining -= 1.0f / 60;
		while (Remaining > 0)
		{
			float const Step = std::min(Remaining, 1.0f / 60);
			Mode.ApplyAt(P, Step, Erase);
			Remaining -= Step;
		}
		R = true;
		return true;
	});
	AddEditorBinding(Batch, Weak, "Editor.RotateSelection", true,
	    {{"Axis", EJsonType::Number}, {"Degrees", EJsonType::Number}, {"Pivot", EJsonType::Array}}, EJsonType::Null,
	    [](FEditorScriptContext&, FMapDocument& D, const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		const int Axis = Integer(Validation, A.Get("Axis"), 0, 2);
		if (!Validation.IsValid())
		{
			return false;
		}
		const float Degrees = Number(Validation, A.Get("Degrees"), -360000, 360000);
		if (!Validation.IsValid())
		{
			return false;
		}
		const Vector3d Pivot = Vector(Validation, A.Get("Pivot"));
		if (!Validation.IsValid())
		{
			return false;
		}
		D.RotateObjects(D.GetSelection(), Axis, Degrees, Pivot);
		if (!Validation.IsValid())
		{
			return false;
		}
		R = {};
		return true;
	});
	AddEditorBinding(Batch, Weak, "Editor.ScaleSelection", true,
	    {{"Factors", EJsonType::Array}, {"Pivot", EJsonType::Array}}, EJsonType::Null,
	    [](FEditorScriptContext&, FMapDocument& D, const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		const Vector3d S = Vector(Validation, A.Get("Factors"));
		if (!Validation.IsValid())
		{
			return false;
		}
		if (!Validation.Check(
		        std::abs(S.GetX()) >= 0.0001 && std::abs(S.GetY()) >= 0.0001 && std::abs(S.GetZ()) >= 0.0001,
		        GENESIS_TEXT("Scale factors must not be zero.")))
		{
			return false;
		}

		const Vector3d Pivot = Vector(Validation, A.Get("Pivot"));
		if (!Validation.IsValid())
		{
			return false;
		}
		D.ScaleObjects(D.GetSelection(), S, Pivot);
		if (!Validation.IsValid())
		{
			return false;
		}
		R = {};
		return true;
	});
	AddEditorBinding(Batch, Weak, "Editor.FlipSelection", true,
	    {{"Axis", EJsonType::Number}, {"Pivot", EJsonType::Array}}, EJsonType::Null,
	    [](FEditorScriptContext&, FMapDocument& D, const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		const int Axis = Integer(Validation, A.Get("Axis"), 0, 2);
		if (!Validation.IsValid())
		{
			return false;
		}
		const Vector3d Pivot = Vector(Validation, A.Get("Pivot"));
		if (!Validation.IsValid())
		{
			return false;
		}
		D.FlipObjects(D.GetSelection(), Axis, Pivot);
		if (!Validation.IsValid())
		{
			return false;
		}
		R = {};
		return true;
	});
	AddEditorBinding(Batch, Weak, "Editor.HollowSelection", true,
	    {{"Thickness", EJsonType::Number}, {"Inward", EJsonType::Bool}}, EJsonType::Bool,
	    [](FEditorScriptContext&, FMapDocument& D, const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		const float Thickness = Number(Validation, A.Get("Thickness"), 0.01f, 65536);
		if (!Validation.IsValid())
		{
			return false;
		}
		R = D.HollowObjects(D.GetSelection(), Thickness, A.Get("Inward").AsBool());
		if (!Validation.IsValid())
		{
			return false;
		}
		return true;
	});
	AddEditorBinding(Batch, Weak, "Editor.SubtractSelection", true, {}, EJsonType::Bool,
	    [](FEditorScriptContext&, FMapDocument& D, const J&, J& R, FString&) -> bool
	{
		R = D.SubtractSelection();
		return true;
	});
	AddEditorBinding(Batch, Weak, "Editor.IntersectSelection", true, {}, EJsonType::Bool,
	    [](FEditorScriptContext&, FMapDocument& D, const J&, J& R, FString&) -> bool
	{
		R = D.IntersectSelection();
		return true;
	});
	AddEditorBinding(Batch, Weak, "Editor.ClipSelection", true,
	    {{"Normal", EJsonType::Array}, {"Point", EJsonType::Array}, {"Mode", EJsonType::String}}, EJsonType::Bool,
	    [](FEditorScriptContext&, FMapDocument& D, const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		Vector3d N = Vector(Validation, A.Get("Normal"));
		if (!Validation.IsValid())
		{
			return false;
		}
		if (!Validation.Check(N.GetX() * N.GetX() + N.GetY() * N.GetY() + N.GetZ() * N.GetZ() > 0.000001,
		        GENESIS_TEXT("Plane normal must not be zero.")))
		{
			return false;
		}

		const FString Name = A.Get("Mode").AsString();
		if (!Validation.Check(Name == "Front" || Name == "Back" || Name == "Split",
		        GENESIS_TEXT("Clip mode must be Front, Back or Split.")))
		{
			return false;
		}

		const Vector3d Point = Vector(Validation, A.Get("Point"));
		if (!Validation.IsValid())
		{
			return false;
		}
		R = D.ClipObjects(D.GetSelection(), Plane(N, Point),
		    Name == "Front"      ? EMapClip::KeepFront
		        : Name == "Back" ? EMapClip::KeepBack
		                         : EMapClip::Split);
		if (!Validation.IsValid())
		{
			return false;
		}
		return true;
	});
	AddEditorBinding(Batch, Weak, "Editor.AddMeshAsset", true,
	    {{"Reference", EJsonType::Object}, {"Position", EJsonType::Array}}, EJsonType::Object,
	    [](FEditorScriptContext&, FMapDocument& D, const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		TOptional<FAssetReference> Ref = FAssetReference::FromJson(A.Get("Reference"), E);
		if (!Ref)
			return false;
		const Vector3d Position = Vector(Validation, A.Get("Position"));
		if (!Validation.IsValid())
		{
			return false;
		}
		Object* O = D.AddMeshAsset(*Ref, Position, E);

		if (!O)
			return false;
		R = Describe(*O);
		return true;
	});
	AddEditorBinding(Batch, Weak, "Editor.SetMeshMaterial", true,
	    {{"ActorId", EJsonType::String}, {"Reference", EJsonType::Object}, {"Slot", EJsonType::Number, false}},
	    EJsonType::Bool, [](FEditorScriptContext&, FMapDocument& D, const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		TOptional<FAssetReference> Ref = FAssetReference::FromJson(A.Get("Reference"), E);
		if (!Ref)
			return false;
		const int Slot = A.Contains("Slot") ? Integer(Validation, A.Get("Slot"), 0, 63) : 0;
		if (!Validation.IsValid())
		{
			return false;
		}
		Object* ActorObject = Actor(Validation, D, A);
		if (!Validation.IsValid())
		{
			return false;
		}
		if (!D.SetMeshMaterial((*ActorObject), *Ref, E, Slot))
			return false;
		R = true;
		return true;
	});
	AddEditorBinding(Batch, Weak, "Editor.AddPrefab", true,
	    {{"Path", EJsonType::String}, {"Position", EJsonType::Array}, {"ExpectedId", EJsonType::String, false}},
	    EJsonType::Object, [](FEditorScriptContext&, FMapDocument& D, const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		const Vector3d Position = Vector(Validation, A.Get("Position"));
		if (!Validation.IsValid())
		{
			return false;
		}
		Object* O = D.AddPrefab(A.Get("Path").AsString(), Position, &E, A.Get("ExpectedId").AsString());

		if (!O)
			return false;
		R = Describe(*O);
		return true;
	});
	AddEditorBinding(Batch, Weak, "Editor.GetCommands", false, {}, EJsonType::Array,
	    [](FEditorScriptContext& C, FMapDocument&, const J&, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		std::shared_ptr<FUICommandList> const Commands = C.GetCommands ? C.GetCommands() : nullptr;
		if (!Validation.Check(Commands != nullptr, GENESIS_TEXT("Editor command host is unavailable.")))
		{
			return false;
		}

		R = J::MakeArray();
		for (const TSharedRef<FBindingContext>& Context : FInputBindingManager::Get().GetContexts())
			for (const TSharedRef<FUICommandInfo>& Command : Context->GetCommands())
				if (Commands->IsActionMapped(Command))
				{
					FJsonValue V = J::MakeObject();
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
	Batch.push_back({"Editor.ExecuteCommand", "Editor.Commands", true,
	    {{"Context", EJsonType::String}, {"Name", EJsonType::String}}, EJsonType::Bool,
	    [Weak](const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		std::shared_ptr<FEditorScriptContext> C = Context(Validation, Weak);
		if (!Validation.IsValid())
		{
			return false;
		}
		if (!Validation.Check(!C->GetDocument()->IsInTransaction(),
		        GENESIS_TEXT("End the transaction before executing editor commands.")))
		{
			return false;
		}

		std::shared_ptr<FUICommandList> const Commands = C->GetCommands ? C->GetCommands() : nullptr;
		if (!Validation.Check(Commands != nullptr, GENESIS_TEXT("Editor command host is unavailable.")))
		{
			return false;
		}

		const TSharedPtr<FBindingContext> Context =
		    FInputBindingManager::Get().FindContext(FName(*A.Get("Context").AsString()));
		if (!Validation.Check(Context != nullptr, GENESIS_TEXT("Command context is missing.")))
		{
			return false;
		}

		const TSharedPtr<FUICommandInfo> Command = Context->FindCommand(FName(*A.Get("Name").AsString()));
		if (!Validation.Check(Command != nullptr && Commands->IsActionMapped(ToSharedRef(Command)),
		        GENESIS_TEXT("Editor command is not mapped.")))
		{
			return false;
		}

		if (!Validation.Check(Commands->CanExecuteAction(*Command) && Commands->IsVisible(*Command),
		        GENESIS_TEXT("Editor command is disabled or hidden.")))
		{
			return false;
		}

		R = Commands->ExecuteAction(*Command);
		return true;
	}});
	Batch.push_back({"Editor.Save", "Editor.Files", true, {{"Path", EJsonType::String}}, EJsonType::Bool,
	    [Weak](const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		std::shared_ptr<FEditorScriptContext> C = Context(Validation, Weak);
		if (!Validation.IsValid())
		{
			return false;
		}
		FMapDocument& D = *C->GetDocument();
		if (!Validation.Check(!D.IsInTransaction(), GENESIS_TEXT("End the transaction before saving.")))
		{
			return false;
		}

		if (!D.Save(A.Get("Path").AsString(), E))
			return false;
		R = true;
		return true;
	}});
	return Registry.RegisterBatch(std::move(Batch), Error);
}
