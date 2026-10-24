// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "GameFramework/CharacterMovement.h"
#include "World/GameWorld.h"
#include <algorithm>
#include <cmath>

namespace
{
	constexpr float DegreesToRadians = 0.01745329252f;
	constexpr float MaxPitch = 89.0f;
} // namespace

void FCharacterMovement::Look(float DeltaYaw, float DeltaPitch)
{
	Yaw = std::fmod(Yaw + DeltaYaw, 360.0f);
	if (Yaw < 0)
		Yaw += 360.0f;
	Pitch = std::clamp(Pitch + DeltaPitch, -MaxPitch, MaxPitch);
}

void FCharacterMovement::Update(const FGameWorld& World, const FCharacterInput& Input, float DeltaSeconds)
{
	const float Delta = std::clamp(DeltaSeconds, 0.0f, Settings.MaxDeltaSeconds);
	const float YawRadians = Yaw * DegreesToRadians, PitchRadians = Pitch * DegreesToRadians;
	const float ForwardX = std::cos(YawRadians), ForwardY = std::sin(YawRadians);
	const float RightX = std::sin(YawRadians), RightY = -std::cos(YawRadians);
	const float Forward = std::clamp(Input.Forward, -1.0f, 1.0f), Right = std::clamp(Input.Right, -1.0f, 1.0f);
	float MoveX = ForwardX * Forward + RightX * Right, MoveY = ForwardY * Forward + RightY * Right;
	const float Length = std::sqrt(MoveX * MoveX + MoveY * MoveY);
	if (Length > 1.0f)
	{
		MoveX /= Length;
		MoveY /= Length;
	}
	float X = Location.Local.GetX(), Y = Location.Local.GetY(), Z = Location.Local.GetZ();
	if (bFlying)
	{
		const float Speed = Settings.FlySpeed * (Input.bRun ? Settings.RunMultiplier : 1.0f) * Delta;
		const float Horizontal = std::cos(PitchRadians);
		X += (ForwardX * Horizontal * Forward + RightX * Right) * Speed;
		Y += (ForwardY * Horizontal * Forward + RightY * Right) * Speed;
		Z += (std::sin(PitchRadians) * Forward + std::clamp(Input.Up, -1.0f, 1.0f)) * Speed;
		VerticalSpeed = 0;
		bOnGround = false;
	}
	else
	{
		const float Speed = Settings.WalkSpeed * (Input.bRun ? Settings.RunMultiplier : 1.0f) * Delta;
		X += MoveX * Speed;
		Y += MoveY * Speed;
		if (Input.bJump && bOnGround)
			VerticalSpeed = Settings.JumpSpeed;
		VerticalSpeed -= Settings.Gravity * Delta;
		Z += VerticalSpeed * Delta;
	}
	Location.Local = Vector3d(X, Y, Z);
	Location = World.Normalize(Location);
	float Ground = 0;
	if (!World.SampleGround(Location, Ground))
	{
		// Nothing loaded below: hold height until the ground streams in.
		if (!bFlying)
		{
			Location.Local = Vector3d(Location.Local.GetX(), Location.Local.GetY(), Z - VerticalSpeed * Delta);
			VerticalSpeed = 0;
		}
		return;
	}
	if (Location.Local.GetZ() <= Ground)
	{
		Location.Local = Vector3d(Location.Local.GetX(), Location.Local.GetY(), Ground);
		VerticalSpeed = 0;
		bOnGround = !bFlying;
		return;
	}
	bOnGround = false;
}

FWorldLocation FCharacterMovement::GetEyeLocation() const
{
	FWorldLocation Eye = Location;
	Eye.Local = Vector3d(Location.Local.GetX(), Location.Local.GetY(), Location.Local.GetZ() + Settings.EyeHeight);
	return Eye;
}
