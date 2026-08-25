(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:fedcba
*)

program p(output); var s: string(20); i, n: integer; c: char;
begin s := 'abcdef'; n := length(s);
 for i := 1 to n div 2 do
  begin c := s[i]; s[i] := s[n-i+1]; s[n-i+1] := c end;
 writeln(s) end.
