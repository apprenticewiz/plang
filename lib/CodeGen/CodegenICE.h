// CodegenICE.h — the codegen internal-consistency-error reporter.
//
// Fully stateless, given its own header so a leaf unit can reach it without
// pulling in all of CodeGenImpl.h.
#pragma once

#include "llvm/ADT/Twine.h"
#include "llvm/Support/ErrorHandling.h"

/// Aborts on an internal codegen inconsistency.
///
/// Reaching one of these means Sema accepted a construct that codegen cannot
/// lower.  Returning a placeholder value instead would emit a program that
/// compiles and runs but computes the wrong answer, so failing loudly here is
/// the only way such a gap becomes visible.
[[noreturn]] inline void codegenICE(const llvm::Twine& What) {
    llvm::report_fatal_error(llvm::Twine("plang codegen: ") + What, false);
}
