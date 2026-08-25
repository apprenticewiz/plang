(*
RUN: %plang %s -o %t
RUN: %run %t
*)

(*
6.4.3.5 makes a file of char a text file, so all four apply to one.
*)

program p(output);
var g: file of char;
begin
  rewrite(g); writeln(g); page(g);
  reset(g); if eoln(g) then writeln('eoln')
end.
