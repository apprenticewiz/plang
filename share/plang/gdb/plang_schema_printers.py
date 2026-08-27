"""gdb pretty-printer for plang EP §6.4.7 schema types with a run-time-varying
extent -- issue #130's real fix.

Why this exists, briefly (full account in project issue #130): DWARF has no
implementation in LLVM's own emitter for a computed MEMBER ADDRESS (confirmed
directly from llvm/lib/CodeGen/AsmPrinter/DwarfUnit.cpp's constructMemberDIE
-- an expression-typed member offset always becomes DW_AT_data_bit_offset, a
bitfield-only attribute, and gdb crashes trying to print a member built that
way). So a schema field declared AFTER a varying-extent one has no correct
static DWARF offset at all; plang's own -g output is honest about this (it
places such a field at the varying field's own probe-approximated size,
matching what the field's declared type COUNT would say for a length-1
instance) rather than anything malformed.

This script sidesteps DWARF for exactly those fields: alongside the DWARF
type, plang (when compiled with -g) writes a sidecar file next to each
source file it compiles, <source>.plang-schemas.json, describing every
run-time-varying schema type's REAL layout as data -- field order, sizes,
alignments, and each array field's bound as a small serialized arithmetic
form over the schema's own discriminants (see CGDebugInfo::jsonEncodeExtentForm
for the producer). This script reads that data and, entirely independently
of what the DWARF type says, walks it against the object's LIVE memory --
exactly the same walk plang's own runtime (SchemaLayoutEngine::rtWalkFields/
alignUpV) performs when it lays the object out in the first place, just
re-run here in Python instead of at compile time in LLVM IR or (the
approach that turned out not to work) DWARF opcodes.

Usage: from a running gdb session,
    (gdb) source /path/to/plang_schema_printers.py
(or add that line to your ~/.gdbinit).

IMPORTANT -- WHOLE-VALUE PRINTING ONLY (issue #145): once loaded, `print q^`
(printing a plang schema-typed value AS A WHOLE) shows every field's real
value, not just the ones DWARF alone could already get right. But a DIRECT
FIELD-PATH expression -- `print q^.tail`, `print q^.k`, `ptype q^.field` --
NEVER goes through this printer at all and may show a wrong value for any
field declared after a varying-extent one. This is not a bug in this script
and cannot be fixed by more plang-side code: gdb's pretty-printer API only
ever intercepts formatting of an already-fully-resolved value; a
sub-expression like `q^.k` is resolved by gdb's own C-expression evaluator,
straight off the (in this case wrong) DWARF member offset, before this
printer is ever invoked. Always use `print q^` (the whole value) to get a
correct field-by-field view of such a schema; never trust a direct
field-path access to a field declared after a varying one.

Known limitations, honestly stated rather than silently wrong:
- Direct field-path access (`print q^.field`, not the whole value `print
  q^`) always bypasses this printer -- see the callout above; this is a
  fundamental limitation of gdb's pretty-printer API, not something this
  script can work around.
- Only a plain (non-variant) schema body is handled; a body with a `case`
  variant part falls back to gdb's own default (DWARF-only, still
  approximate past a varying field) printing.
- A field whose own bound involves a nested schema instantiation, or a
  varying string capacity / subrange (not a plain array), is out of scope
  the same way -- the compiler-side recorder skips emitting layout data for
  a schema body containing one, so this script never sees it and falls back
  the same way.
- lldb support is a documented, deliberately separate follow-up, not
  attempted here -- lldb's synthetic-children/data-formatter Python API is
  a different enough shape (SBValue-based, not gdb.Value-based) to need its
  own script, not a port of this one.
"""

import json
import os

import gdb
import gdb.printing


def _eval_form(form, discs):
    """form: a JSON-decoded ["op", ...] list, per CGDebugInfo::jsonEncodeExtentForm.
    discs: a list of already-read discriminant int values, by index."""
    op = form[0]
    if op == "const":
        return form[1]
    if op == "disc":
        return discs[form[1]]
    if op == "neg":
        return -_eval_form(form[1], discs)
    a = _eval_form(form[1], discs)
    b = _eval_form(form[2], discs)
    if op == "add": return a + b
    if op == "sub": return a - b
    if op == "mul": return a * b
    if op == "div": return int(a / b) if b != 0 else 0
    if op == "mod": return a - b * int(a / b) if b != 0 else 0
    if op == "pow": return a ** b
    return 0


def _align_up(v, align):
    if align <= 1:
        return v
    return (v + align - 1) // align * align


class _SidecarCache(object):
    """One JSON sidecar per compiled source file, loaded lazily and cached
    for the lifetime of this gdb session -- re-reading it on every print
    would be needless I/O for a file that never changes once the program
    it describes is built."""

    def __init__(self):
        self._cache = {}

    def _candidate_paths(self):
        paths = []
        try:
            frame = gdb.selected_frame()
            sal = frame.find_sal()
            if sal and sal.symtab:
                fullname = sal.symtab.fullname()
                if fullname:
                    paths.append(fullname + ".plang-schemas.json")
        except gdb.error:
            pass
        try:
            progspace = gdb.current_progspace()
            if progspace and progspace.filename:
                paths.append(progspace.filename + ".plang-schemas.json")
        except (gdb.error, AttributeError):
            pass
        return paths

    def schemas_for_current_context(self):
        for path in self._candidate_paths():
            if path in self._cache:
                return self._cache[path]
            if os.path.exists(path):
                try:
                    with open(path, "r") as f:
                        data = json.load(f)
                    schemas = data.get("schemas", {})
                    self._cache[path] = schemas
                    return schemas
                except (IOError, ValueError):
                    continue
        return {}


_sidecar = _SidecarCache()


def _layout_walk(schema, base_addr, inferior):
    """Returns a list of (name, addr, kind, extra) for every field in
    declaration order, mirroring SchemaLayoutEngine::rtWalkFields/alignUpV
    exactly: align the running offset up to each field's own alignment,
    record it, then advance past that field's real (possibly
    discriminant-dependent) size."""
    discs = []
    for i, _ in enumerate(schema["discs"]):
        discs.append(_read_i64(inferior, base_addr + i * 8))

    off = schema["hdrBytes"]
    results = []
    for field in schema["fields"]:
        align = field.get("alignBytes", 8)
        off = _align_up(off, align)
        if field["kind"] == "array":
            lo = _eval_form(field["low"], discs)
            hi = _eval_form(field["high"], discs)
            count = max(0, hi - lo + 1)
            stride = _align_up(field["elemSizeBytes"], field["elemAlignBytes"])
            for name in field["names"]:
                results.append((name, base_addr + off, "array",
                                 (count, field["elemSizeBytes"])))
                off += count * stride
        else:
            size = field["sizeBytes"]
            for name in field["names"]:
                results.append((name, base_addr + off, "scalar", size))
                off += size
    return results


def _read_i64(inferior, addr):
    data = inferior.read_memory(addr, 8)
    return int.from_bytes(bytes(data), byteorder="little", signed=True)


class PlangSchemaValue(object):
    """gdb pretty-printer for one schema-typed value. children() drives
    both `print` (gdb formats a struct-shaped printer by its children
    automatically) and `print var.field`-style field access."""

    def __init__(self, val, schema, type_name):
        self.val = val
        self.schema = schema
        self.type_name = type_name

    def to_string(self):
        return self.type_name

    def children(self):
        try:
            base_addr = int(self.val.address)
        except gdb.error:
            return
        inferior = gdb.selected_inferior()
        # Discriminants first, exactly where DWARF already puts them
        # correctly -- shown here too so this printer's output is a
        # complete, self-consistent replacement rather than a partial one
        # a reader has to mentally merge with gdb's own default view.
        for i, name in enumerate(self.schema["discs"]):
            yield name, _read_i64(inferior, base_addr + i * 8)
        for name, addr, kind, extra in _layout_walk(self.schema, base_addr, inferior):
            if kind == "scalar":
                size = extra
                yield name, _read_i64(inferior, addr) if size == 8 else \
                    int.from_bytes(bytes(inferior.read_memory(addr, size)),
                                    byteorder="little", signed=True)
            else:
                count, elem_size = extra
                values = [
                    int.from_bytes(
                        bytes(inferior.read_memory(addr + j * elem_size, elem_size)),
                        byteorder="little", signed=True)
                    for j in range(count)
                ]
                yield name, "{" + ", ".join(str(v) for v in values) + "}"


def _plang_schema_pretty_printer(val):
    schemas = _sidecar.schemas_for_current_context()
    if not schemas:
        return None
    type_name = val.type.strip_typedefs().tag
    if not type_name or type_name not in schemas:
        return None
    return PlangSchemaValue(val, schemas[type_name], type_name)


def register(objfile=None):
    gdb.printing.register_pretty_printer(
        objfile, _plang_schema_pretty_printer, replace=True)


def _print_field_path_warning_once():
    """Issue #145: print a one-time, impossible-to-miss reminder that this
    printer only ever corrects WHOLE-value printing (`print q^`); a direct
    field-path expression (`print q^.field`) is resolved entirely by gdb's
    own C-expression evaluator, off the (potentially wrong) DWARF member
    offset, before this printer is ever consulted -- gdb's pretty-printer
    API has no hook into sub-expression evaluation, only into formatting an
    already-resolved value, so there is no way for this script to intercept
    or correct a field-path access. This is stated here, at load time, in
    addition to the module docstring, because a user who never reads the
    docstring but does source this script interactively should still see it
    before they trust a `print q^.field` result."""
    gdb.write(
        "plang_schema_printers.py loaded: for a schema with a run-time-"
        "varying extent (EP Section 6.4.7), use `print <var>^` (the WHOLE "
        "value) to see every field's correct value. Direct field-path "
        "access such as `print <var>^.field` bypasses this printer "
        "entirely -- gdb resolves that expression itself, straight off the "
        "DWARF member offset, before the pretty-printer ever runs -- and "
        "may show an incorrect value for a field declared after a varying "
        "one. See issue #145.\n")
    gdb.flush()


_print_field_path_warning_once()
register(gdb.current_objfile())
