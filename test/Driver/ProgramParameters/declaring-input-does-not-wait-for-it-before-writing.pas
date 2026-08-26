(*
ISO section 6.10 has the program parameter `input` reset as the program
starts, and section 6.5.5 has reset fill the buffer variable -- which
means reading a character, and on a terminal, waiting for one to be
typed. So

    program count(input, output);
    var i: integer;
    begin for i := 1 to 3 do writeln(i) end.

printed nothing and sat there, having asked for a keystroke with no way
to say so: the prompt it never wrote was still in the output buffer.

The window is filled on first use instead. Nothing is lost by waiting,
because priming pushes the character back and leaves the position where
it was, so an unprimed stream and a primed one are in the same place.

RUN: %plang %s -o %t
RUN: %hold_stdin_open %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1
CHECK-NEXT:2
CHECK-NEXT:3
*)

program count(input, output);
var i: integer;
begin for i := 1 to 3 do writeln(i) end.
