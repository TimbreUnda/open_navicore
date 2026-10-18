"""Evaluate a routing.xml profile's way attributes (access, oneway, speed,
priority) for an OBF road, without the OsmAnd core.

GeneralRouter semantics, as read from the car profile: the <select> elements
of an attribute are tried in document order and the first whose conditions
hold gives the value.  A select matches a road when the road has tag t (and
value v, when given); without t it always matches.  <if param>/<ifnot param>
and numeric <ge/gt/le/lt> containers gate their selects.  A select with inner
numeric conditions (vehicle size limits against a parameter) never matches
with the default parameters (all 0).

Not modelled: point obstacles (traffic signals), turn penalties,
penalty_transition, restrictions, directional maxspeed.
"""
import re
import xml.etree.ElementTree as ET

_NUMBER = re.compile(r'^\s*(\d+(?:\.\d+)?)\s*(mph|knots)?\s*$')


def parse_speed(text):
    """maxspeed tag value -> km/h, or None when it is not a plain number."""
    match = _NUMBER.match(text)
    if not match:
        return None
    value = float(match.group(1))
    if match.group(2) == 'mph':
        value *= 1.609344
    elif match.group(2) == 'knots':
        value *= 1.852
    return value


class Attribute:
    def __init__(self, element, params):
        self.rules = []   # (tag, value, result)
        self._walk(element, params)

    def _walk(self, element, params):
        for child in element:
            if child.tag == 'select':
                if len(child):   # inner numeric conditions on vehicle limits
                    continue
                self.rules.append((child.get('t'), child.get('v'), child.get('value')))
            elif child.tag == 'if':
                if self._param(child.get('param'), params):
                    self._walk(child, params)
            elif child.tag == 'ifnot':
                if not self._param(child.get('param'), params):
                    self._walk(child, params)
            elif child.tag in ('ge', 'gt', 'le', 'lt'):
                left = float(params.get(child.get('value1', '').lstrip(':'), 0) or 0)
                right = float(child.get('value2'))
                holds = {'ge': left >= right, 'gt': left > right,
                         'le': left <= right, 'lt': left < right}[child.tag]
                if holds:
                    self._walk(child, params)

    @staticmethod
    def _param(name, params):
        return bool(params.get(name))

    def evaluate(self, tags):
        """tags: dict tag -> value.  Returns the first matching rule's value
        (a number, or None when no rule matches)."""
        for tag, value, result in self.rules:
            if tag is not None:
                if tag not in tags or (value is not None and tags[tag] != value):
                    continue
            if result.startswith('$'):
                source = tags.get(result[1:])
                number = parse_speed(source) if source is not None else None
                if number is None:
                    continue
                return number
            return float(result)
        return None


class Profile:
    def __init__(self, xml_path, name='car', params=None):
        params = params or {}
        root = ET.parse(xml_path).getroot()
        for element in root.iter('routingProfile'):
            if element.get('name') == name and element.get('baseProfile') == name:
                profile = element
                break
        else:
            raise ValueError('no profile %s in %s' % (name, xml_path))
        self.min_speed = float(profile.get('minSpeed'))
        self.default_speed = float(profile.get('defaultSpeed'))
        self.max_speed = float(profile.get('maxSpeed'))
        self.attributes = {}
        for element in profile.findall('way'):
            self.attributes[element.get('attribute')] = Attribute(element, params)

    def road(self, tags):
        """-> (access, oneway, speed_kmh, priority) for a tag dict."""
        access = self.attributes['access'].evaluate(tags)
        oneway = self.attributes['oneway'].evaluate(tags) or 0
        speed = self.attributes['speed'].evaluate(tags)
        if speed is None:
            speed = self.default_speed
        speed = max(self.min_speed, min(self.max_speed, speed))
        priority = self.attributes['priority'].evaluate(tags)
        return (access is not None and access > 0), int(oneway), speed, \
            0.7 if priority is None else priority
