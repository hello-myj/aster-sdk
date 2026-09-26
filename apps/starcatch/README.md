# Star Catch

Drag the catcher to collect yellow stars and avoid red meteors. Missing a star
or touching a meteor costs one life; the game ends after three losses. Tap to
start or retry. Left/right keys also move the catcher; Enter or Space starts.

Build with the Windows toolchain from the SDK root:

```powershell
.\dist\windows\aster-toolchain.exe build apps\starcatch --target lckfb-s3 --aot
.\dist\windows\aster-toolchain.exe verify dist\lckfb-s3\aster.starcatch.ast --target lckfb-s3
```

The icon is reproducible with `python3 apps/starcatch/make_icon.py`.
The package is `dist/lckfb-s3/aster.starcatch.ast`. The target board may run
in landscape or portrait mode; gameplay scales to the surface dimensions.
