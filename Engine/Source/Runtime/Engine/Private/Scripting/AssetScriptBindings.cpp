// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "Scripting/AssetScriptBindings.h"
#include "Containers/ContainerAliases.h"
#include "Templates/SharedPointer.h"
#include "Internationalization/TextLocalization.h"
#include <cmath>
#include "Misc/ValidationContext.h"
namespace
{
	using J = FJsonValue;
	int Integer(FValidationContext& Validation, const J& V, int Max)
	{
		const double N = V.AsNumber();
		if (!Validation.Check(
		        V.GetType() == EJsonType::Number && std::isfinite(N) && N >= 0 && N <= Max && std::floor(N) == N,
		        GENESIS_TEXT("Invalid asset chunk range.")))
		{
			return {};
		}
		return static_cast<int>(N);
	}
	J Data(const FAssetData& D)
	{
		FJsonValue R = J::MakeObject();
		R["Reference"] = D.GetReference().ToJson();
		R["PackagePath"] = D.PackagePath;
		R["Schema"] = D.Metadata.Schema;
		R["SchemaVersion"] = D.Metadata.SchemaVersion;
		R["Kind"] = D.Metadata.Kind == EAssetPackageKind::Map ? "Map" : "Asset";
		R["ExportBytes"] = static_cast<double>(D.Metadata.ExportBytes);
		R["BulkBytes"] = static_cast<double>(D.Metadata.BulkBytes);
		R["Dependencies"] = J::MakeArray();
		for (const FString& P : D.Metadata.Dependencies)
			R["Dependencies"].Push(P);
		return R;
	}
} // namespace
using FBindingCallback = TFunction<bool(FAssetRegistry&, const J&, J&, FString&)>;
static void AddBinding(TArray<FScriptBinding>& Batch, const std::weak_ptr<FAssetRegistry>& Weak, const char* Name,
    TArray<FScriptArgument> Args, EJsonType Type, const FBindingCallback& Fn)
{
	Batch.push_back({Name, "Assets.Read", false, std::move(Args), Type, [Weak, Fn](const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		TSharedPtr<FAssetRegistry> Assets = Weak.lock();
		if (!Validation.Check(Assets != nullptr, GENESIS_TEXT("Script asset registry has expired.")))
		{
			return false;
		}
		return Fn(*Assets, A, R, E);
	}});
}
bool RegisterAssetScriptBindings(FScriptBindingRegistry& Registry, std::weak_ptr<FAssetRegistry> Weak, FString& Error)
{
	TArray<FScriptBinding> Batch;
	AddBinding(Batch, Weak, "Assets.GetAssets", {{"Offset", EJsonType::Number}, {"Count", EJsonType::Number}},
	    EJsonType::Object, [](FAssetRegistry& Assets, const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		const TArray<FAssetData>& Values = Assets.GetAssets();
		const int Offset = Integer(Validation, A.Get("Offset"), static_cast<int>(Values.size()));
		if (!Validation.IsValid())
		{
			return false;
		}
		const int Count = Integer(Validation, A.Get("Count"), 256);
		if (!Validation.IsValid())
		{
			return false;
		}
		R = J::MakeObject();
		R["Total"] = static_cast<int>(Values.size());
		R["Revision"] = FString::Format("{}", Assets.GetRevision());
		R["Assets"] = J::MakeArray();
		for (int I = Offset; I < static_cast<int>(Values.size()) && I < Offset + Count; ++I)
			R["Assets"].Push(Data(Values[I]));
		return true;
	});
	AddBinding(Batch, Weak, "Assets.Resolve", {{"Reference", EJsonType::Object}}, EJsonType::Object,
	    [](FAssetRegistry& Assets, const J& A, J& R, FString& E) -> bool
	{
		TOptional<FAssetReference> Ref = FAssetReference::FromJson(A.Get("Reference"), E);
		if (!Ref)
			return false;
		const FAssetData* Found = Assets.Resolve(*Ref, E);
		if (!Found)
			return false;
		R = Data(*Found);
		return true;
	});
	AddBinding(Batch, Weak, "Assets.LoadExports", {{"Reference", EJsonType::Object}}, EJsonType::Object,
	    [](FAssetRegistry& Assets, const J& A, J& R, FString& E) -> bool
	{
		TOptional<FAssetReference> Ref = FAssetReference::FromJson(A.Get("Reference"), E);
		if (!Ref)
			return false;
		FAssetPackage P;
		if (!Assets.Load(*Ref, P, E))
			return false;
		R = J::MakeObject();
		R["Id"] = P.Id;
		R["Schema"] = P.Schema;
		R["SchemaVersion"] = P.SchemaVersion;
		R["Exports"] = P.Exports;
		R["BulkBytes"] = static_cast<double>(P.Bulk.size());
		return true;
	});
	AddBinding(Batch, Weak, "Assets.ReadBulk",
	    {{"Reference", EJsonType::Object}, {"Offset", EJsonType::Number}, {"Count", EJsonType::Number}},
	    EJsonType::Array, [](FAssetRegistry& Assets, const J& A, J& R, FString& E) -> bool
	{
		FValidationContext Validation(E);
		TOptional<FAssetReference> Ref = FAssetReference::FromJson(A.Get("Reference"), E);
		if (!Ref)
			return false;
		const int Offset = Integer(Validation, A.Get("Offset"), 2147483647),
		          Count = Integer(Validation, A.Get("Count"), 16384);
		if (!Validation.IsValid())
		{
			return false;
		}
		FAssetPackage P;
		if (!Assets.Load(*Ref, P, E))
			return false;
		if (!Validation.Check(
		        static_cast<size_t>(Offset) <= P.Bulk.size() && static_cast<size_t>(Count) <= P.Bulk.size() - Offset,
		        GENESIS_TEXT("Asset chunk exceeds bulk data.")))
		{
			return false;
		}

		R = J::MakeArray();
		for (int I = 0; I < Count; ++I)
			R.Push(P.Bulk[Offset + I]);
		return true;
	});
	AddBinding(Batch, Weak, "Assets.GetIssues", {}, EJsonType::Array,
	    [](FAssetRegistry& Assets, const J&, J& R, FString&) -> bool
	{
		R = J::MakeArray();
		for (const FAssetRegistryIssue& Issue : Assets.GetIssues())
		{
			FJsonValue V = J::MakeObject();
			V["FilePath"] = Issue.FilePath;
			V["Message"] = Issue.Message;
			R.Push(std::move(V));
		}
		return true;
	});
	AddBinding(Batch, Weak, "Assets.GetMounts", {}, EJsonType::Array,
	    [](FAssetRegistry& Assets, const J&, J& R, FString&) -> bool
	{
		R = J::MakeArray();
		for (const FAssetMount& Mount : Assets.GetMounts())
		{
			FJsonValue V = J::MakeObject();
			V["Root"] = Mount.Root;
			V["Directory"] = Mount.Directory;
			R.Push(std::move(V));
		}
		return true;
	});
	return Registry.RegisterBatch(std::move(Batch), Error);
}
