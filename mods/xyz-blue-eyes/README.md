# XYZ Blue-Eyes Frame

This mod gives **Blue-Eyes White Dragon** an XYZ-style full card frame and
matching duel-hand, Build Deck/Trade, and Library icons.

All other cards retain their retail large-card frames and normal small-frame
colors. Enable `XYZ Blue-Eyes Frame` in the in-game Mods window, then restart
the game if it is already running.

Files in `frames/`:

- `xyz_frame1.png` — full 140×196 card frame (the ornate dark-metal frame
  `frame_styles.purple` actually uses). `art`/`atk`/`def`/`stars`/`attribute`
  in `mod.json`'s `card_layout` are measured against this image's own art
  window (top) and stat band (bottom) — nudge them in `mod.json` if the
  numbers or icon don't look centered once you've seen it rendered; a thin
  band crowded with an 8-star monster's stats is tight by nature.
- `xyz_frame.png` — an earlier, unused draft (aspect ratio doesn't match the
  frame texture's 531×762 and it has no real alpha cutout); kept here but not
  referenced by `mod.json`. Safe to delete.
- `xyz_hand.png` — 52×60 duel/field hand frame
- `xyz_menu_icon.png` — 16×16 Build Deck/Trade icon
- `xyz_library_icon.png` — 12×14 Library icon
