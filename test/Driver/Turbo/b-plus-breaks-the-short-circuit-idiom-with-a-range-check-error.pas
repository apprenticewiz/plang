(*
The negative half of while-and-array-index-idiom-relies-on-short-circuit-
under-b-minus.pas's own proof: the IDENTICAL loop, with {$B+} added, must
fully evaluate `arr[i] <> 'z'` even once `i <= 5` is already false --
which means accessing `arr[6]`, one past the array's last element, and
{$R+} makes that a checked, deterministic abort (Runtime error 201) rather
than an unchecked read into whatever memory follows the array.  This is
what confirms the sibling file's success is really the short-circuit at
work and not a coincidence of what happens to sit at arr[6].
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %checkexit 201 %run %t > %t.out 2> %t.err
RUN: FileCheck --allow-empty %s < %t.out
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
CHECK-NOT: not found
ERR: Runtime error 201 at $
*)

program idiom_full_eval_breaks;
var arr: array[1..5] of Char;
    i: Integer;
begin
  arr[1] := 'a'; arr[2] := 'b'; arr[3] := 'c'; arr[4] := 'd'; arr[5] := 'e';
  {$R+}
  {$B+}
  i := 1;
  while (i <= 5) and (arr[i] <> 'z') do
    i := i + 1;
  writeln('not found, i=', i)
end.
