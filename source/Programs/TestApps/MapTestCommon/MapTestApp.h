// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// FMapTestApp: a test application with the map core running. It starts the
// map editor environment with the built-in Genesis game, owns a map document
// and its views (FMapViewportClient) with their shared settings, and has
// helpers for scripts: prefabs by name, map points in window coordinates,
// and a report of the document in the log.

#pragma once

#include "MapCoreHeaders.h"
#include "MapDocument.h"
#include "MapViewportClient.h"
#include "TestApp.h"

class SViewport;

class FMapTestApp : public FTestApp
{
public:
	using FTestApp::FTestApp;

protected:
	// Starts the environment and opens an empty map.
	bool StartServices() override;
	// Releases the views and the document, then stops the environment.
	// Apps that override it call it last.
	void OnShutdown() override;

	FMapDocument& GetDocument() const { return *Document; }
	// Replaces the document (the views follow).
	void SetDocument(TUniquePtr<FMapDocument> NewDocument);

	// A view of the document, drawn by the window renderer, and its widget.
	TSharedRef<FMapViewportClient> CreateView(EMapViewType Type);
	TSharedRef<SViewport> MakeViewport(const TSharedRef<FMapViewportClient>& View) const;
	const TArray<TSharedPtr<FMapViewportClient>>& GetViews() const { return Views; }

	// A built-in or user prefab's path by name; empty (and a failed check) if none.
	FString FindPrefab(const char* Name);
	// The first point entity of a class in the scope.
	Object* FindByClass(const char* ClassName) const;
	// Where a map point (scope space) is in the window; (-1, -1) if not in the view.
	FVector2 MapToWindow(const FMapViewportClient& View, const Vector3d& Point) const;
	// Logs the counts and the selection bounds of the document.
	void Report(const char* What) const;
	// The selection's bounds; zero when nothing is selected.
	void GetSelectionBounds(Vector3d& OutMin, Vector3d& OutMax) const;

	// A small scene: a plate, a cube, a column, a sphere and a pyramid, a
	// player start and a light. Nothing selected.
	void BuildScene();

	FMapViewSettings ViewSettings;
	FString CursorText;
	FString ToolMessage;

	// The objects of BuildScene.
	Object* Cube = nullptr;
	Object* Column = nullptr;
	Object* Sphere = nullptr;
	Object* Pyramid = nullptr;

private:
	TUniquePtr<FMapRenderResources> RenderResources;
	TUniquePtr<FMapDocument> Document;
	TArray<TSharedPtr<FMapViewportClient>> Views;
	bool bEnvironmentStarted = false;
};
