// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "World/World.h"
#include "Misc/ValidationContext.h"
#include "Containers/ContainerAliases.h"
#include "Internationalization/TextLocalization.h"
#include <cmath>
#include <cfloat>

namespace
{
	using J = FJsonValue;

	bool ValidVector(const Vector3d& V, float Limit = 1.0e9f)
	{
		for (float N : {V.GetX(), V.GetY(), V.GetZ()})
			if (!std::isfinite(N) || std::abs(N) > Limit)
				return false;
		return true;
	}
	bool ValidText(const FString& Value)
	{
		return Value.Len() <= 65536 && Value.View().find('\0') == std::string_view::npos;
	}
	J Vector(const Vector3d& V)
	{
		FJsonValue Out = J::MakeArray();
		Out.Push(V.GetX()).Push(V.GetY()).Push(V.GetZ());
		return Out;
	}
	Vector3d ReadVector(FValidationContext& Validation, const J& In)
	{
		if (!Validation.Check(In.IsArray() && In.Num() == 3, GENESIS_TEXT("Expected a 3D vector.")))
		{
			return {};
		}

		float Values[3];
		for (int I = 0; I < 3; ++I)
		{
			if (!Validation.Check(In[I].GetType() == EJsonType::Number && std::isfinite(In[I].AsNumber()) &&
			            std::abs(In[I].AsNumber()) <= 1.0e9,
			        GENESIS_TEXT("Invalid vector coordinate.")))
			{
				return {};
			}

			Values[I] = In[I].AsFloat();
		}
		return Vector3d(Values[0], Values[1], Values[2]);
	}
	FString Text(FValidationContext& Validation, const J& In)
	{
		if (!Validation.Check(In.GetType() == EJsonType::String && In.AsString().Len() <= 65536 &&
		            In.AsString().View().find('\0') == std::string_view::npos,
		        GENESIS_TEXT("Invalid scene string.")))
		{
			return {};
		}
		return In.AsString();
	}
	TArrayView<const J> Array(FValidationContext& Validation, const J& In, int Limit)
	{
		if (!Validation.Check(In.IsArray() && In.Num() <= Limit, GENESIS_TEXT("Invalid or excessive scene array.")))
		{
			return {};
		}
		return In.GetItems();
	}
	J ComponentJson(const FActorComponent& C)
	{
		FJsonValue Out = J::MakeObject();
		Out["Id"] = C.Id;
		Out["Type"] = C.Type;
		Out["Properties"] = C.Properties;
		Out["References"] = J::MakeArray();
		Out["Assets"] = J::MakeArray();
		for (const FWorldReference& Ref : C.References)
		{
			FJsonValue R = J::MakeObject();
			R["ActorId"] = Ref.ActorId;
			R["ComponentId"] = Ref.ComponentId;
			Out["References"].Push(R);
		}
		for (const FAssetReference& Ref : C.Assets)
			Out["Assets"].Push(Ref.ToJson());
		return Out;
	}
	FActorComponent ReadComponent(FValidationContext& Validation, const J& In)
	{
		if (!Validation.Check(In.IsObject(), GENESIS_TEXT("Expected a component object.")))
		{
			return {};
		}

		FActorComponent C;
		C.Id = Text(Validation, In.Get("Id"));
		if (!Validation.IsValid())
		{
			return {};
		}

		C.Type = Text(Validation, In.Get("Type"));
		if (!Validation.IsValid())
		{
			return {};
		}

		C.Properties = In.Get("Properties");
		if (!Validation.Check(C.Properties.IsObject(), GENESIS_TEXT("Component properties must be an object.")))
		{
			return {};
		}

		for (const FJsonValue& R : Array(Validation, In.Get("References"), 100000))
		{
			C.References.push_back({Text(Validation, R.Get("ActorId")), Text(Validation, R.Get("ComponentId"))});
			if (!Validation.IsValid())
			{
				return {};
			}
		}
		if (!Validation.IsValid())
		{
			return {};
		}

		for (const FJsonValue& R : Array(Validation, In.Get("Assets"), 100000))
		{
			FString Error;
			TOptional<FAssetReference> Ref = FAssetReference::FromJson(R, Error);
			if (!Validation.Check(Ref.has_value(), GENESIS_TEXT("Invalid component asset reference.")))
			{
				return {};
			}

			C.Assets.push_back(*Ref);
		}
		if (!Validation.IsValid())
		{
			return {};
		}
		return C;
	}
	TArray<const FActorComponent*> Components(const FActor& Actor)
	{
		TArray<const FActorComponent*> Result{&Actor.Root};
		for (const FActorComponent& C : Actor.Components)
			Result.push_back(&C);
		return Result;
	}
} // namespace

bool FTransform3D::IsValid() const
{
	return ValidVector(Location) && ValidVector(Vector3d(Rotation.GetYaw(), Rotation.GetPitch(), Rotation.GetRoll())) &&
	    ValidVector(Scale, 1.0e6f) && std::abs(Scale.GetX()) >= 1.0e-6f && std::abs(Scale.GetY()) >= 1.0e-6f &&
	    std::abs(Scale.GetZ()) >= 1.0e-6f;
}
Matrix44 FTransform3D::ToMatrix() const
{
	TransSpace Space;
	Space.SetPosition(Location);
	Space.SetOrientation(Rotation);
	Space.SetScale(Scale);
	Matrix44 Matrix;
	Space.CalTransSpaceMatrix(Matrix.SetIdentity());
	return Matrix;
}
J FTransform3D::ToJson() const
{
	FJsonValue Out = J::MakeObject();
	Out["Location"] = Vector(Location);
	Out["Rotation"] = Vector(Vector3d(Rotation.GetYaw(), Rotation.GetPitch(), Rotation.GetRoll()));
	Out["Scale"] = Vector(Scale);
	return Out;
}
TOptional<FTransform3D> FTransform3D::FromJson(const J& Json, FString& Error)
{
	FValidationContext Validation(Error);

	if (!Validation.Check(Json.IsObject(), GENESIS_TEXT("Expected a transform object.")))
	{
		return {};
	}

	FTransform3D T;
	T.Location = ReadVector(Validation, Json.Get("Location"));
	if (!Validation.IsValid())
	{
		return {};
	}

	const Vector3d R = ReadVector(Validation, Json.Get("Rotation"));
	if (!Validation.IsValid())
	{
		return {};
	}

	T.Rotation.NewVector(R.GetX(), R.GetY(), R.GetZ());
	T.Scale = ReadVector(Validation, Json.Get("Scale"));
	if (!Validation.IsValid())
	{
		return {};
	}

	if (!Validation.Check(T.IsValid(), GENESIS_TEXT("Invalid or singular transform.")))
	{
		return {};
	}

	Error.Empty();
	return T;
}
FActorComponent FActorComponent::New(const FString& Type)
{
	FActorComponent C;
	C.Id = FAssetId::New();
	C.Type = Type;
	return C;
}
FSceneComponent FSceneComponent::New()
{
	FSceneComponent C;
	static_cast<FActorComponent&>(C) = FActorComponent::New("Genesis.Transform");
	return C;
}
FActor FActor::New(const FString& Name)
{
	FActor A;
	A.Id = FAssetId::New();
	A.Name = Name;
	A.Root = FSceneComponent::New();
	return A;
}
J FActorComponent::ToJson() const
{
	return ComponentJson(*this);
}
J FActor::ToJson() const
{
	FJsonValue Actor = J::MakeObject();
	Actor["Id"] = Id;
	Actor["Name"] = Name;
	Actor["Parent"] = ParentId;
	Actor["Root"] = Root.ToJson();
	Actor["Root"]["Transform"] = Root.Transform.ToJson();
	Actor["Components"] = J::MakeArray();
	for (const FActorComponent& C : Components)
		Actor["Components"].Push(C.ToJson());
	return Actor;
}

const FActor* FWorld::FindActor(const FString& Id) const
{
	const TMap<std::string, size_t>::const_iterator It = Index.find(Id.ToStdString());
	return It == Index.end() ? nullptr : &Actors[It->second];
}
const FActorComponent* FWorld::FindComponent(const FString& ActorId, const FString& ComponentId) const
{
	const FActor* A = FindActor(ActorId);
	if (!A)
		return nullptr;
	if (A->Root.Id == ComponentId)
		return &A->Root;
	for (const FActorComponent& C : A->Components)
		if (C.Id == ComponentId)
			return &C;
	return nullptr;
}
bool FWorld::Resolve(
    const FWorldReference& Ref, const FActor*& Actor, const FActorComponent*& Component, FString& Error) const
{
	Actor = nullptr;
	Component = nullptr;
	Error.Empty();
	if (Ref.IsNull())
		return true;
	if (!FAssetId::IsValid(Ref.ActorId) || (!Ref.ComponentId.IsEmpty() && !FAssetId::IsValid(Ref.ComponentId)))
	{
		Error = GENESIS_TEXT("Invalid world reference identity.");
		return false;
	}
	Actor = FindActor(Ref.ActorId);
	if (!Actor)
	{
		Error = GENESIS_TEXT("World reference points to a missing actor.");
		return false;
	}
	if (!Ref.ComponentId.IsEmpty())
	{
		Component = FindComponent(Ref.ActorId, Ref.ComponentId);
		if (!Component)
		{
			Actor = nullptr;
			Error = GENESIS_TEXT("World reference points to a missing or differently owned component.");
			return false;
		}
	}
	return true;
}
bool FWorld::Validate(FString& Error)
{
	Error.Empty();
	Index.clear();
	TSet<std::string> Ids;
	if (Actors.size() > 100000)
	{
		Error = GENESIS_TEXT("Scene exceeds the actor limit.");
		return false;
	}
	for (size_t I = 0; I < Actors.size(); ++I)
	{
		FActor const& A = Actors[I];
		if (!ValidText(A.Name))
		{
			Error = GENESIS_TEXT("Invalid actor name.");
			return false;
		}
		if (!FAssetId::IsValid(A.Id) || !Ids.insert(A.Id.ToStdString()).second)
		{
			Error = GENESIS_TEXT("Invalid or duplicate actor/component UUID.");
			return false;
		}
		Index[A.Id.ToStdString()] = I;
		if (!A.Root.Transform.IsValid() || A.Root.Type != "Genesis.Transform" || A.Components.size() > 1024)
		{
			Error = GENESIS_TEXT("Invalid actor root transform or component count.");
			return false;
		}
		for (const FActorComponent* C : Components(A))
		{
			if (!FAssetId::IsValid(C->Id) || !Ids.insert(C->Id.ToStdString()).second || C->Type.IsEmpty() ||
			    !ValidText(C->Type) || !C->Properties.IsObject() || C->References.size() > 100000 ||
			    C->Assets.size() > 100000)
			{
				Error = GENESIS_TEXT("Invalid or duplicate actor/component UUID or component type.");
				return false;
			}
			for (const FAssetReference& Ref : C->Assets)
				if (!FAssetReference::FromJson(Ref.ToJson(), Error))
					return false;
		}
	}
	for (const FActor& A : Actors)
	{
		TSet<std::string> Chain;
		const FActor* Current = &A;
		while (Current)
		{
			if (!Chain.insert(Current->Id.ToStdString()).second || Chain.size() > 128)
			{
				Error = GENESIS_TEXT("Actor hierarchy contains a cycle or exceeds 128 levels.");
				return false;
			}
			if (Current->ParentId.IsEmpty())
				break;
			Current = FindActor(Current->ParentId);
			if (!Current)
			{
				Error = GENESIS_TEXT("Actor parent is missing.");
				return false;
			}
		}
		for (const FActorComponent* C : Components(A))
			for (const FWorldReference& Ref : C->References)
			{
				const FActor* Owner;
				const FActorComponent* Target;
				if (!Resolve(Ref, Owner, Target, Error))
					return false;
			}
	}
	return true;
}
bool FWorld::AddActor(FActor Actor, FString& Error)
{
	Actors.push_back(std::move(Actor));
	if (Validate(Error))
		return true;
	Actors.pop_back();
	FString Ignored;
	Validate(Ignored);
	return false;
}
bool FWorld::AddComponent(const FString& ActorId, FActorComponent Component, FString& Error)
{
	const TMap<std::string, size_t>::iterator It = Index.find(ActorId.ToStdString());
	if (It == Index.end())
	{
		Error = GENESIS_TEXT("Actor is missing.");
		return false;
	}
	TArray<FActorComponent>& List = Actors[It->second].Components;
	List.push_back(std::move(Component));
	if (Validate(Error))
		return true;
	List.pop_back();
	FString Ignored;
	Validate(Ignored);
	return false;
}
bool FWorld::SetTransform(const FString& ActorId, const FTransform3D& Transform, FString& Error)
{
	const TMap<std::string, size_t>::iterator It = Index.find(ActorId.ToStdString());
	if (It == Index.end() || !Transform.IsValid())
	{
		Error = GENESIS_TEXT("Actor is missing or transform is invalid.");
		return false;
	}
	Actors[It->second].Root.Transform = Transform;
	Error.Empty();
	return true;
}
bool FWorld::SetParent(const FString& ActorId, const FString& ParentId, FString& Error)
{
	const TMap<std::string, size_t>::iterator It = Index.find(ActorId.ToStdString());
	if (It == Index.end())
	{
		Error = GENESIS_TEXT("Actor is missing.");
		return false;
	}
	FActor& A = Actors[It->second];
	const FString Previous = A.ParentId;
	A.ParentId = ParentId;
	if (Validate(Error))
		return true;
	A.ParentId = Previous;
	FString Ignored;
	Validate(Ignored);
	return false;
}
bool FWorld::GetWorldTransform(const FString& ActorId, Matrix44& Matrix, FString& Error) const
{
	const FActor* A = FindActor(ActorId);
	if (!A)
	{
		Error = GENESIS_TEXT("Actor is missing.");
		return false;
	}
	Matrix44 Result = A->Root.Transform.ToMatrix();
	while (!A->ParentId.IsEmpty())
	{
		A = FindActor(A->ParentId);
		if (!A)
		{
			Error = GENESIS_TEXT("Actor parent is missing.");
			return false;
		}
		Matrix44 Combined;
		Combined.Multiply(A->Root.Transform.ToMatrix(), Result);
		Result = Combined;
	}
	for (Vector3d const& Basis : {Vector3d(), Vector3d(1, 0, 0), Vector3d(0, 1, 0), Vector3d(0, 0, 1)})
	{
		Vector3d Point;
		Result.Transform(Point, Basis);
		if (!ValidVector(Point, FLT_MAX))
		{
			Error = GENESIS_TEXT("World transform overflow.");
			return false;
		}
	}
	Matrix = Result;
	Error.Empty();
	return true;
}
bool FWorld::RemoveActor(const FString& ActorId, FString& Error)
{
	if (!FindActor(ActorId))
	{
		Error = GENESIS_TEXT("Actor is missing.");
		return false;
	}
	TSet<std::string> Remove{ActorId.ToStdString()};
	bool Changed = true;
	while (Changed)
	{
		Changed = false;
		for (const FActor& A : Actors)
			if (Remove.contains(A.ParentId.ToStdString()))
				Changed |= Remove.insert(A.Id.ToStdString()).second;
	}
	for (const FActor& A : Actors)
		if (!Remove.contains(A.Id.ToStdString()))
			for (const FActorComponent* C : Components(A))
				for (const FWorldReference& R : C->References)
					if (Remove.contains(R.ActorId.ToStdString()))
					{
						Error = GENESIS_TEXT("An actor outside the subtree still references it.");
						return false;
					}
	std::erase_if(Actors, [&](const FActor& A) { return Remove.contains(A.Id.ToStdString()); });
	return Validate(Error);
}
bool FWorld::DuplicateActor(const FString& ActorId, FString& NewActorId, FString& Error)
{
	if (!FindActor(ActorId))
	{
		Error = GENESIS_TEXT("Actor is missing.");
		return false;
	}
	TSet<std::string> Copy{ActorId.ToStdString()};
	bool Changed = true;
	while (Changed)
	{
		Changed = false;
		for (const FActor& A : Actors)
			if (Copy.contains(A.ParentId.ToStdString()))
				Changed |= Copy.insert(A.Id.ToStdString()).second;
	}
	TMap<std::string, FString> Remap;
	for (const FActor& A : Actors)
		if (Copy.contains(A.Id.ToStdString()))
		{
			Remap[A.Id.ToStdString()] = FAssetId::New();
			for (const FActorComponent* C : Components(A))
				Remap[C->Id.ToStdString()] = FAssetId::New();
		}
	const TFunction<FString(const FString&)> Map = [&](const FString& Id)
	{
		const TMap<std::string, FString>::iterator It = Remap.find(Id.ToStdString());
		return It == Remap.end() ? Id : It->second;
	};
	TArray<FActor> Copies;
	for (const FActor& A : Actors)
		if (Copy.contains(A.Id.ToStdString()))
		{
			FActor B = A;
			B.Id = Map(A.Id);
			B.ParentId = Map(A.ParentId);
			const TFunction<void(FActorComponent&)> Fix = [&](FActorComponent& C)
			{
				C.Id = Map(C.Id);
				for (FWorldReference& Ref : C.References)
				{
					Ref.ActorId = Map(Ref.ActorId);
					Ref.ComponentId = Map(Ref.ComponentId);
				}
			};
			Fix(B.Root);
			for (FActorComponent& C : B.Components)
				Fix(C);
			Copies.push_back(std::move(B));
		}
	const unsigned long long Before = Actors.size();
	Actors.insert(Actors.end(), Copies.begin(), Copies.end());
	if (!Validate(Error))
	{
		Actors.resize(Before);
		FString Ignored;
		Validate(Ignored);
		return false;
	}
	NewActorId = Map(ActorId);
	return true;
}
J FWorld::ToJson() const
{
	FJsonValue Out = J::MakeObject();
	Out["Version"] = 1;
	Out["Actors"] = J::MakeArray();
	for (const FActor& A : Actors)
		Out["Actors"].Push(A.ToJson());
	return Out;
}
TOptional<FWorld> FWorld::FromJson(const J& Json, FString& Error)
{
	FValidationContext Validation(Error);

	if (!Validation.Check(Json.IsObject() && Json.Get("Version").GetType() == EJsonType::Number &&
	            Json.Get("Version").AsNumber() == 1,
	        GENESIS_TEXT("Unsupported runtime scene version.")))
	{
		return {};
	}

	FWorld World;
	for (const FJsonValue& In : Array(Validation, Json.Get("Actors"), 100000))
	{
		FActor A;
		A.Id = Text(Validation, In.Get("Id"));
		if (!Validation.IsValid())
		{
			return {};
		}

		A.Name = Text(Validation, In.Get("Name"));
		if (!Validation.IsValid())
		{
			return {};
		}

		A.ParentId = Text(Validation, In.Get("Parent"));
		if (!Validation.IsValid())
		{
			return {};
		}

		static_cast<FActorComponent&>(A.Root) = ReadComponent(Validation, In.Get("Root"));
		if (!Validation.IsValid())
		{
			return {};
		}

		TOptional<FTransform3D> Transform = FTransform3D::FromJson(In.Get("Root").Get("Transform"), Error);
		if (!Validation.Check(Transform.has_value(), GENESIS_TEXT("Invalid root component transform.")))
		{
			return {};
		}

		A.Root.Transform = *Transform;
		for (const FJsonValue& C : Array(Validation, In.Get("Components"), 1024))
		{
			A.Components.push_back(ReadComponent(Validation, C));
			if (!Validation.IsValid())
			{
				return {};
			}
		}
		if (!Validation.IsValid())
		{
			return {};
		}

		World.Actors.push_back(std::move(A));
	}
	if (!Validation.IsValid())
	{
		return {};
	}

	if (!World.Validate(Error))
		return {};
	return World;
}
