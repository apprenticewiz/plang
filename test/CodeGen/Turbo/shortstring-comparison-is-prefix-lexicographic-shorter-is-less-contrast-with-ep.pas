(*
Turbo string[N] semantics item, concrete work 2 -- the single most important
correctness proof for this item: ShortString comparison is PREFIX
lexicographic with SHORTER treated as LESS (plang_sstr_lt and siblings,
runtime/plang_sstr.cpp), the OPPOSITE of EP's string(N), which pads the
shorter operand out with spaces before comparing (plang_str_lt and
siblings) -- so 'a' < 'a ' is TRUE for a ShortString and FALSE (they
compare EQUAL) for an EP VarString.

Both programs live in this one file (via split-file) specifically to make
the contrast explicit side by side, as suggested by this item's own
verification plan -- and the EP half is exactly as much a regression test
as a demonstration: CGBinaryOps.cpp's EP string-comparison block
(exprIsStringLike/plang_str_* family) is NOT touched by this item, only
given a new ShortString sibling checked first, and this confirms that
holds.

RUN: split-file %s %t.dir
RUN: %plang -std=turbo %t.dir/short.pas -o %t.short
RUN: %run %t.short | FileCheck --check-prefix=SHORT --strict-whitespace --match-full-lines %s
RUN: %plang_ep %t.dir/ep.pas -o %t.ep
RUN: %run %t.ep | FileCheck --check-prefix=EP --strict-whitespace --match-full-lines %s
*)

//--- short.pas
program shortp;
var s1, s2: string[10];
begin
  s1 := 'a';
  s2 := 'a ';
  if s1 < s2  then writeln('lt: true')  else writeln('lt: false');
  if s1 = s2  then writeln('eq: true')  else writeln('eq: false');
  if s1 <= s2 then writeln('le: true')  else writeln('le: false');
  if s2 > s1  then writeln('gt: true')  else writeln('gt: false');
end.

//--- ep.pas
program epp;
var s1, s2: string(10);
begin
  s1 := 'a';
  s2 := 'a ';
  if s1 < s2  then writeln('lt: true')  else writeln('lt: false');
  if s1 = s2  then writeln('eq: true')  else writeln('eq: false');
  if s1 <= s2 then writeln('le: true')  else writeln('le: false');
  if s2 > s1  then writeln('gt: true')  else writeln('gt: false');
end.

(*
SHORT:lt: true
SHORT-NEXT:eq: false
SHORT-NEXT:le: true
SHORT-NEXT:gt: true

EP:lt: false
EP-NEXT:eq: true
EP-NEXT:le: true
EP-NEXT:gt: false
*)
