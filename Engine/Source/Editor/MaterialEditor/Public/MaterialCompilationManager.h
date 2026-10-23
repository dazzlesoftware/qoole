// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#pragma once
#include "Assets/RenderAssets.h"
#include <condition_variable>
#include <mutex>
#include <thread>

struct FMaterialCompilationResult
{
	uint64 Revision = 0;
	bool bSuccess = false;
	FMaterialAsset Material;
	FString Error;
};
// One worker per editing session. Pending requests coalesce to the latest
// snapshot; outdated results never publish. This class never accesses Slate,
// the registry or GPU resources. Consumers publish results on the UI thread.
class FMaterialCompilationManager
{
  public:
	FMaterialCompilationManager();
	~FMaterialCompilationManager();
	FMaterialCompilationManager(const FMaterialCompilationManager&) = delete;
	FMaterialCompilationManager& operator=(const FMaterialCompilationManager&) = delete;
	void Submit(const FMaterialAsset&, uint64 Revision);
	TOptional<FMaterialCompilationResult> TakeCompleted();
	bool IsBusy() const;

  private:
	void Run();
	mutable std::mutex Mutex;
	std::condition_variable Wake;
	TOptional<FMaterialCompilationResult> Pending, Completed;
	uint64 LatestRevision = 0;
	bool bStopping = false, bRunning = false;
	std::thread Worker;
};
