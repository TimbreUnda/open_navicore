"""OBF protobuf schema, recovered from OsmAnd's generated OBF.pb.cc.

The core ships no .proto file, only the generated C++ whose
InternalAddGeneratedFile() call carries the serialized FileDescriptorProto as
C string literals.  This decodes it and builds message classes at runtime, so
the OBF routing and HH sections can be read from Python without protoc.

    from obf_schema import load
    messages = load(pb_cc_path)       # name -> message class, e.g. messages['OsmAndRoutingIndex']
"""
import re
import sys

from google.protobuf import descriptor_pb2, descriptor_pool, message_factory


_SIMPLE_ESCAPES = {'n': 10, 't': 9, 'r': 13, 'a': 7, 'b': 8, 'f': 12, 'v': 11,
                   '"': 34, "'": 39, '\\': 92, '?': 63}
_ESCAPE = re.compile(r'\\(?:([0-7]{1,3})|x([0-9a-fA-F]+)|(.))', re.S)


def _c_string_bytes(literal_body):
    # C escapes (octal, hex, \n, \", \\, \? ...) in a quoted literal -> bytes.
    out = bytearray()
    position = 0
    for match in _ESCAPE.finditer(literal_body):
        out += literal_body[position:match.start()].encode('latin-1')
        octal, hexa, simple = match.groups()
        out.append(int(octal, 8) & 0xFF if octal else
                   int(hexa, 16) & 0xFF if hexa else _SIMPLE_ESCAPES[simple])
        position = match.end()
    out += literal_body[position:].encode('latin-1')
    return bytes(out)


def descriptor_bytes(pb_cc_path):
    text = open(pb_cc_path, encoding='latin-1').read()
    start = text.index('InternalAddGeneratedFile(') + len('InternalAddGeneratedFile(')
    end = text.index(');', start)
    call = text[start:end]
    # Everything up to the final ", <length>" argument is string literals.
    literals = re.findall(r'"((?:[^"\\]|\\.)*)"', call)
    data = b''.join(_c_string_bytes(piece) for piece in literals)
    declared = int(call.rsplit(',', 1)[1])
    if len(data) != declared:
        raise ValueError('descriptor length %d != declared %d' % (len(data), declared))
    return data


def load(pb_cc_path):
    file_proto = descriptor_pb2.FileDescriptorProto.FromString(descriptor_bytes(pb_cc_path))
    pool = descriptor_pool.DescriptorPool()
    pool.Add(file_proto)
    classes = {}
    for name in file_proto.message_type:
        desc = pool.FindMessageTypeByName(file_proto.package + '.' + name.name)
        classes[name.name] = message_factory.GetMessageClass(desc)
    return classes, pool.FindFileByName(file_proto.name)


def dump(file_desc, only=None):
    def walk(desc, indent=''):
        print('%smessage %s' % (indent, desc.name))
        for field in desc.fields:
            kind = field.message_type.name if field.message_type else field.type
            print('%s  %2d %-28s %s %s' % (indent, field.number, field.name,
                                           'rep' if field.is_repeated else 'opt', kind))
        for nested in desc.nested_types:
            walk(nested, indent + '  ')
    for name, desc in file_desc.message_types_by_name.items():
        if only is None or name in only:
            walk(desc)


if __name__ == '__main__':
    _, file_desc = load(sys.argv[1])
    dump(file_desc, set(sys.argv[2:]) or None)
