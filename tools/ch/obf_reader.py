"""Minimal OBF container reader: top-level sections and their raw bytes.

An OBF file is a protobuf OsmAndStructure whose index sections are written as
tag + 4-byte big-endian length (wire type 6, OsmAnd's own) instead of a
varint-length wire type 2.  Section field numbers: 6 map, 7 address,
8 transport, 4 poi, 9 routing, 10 HH routing.
"""
import struct

ROUTING = 9
HH_ROUTING = 10


def _varint(f):
    shift = value = 0
    while True:
        byte = f.read(1)
        if not byte:
            return None
        value |= (byte[0] & 0x7F) << shift
        if not byte[0] & 0x80:
            return value
        shift += 7


def sections(path):
    """Yield (field_number, file_offset, length) for each length-prefixed section."""
    with open(path, 'rb') as f:
        while True:
            tag = _varint(f)
            if tag is None:
                return
            field, wire = tag >> 3, tag & 7
            if wire == 0:
                _varint(f)
            elif wire == 2:
                length = _varint(f)
                yield field, f.tell(), length
                f.seek(length, 1)
            elif wire == 6:
                length = struct.unpack('>I', f.read(4))[0]
                yield field, f.tell(), length
                f.seek(length, 1)
            elif wire == 5:
                f.seek(4, 1)
            elif wire == 1:
                f.seek(8, 1)
            else:
                raise ValueError('bad wire type %d at %d' % (wire, f.tell()))


def fields(buf, start=0, end=None, int6=()):
    """Yield (field_number, wire_type, value) over a message body.

    value is an int for varints and fixed32/64 (fixed32 big-endian, not
    sign-adjusted), and a (start, end) slice of buf for wire types 2 and 6.
    Wire type 6 is a 4-byte big-endian number: a body length for nested
    messages, but a plain value for the field numbers listed in int6 (route
    box shiftToData).  Works on OsmAnd's mixed encoding, unlike the generated
    protobuf parser.
    """
    position = start
    end = len(buf) if end is None else end
    while position < end:
        tag = shift = 0
        while True:
            byte = buf[position]
            position += 1
            tag |= (byte & 0x7F) << shift
            shift += 7
            if not byte & 0x80:
                break
        field, wire = tag >> 3, tag & 7
        if wire == 0:
            value = shift = 0
            while True:
                byte = buf[position]
                position += 1
                value |= (byte & 0x7F) << shift
                shift += 7
                if not byte & 0x80:
                    break
            yield field, wire, value
        elif wire == 2:
            length = shift = 0
            while True:
                byte = buf[position]
                position += 1
                length |= (byte & 0x7F) << shift
                shift += 7
                if not byte & 0x80:
                    break
            yield field, wire, (position, position + length)
            position += length
        elif wire == 6:
            length = struct.unpack_from('>I', buf, position)[0]
            position += 4
            if field in int6:
                yield field, wire, length
            else:
                yield field, wire, (position, position + length)
                position += length
        elif wire == 5:
            # OBF writes its fixed32 values (box shiftToData) big-endian.
            yield field, wire, struct.unpack_from('>I', buf, position)[0]
            position += 4
        elif wire == 1:
            yield field, wire, struct.unpack_from('<Q', buf, position)[0]
            position += 8
        else:
            raise ValueError('bad wire type %d at %d' % (wire, position))


def read_section(path, offset, length):
    with open(path, 'rb') as f:
        f.seek(offset)
        return f.read(length)
