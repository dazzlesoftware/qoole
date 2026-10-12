Data for GenesisMapEditor that is not tied to any game:

  ents/genesis.ent   entity definitions of the built-in "Genesis" game
  gems/*.gem         entity wireframes (the crosshair and undefined-entity
                     markers every entity set needs, a light and a player)
  brushes/*.qle      prefab brushes for Object > Add Brush

The gems and prefab brushes come from GenesisEditor.pak, the data file of
Qoole 99 (released under the GNU GPL with its source). The editor also
reads that pak when it is present, for the legacy games' entity sets,
palettes and the rest of the prefab library.
