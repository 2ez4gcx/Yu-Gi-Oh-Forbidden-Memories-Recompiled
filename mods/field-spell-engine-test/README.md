# Field spell engine test

This mod is off by default. Enable it in **Game > Mods**, then restart the
game.

It gives the retail Mountain field one deliberately conspicuous blue-magenta
image in `mod.json`; the engine cuts it into Mountain's five floor textures.
The same texture must appear through both paths:

1. Add `Prism Peak` (the newly added card which copies Mountain) to a deck
   and play it.
2. Summon `Kuriboh` face up. Its test monster effect invokes retail
   `Mountain`, without playing a Mountain card.

In both cases the floor must change to the identical blue-magenta test
texture, while the arena walls stay as they were at duel start. `Prism Peak`
is an added card, so for a quick manual test run with
`MEMORIES_DEBUG_CHEST=3` and add it in Build Deck.
