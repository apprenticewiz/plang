(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t < %t.dir/stdin.txt | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:eoln
CHECK-NEXT:[abc]
*)

//--- test.pas
program p;
var s: string(20);
begin
  read(s);
  if eoln then writeln('eoln') else writeln('noeoln');
  writeln('[', s, ']')
end.

//--- stdin.txt
abc
