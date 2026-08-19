"""LLDB data formatters for plang (ISO 7185/10206 Extended Pascal) binaries
compiled with `-g`.

LLDB has no language plugin for DW_LANG_Pascal83 ("This version of LLDB
has no plugin for the language 'pascal83'"), so it falls back to generic,
C-flavored rendering for everything. Most of that is already fine as-is
(booleans print as true/false, characters as 'x', and named enumerators
by name), but a pointer shows only a bare address with no indication of
what it points to. This module adds a summary for pointer values that
dereferences and shows the pointee, or `nil` for a null pointer.

Usage:
    (lldb) command script import tools/lldb/plang_formatters.py

or add that line to ~/.lldbinit to load it for every session. The
formatter registers under a "Pascal" type category, restricted to values
from Pascal compile units (SBTypeCategory.AddLanguage), so it never fires
for other languages in a mixed-language debugging session. It can still
be toggled independently of unloading the module:

    (lldb) type category disable Pascal

What this can't fix: LLDB synthesizes each scalar type's displayed name
(plang's "integer"/"real" DWARF base types show as "long"/"double") from
its DW_ATE_*/byte-size encoding while importing DWARF types into LLDB's
internal Clang-based type system, before any formatter ever runs. A data
formatter can add a summary alongside a value but cannot rename the type
shown in parentheses — only a real LLDB Language plugin could, which is
a much larger undertaking than a formatter script.
"""

import lldb


def pascal_pointer_summary(valobj, internal_dict):
    if not valobj.GetType().IsPointerType():
        return None
    if valobj.GetValueAsUnsigned(0) == 0:
        return "nil"
    pointee = valobj.Dereference()
    if not pointee.IsValid():
        return None
    inner = pointee.GetSummary() or pointee.GetValue()
    if inner is None:
        return None
    return "-> " + inner


def __lldb_init_module(debugger, internal_dict):
    category = debugger.GetCategory("Pascal")
    if not category.IsValid():
        category = debugger.CreateCategory("Pascal")
    category.AddLanguage(lldb.eLanguageTypePascal83)

    name_spec = lldb.SBTypeNameSpecifier(r".*\*$", True)
    summary = lldb.SBTypeSummary.CreateWithFunctionName(
        "plang_formatters.pascal_pointer_summary", lldb.eTypeOptionCascade
    )
    category.AddTypeSummary(name_spec, summary)

    # SBTypeCategory.SetEnabled() only flags the category object itself; it
    # does not insert it into LLDB's active formatter search order the way
    # the "type category enable" command does. Go through HandleCommand for
    # the half that actually has an effect.
    debugger.HandleCommand("type category enable Pascal")

    print('plang formatters loaded (category "Pascal": pointer summaries).')
