// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "MaterialCompilationManager.h"
#include "Internationalization/TextLocalization.h"
#include "MaterialEditingLibrary.h"
#include <exception>
FMaterialCompilationManager::FMaterialCompilationManager() : Worker([this] { Run(); }) {}
FMaterialCompilationManager::~FMaterialCompilationManager()
{
	{
		std::lock_guard Lock(Mutex);
		bStopping = true;
		Pending.reset();
	}
	Wake.notify_one();
	Worker.join();
}
void FMaterialCompilationManager::Submit(const FMaterialAsset& M, uint64 Revision)
{
	std::lock_guard Lock(Mutex);
	if (Revision < LatestRevision || bStopping)
		return;
	LatestRevision = Revision;
	Pending = FMaterialCompilationResult{Revision, false, M, {}};
	Completed.reset();
	Wake.notify_one();
}
TOptional<FMaterialCompilationResult> FMaterialCompilationManager::TakeCompleted()
{
	std::lock_guard Lock(Mutex);
	TOptional<FMaterialCompilationResult> Result = std::move(Completed);
	Completed.reset();
	return Result;
}
bool FMaterialCompilationManager::IsBusy() const
{
	std::lock_guard Lock(Mutex);
	return bRunning || Pending.has_value() || Completed.has_value();
}
void FMaterialCompilationManager::Run()
{
	for (;;)
	{
		FMaterialCompilationResult Job;
		{
			std::unique_lock Lock(Mutex);
			Wake.wait(Lock, [&] { return bStopping || Pending.has_value(); });
			if (bStopping)
				return;
			Job = std::move(*Pending);
			Pending.reset();
			bRunning = true;
		}
		try
		{
			Job.bSuccess = FMaterialEditingLibrary::CompileMaterial(Job.Material, Job.Error);
		}
		catch (const std::exception& E)
		{
			Job.Error = E.what();
		}
		catch (...)
		{
			Job.Error = GENESIS_TEXT("Material compiler failed with an unknown exception.");
		}
		{
			std::lock_guard Lock(Mutex);
			bRunning = false;
			if (!bStopping && Job.Revision == LatestRevision && !Pending)
				Completed = std::move(Job);
		}
	}
}
