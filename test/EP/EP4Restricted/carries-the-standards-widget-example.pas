(*
Compiled from a copy in %t.dir, not %s directly: this program declares a
module, and the compiler writes its .pmi beside whatever file it compiled
-- compiling %s in place would write the .pmi into the checked-in source
tree itself, on every test run.

RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:0 0.0
*)

//--- test.pas
module widget_module interface;
export widgets = (widget, copy_widget, increment_widget, print_widget);
type real_widget = record f1: integer; f2: real end;
     widget = restricted real_widget;
procedure copy_widget(source: real_widget; var target: real_widget);
function increment_widget(w: real_widget): widget;
procedure print_widget(var f: text; w: real_widget);
end;
function increment_widget;
var mycopy: real_widget;
begin mycopy.f1 := w.f1 + 1; mycopy.f2 := w.f2 + 1.0;
  increment_widget := mycopy end;
procedure copy_widget;
begin target := source end;
procedure print_widget;
begin writeln(f, w.f1, ' ', w.f2:3:1) end;
end.
program p(output);
import widget_module;
var a, b: widget;
begin copy_widget(a, b); print_widget(output, b) end.
