# Star Catch

Drag the catcher to collect yellow stars and avoid red meteors. Missing a star
or touching a meteor costs one life; the game ends after three losses. Tap to
start or retry. Left/right keys also move the catcher; Enter or Space starts.

Activate the SDK environment, then build from the SDK root:

```powershell
aster build apps\starcatch --target xtensa --aot
aster verify apps\starcatch\dist\xtensa\aster.starcatch.ast --target xtensa
```

The icon is reproducible with `python3 apps/starcatch/make_icon.py`.
The package is `apps/starcatch/dist/xtensa/aster.starcatch.ast`. The target board may run
in landscape or portrait mode; gameplay scales to the surface dimensions.
