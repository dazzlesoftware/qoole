// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.

#include "MapCorePCH.h"
#include "GenesisGame.h"
#include "Texture.h"

GenesisGame::GenesisGame(void) : Game(GetNumUtils()) {
	strcpy(gameName, "Genesis");
	strcpy(palDef, "");
	strcpy(palName, "");
	strcpy(defTexture, "");
	strcpy(texExt, "");
}

bool
GenesisGame::Init(void) {
	if (initialized)
		return true;

	// No texture folders: every name is unknown and kept as given.
	texDB = new TexDB(this);
	initialized = true;
	return true;
}
