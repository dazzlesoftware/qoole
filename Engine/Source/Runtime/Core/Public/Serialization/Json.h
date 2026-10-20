// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// A small JSON value with a parser and a writer, for settings files (dock
// layouts, key bindings). Objects keep their members in insertion order so
// written files are stable and diff well.
//
//   FJsonValue Root = FJsonValue::MakeObject();
//   Root["Version"] = 3;
//   Root["Tabs"].Push("Outliner");
//   FString Text = Root.ToString();
//   TOptional<FJsonValue> Back = FJsonValue::Parse(Text.View());

#pragma once

#include "Containers/GenesisString.h"
#include "Misc/AssertionMacros.h"

enum class EJsonType : uint8
{
	Null,
	Bool,
	Number,
	String,
	Array,
	Object,
};

class FJsonValue
{
public:
	FJsonValue() = default;
	FJsonValue(std::nullptr_t) {}
	FJsonValue(bool bValue) : Type(EJsonType::Bool), bBool(bValue) {}
	FJsonValue(int32 Value) : Type(EJsonType::Number), Number(Value) {}
	FJsonValue(float Value) : Type(EJsonType::Number), Number(Value) {}
	FJsonValue(double Value) : Type(EJsonType::Number), Number(Value) {}
	FJsonValue(const char* Value) : Type(EJsonType::String), String(Value) {}
	FJsonValue(FString Value) : Type(EJsonType::String), String(std::move(Value)) {}

	static FJsonValue MakeArray();
	static FJsonValue MakeObject();

	EJsonType GetType() const { return Type; }
	bool IsNull() const { return Type == EJsonType::Null; }
	bool IsObject() const { return Type == EJsonType::Object; }
	bool IsArray() const { return Type == EJsonType::Array; }

	bool AsBool(bool Default = false) const { return Type == EJsonType::Bool ? bBool : Default; }
	double AsNumber(double Default = 0.0) const { return Type == EJsonType::Number ? Number : Default; }
	float AsFloat(float Default = 0.0f) const { return Type == EJsonType::Number ? static_cast<float>(Number) : Default; }
	int32 AsInt(int32 Default = 0) const { return Type == EJsonType::Number ? static_cast<int32>(Number) : Default; }
	FString AsString(const FString& Default = FString()) const { return Type == EJsonType::String ? String : Default; }

	// Arrays.
	int32 Num() const { return static_cast<int32>(Type == EJsonType::Object ? Keys.size() : Items.size()); }
	const FJsonValue& operator[](int32 Index) const { return Items[static_cast<SIZE_T>(Index)]; }
	FJsonValue& operator[](int32 Index) { return Items[static_cast<SIZE_T>(Index)]; }
	// Appends (turns a null value into an array first); returns this array.
	FJsonValue& Push(FJsonValue Value);
	const TArray<FJsonValue>& GetItems() const { return Items; }

	// Objects. The non-const operator[] adds a null member when missing (and
	// turns a null value into an object first).
	FJsonValue& operator[](std::string_view Key);
	FJsonValue& operator[](const char* Key) { return (*this)[std::string_view(Key)]; }
	const FJsonValue* Find(std::string_view Key) const;
	// The member, or a shared null value.
	const FJsonValue& Get(std::string_view Key) const;
	bool Contains(std::string_view Key) const { return Find(Key) != nullptr; }
	const TArray<FString>& GetKeys() const { return Keys; }

	FString ToString(bool bPretty = true) const;
	// Strict JSON (no comments, no trailing commas). On failure returns
	// nothing and, if given, describes the error with its line and column.
	static TOptional<FJsonValue> Parse(std::string_view Text, FString* OutError = nullptr);

	bool operator==(const FJsonValue& Other) const;

private:
	void Write(FString& Out, bool bPretty, int32 Indent) const;

	EJsonType Type = EJsonType::Null;
	bool bBool = false;
	double Number = 0.0;
	FString String;
	TArray<FJsonValue> Items;  // array items, or object values
	TArray<FString> Keys;      // object keys, parallel to Items
};
