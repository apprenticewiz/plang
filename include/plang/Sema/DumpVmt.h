// DumpVmt.h — Turbo Tier 5, Cluster A item 1's own debug-introspection
// mechanism for the VMT slot table.
//
// There is no CodeGen for an object type yet (that is item 2's job), so
// there is nothing an end-to-end compile-and-run test could observe to
// prove the VMT slot-assignment algorithm (Sema::resolveObjectType,
// SemaType.cpp) got the right answer -- no lowered struct, no vtable
// global, nothing a debugger or a runtime print statement could inspect.
// -dump-vmt (wired up in Driver.cpp/Frontend.cpp exactly like the existing
// -dump-ast/-dump-parse-tree read-only inspection modes: runs Sema, then
// prints and stops, before anything that would need CodeGen) is a small,
// dedicated text dump of exactly the POST-SEMA state a VMT correctness test
// needs: every object type's ancestor, its flattened field list, and its
// own final VMT slot table -- independent of, and not derived from, the
// ordinary -dump-ast printer, which prints the raw (pre- or post-Sema)
// PARSE TREE and has no way to show a resolved Type::VmtSlots at all.
#pragma once

#include <ostream>

namespace plang {

struct ProgramNode;

/// Prints one '(vmt TypeName ...)' form per object type reachable from
/// \p Program (its own top-level block, a standalone unit's interface and
/// implementation blocks, and any EP module bodies -- object types are
/// Turbo-only, so only the first two are ever populated in practice, but
/// the walk is generic).  Must be called AFTER Sema has resolved \p Program
/// (TypeNode::ResolvedType is what this reads); an object type Sema itself
/// rejected (an unresolvable ancestor, ...) resolves to the TyErr sentinel
/// and is silently skipped, exactly the way -dump-ast already only ever
/// runs after a successful check() (Frontend.cpp).
void printVmt(const ProgramNode& Program, std::ostream& Os);

} // namespace plang
