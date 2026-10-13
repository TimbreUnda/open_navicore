#!/usr/bin/env python3
"""Compares rendered UI states with the navigation mockups.

  compare.py RENDER_DIR DESIGN_DIR OUT_DIR [STATE...]

RENDER_DIR holds <state>.argb from ui-render-tool. DESIGN_DIR holds
<state>_full.png (the mockup) and <state>_map.png (the mockup with the UI
hidden). Our overlay is blended over the mockup's own map, so only UI
pixels can differ. Writes <state>_cmp.png (design | ours | diff x4) and
prints mean/max error and the share of pixels off by more than 16 levels.
"""
import os
import sys

import numpy as np
from PIL import Image

W, H = 1024, 574


def main():
    render, design, out = sys.argv[1:4]
    os.makedirs(out, exist_ok=True)
    for state in sys.argv[4:] or ('free', 'preview', 'guidance', 'search', 'lock'):
        raw = np.fromfile(os.path.join(render, state + '.argb'), dtype=np.uint8)
        bgra = raw.reshape(H, W, 4).astype(np.float32)
        ui = bgra[..., [2, 1, 0]]
        alpha = bgra[..., 3:4] / 255.0
        base = np.asarray(Image.open(os.path.join(design, state + '_map.png')).convert('RGB'),
                          dtype=np.float32)
        ours = ui * alpha + base * (1.0 - alpha)
        mock = np.asarray(Image.open(os.path.join(design, state + '_full.png')).convert('RGB'),
                          dtype=np.float32)
        diff = np.abs(ours - mock).max(axis=2)
        changed = (np.abs(mock - base).max(axis=2) > 0) | (alpha[..., 0] > 0)
        print('%-9s mean=%.2f max=%d off>16: %.2f%% of UI pixels' % (
            state, diff[changed].mean(), diff.max(),
            100.0 * (diff[changed] > 16).mean()))
        strip = np.concatenate([mock, ours, np.clip(diff * 4, 0, 255)[..., None].repeat(3, 2)],
                               axis=1)
        Image.fromarray(strip.astype(np.uint8)).save(os.path.join(out, state + '_cmp.png'))


if __name__ == '__main__':
    main()
