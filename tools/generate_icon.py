"""Vector source for Stunt Car Racer's elevated-track car."""

import sys
from pathlib import Path

sys.dont_write_bytecode = True
from iconlib import Canvas, generate

ROOT = Path(__file__).resolve().parents[1]


def draw(canvas: Canvas) -> None:
    """A red racing car on a rising track, not a game screenshot."""
    dark = (24, 38, 60)
    canvas.circle(50, 50, 48, dark)
    canvas.polygon(((6, 74), (88, 51), (94, 62), (15, 85)), (126, 147, 168))
    canvas.line(((12, 69), (86, 48)), 3, (255, 199, 70))
    canvas.polygon(((22, 48), (29, 36), (56, 29), (68, 36), (77, 36),
                    (79, 48), (23, 63)), (239, 67, 65))
    canvas.polygon(((34, 39), (54, 34), (61, 39), (31, 47)), (178, 229, 244))
    for x, y in ((33, 59), (69, 49)):
        canvas.circle(x, y, 8, dark)
        canvas.circle(x, y, 4, (230, 238, 242))


if __name__ == "__main__":
    raise SystemExit(generate(draw, ROOT / "src/res/stuntcarracer.ico"))
