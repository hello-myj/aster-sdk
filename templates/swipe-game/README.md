# Swipe game

A complete falling-block game with touch control and four sound effects.
Swipe left/right to move, tap to rotate, swipe down to drop; the top buttons pause or mute.

Run `aster build` then `aster run`. Edit `src/main.c` for UI/audio and `src/game.c` for game rules.
The manifest declares sources, icon, assets and target. No project build script is needed.
Assets are included, so building does not require Pillow or an asset-generation step.
