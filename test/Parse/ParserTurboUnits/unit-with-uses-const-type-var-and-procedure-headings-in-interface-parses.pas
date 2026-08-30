(*
A realistic unit interface section: a 'uses' clause, one of each
declaration kind, and a procedure and a function each written as a heading
alone (no body -- Turbo gives the body in the implementation section
instead).  parseProcDecl's own HeadingOnly path (shared with EP's module
interfaces) marks each of these IsForward, which the printer shows as
'forward' the same way an explicit forward declaration prints.
*)

(*
RUN: %plang_ir -std=turbo -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

unit Widgets;

interface

uses SysUtils, Strings;

const MaxWidgets = 10;
type WidgetId = Integer;
var WidgetCount: Integer;

procedure Reset;
function NextId(x: Integer): Integer;

implementation

procedure Reset;
begin
end;

function NextId(x: Integer): Integer;
begin
  NextId := x + 1;
end;

end.

(*
CHECK:(unit Widgets
CHECK-NEXT:  (interface (uses SysUtils Strings)
CHECK-NEXT:    (const MaxWidgets 10)
CHECK-NEXT:    (typedef WidgetId Integer)
CHECK-NEXT:    (var (WidgetCount) Integer)
CHECK-NEXT:    (procedure Reset () forward)
CHECK-NEXT:    (function NextId ((x Integer)) Integer forward)
CHECK-NEXT:    ())
CHECK-NEXT:  (implementation
CHECK-NEXT:    (procedure Reset ()
CHECK-NEXT:      (compound))
CHECK-NEXT:    (function NextId ((x Integer)) Integer
CHECK-NEXT:      (compound
CHECK-NEXT:        (assign NextId (+ x 1))))
CHECK-NEXT:    ())
CHECK-NEXT:)
*)
