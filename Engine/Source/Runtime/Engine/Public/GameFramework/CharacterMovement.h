// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "World/WorldLocation.h"

class FGameWorld;

struct FCharacterInput
{
	float Forward = 0, Right = 0, Up = 0; // -1 to 1; Up is used while flying
	bool bRun = false, bJump = false;
};

struct FCharacterMovementSettings
{
	float WalkSpeed = 600.0f; // world units a second
	float RunMultiplier = 2.5f;
	float FlySpeed = 4000.0f;
	float Gravity = 2000.0f;
	float JumpSpeed = 700.0f;
	float EyeHeight = 170.0f;
	float MaxDeltaSeconds = 0.1f; // longer frames are split no further, only capped
};

// Walking and flying over loaded terrain, as Unreal's character movement.
// The character's feet are a region-relative location; moving across a region
// boundary re-expresses it in the region it entered. Where no terrain is
// loaded below (the void between regions, or a region still streaming in)
// the character keeps its height instead of falling.
class FCharacterMovement
{
public:
	FCharacterMovementSettings Settings;
	FWorldLocation Location;  // feet
	float Yaw = 0, Pitch = 0; // degrees; yaw 0 faces +X, pitch up is positive
	float VerticalSpeed = 0;
	bool bOnGround = false, bFlying = false;

	void Look(float DeltaYaw, float DeltaPitch);
	void Update(const FGameWorld& World, const FCharacterInput& Input, float DeltaSeconds);
	FWorldLocation GetEyeLocation() const;
};
