/*
Copyright (C) 2026 Dazzle Software, LLC

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

// Loading and saving maps (.map and .qle) with the MFC-free map core.

#include "doctest.h"
#include "TestUtils.h"

#include "EntList.h"
#include "Entity.h"
#include "Game.h"
#include "Geometry.h"
#include "Mapio.h"
#include "Objects.h"
#include "Texture.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace
{
	// A game with no files of its own. Textures named in a map are simply
	// not found, which the map core allows.
	class FTestGame : public Game
	{
	public:
		FTestGame() : Game(1)
		{
			std::strcpy(gameName, "GenesisTest");
		}

		bool Init(void) override
		{
			if (!texDB)
				texDB = new TexDB(this);
			return true;
		}

		WireFrameGeom* LoadModel(char*) override { return nullptr; }
		bool LoadTexture(Texture*, char*, int) override { return false; }
		int GetNumUtils(void) const override { return 1; }
		const char* GetUtilName(int) const override { return "None"; }
	};

	// The editor reads entity definitions (ents/*.ent) and the wireframes
	// every entity list needs (gems/*.gem) from GenesisEditor.pak. The tests
	// write stand-ins to a data folder the map core searches: a definition
	// of info_player_start, and a cube for every wireframe.
	void ProvideDataFiles()
	{
		static FTempDir DataDir;
		static bool bProvided = false;
		if (bProvided)
			return;
		bProvided = true;

		// "GEOM", vertex and edge counts, then a 16-unit cube (coordinates
		// are stored doubled): 8 vertices, 12 edges.
		TArray<uint8> Gem = { 'G', 'E', 'O', 'M', 8, 0, 12, 0 };
		for (int i = 0; i < 8; i++)
		{
			Gem.push_back(static_cast<uint8>((i & 1) ? 16 : -16));
			Gem.push_back(static_cast<uint8>((i & 2) ? 16 : -16));
			Gem.push_back(static_cast<uint8>((i & 4) ? 16 : -16));
		}
		for (int a = 0; a < 8; a++)
		{
			for (int Bit = 1; Bit < 8; Bit <<= 1)
			{
				if (!(a & Bit))
				{
					Gem.push_back(static_cast<uint8>(a));
					Gem.push_back(static_cast<uint8>(a | Bit));
				}
			}
		}

		std::filesystem::create_directories(std::filesystem::path(DataDir.Path.GetData()) / "gems");
		for (const char* Name : { "gems\\cross1.gem", "gems\\cross2.gem", "gems\\undef.gem" })
		{
			FFile File;
			REQUIRE(File.Open(DataDir.File(Name), EFileOpenMode::Write));
			REQUIRE(File.Write(Gem.data(), (int64)Gem.size()) == (int64)Gem.size());
		}
		REQUIRE(FFile::WriteAllText(DataDir.File("genesis-test.ent"),
			"entity info_player_start Null->Start Start\n"
			"{\n"
			"}\n"));

		LFile::UseDir(DataDir.Path.ToAnsi().c_str());
	}

	// Makes an FTestGame the current game, with the test entity list.
	struct FTestGameScope
	{
		FTestGame* TestGame = new FTestGame;	// owned by Game's list

		FTestGameScope()
		{
			ProvideDataFiles();

			char NoPalette[] = "";
			REQUIRE(Game::Set(TestGame, NoPalette, 1.0f));

			char EntFile[] = "genesis-test.ent";
			EntList::Set(EntList::Find(EntFile));
		}
	};

	// A brush in Quake .map syntax: an axis-aligned box from Min to Max.
	std::string Brush(int MinX, int MinY, int MinZ, int MaxX, int MaxY, int MaxZ)
	{
		char Text[1024];
		std::snprintf(Text, sizeof(Text),
			"{\n"
			"( %d %d %d ) ( %d %d %d ) ( %d %d %d ) base 0 0 0 1 1\n"
			"( %d %d %d ) ( %d %d %d ) ( %d %d %d ) base 0 0 0 1 1\n"
			"( %d %d %d ) ( %d %d %d ) ( %d %d %d ) base 0 0 0 1 1\n"
			"( %d %d %d ) ( %d %d %d ) ( %d %d %d ) base 0 0 0 1 1\n"
			"( %d %d %d ) ( %d %d %d ) ( %d %d %d ) base 0 0 0 1 1\n"
			"( %d %d %d ) ( %d %d %d ) ( %d %d %d ) base 0 0 0 1 1\n"
			"}\n",
			MinX, MinY, MinZ, MinX, MinY + 1, MinZ, MinX, MinY, MinZ + 1,	// -x
			MinX, MinY, MinZ, MinX, MinY, MinZ + 1, MinX + 1, MinY, MinZ,	// -y
			MinX, MinY, MinZ, MinX + 1, MinY, MinZ, MinX, MinY + 1, MinZ,	// -z
			MaxX, MaxY, MaxZ, MaxX, MaxY + 1, MaxZ, MaxX + 1, MaxY, MaxZ,	// +z
			MaxX, MaxY, MaxZ, MaxX + 1, MaxY, MaxZ, MaxX, MaxY, MaxZ + 1,	// +y
			MaxX, MaxY, MaxZ, MaxX, MaxY, MaxZ + 1, MaxX, MaxY + 1, MaxZ);	// +x
		return Text;
	}

	// A Quake map: a 128 x 128 x 16 floor and a 32-unit block on it in
	// worldspawn, and a player start above them.
	std::string BoxMap()
	{
		return "// Genesis test map\n"
			"{\n"
			"\"classname\" \"worldspawn\"\n"
			"\"message\" \"Box\"\n"
			+ Brush(-64, -64, -16, 64, 64, 0)
			+ Brush(-16, -16, 0, 16, 16, 32)
			+ "}\n"
			"{\n"
			"\"classname\" \"info_player_start\"\n"
			"\"origin\" \"0 0 72\"\n"
			"}\n";
	}

	bool WriteText(const std::string& Path, const std::string& Text)
	{
		return FFile::WriteAllText(FString::FromAnsi(Path), Text.c_str());
	}

	// Finds the first node whose entity has the given classname.
	Object* FindEntity(Object& Node, const char* Classname)
	{
		if (Node.HasEntity() && std::strcmp(Node.GetEntityPtr()->GetClassname(), Classname) == 0)
			return &Node;
		for (int i = 0; i < Node.GetNumChildren(); i++)
		{
			if (Object* Found = FindEntity(Node.GetChild(i), Classname))
				return Found;
		}
		return nullptr;
	}

	void CollectBrushes(Object& Node, std::vector<Object*>& OutBrushes)
	{
		if (Node.HasBrush())
			OutBrushes.push_back(&Node);
		for (int i = 0; i < Node.GetNumChildren(); i++)
			CollectBrushes(Node.GetChild(i), OutBrushes);
	}

	// A node's bounds in map coordinates. GetBoundingVectors is relative to
	// the node's position, which is relative to its parent's (none of these
	// nodes is rotated or scaled).
	void GetWorldBounds(Object& Node, Vector3d& Min, Vector3d& Max)
	{
		Node.GetBoundingVectors(Min, Max);
		for (Object* Ancestor = &Node; Ancestor; Ancestor = Ancestor->GetParentPtr())
		{
			Min.AddVector(Ancestor->GetPosition());
			Max.AddVector(Ancestor->GetPosition());
		}
	}

	void CheckBounds(Object& Node, const Vector3d& ExpectedMin, const Vector3d& ExpectedMax)
	{
		Vector3d Min, Max;
		GetWorldBounds(Node, Min, Max);
		CHECK(Min.GetX() == doctest::Approx(ExpectedMin.GetX()));
		CHECK(Min.GetY() == doctest::Approx(ExpectedMin.GetY()));
		CHECK(Min.GetZ() == doctest::Approx(ExpectedMin.GetZ()));
		CHECK(Max.GetX() == doctest::Approx(ExpectedMax.GetX()));
		CHECK(Max.GetY() == doctest::Approx(ExpectedMax.GetY()));
		CHECK(Max.GetZ() == doctest::Approx(ExpectedMax.GetZ()));
	}

	void CheckBoxMap(Object& Root)
	{
		CHECK(Root.IsRoot());
		REQUIRE(Root.HasEntity());
		CHECK(std::strcmp(Root.GetEntityPtr()->GetClassname(), "worldspawn") == 0);
		CHECK(Root.CountBrushes() == 2);
		CHECK(Root.CountEntities() == 2);

		// Both brushes are six-sided boxes.
		std::vector<Object*> Brushes;
		CollectBrushes(Root, Brushes);
		REQUIRE(Brushes.size() == 2);
		for (Object* BrushNode : Brushes)
		{
			CHECK(BrushNode->GetBrush().GetNumFaces() == 6);
			CHECK(BrushNode->GetBrush().GetNumVertices() == 8);
		}

		Object* Start = FindEntity(Root, "info_player_start");
		REQUIRE(Start != nullptr);
		const Vector3d& Origin = Start->GetPosition();
		CHECK(Origin.GetX() == doctest::Approx(0.0f));
		CHECK(Origin.GetY() == doctest::Approx(0.0f));
		CHECK(Origin.GetZ() == doctest::Approx(72.0f));
	}

	// Bounds of the two brushes together.
	void CheckBoxBrushBounds(Object& Root)
	{
		std::vector<Object*> Brushes;
		CollectBrushes(Root, Brushes);
		REQUIRE(Brushes.size() == 2);
		Object* Floor = Brushes[0];
		Object* Block = Brushes[1];
		if (Floor->GetBoundRadius() < Block->GetBoundRadius())
			std::swap(Floor, Block);
		CheckBounds(*Floor, Vector3d(-64, -64, -16), Vector3d(64, 64, 0));
		CheckBounds(*Block, Vector3d(-16, -16, 0), Vector3d(16, 16, 32));
	}
}

TEST_CASE("A .map file loads into an object tree")
{
	FTestGameScope GameScope;
	FTempDir Temp;
	const std::string MapPath = Temp.AnsiFile("box.map");
	REQUIRE(WriteText(MapPath, BoxMap()));

	std::unique_ptr<Object> Root(MapIO::ReadMap(MapPath.c_str()));
	REQUIRE(Root != nullptr);
	CheckBoxMap(*Root);
	CheckBoxBrushBounds(*Root);
}

TEST_CASE("A worldspawn with a single brush keeps it in a child node")
{
	// Qoole made the one brush node itself the worldspawn, and with it the
	// parent of every other entity, so a node held a brush and children.
	FTestGameScope GameScope;
	FTempDir Temp;
	const std::string MapPath = Temp.AnsiFile("single.map");
	REQUIRE(WriteText(MapPath,
		"{\n"
		"\"classname\" \"worldspawn\"\n"
		+ Brush(-64, -64, -16, 64, 64, 0)
		+ "}\n"
		"{\n"
		"\"classname\" \"info_player_start\"\n"
		"\"origin\" \"0 0 40\"\n"
		"}\n"));

	std::unique_ptr<Object> Root(MapIO::ReadMap(MapPath.c_str()));
	REQUIRE(Root != nullptr);
	CHECK_FALSE(Root->HasBrush());
	CHECK(Root->GetNumChildren() == 2);
	CHECK(Root->CountBrushes() == 1);
	CHECK(Root->CountEntities() == 2);

	std::vector<Object*> Brushes;
	CollectBrushes(*Root, Brushes);
	REQUIRE(Brushes.size() == 1);
	CHECK(Brushes[0]->IsLeaf());
	CheckBounds(*Brushes[0], Vector3d(-64, -64, -16), Vector3d(64, 64, 0));

	// Saving keeps the player start.
	const std::string OutPath = Temp.AnsiFile("saved.map");
	REQUIRE(MapIO::WriteMap(OutPath.c_str(), *Root, nullptr));
	std::unique_ptr<Object> Reloaded(MapIO::ReadMap(OutPath.c_str()));
	REQUIRE(Reloaded != nullptr);
	CHECK(Reloaded->CountBrushes() == 1);
	CHECK(Reloaded->CountEntities() == 2);
	Object* Start = FindEntity(*Reloaded, "info_player_start");
	REQUIRE(Start != nullptr);
	CHECK(Start->GetPosition().GetZ() == doctest::Approx(40.0f));
}

TEST_CASE("A map survives saving as .map and as .qle")
{
	FTestGameScope GameScope;
	FTempDir Temp;
	const std::string MapPath = Temp.AnsiFile("box.map");
	REQUIRE(WriteText(MapPath, BoxMap()));
	std::unique_ptr<Object> Root(MapIO::ReadMap(MapPath.c_str()));
	REQUIRE(Root != nullptr);

	SUBCASE(".map")
	{
		const std::string OutPath = Temp.AnsiFile("saved.map");
		REQUIRE(MapIO::WriteMap(OutPath.c_str(), *Root, nullptr));

		FString Text;
		REQUIRE(FFile::ReadAllText(FString::FromAnsi(OutPath), Text));
		CHECK(Text.Find("// Genesis Editor generated MAP") == 0);
		CHECK(Text.Find("info_player_start") > 0);

		std::unique_ptr<Object> Reloaded(MapIO::ReadMap(OutPath.c_str()));
		REQUIRE(Reloaded != nullptr);
		CheckBoxMap(*Reloaded);
		CheckBoxBrushBounds(*Reloaded);
	}

	SUBCASE(".qle")
	{
		const std::string QlePath = Temp.AnsiFile("saved.qle");
		REQUIRE(Root->SaveObjFile(QlePath.c_str()));

		std::unique_ptr<Object> Reloaded(Object::CreateObjFromFile(QlePath.c_str()));
		REQUIRE(Reloaded != nullptr);
		CheckBoxMap(*Reloaded);
		CheckBoxBrushBounds(*Reloaded);
	}
}

TEST_CASE("CSG subtraction carves one brush out of another")
{
	FTestGameScope GameScope;
	FTempDir Temp;

	// Worldspawn holds a 128-unit cube and a 32-unit cube at its centre.
	const std::string TwoBrushes =
		"{\n"
		"\"classname\" \"worldspawn\"\n"
		+ Brush(-64, -64, -64, 64, 64, 64)
		+ Brush(-16, -16, -16, 16, 16, 16)
		+ "}\n";
	const std::string MapPath = Temp.AnsiFile("two.map");
	REQUIRE(WriteText(MapPath, TwoBrushes));
	std::unique_ptr<Object> Root(MapIO::ReadMap(MapPath.c_str()));
	REQUIRE(Root != nullptr);
	REQUIRE(Root->CountBrushes() == 2);

	// The child with the smaller bound radius is the cutter.
	Object* Big = &Root->GetChild(0);
	Object* Small = &Root->GetChild(1);
	if (Big->GetBoundRadius() < Small->GetBoundRadius())
		std::swap(Big, Small);

	LinkList<ObjectPtr> Cutters;
	Cutters.AppendNode(*new ObjectPtr(Small));
	const bool bModified = Big->CSGSubtract(Cutters);
	Cutters.DeleteAllNodes();
	REQUIRE(bModified);

	// The pieces of the big cube are added next to it, and the emptied node
	// is left for the caller to delete (as the editor's CSG operation does).
	CHECK(Big->IsLeaf());
	CHECK_FALSE(Big->HasBrush());
	Root->RemoveChild(*Big, false);
	delete Big;

	// Six slabs surround the hole. Together they span the original cube and
	// none reaches into the hole.
	std::vector<Object*> Pieces;
	for (int i = 0; i < Root->GetNumChildren(); i++)
	{
		if (&Root->GetChild(i) != Small && Root->GetChild(i).HasBrush())
			Pieces.push_back(&Root->GetChild(i));
	}
	CHECK(Pieces.size() == 6);

	Vector3d UnionMin(1e9f, 1e9f, 1e9f), UnionMax(-1e9f, -1e9f, -1e9f);
	for (Object* Piece : Pieces)
	{
		CHECK(Piece->GetBrush().GetNumFaces() == 6);
		Vector3d Min, Max;
		GetWorldBounds(*Piece, Min, Max);
		UnionMin.NewVector(Min(UnionMin.GetX(), Min.GetX()), Min(UnionMin.GetY(), Min.GetY()), Min(UnionMin.GetZ(), Min.GetZ()));
		UnionMax.NewVector(Max(UnionMax.GetX(), Max.GetX()), Max(UnionMax.GetY(), Max.GetY()), Max(UnionMax.GetZ(), Max.GetZ()));

		const bool bOverlapsHole =
			Min.GetX() < 16 - 0.01f && Max.GetX() > -16 + 0.01f &&
			Min.GetY() < 16 - 0.01f && Max.GetY() > -16 + 0.01f &&
			Min.GetZ() < 16 - 0.01f && Max.GetZ() > -16 + 0.01f;
		CHECK_FALSE(bOverlapsHole);
	}
	CHECK(UnionMin.GetX() == doctest::Approx(-64.0f));
	CHECK(UnionMin.GetY() == doctest::Approx(-64.0f));
	CHECK(UnionMin.GetZ() == doctest::Approx(-64.0f));
	CHECK(UnionMax.GetX() == doctest::Approx(64.0f));
	CHECK(UnionMax.GetY() == doctest::Approx(64.0f));
	CHECK(UnionMax.GetZ() == doctest::Approx(64.0f));
}
