// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "MapObjectProperties.h"

#include "MapEditorPrivate.h"

#include <cstdlib>

namespace
{
	// Property names: "Name", "Position", "Texture", "Class", "Key:<key>"
	// and "Flag:<key>:<bit value>".
	const FName NameProperty("Name");
	const FName PositionProperty("Position");
	const FName TextureProperty("Texture");
	const FName ClassProperty("Class");

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
	return FMapDocument::IsGroup(Obj) ? FString("Group") : FString("Brush");
}

void FMapObjectProperties::GetProperties(TArray<FPropertyDesc>& Out) const
{
	Out.push_back(FPropertyDesc(NameProperty, EPropertyType::String, "Name", "Object"));
	Out.push_back(FPropertyDesc(PositionProperty, EPropertyType::Vector3, "Position", "Object").SetToolTip("In the space of the group being edited"));
	if (Obj.HasBrush() || Obj.CountBrushes() > 0)
	{
		Out.push_back(FPropertyDesc(TextureProperty, EPropertyType::String, "Texture", "Surface").SetToolTip("Applied to every face"));
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
	if (Property == NameProperty)
	{
		return FString::FromAnsi(Obj.GetObjName());
	}
	if (Property == PositionProperty)
	{
		const Vector3d& P = Obj.GetPosition();
		return FPropertyVector3{ P.GetX(), P.GetY(), P.GetZ() };
	}
	if (Property == TextureProperty)
	{
		return FString::FromAnsi(FirstTexture(Obj));
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
			Document.MoveObjects({ &Obj }, Delta);
		}
		return;
	}
	if (Property == TextureProperty)
	{
		Document.SetTexture({ &Obj }, ValueText(Value));
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
