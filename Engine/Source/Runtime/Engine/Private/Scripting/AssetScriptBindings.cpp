// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "Scripting/AssetScriptBindings.h"
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
int Integer(const J& V, int Max)
{
	const auto N = V.AsNumber();
	Require(V.GetType() == EJsonType::Number && std::isfinite(N) && N >= 0 && N <= Max && std::floor(N) == N,
			"Invalid asset chunk range.");
	return static_cast<int>(N);
}
J Data(const FAssetData& D)
{
	auto R = J::MakeObject();
	R["Reference"] = D.GetReference().ToJson();
	R["PackagePath"] = D.PackagePath;
	R["Schema"] = D.Metadata.Schema;
	R["SchemaVersion"] = D.Metadata.SchemaVersion;
	R["Kind"] = D.Metadata.Kind == EAssetPackageKind::Map ? "Map" : "Asset";
	R["ExportBytes"] = static_cast<double>(D.Metadata.ExportBytes);
	R["BulkBytes"] = static_cast<double>(D.Metadata.BulkBytes);
	R["Dependencies"] = J::MakeArray();
	for (const auto& P : D.Metadata.Dependencies)
		R["Dependencies"].Push(P);
	return R;
}
} // namespace
bool RegisterAssetScriptBindings(FScriptBindingRegistry& Registry, std::weak_ptr<FAssetRegistry> Weak,
								 FString& Error)
{
	TArray<FScriptBinding> Batch;
	const auto Add = [&](const char* Name, TArray<FScriptArgument> Args, EJsonType Type, auto Fn) {
		Batch.push_back(
			{Name, "Assets.Read", false, std::move(Args), Type, [Weak, Fn](const J& A, J& R, FString& E) {
				 auto Assets = Weak.lock();
				 Require(Assets != nullptr, "Script asset registry has expired.");
				 return Fn(*Assets, A, R, E);
			 }});
	};
	Add("Assets.GetAssets", {{"Offset", EJsonType::Number}, {"Count", EJsonType::Number}}, EJsonType::Object,
		[](auto& Assets, const J& A, J& R, FString&) {
			const auto& Values = Assets.GetAssets();
			const auto Offset = Integer(A.Get("Offset"), static_cast<int>(Values.size()));
			const auto Count = Integer(A.Get("Count"), 256);
			R = J::MakeObject();
			R["Total"] = static_cast<int>(Values.size());
			R["Revision"] = FString::Format("{}", Assets.GetRevision());
			R["Assets"] = J::MakeArray();
			for (int I = Offset; I < static_cast<int>(Values.size()) && I < Offset + Count; ++I)
				R["Assets"].Push(Data(Values[I]));
			return true;
		});
	Add("Assets.Resolve", {{"Reference", EJsonType::Object}}, EJsonType::Object,
		[](auto& Assets, const J& A, J& R, FString& E) {
			auto Ref = FAssetReference::FromJson(A.Get("Reference"), E);
			if (!Ref)
				return false;
			auto* Found = Assets.Resolve(*Ref, E);
			if (!Found)
				return false;
			R = Data(*Found);
			return true;
		});
	Add("Assets.LoadExports", {{"Reference", EJsonType::Object}}, EJsonType::Object,
		[](auto& Assets, const J& A, J& R, FString& E) {
			auto Ref = FAssetReference::FromJson(A.Get("Reference"), E);
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
	Add("Assets.ReadBulk",
		{{"Reference", EJsonType::Object}, {"Offset", EJsonType::Number}, {"Count", EJsonType::Number}},
		EJsonType::Array, [](auto& Assets, const J& A, J& R, FString& E) {
			auto Ref = FAssetReference::FromJson(A.Get("Reference"), E);
			if (!Ref)
				return false;
			const auto Offset = Integer(A.Get("Offset"), 2147483647), Count = Integer(A.Get("Count"), 16384);
			FAssetPackage P;
			if (!Assets.Load(*Ref, P, E))
				return false;
			Require(static_cast<size_t>(Offset) <= P.Bulk.size() &&
						static_cast<size_t>(Count) <= P.Bulk.size() - Offset,
					"Asset chunk exceeds bulk data.");
			R = J::MakeArray();
			for (int I = 0; I < Count; ++I)
				R.Push(P.Bulk[Offset + I]);
			return true;
		});
	Add("Assets.GetIssues", {}, EJsonType::Array, [](auto& Assets, const J&, J& R, FString&) {
		R = J::MakeArray();
		for (const auto& Issue : Assets.GetIssues())
		{
			auto V = J::MakeObject();
			V["FilePath"] = Issue.FilePath;
			V["Message"] = Issue.Message;
			R.Push(std::move(V));
		}
		return true;
	});
	Add("Assets.GetMounts", {}, EJsonType::Array, [](auto& Assets, const J&, J& R, FString&) {
		R = J::MakeArray();
		for (const auto& Mount : Assets.GetMounts())
		{
			auto V = J::MakeObject();
			V["Root"] = Mount.Root;
			V["Directory"] = Mount.Directory;
			R.Push(std::move(V));
		}
		return true;
	});
	return Registry.RegisterBatch(std::move(Batch), Error);
}
