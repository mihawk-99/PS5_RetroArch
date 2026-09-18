"""A minimal stand-in for the `jsonschema` package, for building third_party/mbedtls.

Why this file exists. mbedTLS regenerates two source files during its build by
running scripts/generate_driver_wrappers.py, which imports `jsonschema` and calls
`jsonschema.validate(...)` once to check its own driver JSON against a schema
before generating code. That import is the only use of the package, and it is not
installed on this machine.

Two honest ways to satisfy it: install python-jsonschema, or provide this. As
installed, the generated files are already present in the mbedTLS tree and the
generator is only re-run because a template is newer than them; the validation is
a development-time self-check of mbedTLS's own data files, not part of the
compiler path.

This module therefore does exactly what the caller needs and no more:
`validate()` checks the parts of JSON Schema the generator's schema uses, and
raises `exceptions.ValidationError` when a value does not match, so a genuinely
malformed data file still fails loudly. It is not a JSON Schema implementation and
does not pretend to be one.

Put it on the path with `PYTHONPATH=tooling/pystub` (tools/build-prospero.sh does
this). If the real package is installed, it takes precedence when this directory
is not on the path.
"""

from __future__ import annotations

from typing import Any


class exceptions:  # noqa: N801 - mirrors the real package's namespace
    class ValidationError(Exception):
        """Raised when a value does not match the schema."""

        def __init__(self, message: str, path: str = "") -> None:
            super().__init__(f"{message} (at {path})" if path else message)
            self.message = message
            self.path = path


_TYPES = {
    "object": dict,
    "array": list,
    "string": str,
    "boolean": bool,
    "number": (int, float),
    "integer": int,
    "null": type(None),
}


def _check(value: Any, schema: Any, path: str) -> None:
    if not isinstance(schema, dict):
        return

    expected = schema.get("type")
    if expected is not None:
        names = expected if isinstance(expected, list) else [expected]
        allowed = tuple(_TYPES[name] for name in names if name in _TYPES)
        # bool is a subclass of int; an integer field must not accept True.
        if "integer" in names and isinstance(value, bool):
            raise exceptions.ValidationError(f"expected {expected}, got boolean", path)
        if allowed and not isinstance(value, allowed):
            raise exceptions.ValidationError(f"expected {expected}, got {type(value).__name__}", path)

    if "enum" in schema and value not in schema["enum"]:
        raise exceptions.ValidationError(f"{value!r} is not one of {schema['enum']!r}", path)

    if isinstance(value, dict):
        for name in schema.get("required", []):
            if name not in value:
                raise exceptions.ValidationError(f"missing required property {name!r}", path)
        properties = schema.get("properties", {})
        for name, item in value.items():
            if name in properties:
                _check(item, properties[name], f"{path}/{name}")
            elif isinstance(schema.get("additionalProperties"), dict):
                _check(item, schema["additionalProperties"], f"{path}/{name}")
    elif isinstance(value, list) and "items" in schema:
        for index, item in enumerate(value):
            _check(item, schema["items"], f"{path}/{index}")


def validate(instance: Any = None, schema: Any = None, **kwargs: Any) -> None:
    """Validate `instance` against `schema`, raising ValidationError on a mismatch."""
    if schema is None:
        return
    _check(instance, schema, "#")
