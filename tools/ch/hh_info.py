"""Print the HH routing sections of OBF files: edition, profile and parameters.

Only the header fields (edition, profile, profileParams) are read, so this is
fast and does not need the schema.

    python3 hh_info.py <map.obf>...
"""
import datetime
import glob
import os
import sys

import obf_reader


def header(buf):
    edition, profile, params = 0, '', []
    for field, wire, value in obf_reader.fields(buf):
        if field == 1:
            edition = value
        elif field == 2:
            profile = buf[value[0]:value[1]].decode()
        elif field == 3:
            params.append(buf[value[0]:value[1]].decode())
        elif field >= 4:
            break  # tag table, point boxes and segments follow
    return edition, profile, params


def main(paths):
    for path in paths:
        row = []
        for field, offset, length in obf_reader.sections(path):
            if field != obf_reader.HH_ROUTING:
                continue
            with open(path, 'rb') as f:
                f.seek(offset)
                buf = f.read(4096)  # the header is a few hundred bytes
            edition, profile, params = header(buf)
            when = datetime.datetime.fromtimestamp(edition / 1000, datetime.timezone.utc)
            row.append('%s(%s) %.1fMB %s' % (profile, ','.join(p for p in params if p),
                                              length / 1e6, when.strftime('%Y-%m-%d %H:%M')))
        print(os.path.basename(path), '|', ' | '.join(row) or 'no HH')


if __name__ == '__main__':
    main(sorted(p for arg in sys.argv[1:] for p in glob.glob(arg)))
