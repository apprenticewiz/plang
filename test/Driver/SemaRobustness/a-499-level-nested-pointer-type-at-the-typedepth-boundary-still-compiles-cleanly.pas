(*
Issue #596: Sema::resolveType/resolveTypeImpl (SemaType.cpp) had NO
recursion guard at all -- not even the "insufficient but present" kind
Parser::parseTypeExpr's own MaxTypeDepth ceiling (ParseType.cpp, issue
#63) has -- despite walking the identical recursive type-denoter
structure a second time, once parsing has already accepted it. A
pointer-type chain nested up to (just under) the parser's own
MaxTypeDepth=500 ceiling -- input the parser fully accepts as legal --
reliably segfaulted the frontend in Sema, past parsing entirely, with no
diagnostic at all under a constrained stack (`ulimit -s 704`, Release
per the issue's own bisection) -- a previously-unaudited gap in the
stack-headroom-guard family (Parser::parsePower #550/#551, Parser::
parseStatement #562, Sema::checkExpr/checkBinary and SemaType.cpp's own
constBound/buildExtentForm #556, Parser::parseFactor #572). Fixed by
giving Sema a dedicated TypeDepth/MaxTypeDepth=500 term-count ceiling
(mirroring the parser's own, for a friendlier diagnostic on an ordinary
large-enough stack) PLUS a plang::stackNearlyExhausted(StackBaseline)
check, both at resolveType's own top -- the single choke point every
recursive re-entry into type resolution (a pointer's base, an array's
element, a record field's own type, a set-of/file-of's component type)
funnels through, since resolveTypeImpl always recurses back into
resolveType rather than into itself directly.

This file is the false-positive half of that fix's regression coverage:
499 levels of '^' nesting (the issue's own repro depth, one under the
parser's own MaxTypeDepth=500 ceiling) must still compile cleanly on a
normal (default, ~8MiB) stack -- the new guard must not narrow what
already-legitimate, boundary-exact input this compiler accepts. Deeper
nesting past the parser's own ceiling is already covered by the existing
deeply-nested-pointer-dereference-chain-does-not-crash-the-compiler.pas
sibling test (ParserRobustness), which exercises the PARSER's own
term-count ceiling on a normal stack, well before Sema is ever reached;
this file's own job is narrower and Sema-side: prove a legal, parser-
accepted type denoter still resolves cleanly through Sema too.

(The stack-exhaustion crash itself needs a constrained ulimit to
reproduce, which lit's own internal RUN-line shell cannot script
portably -- see test/Turbo/ioresult-matrix-real-filesystem-driven-error-
codes.pas's own comment on why a ulimit-bounded RUN line has no precedent
in this suite, and the a-499-level-nested-if-chain-at-the-stmtdepth-
boundary-still-compiles-cleanly.pas (ParserRobustness, issue #562) and
a-499-level-nested-parenthesization-at-the-exprdepth-boundary-still-
compiles-cleanly.pas (ParserRobustness, issue #572) sibling tests' own
identical disclaimer. Verified manually instead, both before and after
this fix, at `ulimit -s 704`, Release (matching the issue's own bisected
threshold for this exact 499-level input): before, 499 levels of '^'
segfaults (SIGSEGV, exit 139, no diagnostic, past parsing and into
Sema::resolveTypeImpl per gdb); after, it either compiles cleanly or is
rejected with the same "type is nested too deeply" diagnostic the
parser's own term-count ceiling already uses -- never a raw crash -- and
recorded in this fix's own PR.)
*)

(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:ok
*)

program p;
type t = ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^integer;
var x: t;
begin
  writeln('ok');
end.
