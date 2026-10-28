#!/usr/bin/env python3
"""Export an audition pack to navi_voiced's offline recorded-pack layout.

Usage: python3 tools/export_recorded_voice_pack.py COMPACT_PACK OUTPUT_DIRECTORY
The source contains pack.json, recipes.json, and male/female/clips/*.wav.
No generation, network access, or audio re-encoding is performed.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import wave


def export(source, output):
    pack = json.loads((source / 'pack.json').read_text())
    recipes = json.loads((source / 'recipes.json').read_text())
    ids = {row['id'] for row in pack['fragments']}
    if len(ids) != len(pack['fragments']) or not ids:
        raise ValueError('Empty or duplicate fragment IDs')
    if any(not re.fullmatch(r'[a-z0-9-]{1,64}', ident) for ident in ids):
        raise ValueError('Invalid fragment ID')
    mapping = {}
    for row in recipes:
        clips = row['clips']
        if not 1 <= len(clips) <= 3 or not set(clips) <= ids:
            raise ValueError('Invalid recipe clips')
        # Include spoken words, existing digit-based prompts, and corrected U-turn wording.
        for text in [row['text'], row['runtime_text'], row['runtime_text'].replace('розвернітеся', 'розверніться').replace('Розвернітеся', 'Розверніться')]:
            text = text.rstrip('.\r ')
            if not text or '\t' in text or '\n' in text:
                raise ValueError('Invalid recipe text')
            if text in mapping and mapping[text] != clips:
                raise ValueError('Conflicting recipe')
            mapping[text] = clips
    if len(mapping) > 8192:
        raise ValueError('Too many recipe aliases')
    # Validate every clip before writing output, including the radio format and size bound.
    for voice in ['male', 'female']:
        for ident in sorted(ids):
            path = source / voice / 'clips' / (ident + '.wav')
            with wave.open(str(path), 'rb') as audio:
                if ((audio.getnchannels(), audio.getsampwidth(), audio.getframerate()) != (1, 2, 24000)
                        or not 0 < audio.getnframes() <= 240000):
                    raise ValueError('Invalid clip format: ' + str(path))
    for voice in ['male', 'female']:
        dest = output / voice / 'clips'
        dest.mkdir(parents=True, exist_ok=True)
        for ident in sorted(ids):
            shutil.copyfile(source / voice / 'clips' / (ident + '.wav'), dest / (ident + '.wav'))
    manifest = ['NAVI-RECORDED-1\t24000\t480']
    manifest += [text + '\t' + ','.join(clips) for text, clips in sorted(mapping.items())]
    (output / 'recipes.tsv').write_text('\n'.join(manifest) + '\n')
    (output / 'pack.json').write_text(json.dumps(pack, ensure_ascii=False, indent=2) + '\n')
    print(f'Exported {len(ids)} clips per voice, {len(recipes)} sentences, {len(mapping)} lookup aliases')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    export(args.source, args.output)
