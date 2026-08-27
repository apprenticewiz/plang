(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:97
*)

(* Issue #296: EP §6.1.7 makes a one-character string literal -- the 'q'
   below -- type char, not string(1), so it reaches update(f, name) shaped
   like a char VALUE rather than a string.  EP §6.7.5.2's update(f, name)
   takes that name the same way reset/rewrite/extend already do: as the
   external file to (re)open, and on an INDEXED (direct-access) file this
   argument reached plang_update as a bare, unconverted i8 -- LLVM's IR
   verifier rejected it (a scalar where the runtime's `const char *`
   parameter wants a pointer) before the program got to run at all. *)

program p;
var f: file [1..100] of char;
    c: char;
begin
  rewrite(f, 'q'); write(f, 'a'); close(f);
  update(f, 'q'); read(f, c); writeln(ord(c):1)
end.
