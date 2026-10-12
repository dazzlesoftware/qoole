// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "Serialization/Json.h"

#include <charconv>
#include <cmath>

FJsonValue FJsonValue::MakeArray()
{
	FJsonValue V;
	V.Type = EJsonType::Array;
	return V;
}

FJsonValue FJsonValue::MakeObject()
{
	FJsonValue V;
	V.Type = EJsonType::Object;
	return V;
}

FJsonValue& FJsonValue::Push(FJsonValue Value)
{
	if (Type == EJsonType::Null)
	{
		Type = EJsonType::Array;
	}
	check(Type == EJsonType::Array);
	Items.push_back(std::move(Value));
	return *this;
}

FJsonValue& FJsonValue::operator[](std::string_view Key)
{
	if (Type == EJsonType::Null)
	{
		Type = EJsonType::Object;
	}
	check(Type == EJsonType::Object);
	for (SIZE_T I = 0; I < Keys.size(); ++I)
	{
		if (Keys[I].View() == Key)
		{
			return Items[I];
		}
	}
	Keys.emplace_back(Key);
	Items.emplace_back();
	return Items.back();
}

const FJsonValue* FJsonValue::Find(std::string_view Key) const
{
	if (Type != EJsonType::Object)
	{
		return nullptr;
	}
	for (SIZE_T I = 0; I < Keys.size(); ++I)
	{
		if (Keys[I].View() == Key)
		{
			return &Items[I];
		}
	}
	return nullptr;
}

const FJsonValue& FJsonValue::Get(std::string_view Key) const
{
	static const FJsonValue Null;
	const FJsonValue* Value = Find(Key);
	return Value ? *Value : Null;
}

bool FJsonValue::operator==(const FJsonValue& Other) const
{
	if (Type != Other.Type)
	{
		return false;
	}
	switch (Type)
	{
	case EJsonType::Null: return true;
	case EJsonType::Bool: return bBool == Other.bBool;
	case EJsonType::Number: return Number == Other.Number;
	case EJsonType::String: return String == Other.String;
	case EJsonType::Array: return Items == Other.Items;
	case EJsonType::Object:
		if (Keys.size() != Other.Keys.size())
		{
			return false;
		}
		for (SIZE_T I = 0; I < Keys.size(); ++I)
		{
			const FJsonValue* O = Other.Find(Keys[I].View());
			if (!O || !(*O == Items[I]))
			{
				return false;
			}
		}
		return true;
	}
	return false;
}

// ---------------------------------------------------------------------------
// Writing

namespace
{
	void WriteString(FString& Out, std::string_view S)
	{
		Out += '"';
		for (const char C : S)
		{
			switch (C)
			{
			case '"': Out += "\\\""; break;
			case '\\': Out += "\\\\"; break;
			case '\n': Out += "\\n"; break;
			case '\r': Out += "\\r"; break;
			case '\t': Out += "\\t"; break;
			case '\b': Out += "\\b"; break;
			case '\f': Out += "\\f"; break;
			default:
				if (static_cast<unsigned char>(C) < 0x20)
				{
					Out += FString::Format("\\u{:04x}", static_cast<int32>(C));
				}
				else
				{
					Out += C;
				}
			}
		}
		Out += '"';
	}

	void NewLine(FString& Out, bool bPretty, int32 Indent)
	{
		if (bPretty)
		{
			Out += '\n';
			for (int32 I = 0; I < Indent; ++I)
			{
				Out += '\t';
			}
		}
	}
}

void FJsonValue::Write(FString& Out, bool bPretty, int32 Indent) const
{
	switch (Type)
	{
	case EJsonType::Null: Out += "null"; break;
	case EJsonType::Bool: Out += bBool ? "true" : "false"; break;
	case EJsonType::Number:
	{
		if (!std::isfinite(Number))
		{
			Out += "null";
			break;
		}
		char Buffer[32];
		const auto Result = std::to_chars(Buffer, Buffer + sizeof(Buffer), Number);
		Out += std::string_view(Buffer, static_cast<SIZE_T>(Result.ptr - Buffer));
		break;
	}
	case EJsonType::String: WriteString(Out, String.View()); break;
	case EJsonType::Array:
		if (Items.empty())
		{
			Out += "[]";
			break;
		}
		Out += '[';
		for (SIZE_T I = 0; I < Items.size(); ++I)
		{
			if (I)
			{
				Out += ',';
			}
			NewLine(Out, bPretty, Indent + 1);
			Items[I].Write(Out, bPretty, Indent + 1);
		}
		NewLine(Out, bPretty, Indent);
		Out += ']';
		break;
	case EJsonType::Object:
		if (Items.empty())
		{
			Out += "{}";
			break;
		}
		Out += '{';
		for (SIZE_T I = 0; I < Items.size(); ++I)
		{
			if (I)
			{
				Out += ',';
			}
			NewLine(Out, bPretty, Indent + 1);
			WriteString(Out, Keys[I].View());
			Out += bPretty ? ": " : ":";
			Items[I].Write(Out, bPretty, Indent + 1);
		}
		NewLine(Out, bPretty, Indent);
		Out += '}';
		break;
	}
}

FString FJsonValue::ToString(bool bPretty) const
{
	FString Out;
	Write(Out, bPretty, 0);
	if (bPretty)
	{
		Out += '\n';
	}
	return Out;
}

// ---------------------------------------------------------------------------
// Parsing

namespace
{
	class FJsonParser
	{
	public:
		explicit FJsonParser(std::string_view InText) : Text(InText) {}

		TOptional<FJsonValue> ParseDocument(FString* OutError)
		{
			FJsonValue Value;
			if (!ParseValue(Value, 0))
			{
				Report(OutError);
				return {};
			}
			SkipSpace();
			if (Pos != Text.size())
			{
				Error = "unexpected text after the value";
				Report(OutError);
				return {};
			}
			return Value;
		}

	private:
		static constexpr int32 MaxDepth = 256;

		void Report(FString* OutError) const
		{
			if (!OutError)
			{
				return;
			}
			int32 Line = 1, Column = 1;
			for (SIZE_T I = 0; I < ErrorPos && I < Text.size(); ++I)
			{
				if (Text[I] == '\n')
				{
					++Line;
					Column = 1;
				}
				else
				{
					++Column;
				}
			}
			*OutError = FString::Format("JSON error at line {}, column {}: {}", Line, Column, Error);
		}

		bool Fail(const char* Message)
		{
			if (Error.empty())
			{
				Error = Message;
				ErrorPos = Pos;
			}
			return false;
		}

		void SkipSpace()
		{
			while (Pos < Text.size() && (Text[Pos] == ' ' || Text[Pos] == '\t' || Text[Pos] == '\n' || Text[Pos] == '\r'))
			{
				++Pos;
			}
		}

		bool Consume(std::string_view Word)
		{
			if (Text.substr(Pos, Word.size()) == Word)
			{
				Pos += Word.size();
				return true;
			}
			return false;
		}

		bool ParseValue(FJsonValue& Out, int32 Depth)
		{
			if (Depth > MaxDepth)
			{
				return Fail("nested too deeply");
			}
			SkipSpace();
			if (Pos >= Text.size())
			{
				return Fail("unexpected end of text");
			}
			const char C = Text[Pos];
			if (C == '{')
			{
				return ParseObject(Out, Depth);
			}
			if (C == '[')
			{
				return ParseArray(Out, Depth);
			}
			if (C == '"')
			{
				FString S;
				if (!ParseString(S))
				{
					return false;
				}
				Out = FJsonValue(std::move(S));
				return true;
			}
			if (Consume("true"))
			{
				Out = FJsonValue(true);
				return true;
			}
			if (Consume("false"))
			{
				Out = FJsonValue(false);
				return true;
			}
			if (Consume("null"))
			{
				Out = FJsonValue();
				return true;
			}
			if (C == '-' || (C >= '0' && C <= '9'))
			{
				return ParseNumber(Out);
			}
			return Fail("unexpected character");
		}

		bool ParseNumber(FJsonValue& Out)
		{
			const SIZE_T Start = Pos;
			if (Text[Pos] == '-')
			{
				++Pos;
			}
			while (Pos < Text.size() && ((Text[Pos] >= '0' && Text[Pos] <= '9') || Text[Pos] == '.' || Text[Pos] == 'e' || Text[Pos] == 'E' || Text[Pos] == '+' || Text[Pos] == '-'))
			{
				++Pos;
			}
			double Value = 0.0;
			const auto Result = std::from_chars(Text.data() + Start, Text.data() + Pos, Value);
			if (Result.ec != std::errc() || Result.ptr != Text.data() + Pos)
			{
				Pos = Start;
				return Fail("invalid number");
			}
			Out = FJsonValue(Value);
			return true;
		}

		bool ParseHex4(uint32& Out)
		{
			if (Pos + 4 > Text.size())
			{
				return Fail("truncated \\u escape");
			}
			Out = 0;
			for (int32 I = 0; I < 4; ++I)
			{
				const char H = Text[Pos++];
				Out <<= 4;
				if (H >= '0' && H <= '9') Out |= static_cast<uint32>(H - '0');
				else if (H >= 'a' && H <= 'f') Out |= static_cast<uint32>(H - 'a' + 10);
				else if (H >= 'A' && H <= 'F') Out |= static_cast<uint32>(H - 'A' + 10);
				else return Fail("invalid \\u escape");
			}
			return true;
		}

		bool ParseString(FString& Out)
		{
			++Pos; // opening quote
			std::string Result;
			while (true)
			{
				if (Pos >= Text.size())
				{
					return Fail("unterminated string");
				}
				const char C = Text[Pos++];
				if (C == '"')
				{
					break;
				}
				if (static_cast<unsigned char>(C) < 0x20)
				{
					--Pos;
					return Fail("control character in string");
				}
				if (C != '\\')
				{
					Result += C;
					continue;
				}
				if (Pos >= Text.size())
				{
					return Fail("unterminated escape");
				}
				const char E = Text[Pos++];
				switch (E)
				{
				case '"': Result += '"'; break;
				case '\\': Result += '\\'; break;
				case '/': Result += '/'; break;
				case 'b': Result += '\b'; break;
				case 'f': Result += '\f'; break;
				case 'n': Result += '\n'; break;
				case 'r': Result += '\r'; break;
				case 't': Result += '\t'; break;
				case 'u':
				{
					uint32 Code = 0;
					if (!ParseHex4(Code))
					{
						return false;
					}
					if (Code >= 0xD800 && Code <= 0xDBFF && Text.substr(Pos, 2) == "\\u")
					{
						Pos += 2;
						uint32 Low = 0;
						if (!ParseHex4(Low))
						{
							return false;
						}
						if (Low >= 0xDC00 && Low <= 0xDFFF)
						{
							Code = 0x10000 + ((Code - 0xD800) << 10) + (Low - 0xDC00);
						}
					}
					FUtf8::Encode(static_cast<char32_t>(Code), Result);
					break;
				}
				default:
					--Pos;
					return Fail("invalid escape");
				}
			}
			Out = FString(Result);
			return true;
		}

		bool ParseArray(FJsonValue& Out, int32 Depth)
		{
			++Pos;
			Out = FJsonValue::MakeArray();
			SkipSpace();
			if (Pos < Text.size() && Text[Pos] == ']')
			{
				++Pos;
				return true;
			}
			while (true)
			{
				FJsonValue Item;
				if (!ParseValue(Item, Depth + 1))
				{
					return false;
				}
				Out.Push(std::move(Item));
				SkipSpace();
				if (Pos < Text.size() && Text[Pos] == ',')
				{
					++Pos;
					continue;
				}
				if (Pos < Text.size() && Text[Pos] == ']')
				{
					++Pos;
					return true;
				}
				return Fail("expected ',' or ']'");
			}
		}

		bool ParseObject(FJsonValue& Out, int32 Depth)
		{
			++Pos;
			Out = FJsonValue::MakeObject();
			SkipSpace();
			if (Pos < Text.size() && Text[Pos] == '}')
			{
				++Pos;
				return true;
			}
			while (true)
			{
				SkipSpace();
				if (Pos >= Text.size() || Text[Pos] != '"')
				{
					return Fail("expected a member name");
				}
				FString Key;
				if (!ParseString(Key))
				{
					return false;
				}
				SkipSpace();
				if (Pos >= Text.size() || Text[Pos] != ':')
				{
					return Fail("expected ':'");
				}
				++Pos;
				FJsonValue Value;
				if (!ParseValue(Value, Depth + 1))
				{
					return false;
				}
				Out[Key.View()] = std::move(Value);
				SkipSpace();
				if (Pos < Text.size() && Text[Pos] == ',')
				{
					++Pos;
					continue;
				}
				if (Pos < Text.size() && Text[Pos] == '}')
				{
					++Pos;
					return true;
				}
				return Fail("expected ',' or '}'");
			}
		}

		std::string_view Text;
		SIZE_T Pos = 0;
		std::string Error;
		SIZE_T ErrorPos = 0;
	};
}

TOptional<FJsonValue> FJsonValue::Parse(std::string_view Text, FString* OutError)
{
	return FJsonParser(Text).ParseDocument(OutError);
}
