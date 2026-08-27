"""gdb pretty-printer for plang EP §6.4.7 schema types with a run-time-varying
extent -- issue #130's real fix, extended by issues #140/#141 to give the
sidecar a real identity.

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
alignments, each field's plang type kind (issue #144), and each array
field's bound as a small serialized arithmetic form over the schema's own
discriminants (see CGDebugInfo::jsonEncodeExtentForm for the producer).
This script reads that data and, entirely independently of what the DWARF
type says, walks it against the object's LIVE memory -- exactly the same
walk plang's own runtime (SchemaLayoutEngine::rtWalkFields/alignUpV)
performs when it lays the object out in the first place, just re-run here
in Python instead of at compile time in LLVM IR or (the approach that
turned out not to work) DWARF opcodes.

Issue #140 (same name, different body): two schema types (two different
procedures, or two different modules) can declare their own schema type
under the identical NAME with a completely different field layout. The
sidecar now records every distinct layout it sees under a name as its own
entry, keyed by a structural fingerprint (CGDebugInfo::computeSchemaFingerprint
-- a hash over the discriminant count and every body field's own name/kind/
byte size, in DWARF declaration order). This script recomputes the identical
fingerprint from the live value's own DWARF type (_type_fingerprint below,
walking the same DIType shape buildSchemaDIType built: N named discriminant
members, then one final anonymous member wrapping the body struct) and picks
the matching sidecar variant -- so a live value gets the layout that
actually describes IT, not just the first same-named layout the sidecar
happens to list first.

Issue #141 (stale sidecar): a rebuild of the same source path (even to a
different output, even without -g) overwrites the one sidecar file next to
it, so a gdb session against an OLDER already-built -g binary can load a
sidecar written by a LATER, different compile. Every compile mints a random
64-bit "buildId", embeds it in the sidecar's own top-level "buildId" field
AND in the live binary's own DW_AT_producer string (readable back via
gdb.Symtab.producer, independent of the sidecar file). This script compares
the two before trusting the sidecar at all; a mismatch prints a one-time
warning and falls back to gdb's own default (DWARF-only, honestly
approximate) printing rather than confidently showing a different program's
layout.

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
- If a name collides in the sidecar (more than one variant) and this live
  value's own fingerprint matches NONE of them (e.g. it was compiled by a
  version of plang whose fingerprint formula drifted from this script's --
  see CGDebugInfo::computeSchemaFingerprint's own comment), this falls back
  to gdb's default printing with a one-time warning, rather than guessing
  which variant is closest.
- lldb support is a documented, deliberately separate follow-up, not
  attempted here -- lldb's synthetic-children/data-formatter Python API is
  a different enough shape (SBValue-based, not gdb.Value-based) to need its
  own script, not a port of this one.

issue #144 also hardened this script against malformed/unexpected sidecar
content: every risky operation (indexing into the JSON, walking an
ExtentForm, reading target memory) is wrapped so a missing key or a
pathologically nested form degrades to a clear, bounded message -- either
for just the one field that hit it (the rest of the struct still prints)
or, for a structural failure (no "fields"/"discs" at all), by returning
None so gdb falls back to its own default DWARF printing instead of
splicing a raw Python traceback into the output.
"""

import struct
import sys

import json
import os
import re
import sys

import gdb
import gdb.printing


def _fnv1a64(s):
    """FNV-1a, 64-bit -- must match CGDebugInfo.cpp's fnv1a64 exactly, byte
    for byte, since the two are never compared by re-deriving one from the
    other; they only ever need to land on the identical 64-bit value from
    the identical canonical string."""
    h = 0xcbf29ce484222325
    for b in s.encode("utf-8"):
        h ^= b
        h = (h * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h


class _SchemaDataError(Exception):
    """Raised for a sidecar shape this script cannot make sense of --
    caught close to where it happens so one bad field or a structurally
    broken schema entry degrades cleanly instead of as a raw traceback."""


# Matches Sema's own MaxExprDepth (lib/Sema/SemaExpr.cpp) -- an ExtentForm's
# nesting is bounded by the same expression-depth guard on the compiler
# side, so a form nested past this is not a real one; walking it further
# would just be chasing a corrupted or hand-edited sidecar into a Python
# RecursionError.
#
# Python's own default recursion limit (1000) is already partly spent on
# gdb's own call stack by the time _eval_form's first call is reached, so
# raising it here gives this guard the headroom to actually fire -- a
# clean _SchemaDataError -- instead of losing the race to a raw
# RecursionError from the interpreter itself.
_MAX_FORM_DEPTH = 1000
if sys.getrecursionlimit() < _MAX_FORM_DEPTH + 500:
    sys.setrecursionlimit(_MAX_FORM_DEPTH + 500)


def _eval_form(form, discs, depth=0):
    """form: a JSON-decoded ["op", ...] list, per CGDebugInfo::jsonEncodeExtentForm.
    discs: a list of already-read discriminant int values, by index."""
    if depth > _MAX_FORM_DEPTH:
        raise _SchemaDataError("extent form nested past %d levels" % _MAX_FORM_DEPTH)
    if not isinstance(form, list) or not form:
        raise _SchemaDataError("malformed extent form: %r" % (form,))
    op = form[0]
    if op == "const":
        if len(form) < 2:
            raise _SchemaDataError("'const' form missing its value")
        return form[1]
    if op == "disc":
        if len(form) < 2:
            raise _SchemaDataError("'disc' form missing its index")
        try:
            return discs[form[1]]
        except (IndexError, TypeError):
            raise _SchemaDataError("'disc' index %r out of range" % (form[1],))
    if op == "neg":
        if len(form) < 2:
            raise _SchemaDataError("'neg' form missing its operand")
        return -_eval_form(form[1], discs, depth + 1)
    if len(form) < 3:
        raise _SchemaDataError("'%s' form missing an operand" % (op,))
    a = _eval_form(form[1], discs, depth + 1)
    b = _eval_form(form[2], discs, depth + 1)
    if op == "add": return a + b
    if op == "sub": return a - b
    if op == "mul": return a * b
    if op == "div": return int(a / b) if b != 0 else 0
    if op == "mod": return a - b * int(a / b) if b != 0 else 0
    if op == "pow": return a ** b
    raise _SchemaDataError("unknown extent form op %r" % (op,))


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

    def sidecar_for_current_context(self):
        """Returns the parsed sidecar dict ({"buildId":..., "schemas":{...}})
        for whichever candidate path exists and parses, or None."""
        for path in self._candidate_paths():
            if path in self._cache:
                return self._cache[path]
            if os.path.exists(path):
                try:
                    with open(path, "r") as f:
                        data = json.load(f)
                    self._cache[path] = data
                    return data
                except (IOError, ValueError):
                    continue
        return None


_sidecar = _SidecarCache()
# Names already warned about (either a buildId mismatch or an unmatched
# fingerprint) -- printed once per gdb session per name, not once per print,
# so stepping through a loop of prints doesn't spam the same warning.
_warned = set()


def _warn_once(key, message):
    if key in _warned:
        return
    _warned.add(key)
    print("plang schema pretty-printer: " + message, file=sys.stderr)


def _live_build_id():
    """The "schemabuildid:<16 hex digits>" token CGDebugInfo's constructor
    appended to this compile's own DW_AT_producer string, read back from
    the current frame's symtab -- independent of any file on disk, this is
    the live binary's own honest answer to "which compile am I". None if
    unavailable (older plang binary predating issue #141, or no frame
    context) -- callers treat that as "nothing to check against", not as a
    mismatch, since there is nothing to disagree with the sidecar about."""
    try:
        frame = gdb.selected_frame()
        sal = frame.find_sal()
        if not sal or not sal.symtab:
            return None
        producer = sal.symtab.producer
        if not producer:
            return None
        m = re.search(r"schemabuildid:([0-9a-fA-F]{16})", producer)
        return m.group(1).lower() if m else None
    except gdb.error:
        return None


def _type_fingerprint(t):
    """Recomputes CGDebugInfo::computeSchemaFingerprint's own hash purely
    from a live DWARF type -- t is the wrapper struct buildSchemaDIType
    built: N named discriminant members (t.fields()[:-1]) followed by one
    final ANONYMOUS member (t.fields()[-1], name None) wrapping the body's
    own struct DIType. Returns None if t isn't shaped that way (defensive:
    a plain fixed-extent schema recurses straight to its body with no
    wrapper at all, and never reaches this function via
    _plang_schema_pretty_printer's own sidecar-variants-lookup gate)."""
    t = t.strip_typedefs()
    fields = list(t.fields())
    if not fields or fields[-1].name is not None:
        return None
    disc_fields, body_field = fields[:-1], fields[-1]
    canon = "D%d;" % len(disc_fields)
    try:
        body_type = body_field.type.strip_typedefs()
        for f in body_type.fields():
            if f.name is None:
                continue
            size = int(f.type.sizeof)
            kind = "array" if f.type.strip_typedefs().code == gdb.TYPE_CODE_ARRAY else "scalar"
            canon += "%s:%s:%d;" % (f.name, kind, size)
    except gdb.error:
        return None
    return "%016x" % _fnv1a64(canon)


def _read_i64(inferior, addr):
    data = inferior.read_memory(addr, 8)
    return int.from_bytes(bytes(data), byteorder="little", signed=True)


def _layout_walk(schema, base_addr, inferior, discs, hdr_override=None):
    """Returns a list of (name, addr, kind, extra) for every field in
    declaration order, mirroring SchemaLayoutEngine::rtWalkFields/alignUpV
    exactly: align the running offset up to each field's own alignment,
    record it, then advance past that field's real (possibly
    discriminant-dependent) size.

    Raises _SchemaDataError for a structural problem (an entry too broken
    to walk at all); a problem local to one field is instead folded into
    that field's own "extra" as an error marker, so the fields around it
    still come out right.

    discs is supplied by the caller rather than read from base_addr here:
    for a var-parameter reference (see PlangSchemaValue.children's own
    "ref" branch) the discriminants live in the shadow block, not adjacent
    to the body base_addr is about to become -- issue #142.  hdr_override
    lets that same caller start the walk at the body's own offset 0 rather
    than schema["hdrBytes"], which still (correctly) describes the header a
    DIRECT object of this schema carries, not a reference's shadow block."""
    off = schema["hdrBytes"] if hdr_override is None else hdr_override
    results = []
    for field in schema["fields"]:
        try:
            names = field["names"]
            align = field.get("alignBytes", 8)
            off = _align_up(off, align)
            if field["kind"] == "array":
                lo = _eval_form(field["low"], discs)
                hi = _eval_form(field["high"], discs)
                count = max(0, hi - lo + 1)
                stride = _align_up(field["elemSizeBytes"], field["elemAlignBytes"])
                for name in names:
                    results.append((name, base_addr + off, "array",
                                     (count, field["elemSizeBytes"])))
                    off += count * stride
            elif field["kind"] == "scalar":
                size = field["sizeBytes"]
                type_kind = field.get("typeKind", "integer")
                for name in names:
                    results.append((name, base_addr + off, "scalar",
                                     (size, type_kind, field)))
                    off += size
            else:
                raise _SchemaDataError("unknown field kind %r" % (field["kind"],))
        except _SchemaDataError as e:
            # A field we could not lay out (bad "kind", bad extent form, ...)
            # -- surface an error marker for it and stop the walk, since
            # every field after this one has an offset that depends on this
            # field's own (unknown) size. Whatever laid out fine so far is
            # still returned.
            results.append((field.get("names", ["?"])[0] if isinstance(field, dict) else "?",
                             base_addr, "error", str(e)))
            break
        except (KeyError, TypeError) as e:
            results.append((field.get("names", ["?"])[0] if isinstance(field, dict) else "?",
                             base_addr, "error", "malformed field entry: %s" % e))
            break
    return results


def _format_set(inferior, addr, size_bytes, set_base):
    """Decodes a plang set's flat bitmask -- one bit per ordinal, bit 0
    standing for set_base (SetOps::setBitIndex/setBaseOffset) -- into the
    member ordinals the program actually put in."""
    raw = bytes(inferior.read_memory(addr, size_bytes))
    bits = int.from_bytes(raw, byteorder="little", signed=False)
    members = [i + set_base for i in range(size_bytes * 8) if (bits >> i) & 1]
    return "[" + ", ".join(str(m) for m in members) + "]"


def _format_scalar(inferior, addr, size, type_kind, field):
    """Formats one non-array field according to its real plang type
    (issue #144) instead of blindly reading it as a signed integer."""
    raw = bytes(inferior.read_memory(addr, size))
    if type_kind == "real":
        if size == 8:
            return struct.unpack("<d", raw)[0]
        if size == 4:
            return struct.unpack("<f", raw)[0]
        raise _SchemaDataError("real field has unexpected size %d" % size)
    if type_kind == "complex":
        if size != 16:
            raise _SchemaDataError("complex field has unexpected size %d" % size)
        re, im = struct.unpack("<dd", raw)
        return "{re = %r, im = %r}" % (re, im)
    if type_kind == "set":
        set_base = field.get("setBase", 0)
        return _format_set(inferior, addr, size, set_base)
    if type_kind == "boolean":
        return bool(int.from_bytes(raw, byteorder="little", signed=False) & 1)
    if type_kind == "char":
        v = raw[0] if raw else 0
        return "'%s'" % chr(v) if 32 <= v < 127 else "chr(%d)" % v
    if type_kind == "pointer":
        return hex(int.from_bytes(raw, byteorder="little", signed=False))
    # "integer" and anything this script does not special-case: the same
    # raw-integer read this script has always done, which is exactly right
    # for an ordinal (Integer, Subrange, Enum) field.
    if size == 8:
        return _read_i64(inferior, addr)
    return int.from_bytes(raw, byteorder="little", signed=True)


def _read_ptr(inferior, addr):
    data = inferior.read_memory(addr, 8)
    return int.from_bytes(bytes(data), byteorder="little", signed=False)


class PlangSchemaValue(object):
    """gdb pretty-printer for one schema-typed value. children() drives
    both `print` (gdb formats a struct-shaped printer by its children
    automatically) and `print var.field`-style field access.

    is_ref (issue #142): true for a schema var/value PARAMETER, whose ABI
    pointer addresses the object's BODY, never its header -- CGDebugInfo::
    declareSchemaParamRef builds a small debug-only shadow block instead of
    describing the real object directly: [hdrBytes worth of discriminant
    words][one pointer to the real body], tagged "<name>.ref" so this class
    can tell it apart from a direct object's own header-at-offset-0 layout
    (self.val.address IS that header, for a direct object -- for a
    reference it is the shadow block's own address, and the real body sits
    wherever the trailing pointer says, not necessarily anywhere near it)."""

    def __init__(self, val, schema, type_name, is_ref):
        self.val = val
        self.schema = schema
        self.type_name = type_name
        self.is_ref = is_ref

    def to_string(self):
        return self.type_name

    def children(self):
        try:
            base_addr = int(self.val.address)
        except gdb.error:
            return
        inferior = gdb.selected_inferior()
        try:
            disc_names = self.schema["discs"]
        except (KeyError, TypeError):
            # Should not happen -- _plang_schema_pretty_printer already
            # checked this schema has "discs" -- but children() is called
            # by gdb well after that check, against whatever the sidecar
            # says now, so this stays defensive rather than trusting it.
            yield "<plang-schema-error>", "malformed sidecar entry (no 'discs')"
            return

        # Discriminants first, exactly where DWARF already puts them
        # correctly -- shown here too so this printer's output is a
        # complete, self-consistent replacement rather than a partial one
        # a reader has to mentally merge with gdb's own default view. Also
        # collected into `discs` (values, not names) since _layout_walk
        # needs them by index -- issue #142: for a var/value-parameter
        # reference these come from the SHADOW BLOCK at base_addr, which is
        # correct either way (a direct object's own header starts with the
        # identical discriminant words).
        discs = []
        for i, name in enumerate(disc_names):
            try:
                value = _read_i64(inferior, base_addr + i * 8)
            except gdb.error as e:
                yield name, "<error reading discriminant: %s>" % e
                discs.append(0)  # keep the index aligned for _layout_walk
                continue
            discs.append(value)
            yield name, value

        if self.is_ref:
            # The shadow block's trailing member: a plain pointer to the
            # real body, stored (not computed) at the header's own size --
            # matches declareSchemaParamRef's own layout exactly.
            try:
                body_addr = _read_ptr(inferior, base_addr + self.schema["hdrBytes"])
            except (gdb.error, KeyError, TypeError) as e:
                yield "<plang-schema-error>", \
                    "could not read the referenced body's address: %s" % e
                return
            field_addr, hdr_for_walk = body_addr, 0
        else:
            field_addr, hdr_for_walk = base_addr, None

        try:
            walked = _layout_walk(self.schema, field_addr, inferior, discs, hdr_for_walk)
        except _SchemaDataError as e:
            yield "<plang-schema-error>", str(e)
            return
        except (KeyError, TypeError) as e:
            yield "<plang-schema-error>", "malformed sidecar entry: %s" % e
            return

        for name, addr, kind, extra in walked:
            try:
                if kind == "scalar":
                    size, type_kind, field = extra
                    yield name, _format_scalar(inferior, addr, size, type_kind, field)
                elif kind == "array":
                    count, elem_size = extra
                    values = [
                        int.from_bytes(
                            bytes(inferior.read_memory(addr + j * elem_size, elem_size)),
                            byteorder="little", signed=True)
                        for j in range(count)
                    ]
                    yield name, "{" + ", ".join(str(v) for v in values) + "}"
                else:  # "error", from _layout_walk's own per-field fallback
                    yield name, "<plang-schema-error: %s>" % extra
            except (gdb.error, gdb.MemoryError, _SchemaDataError,
                    OverflowError, struct.error) as e:
                # A problem reading or formatting just THIS field (e.g. an
                # address gdb can no longer read, or a field whose declared
                # size does not match its typeKind) -- yield an error marker
                # for it alone so every field around it still prints.
                yield name, "<error: %s>" % e


def _plang_schema_pretty_printer(val):
    sidecar = _sidecar.sidecar_for_current_context()
    if not sidecar:
        return None
    schemas = sidecar.get("schemas", {})
    try:
        type_name = val.type.strip_typedefs().tag
    except gdb.error:
        return None
    if not type_name:
        return None
    # ".ref": CGDebugInfo::declareSchemaParamRef's own tag suffix for a
    # schema var/value parameter's shadow block (issue #142) -- the schema
    # layout data itself is recorded under the bare name either way (the
    # body's own shape does not depend on how its caller reached it), only
    # how this printer reads the discriminants/body out of memory differs.
    is_ref = type_name.endswith(".ref")
    lookup_name = type_name[:-len(".ref")] if is_ref else type_name
    if lookup_name not in schemas:
        return None

    # Issue #141: an older/newer binary loading a sidecar some OTHER compile
    # of this same source wrote is a detectable mismatch -- fall back to
    # gdb's own (honest, approximate) default printing rather than trust
    # data that may describe a different program entirely.
    sidecar_id = sidecar.get("buildId")
    live_id = _live_build_id()
    if sidecar_id and live_id and sidecar_id.lower() != live_id.lower():
        _warn_once(
            "buildid:" + lookup_name,
            "sidecar buildId %s does not match this binary's own %s -- the "
            "sidecar was written by a DIFFERENT compile of this source "
            "(likely a later rebuild). Falling back to gdb's default "
            "DWARF-only printing for '%s' rather than trust it."
            % (sidecar_id, live_id, lookup_name))
        return None

    # issue #144: a structural failure -- a variant with no "fields"/"discs"
    # at all -- is not something one error-marker child can paper over
    # (there is no layout to walk in the first place), so it's filtered out
    # here rather than trusted; gdb falls back to its own default DWARF-only
    # printing if that leaves nothing usable, exactly as if this script had
    # never seen the type.
    def _structurally_valid(variant):
        return isinstance(variant, dict) and "fields" in variant and "discs" in variant

    variants = [v for v in schemas[lookup_name] if _structurally_valid(v)]
    if not variants:
        return None
    if len(variants) == 1:
        return PlangSchemaValue(val, variants[0], lookup_name, is_ref)

    # Issue #140: more than one schema was recorded under this same name --
    # pick the variant whose structural fingerprint matches this LIVE
    # value's own DWARF type, rather than just the first one. Note: for a
    # ".ref" shadow block, val.type is shaped differently from a direct
    # object's wrapper type (discs + a trailing POINTER field, not discs +
    # an embedded body struct), so _type_fingerprint returns None here and
    # this can't disambiguate a same-named collision reached through a var/
    # value parameter -- falls through to the safe "ambiguous" warning
    # below rather than guessing, same as any other unmatched fingerprint.
    fp = _type_fingerprint(val.type)
    for variant in variants:
        if fp is not None and variant.get("fp") == fp:
            return PlangSchemaValue(val, variant, lookup_name, is_ref)

    _warn_once(
        "ambiguous:" + lookup_name,
        "%d incompatible layouts are recorded for schema type '%s' and none "
        "of their fingerprints match this value's own DWARF type -- falling "
        "back to gdb's default DWARF-only printing rather than guess which "
        "one applies." % (len(variants), lookup_name))
    return None


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
