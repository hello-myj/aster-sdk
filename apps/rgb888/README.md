# Adaptive Color Lab

One APP source chooses the color backend from runtime display capabilities:
P4 firmware with CONFIG_ASTER_P4_RGB666_OUTPUT uses a native BGR888 canvas,
full-color native commands, PPA fill and DMA2D copy, then RGB666 output.
An RGB565 device uses its existing 565 canvas and native firmware primitives.
The SDK converts per-primitive colors; no RGB888 framebuffer is allocated on 565.
Extra geometry such as alpha rectangles uses bounded guest-side 565 drawing.
This is immediate mode; legacy rendering may publish several sub-batches.
Uniform-alpha text requires the native RGB888 protocol and otherwise returns ENOTSUP.

From this directory:

```text
python ../../tools/aster.py build . --target riscv
python ../../tools/deploy.py dist/riscv/aster.example.rgb888.ast --target riscv --port COM11 --run
```

Build `--target xtensa` for lckfb. AOT is per-CPU; the source and color API are shared.
Four gradients are red/green/blue/gray, with fonts and alpha geometry underneath.
The layout follows the display dimensions. Up to five contact slots draw markers;
a device with fewer contacts naturally delivers fewer events.

`aster_rgb888.h` exposes the strict negotiated native interface (ENOTSUP on old
hosts). `aster_color.h` adds adaptive RGB565 fallback. Existing APP helpers and
legacy RGB565 image decoders/assets keep their original behavior and precision.
New packed RGB images can use `aster_color_blit_rgb888`, with final quantization
only when the selected destination is RGB565.

P4 480x480 native canvas: 675 KiB, pitch 1440 bytes. Overlay capture/restore preserves
all 24 stored color bits. Memory budgets include 1 MiB initial memory and a 4 MiB
maximum for this demonstration. Building is not physical validation on lckfb.

Physical P4 v3.2 verification: installed through shared UART and launched with
RISC-V AOT, 480x480, pitch=1440, native format=0x1903. The user confirmed normal
gradients/text/touch display and normal restoration after closing the settings
panel. The same source also builds as Xtensa AOT; lckfb was compile-tested only.
