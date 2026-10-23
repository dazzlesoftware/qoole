// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Containers/ContainerAliases.h"

// Owns cleanup for an operation with multiple exits. Cleanup must not throw.
class FScopeExit final
{
public:
	explicit FScopeExit(TFunction<void()> InCleanup) : Cleanup(std::move(InCleanup)) {}
	~FScopeExit()
	{
		if (Cleanup)
		{
			Cleanup();
		}
	}

	FScopeExit(const FScopeExit&) = delete;
	FScopeExit& operator=(const FScopeExit&) = delete;
	void Release()
	{
		Cleanup = {};
	}

private:
	TFunction<void()> Cleanup;
};
