// Copyright (C) Dazzle Software, LLC. Licensed under the GNU GPL v2 or later.
//
// The built-in "Genesis" game: maps for Genesis Engine, which has no game
// folder, texture files, palette or compile tools yet. Faces keep the
// texture names they are given (FaceTex remembers names the game cannot
// find), entity definitions come from ents/genesis.ent and models are not
// loaded. It needs no setup, so the editor can always open a map.

#ifndef _GENESISGAME_H_
#define _GENESISGAME_H_

#include "Game.h"

class GenesisGame : public Game {
public:
	GenesisGame(void);

	static const char *GetEntFileName(void) { return "genesis.ent"; }

	bool Init(void);
	WireFrameGeom *LoadModel(char *filename) { return NULL; }
	bool LoadTexture(Texture *texture, char *filename, int offset) { return false; }

	int GetNumUtils(void) const { return 1; }
	const char *GetUtilName(int index) const { return "None"; }
};

#endif // _GENESISGAME_H_
