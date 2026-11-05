"""Dependency-free validator for the checked-in source schema's JSON Schema subset."""
from datetime import datetime
import json
from pathlib import Path
import re
from urllib.parse import urlsplit


def validate(value, schema=None, location='$', root=None):
    if schema is None:
        schema = json.loads(Path(__file__).with_name('source.schema.json').read_text())
    if root is None:
        root = schema
    if '$ref' in schema:
        schema = root['$defs'][schema['$ref'].removeprefix('#/$defs/')]
    allowed = {'$schema', '$id', '$comment', '$defs', 'title', 'description', 'type', 'properties',
               'required', 'additionalProperties', 'items', 'minItems', 'maxItems', 'uniqueItems',
               'minLength', 'pattern', 'minimum', 'const', 'format'}
    if set(schema) - allowed:
        raise ValueError('Unsupported schema keyword')

    def check(condition, reason):
        if not condition:
            raise ValueError(location + ': ' + reason)

    types = {'object': dict, 'array': list, 'string': str, 'integer': int, 'boolean': bool}
    if 'type' in schema:
        check(type(value) is types[schema['type']], 'wrong type')
    if 'const' in schema:
        check(value == schema['const'], 'unexpected value')
    if isinstance(value, dict):
        check(set(schema.get('required', [])) <= set(value), 'missing required field')
        properties = schema.get('properties', {})
        for key, item in value.items():
            if key in properties:
                validate(item, properties[key], location + '.' + key, root)
            elif schema.get('additionalProperties') is False:
                check(False, 'unexpected field')
            elif isinstance(schema.get('additionalProperties'), dict):
                validate(item, schema['additionalProperties'], location + '.' + key, root)
    elif isinstance(value, list):
        check(len(value) >= schema.get('minItems', 0), 'too few items')
        check(len(value) <= schema.get('maxItems', len(value)), 'too many items')
        if schema.get('uniqueItems'):
            check(len({json.dumps(item, sort_keys=True) for item in value}) == len(value), 'duplicate item')
        for index, item in enumerate(value):
            if 'items' in schema:
                validate(item, schema['items'], location + '[' + str(index) + ']', root)
    elif isinstance(value, str):
        check(len(value) >= schema.get('minLength', 0), 'empty string')
        if 'pattern' in schema:
            check(re.search(schema['pattern'], value) is not None, 'pattern mismatch')
        if schema.get('format') == 'uri':
            parts = urlsplit(value)
            check(parts.scheme == 'https' and bool(parts.hostname) and not parts.username and not parts.password
                  and not any(c.isspace() for c in value), 'expected absolute HTTPS URL')
        if schema.get('format') == 'date-time':
            check(datetime.fromisoformat(value.replace('Z', '+00:00')).tzinfo is not None, 'timezone required')
    elif type(value) is int and 'minimum' in schema:
        check(value >= schema['minimum'], 'below minimum')
