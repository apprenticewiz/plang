(*
RUN: split-file %s %t.dir
RUN: %plang %t.dir/test.pas -o %t
RUN: %t < %t.dir/stdin.txt | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:via output
CHECK-NEXT:n=55
CHECK-NEXT:eof
*)

//--- test.pas
program p(input, output);
var n: integer;
begin
  writeln(output, 'via output');
  readln(input, n);
  writeln('n=', n);
  if eof(input) then writeln('eof') else writeln('noeof')
end.

//--- stdin.txt
55
