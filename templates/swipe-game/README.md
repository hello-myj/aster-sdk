# Swipe game

A complete falling-block game with touch control and four sound effects.
Swipe left/right to move, tap to rotate, swipe down to drop; the top buttons pause or mute.

From the generated project, run `aster build` and then `aster run --port PORT`. Use the SDK architecture selected at project creation; the device checks required capabilities at launch. Edit `src/main.c` for UI/audio and `src/game.c` for game rules.
The manifest declares sources, icon, assets and target. No project build script is needed.
Assets are included, so building does not require Pillow or an asset-generation step.
