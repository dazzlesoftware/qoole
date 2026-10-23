// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "MapObjectProperties.h"

#include "MapEditorPrivate.h"
#include "MapAssetLibrary.h"

#include <cstdlib>

namespace
{
	// Property names: "Name", "Position", "Texture", "Class", "Key:<key>"
	// and "Flag:<key>:<bit value>".
	const FName NameProperty("Name");
	const FName PositionProperty("Position");
	const FName TextureProperty("Texture");
	const FName ClassProperty("Class");
	const FName RotationProperty("Rotation");
	const FName ScaleProperty("Scale");
	const FName MeshProperty("StaticMesh");
	const FName LandscapeSizeProperty("LandscapeSize");
	const FName LandscapeQuadsProperty("LandscapeQuads");

	TArray<FString> MaterialOptions()
	{
		TArray<FString> Result{"Use Mesh Default"};
		if (const auto Registry = FMapAssetLibrary::GetRegistry())
			for (const auto& Asset : Registry->GetAssets())
				if (IsMaterialAsset(Asset.Metadata))
					Result.push_back(Asset.PackagePath);
		return Result;
	}

	TArray<FString> MeshOptions()
	{
		TArray<FString> Options;
		for (const std::string& Name : StaticMesh::GetBasicShapeNames())
		{
			Options.push_back(FString(Name));
		}
		return Options;
	}

	FString KeyProperty(const char* Key) { return FString::Format("Key:{}", Key); }

	EntInfo* FindInfo(const Object& Obj)
	{
		EntList* List = EntList::Get();
		return Obj.HasEntity() && List ? List->FindEnt(Obj.GetEntityPtr()->GetClassname()) : nullptr;
	}

	// The first brush face's texture in an object or a group.
	const char* FirstTexture(const Object& Obj)
	{
		if (Obj.HasBrush())
		{
			Geometry& Brush = Obj.GetBrush();
			return Brush.GetNumFaces() > 0 ? Brush.GetFaceTexturePtr(0)->GetTName() : "";
		}
		for (int Index = 0; Index < Obj.GetNumChildren(); ++Index)
		{
			const char* Name = FirstTexture(Obj.GetChild(Index));
			if (Name && Name[0])
			{
				return Name;
			}
		}
		return "";
	}

	FString ValueText(const FPropertyValue& Value)
	{
		if (const int32* Int = std::get_if<int32>(&Value)) return FString::Format("{}", *Int);
		if (const float* Float = std::get_if<float>(&Value))
		{
			std::string Text = std::format("{:.3f}", *Float);
			while (!Text.empty() && Text.back() == '0') Text.pop_back();
			if (!Text.empty() && Text.back() == '.') Text.pop_back();
			return FString(Text);
		}
		if (const FString* String = std::get_if<FString>(&Value)) return *String;
		if (const bool* Bool = std::get_if<bool>(&Value)) return *Bool ? FString("1") : FString("0");
		return FString();
	}
}

FString FMapObjectProperties::GetDisplayName() const
{
	const char* Name = Obj.GetObjName();
	if (Name && Name[0])
	{
		return FString::FromAnsi(Name);
	}
	if (Obj.HasEntity())
	{
		return FString::FromAnsi(Obj.GetEntityPtr()->GetClassname());
	}
	if (Obj.HasMesh())
	{
		return FString::FromAnsi(Obj.GetMeshName());
	}
	return FMapDocument::IsGroup(Obj) ? FString("Group") : FString("Brush");
}

void FMapObjectProperties::GetProperties(TArray<FPropertyDesc>& Out) const
{
	Out.push_back(FPropertyDesc(NameProperty, EPropertyType::String, "Name", "Object"));
	if (Obj.HasMesh())
	{
		// Unreal's Transform section, then the mesh.
		Out.push_back(FPropertyDesc(PositionProperty, EPropertyType::Vector3, "Location", "Transform").SetToolTip("In the space of the group being edited"));
		Out.push_back(FPropertyDesc(RotationProperty, EPropertyType::Vector3, "Rotation", "Transform").SetToolTip("Degrees: roll about X, pitch about Y, yaw about Z"));
		Out.push_back(FPropertyDesc(ScaleProperty, EPropertyType::Vector3, "Scale", "Transform"));
		if (Obj.HasLandscape())
		{
			// Sculpt it in the Landscape mode (Shift+2).
			Out.push_back(FPropertyDesc(LandscapeQuadsProperty, EPropertyType::String, "Quads", "Landscape").SetReadOnly());
			Out.push_back(FPropertyDesc(LandscapeSizeProperty, EPropertyType::String, "Size", "Landscape").SetReadOnly());
			return;
		}
		if(const auto Asset=Obj.GetMeshAsset()) {
			Out.push_back(FPropertyDesc(MeshProperty,EPropertyType::String,"Static Mesh","Static Mesh").SetReadOnly());
			for (int I = 0; I < Asset->Data.GetNumMaterials(); ++I)
				Out.push_back(
					FPropertyDesc(FName(*FString::Format("MaterialSlot:{}", I)), EPropertyType::Enum,
								  Asset->Data.GetMaterialName(I), "Materials")
						.SetOptions(MaterialOptions())
						.SetToolTip("Use Mesh Default clears this actor override; other slots remain unchanged."));
		}
		else
			Out.push_back(FPropertyDesc(MeshProperty, EPropertyType::Enum, "Static Mesh", "Static Mesh")
							  .SetOptions(MeshOptions()));
		return;
	}
	Out.push_back(FPropertyDesc(PositionProperty, EPropertyType::Vector3, "Position", "Object")
					  .SetToolTip("In the space of the group being edited"));
	if (Obj.HasBrush() || Obj.CountBrushes() > 0)
	{
		Out.push_back(FPropertyDesc(TextureProperty, EPropertyType::String, "Texture", "Surface")
						  .SetToolTip("Applied to every face"));
	}
	if (!Obj.HasEntity())
	{
		return;
	}
	Entity* Ent = Obj.GetEntityPtr();
	Out.push_back(FPropertyDesc(ClassProperty, EPropertyType::String, "Class", "Entity").SetReadOnly());

	TArray<FString> Listed;
	if (EntInfo* Info = FindInfo(Obj))
	{
		for (int Index = 0; Index < Info->GetNumEntries(); ++Index)
		{
			const FEntEntry* Entry = Info->GetEntryNum(Index);
			if (Entry->vartype == VAR_NOTUSER || !Entry->name)
			{
				continue;
			}
			Listed.push_back(FString::FromAnsi(Entry->name));
			const FString Tip = Entry->desc ? FString::FromAnsi(Entry->desc) : FString();
			const FString Name = FString::FromAnsi(Entry->name);
			switch (Entry->vartype)
			{
			case VAR_INT:
				Out.push_back(FPropertyDesc(FName(KeyProperty(Entry->name)), EPropertyType::Int, Name, "Entity").SetToolTip(Tip));
				break;
			case VAR_FLOAT:
				Out.push_back(FPropertyDesc(FName(KeyProperty(Entry->name)), EPropertyType::Float, Name, "Entity").SetToolTip(Tip));
				break;
			case VAR_TYPEDEF:
			case VAR_STRDEF:
			{
				TArray<FString> Options;
				for (int Option = 0; Option < Entry->entDef->GetNumEntries(); ++Option)
				{
					Options.push_back(FString::FromAnsi(Entry->entDef->GetEntryNum(Option)->name));
				}
				Out.push_back(FPropertyDesc(FName(KeyProperty(Entry->name)), EPropertyType::Enum, Name, "Entity").SetOptions(Options).SetToolTip(Tip));
				break;
			}
			case VAR_FLAGDEF:
				for (int Flag = 0; Flag < Entry->entDef->GetNumEntries(); ++Flag)
				{
					const FDefEntry* Def = Entry->entDef->GetEntryNum(Flag);
					Out.push_back(FPropertyDesc(FName(FString::Format("Flag:{}:{}", Entry->name, Def->val)), EPropertyType::Bool,
						FString::FromAnsi(Def->name), FString::Format("Entity: {}", Name)).SetToolTip(Def->desc ? FString::FromAnsi(Def->desc) : FString()));
				}
				break;
			default:
				Out.push_back(FPropertyDesc(FName(KeyProperty(Entry->name)), EPropertyType::String, Name, "Entity").SetToolTip(Tip));
				break;
			}
		}
	}
	// Keys set on the entity that its class does not define.
	for (int Index = 0; Index < Ent->GetNumKeys(); ++Index)
	{
		const char* Key = Ent->GetKeyNum(Index);
		if (!Key || !stricmp(Key, "classname") || !stricmp(Key, "origin"))
		{
			continue;
		}
		const FString KeyName = FString::FromAnsi(Key);
		bool bListed = false;
		for (const FString& Name : Listed)
		{
			bListed |= Name.Equals(KeyName.View(), ESearchCase::IgnoreCase);
		}
		if (!bListed)
		{
			Out.push_back(FPropertyDesc(FName(KeyProperty(Key)), EPropertyType::String, KeyName, "Other keys"));
		}
	}
}

FPropertyValue FMapObjectProperties::GetValue(FName Property) const
{
	const FString Name = Property.ToString();
	if(Name.StartsWith("MaterialSlot:") && Obj.GetMeshAsset()) {
		const int I=std::atoi(Name.ToAnsi().c_str()+13); const auto& Asset=*Obj.GetMeshAsset();
		if (I < 0 || I >= Asset.Data.GetNumMaterials())
			return int32(0);
		const auto Ref =
			I == 0 ? Asset.MaterialOverride
				   : (I <= static_cast<int>(Asset.AdditionalOverrides.size()) ? Asset.AdditionalOverrides[I - 1]
																			  : FAssetReference{});
		const auto Options = MaterialOptions();
		if (Ref.IsNull())
			return int32(0);
		const auto Registry = FMapAssetLibrary::GetRegistry();
		FString Error;
		const auto* Data = Registry ? Registry->Resolve(Ref, Error) : nullptr;
		for (size_t N = 1; N < Options.size(); ++N)
			if (Data && Options[N] == Data->PackagePath)
				return static_cast<int32>(N);
		return int32(0);
	}
	if (Property == NameProperty)
	{
		return FString::FromAnsi(Obj.GetObjName());
	}
	if (Property == PositionProperty)
	{
		const Vector3d& P = Obj.GetPosition();
		return FPropertyVector3{P.GetX(), P.GetY(), P.GetZ()};
	}
	if (Property == TextureProperty)
	{
		return FString::FromAnsi(FirstTexture(Obj));
	}
	if (Obj.HasMesh())
	{
		if (Property == RotationProperty)
		{
			const Vector3d R = FMapDocument::GetMeshRotation(Obj);
			return FPropertyVector3{ R.GetX(), R.GetY(), R.GetZ() };
		}
		if (Property == ScaleProperty)
		{
			const Vector3d& S = Obj.GetScale();
			return FPropertyVector3{ S.GetX(), S.GetY(), S.GetZ() };
		}
		if (Obj.HasLandscape() && Property == LandscapeQuadsProperty)
		{
			const Landscape& Land = Obj.GetLandscape();
			return FString::Format("{} x {} ({} heights)", Land.GetQuadsX(), Land.GetQuadsY(), Land.GetHeights().size());
		}
		if (Obj.HasLandscape() && Property == LandscapeSizeProperty)
		{
			const Landscape& Land = Obj.GetLandscape();
			return FString::Format("{:.0f} x {:.0f} units ({:.0f} a quad)", Land.GetSizeX(), Land.GetSizeY(), Land.GetQuadSize());
		}
		if (Property == MeshProperty)
		{
			if(const auto Asset=Obj.GetMeshAsset()) return Asset->Source.Path;
			const TArray<FString> Options = MeshOptions();
			for (SIZE_T Index = 0; Index < Options.size(); ++Index)
			{
				if (Options[Index].Equals(Obj.GetMeshName(), ESearchCase::IgnoreCase))
				{
					return static_cast<int32>(Index);
				}
			}
			return static_cast<int32>(0);
		}
	}
	if (!Obj.HasEntity())
	{
		return FString();
	}
	Entity* Ent = Obj.GetEntityPtr();
	if (Property == ClassProperty)
	{
		return FString::FromAnsi(Ent->GetClassname());
	}
	if (Name.StartsWith("Flag:"))
	{
		const TArray<FString> Parts = Name.ParseIntoArray(":");
		const char* Value = Parts.size() == 3 ? Ent->GetKey(Parts[1].ToAnsi().c_str()) : nullptr;
		const int Bits = Value ? std::atoi(Value) : 0;
		return (Bits & std::atoi(Parts[2].ToAnsi().c_str())) != 0;
	}
	if (!Name.StartsWith("Key:"))
	{
		return FString();
	}
	const std::string Key = Name.Mid(4).ToAnsi();
	const char* Raw = Ent->GetKey(Key.c_str());
	const FEntEntry* Entry = nullptr;
	if (EntInfo* Info = FindInfo(Obj))
	{
		for (int Index = 0; Index < Info->GetNumEntries(); ++Index)
		{
			if (Info->GetEntryNum(Index)->name && !stricmp(Info->GetEntryNum(Index)->name, Key.c_str()))
			{
				Entry = Info->GetEntryNum(Index);
			}
		}
	}
	// Unset keys show the definition's default.
	const char* Text = Raw ? Raw : (Entry && Entry->def ? Entry->def : "");
	switch (Entry ? Entry->vartype : VAR_STR)
	{
	case VAR_INT: return static_cast<int32>(std::atoi(Text));
	case VAR_FLOAT: return static_cast<float>(std::atof(Text));
	case VAR_TYPEDEF:
	case VAR_STRDEF:
		for (int Option = 0; Option < Entry->entDef->GetNumEntries(); ++Option)
		{
			const FDefEntry* Def = Entry->entDef->GetEntryNum(Option);
			const bool bMatch = Entry->vartype == VAR_STRDEF ? (Def->str && !stricmp(Def->str, Text)) : (Text[0] && Def->val == std::atoi(Text));
			if (bMatch)
			{
				return static_cast<int32>(Option);
			}
		}
		return static_cast<int32>(0);
	default:
		return FString::FromAnsi(Text);
	}
}

void FMapObjectProperties::SetValue(FName Property, const FPropertyValue& Value)
{
	const FString Name = Property.ToString();
	if(Name.StartsWith("MaterialSlot:") && Obj.GetMeshAsset()) {
		const int Slot=std::atoi(Name.ToAnsi().c_str()+13); const auto Options=MaterialOptions();
		if(const auto* Index=std::get_if<int32>(&Value);Index && *Index>=0 && *Index<static_cast<int32>(Options.size())) {
			FAssetReference Ref; FString Error;
			if(*Index>0) { const auto Registry=FMapAssetLibrary::GetRegistry(); const auto* Data=Registry?Registry->Resolve({{},Options[*Index]},Error):nullptr; if(!Data) { UE_LOG(LogMapEditor,Warning,"{}",Error); return; } Ref=Data->GetReference(); }
			if(!Document.SetMeshMaterial(Obj,Ref,Error,Slot)) UE_LOG(LogMapEditor,Warning,"{}",Error);
		}
		return;
	}
	if (Property == NameProperty)
	{
		Document.SetObjectName(Obj, ValueText(Value));
		return;
	}
	if (Property == PositionProperty)
	{
		if (const FPropertyVector3* V = std::get_if<FPropertyVector3>(&Value))
		{
			Vector3d Delta(V->X, V->Y, V->Z);
			Delta.SubVector(Obj.GetPosition());
			Document.MoveObjects({&Obj}, Delta);
		}
		return;
	}
	if (Property == TextureProperty)
	{
		Document.SetTexture({&Obj}, ValueText(Value));
		return;
	}
	if (Property == RotationProperty)
	{
		if (const FPropertyVector3* V = std::get_if<FPropertyVector3>(&Value))
		{
			Document.SetMeshRotation(Obj, Vector3d(V->X, V->Y, V->Z));
		}
		return;
	}
	if (Property == ScaleProperty)
	{
		if (const FPropertyVector3* V = std::get_if<FPropertyVector3>(&Value))
		{
			Document.SetMeshScale(Obj, Vector3d(V->X, V->Y, V->Z));
		}
		return;
	}
	if (Property == MeshProperty)
	{
		const TArray<FString> Options = MeshOptions();
		if (const int32* Index = std::get_if<int32>(&Value); Index && *Index >= 0 && *Index < static_cast<int32>(Options.size()))
		{
			Document.SetMesh(Obj, Options[static_cast<SIZE_T>(*Index)]);
		}
		return;
	}
	if (!Obj.HasEntity())
	{
		return;
	}
	Entity* Ent = Obj.GetEntityPtr();
	if (Name.StartsWith("Flag:"))
	{
		const TArray<FString> Parts = Name.ParseIntoArray(":");
		if (Parts.size() != 3)
		{
			return;
		}
		const char* Current = Ent->GetKey(Parts[1].ToAnsi().c_str());
		int Bits = Current ? std::atoi(Current) : 0;
		const int Bit = std::atoi(Parts[2].ToAnsi().c_str());
		Bits = std::get<bool>(Value) ? (Bits | Bit) : (Bits & ~Bit);
		Document.SetEntityKey({ &Obj }, Parts[1], Bits ? FString::Format("{}", Bits) : FString());
		return;
	}
	if (!Name.StartsWith("Key:"))
	{
		return;
	}
	const FString Key = Name.Mid(4);
	// Choices store the option's value, not its label.
	if (const int32* Index = std::get_if<int32>(&Value); Index && FindInfo(Obj))
	{
		EntInfo* Info = FindInfo(Obj);
		for (int Entry = 0; Entry < Info->GetNumEntries(); ++Entry)
		{
			const FEntEntry* E = Info->GetEntryNum(Entry);
			if (E->name && Key.Equals(E->name, ESearchCase::IgnoreCase) && (E->vartype == VAR_TYPEDEF || E->vartype == VAR_STRDEF))
			{
				if (*Index < 0 || *Index >= E->entDef->GetNumEntries())
				{
					return;
				}
				const FDefEntry* Def = E->entDef->GetEntryNum(*Index);
				Document.SetEntityKey({ &Obj }, Key, E->vartype == VAR_STRDEF ? FString::FromAnsi(Def->str) : FString::Format("{}", Def->val));
				return;
			}
		}
	}
	Document.SetEntityKey({ &Obj }, Key, ValueText(Value));
}
