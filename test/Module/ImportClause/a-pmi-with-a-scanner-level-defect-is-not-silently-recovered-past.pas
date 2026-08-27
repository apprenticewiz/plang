(*
loadPMI wraps a .pmi's content with a synthetic "program NAME; begin end."
before parsing it, since the parser reads a file of modules followed by a
program and a .pmi holds only the modules.  That wrapper name used to be
"__pmi__", whose leading underscore, trailing underscore, AND doubled
underscore each trip ISO 10206 6.1.3's own placement rule on every single
load -- an error with nothing to do with the file actually being read.  So
loadPMI could not gate success on "did parsing report any error"
(PMIDiags.hasErrors()) -- that was true even for a real, freshly
compiler-written .pmi -- and used "did parsing produce a tree" (!Prog)
instead.

That gate is too coarse for a .pmi with its OWN scanner-level defect, as
opposed to a parser-level one: Parser::parse() only returns null once its
OWN error counter is nonzero, but a Scanner-level error (here, "_oops", an
identifier that itself violates the underscore-placement rule) reports
straight to the diagnostics engine without ever incrementing that counter.
Parsing "recovers" a full tree despite it, so the malformed file used to be
accepted as a clean interface -- silently, with neither a diagnostic nor a
failed import -- rather than rejected the way the hand-authored, totally
unparsable "broken.pmi" case already is.

RUN: split-file %s %t.dir
RUN: not %plang -std=iso10206 -I%t.dir %t.dir/prog.pas -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
RUN: FileCheck --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
ERR: could not be parsed
ERR-ABSENT-NOT: no module named
*)

//--- bad.pmi
module bad interface;
export bad = (okval);
var _oops: integer;
function okval: integer;
end.

//--- prog.pas
program p;
import bad;
begin writeln(okval) end.
