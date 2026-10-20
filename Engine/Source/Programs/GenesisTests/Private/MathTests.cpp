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

#include "doctest.h"

#include "Math3D.h"

namespace
{
	void CheckVector(const Vector3d& Vec, float X, float Y, float Z)
	{
		CHECK(Vec.GetX() == doctest::Approx(X).epsilon(0.0001));
		CHECK(Vec.GetY() == doctest::Approx(Y).epsilon(0.0001));
		CHECK(Vec.GetZ() == doctest::Approx(Z).epsilon(0.0001));
	}
}

TEST_CASE("Vector3d arithmetic")
{
	Vector3d A(1.0f, 2.0f, 3.0f);
	Vector3d B(4.0f, -5.0f, 6.0f);

	CHECK(A.DotVector(B) == doctest::Approx(12.0f));
	CHECK(A.GetMagSqr() == doctest::Approx(14.0f));

	Vector3d Sum;
	CheckVector(Sum.AddVector(A, B), 5.0f, -3.0f, 9.0f);
	Vector3d Diff;
	CheckVector(Diff.SubVector(A, B), -3.0f, 7.0f, -3.0f);
	Vector3d Scaled;
	CheckVector(Scaled.MultVector(A, 2.0f), 2.0f, 4.0f, 6.0f);

	Vector3d Cross;
	CheckVector(Cross.CrossVector(Vector3d(1, 0, 0), Vector3d(0, 1, 0)), 0.0f, 0.0f, 1.0f);

	Vector3d Unit(3.0f, 0.0f, 4.0f);
	Unit.Normalize();
	CHECK(Unit.GetMag() == doctest::Approx(1.0f));
	CheckVector(Unit, 0.6f, 0.0f, 0.8f);

	CHECK(Vector3d(1, 0, 0).IsParallel(Vector3d(-2, 0, 0)));
	CHECK_FALSE(Vector3d(1, 0, 0).IsParallel(Vector3d(0, 1, 0)));
	CHECK(Vector3d(1, 2, 3) == Vector3d(1, 2, 3));
}

TEST_CASE("Plane from three points follows the .map winding")
{
	// The x = -64 face of a Quake brush; its normal points out of the brush.
	Plane Face(Vector3d(-64, -64, -16), Vector3d(-64, -63, -16), Vector3d(-64, -64, -15));
	CheckVector(Face.GetNorm(), -1.0f, 0.0f, 0.0f);

	CHECK(Face.DistOffPlane(Vector3d(-80, 0, 0)) == doctest::Approx(16.0f));
	CHECK(Face.DistOffPlane(Vector3d(0, 0, 0)) == doctest::Approx(-64.0f));
	CHECK(Face.PointInPlane(Vector3d(-64, 100, 100)) == 0);

	// Three axis planes meet in one point.
	Plane X(Vector3d(1, 0, 0), Vector3d(10, 0, 0));
	Plane Y(Vector3d(0, 1, 0), Vector3d(0, 20, 0));
	Plane Z(Vector3d(0, 0, 1), Vector3d(0, 0, 30));
	Vector3d Corner;
	REQUIRE(X.Intersect(Y, Z, Corner));
	CheckVector(Corner, 10.0f, 20.0f, 30.0f);

	// A line through the origin along x hits the x = 10 plane.
	Vector3d Hit;
	REQUIRE(X.Intersect(Line(Vector3d(0, 0, 0), Vector3d(1, 0, 0)), Hit));
	CheckVector(Hit, 10.0f, 0.0f, 0.0f);
}

TEST_CASE("Matrix44 transforms")
{
	Matrix44 M;
	Vector3d V(1.0f, 0.0f, 0.0f);

	M.SetIdentity();
	CheckVector(M.Transform(V), 1.0f, 0.0f, 0.0f);

	M.SetRotateZ(DEG2RAD(90.0f));
	V.NewVector(1.0f, 0.0f, 0.0f);
	CheckVector(M.Transform(V), 0.0f, 1.0f, 0.0f);

	M.SetTranslation(Vector3d(5.0f, -2.0f, 3.0f));
	V.NewVector(1.0f, 1.0f, 1.0f);
	CheckVector(M.Transform(V), 6.0f, -1.0f, 4.0f);

	M.SetScale(Vector3d(2.0f, 3.0f, 4.0f));
	V.NewVector(1.0f, 1.0f, 1.0f);
	CheckVector(M.Transform(V), 2.0f, 3.0f, 4.0f);

	// Translate after scale: T * S.
	Matrix44 T, S, TS;
	T.SetTranslation(Vector3d(10.0f, 0.0f, 0.0f));
	S.SetScale(Vector3d(2.0f, 2.0f, 2.0f));
	TS.Multiply(T, S);
	V.NewVector(1.0f, 1.0f, 1.0f);
	CheckVector(TS.Transform(V), 12.0f, 2.0f, 2.0f);

	// A translation and its inverse cancel out.
	Matrix44 Inv, Round;
	Inv.SetInvTranslation(Vector3d(10.0f, 0.0f, 0.0f));
	Round.Multiply(Inv, T);
	V.NewVector(7.0f, 8.0f, 9.0f);
	CheckVector(Round.Transform(V), 7.0f, 8.0f, 9.0f);
}
