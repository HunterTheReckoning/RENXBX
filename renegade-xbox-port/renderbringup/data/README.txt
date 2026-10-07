Files placed here are copied to the root of the test disc (D:\), where the engine finds them by
name as it would in the game's folder. Extract them from your own game archives, e.g.:

  python3 ../../tools/extract_from_mix.py /workspaces/gamedata/always.dat . l05_grass.dds if_gdi_logo2.dds

These are EA's game files: keep them out of git.

For the model scene, link the game's archives in (they are read as the game reads them):

  ln -sf /workspaces/gamedata/always.dat /workspaces/gamedata/Always2.dat /workspaces/gamedata/always.dbs \
        /workspaces/renegade-xbox-port/smoketest/data/M00_Tutorial.mix .
