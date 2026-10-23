// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
#include "MapFeaturePanelHost.h"
#include "MapDocument.h"
void FMapPanelTransaction::Begin(FMapDocument* Current, const FString& Description)
{
	if (!Current || Current->IsInTransaction())
		return;
	Current->BeginTransaction(Description);
	Owner = Current;
	Id = Current->GetActiveTransactionId();
}
void FMapPanelTransaction::End(FMapDocument* Current)
{
	if (Current && Current == Owner && Id != 0 && Current->GetActiveTransactionId() == Id)
		Current->EndTransaction();
	Owner = nullptr;
	Id = 0;
}
