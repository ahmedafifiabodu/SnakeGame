"""Assemble a PNG sequence from `NeonCoil.exe --capture` into a GIF.

The game writes full 1920x960 frames; GitHub wants something a couple of
megabytes at most, so this downscales, quantises to a shared 256-colour
palette and writes one optimised GIF.

A shared palette matters more than usual here: the art is a narrow band of
neon on near-black, and per-frame palettes make that band shimmer between
frames. One palette sampled across the whole clip keeps it stable.

    python tools/make_gif.py <frame-dir> <out.gif> [--width 720] [--fps 15]
"""

import argparse
import pathlib
import sys

from PIL import Image


def build(frame_dir: pathlib.Path, out: pathlib.Path, width: int, fps: int, colours: int) -> None:
    paths = sorted(frame_dir.glob("frame_*.png"))
    if not paths:
        sys.exit(f"no frame_*.png in {frame_dir}")

    frames = []
    for path in paths:
        with Image.open(path) as image:
            frame = image.convert("RGB")
            height = round(frame.height * width / frame.width)
            frames.append(frame.resize((width, height), Image.LANCZOS))

    # Sample the palette from frames spread across the clip rather than from
    # the first one, so a colour that only shows up mid-run (a bonus fruit, an
    # ability tint) still gets a slot.
    step = max(1, len(frames) // 12)
    sampled = frames[::step]
    stacked = Image.new("RGB", (width, frames[0].height * len(sampled)))
    for index, frame in enumerate(sampled):
        stacked.paste(frame, (0, index * frame.height))

    palette = stacked.quantize(colors=colours, method=Image.MEDIANCUT)
    quantised = [frame.quantize(palette=palette, dither=Image.FLOYDSTEINBERG) for frame in frames]

    quantised[0].save(
        out,
        save_all=True,
        append_images=quantised[1:],
        duration=round(1000 / fps),
        loop=0,
        optimize=True,
        disposal=1,
    )

    print(f"{out}  {len(quantised)} frames  {out.stat().st_size / 1_048_576:.2f} MB")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("frames", type=pathlib.Path)
    parser.add_argument("out", type=pathlib.Path)
    parser.add_argument("--width", type=int, default=720)
    parser.add_argument("--fps", type=int, default=15)
    parser.add_argument("--colours", type=int, default=256)
    args = parser.parse_args()

    build(args.frames, args.out, args.width, args.fps, args.colours)


if __name__ == "__main__":
    main()
