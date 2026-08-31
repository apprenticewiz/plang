// Turbo Tier 4, Cluster C item 5: the real, shipped `Crt` unit -- POSIX
// terminals only (Linux and macOS; no Windows console -- an already-made,
// non-negotiable scope call for this item, matching this project's own CI
// matrix).
//
// Everything below that is screen/cursor/color state -- ClrScr, ClrEol,
// GotoXY, WhereX/WhereY, TextColor/TextBackground/TextAttr, Window/WindMin/
// WindMax -- is plain Pascal, built entirely on the ordinary `Write` and
// this unit's own internal state: it needs nothing from the runtime beyond
// what every other Turbo program already links, because "draw on the
// screen" for a POSIX terminal just means "send the right ANSI/VT100
// escape sequence", and Write already sends bytes to stdout. Only Delay,
// KeyPressed and ReadKey need real OS support (a wait; raw, unbuffered
// keyboard input) -- those three are declared as ordinary Turbo builtins
// instead of unit exports; see Builtins.def's own comment, right next to
// their declarations, for exactly why, and runtime/plang_crt.cpp for their
// implementation.
//
// State/init: real TP's own Crt puts the terminal into raw mode (for
// ReadKey/KeyPressed) from the unit's own initialization section, restored
// on program exit.  Turbo Tier 4, Cluster A's own report documents that a
// USED unit's initialization section does not run automatically yet (only
// the main program's own, or a unit compiled and run standalone) -- so this
// unit cannot rely on a `begin...end` of its own (and deliberately has
// none) to set up terminal state, screen state, or anything else.  Instead,
// EVERY exported routine below starts by calling EnsureInit, a private
// procedure that sets this unit's own default state (TextAttr=7, an
// 80x25 window, cursor at 1,1) exactly once, the first time ANYTHING in
// this unit is used -- regardless of whether that ever-first call came
// through a real unit-init mechanism or not.  The one gap this cannot
// close: a program that reads TextAttr/WindMin/WindMax directly, with no
// prior CALL into this unit (ClrScr, GotoXY, ...), sees LLVM's own global
// zero-initializer (0), not this unit's documented defaults -- there is no
// Pascal-level "read" to hook the way every exported procedure/function
// call already is. Real TP code overwhelmingly calls something (ClrScr is
// close to universal) before ever consulting these, so this is judged an
// acceptable, narrow, documented gap rather than something worth a less
// natural design to close.  Raw terminal mode for
// KeyPressed/ReadKey uses the identical lazy-first-call pattern, entirely
// inside runtime/plang_crt.cpp's own ensureRawMode/atexit pair, so it too
// works whether or not Crt's own Pascal-level init ever runs.  Both halves
// of this design solve the same problem the same way, deliberately, so
// that closing the unit-init gap later (a real Tier 4 item of its own) is
// a pure simplification here, not a correctness fix.
unit Crt;

interface

const
  { The 16 real Borland text-mode colors (confirmed against Borland's own
    Turbo Pascal 7 documentation and fpc's crth.inc): 0-7 are the low-
    intensity set, 8-15 the same 8 hues with the high-intensity/bold bit
    set.  Only 0-7 (or 0-7 + Blink) are meaningful to TextBackground --
    real DOS text-mode hardware has no bright background, only blink,
    which is why background colors 8-15 are not separate named constants
    the way foreground ones are. }
  Black        = 0;
  Blue         = 1;
  Green        = 2;
  Cyan         = 3;
  Red          = 4;
  Magenta      = 5;
  Brown        = 6;
  LightGray    = 7;
  DarkGray     = 8;
  LightBlue    = 9;
  LightGreen   = 10;
  LightCyan    = 11;
  LightRed     = 12;
  LightMagenta = 13;
  Yellow       = 14;
  White        = 15;
  Blink        = 128;

var
  { The current foreground/background/blink attribute, packed the same way
    real TP's own TextAttr is: bits 0-3 foreground (0-15), bits 4-6
    background (0-7), bit 7 blink.  A real, readable/settable variable --
    assigning it directly (as some real TP programs do, bypassing
    TextColor/TextBackground) is honored the next time anything is drawn,
    matching real TP's own documented behavior of only ever consulting
    TextAttr's CURRENT value at write time. }
  TextAttr: Byte;
  { The active window's top-left/bottom-right corners, packed as real TP's
    own WindMin/WindMax are: WindMin = (Y1-1) shl 8 + (X1-1), WindMax =
    (Y2-1) shl 8 + (X2-1).  Exported (not kept as unit-private state)
    because real TP's own Crt exports both under exactly this name. }
  WindMin: Word;
  WindMax: Word;

procedure ClrScr;
procedure ClrEol;
procedure GotoXY(X, Y: Integer);
function WhereX: Byte;
function WhereY: Byte;
procedure TextColor(Color: Byte);
procedure TextBackground(Color: Byte);
procedure Window(X1, Y1, X2, Y2: Integer);

implementation

const
  { No live terminal-size query (no TIOCGWINSZ ioctl) is in this item's own
    scope -- nothing in the "what Crt needs to provide" list asks for
    ScreenWidth/ScreenHeight, and real TP itself never queried anything,
    always assuming the BIOS/DOS-reported mode's own fixed size.  80x25 is
    that same fixed assumption, carried over unchanged; a real terminal
    narrower or wider than 80x25 still works fine for GotoXY/TextColor/etc
    (those only ever emit escape sequences the real terminal interprets
    against ITS OWN actual size), it is only ClrScr/ClrEol's own "is this
    the full screen" fast-path check below that is sized against this
    constant rather than the terminal's real dimensions. }
  DefaultScreenWidth  = 80;
  DefaultScreenHeight = 25;

  { AnsiTbl[F] is the ANSI SGR color digit (30+d for foreground, 40+d for
    background) for Borland color index F (0-7).  Borland's own 8-color
    order (Black,Blue,Green,Cyan,Red,Magenta,Brown,LightGray) is NOT ANSI's
    own order (Black,Red,Green,Yellow,Blue,Magenta,Cyan,White) -- confirmed
    against fpc's own unix/crt.pp, whose identical-purpose AnsiTbl constant
    ('04261537') this matches digit-for-digit; Brown -> ANSI Yellow (index
    3) is not a mistake, it is the standard, widely-used substitution DOS's
    own brown (a dimmed yellow on real CGA/EGA hardware) gets on a terminal
    that has no separate "brown". }
  AnsiTbl: array[0..7] of Char = ('0', '4', '2', '6', '1', '5', '3', '7');

var
  Ready:   Boolean; { EnsureInit has run }
  CursorX: Integer; { 1-based, WINDOW-relative -- GotoXY's own X }
  CursorY: Integer; { 1-based, WINDOW-relative -- GotoXY's own Y }

// See this file's own header comment for why every exported routine below
// calls this first, rather than relying on a unit initialization section.
procedure EnsureInit;
begin
  if Ready then Exit;
  Ready     := True;
  TextAttr  := LightGray; { real TP's own default: light gray on black }
  WindMin   := 0;
  WindMax   := ((DefaultScreenHeight - 1) shl 8) or (DefaultScreenWidth - 1);
  CursorX   := 1;
  CursorY   := 1;
end;

// Sends the escape sequence that makes the real terminal's own current SGR
// state match TextAttr.  Called after TextAttr changes AND before ClrScr's
// own ESC[2J (real TP's own ClrScr fills with the CURRENT attribute, and
// on a real terminal that means the terminal's own SGR state has to already
// be right before the clear, since ESC[2J fills using whatever the
// terminal's own current background already is).
procedure ApplyAttr;
var
  Fg, Bg: Byte;
begin
  Fg := TextAttr and $0F;
  Bg := (TextAttr shr 4) and $07;
  Write(Chr(27), '[0');
  if Fg > 7 then Write(';1');                     // high-intensity foreground
  Write(';3', AnsiTbl[Fg and $07]);
  Write(';4', AnsiTbl[Bg]);
  if (TextAttr and Blink) <> 0 then Write(';5');
  Write('m');
end;

function WindowWidth: Integer;
begin
  WindowWidth := (WindMax and $FF) - (WindMin and $FF) + 1;
end;

function WindowHeight: Integer;
begin
  WindowHeight := (WindMax shr 8) - (WindMin shr 8) + 1;
end;

// True when the active window is exactly the default 80x25 full screen --
// ClrScr/ClrEol's own fast path (a single ESC[2J/ESC[K, which a real
// terminal applies to its own whole actual screen/line, not just 80x25) is
// only correct when the window is not a proper sub-region of it.
function FullWindow: Boolean;
begin
  FullWindow := (WindMin = 0) and
    (WindMax = ((DefaultScreenHeight - 1) shl 8) or (DefaultScreenWidth - 1));
end;

procedure ClrScr;
var
  Row: Integer;
begin
  EnsureInit;
  ApplyAttr;
  if FullWindow then
    Write(Chr(27), '[2J', Chr(27), '[H')
  else
  begin
    for Row := 1 to WindowHeight do
    begin
      Write(Chr(27), '[', (WindMin shr 8) + Row, ';', (WindMin and $FF) + 1, 'H');
      Write(Chr(27), '[', WindowWidth, 'X');
    end;
    Write(Chr(27), '[', (WindMin shr 8) + 1, ';', (WindMin and $FF) + 1, 'H');
  end;
  CursorX := 1;
  CursorY := 1;
end;

procedure ClrEol;
var
  Remaining: Integer;
begin
  EnsureInit;
  ApplyAttr;
  if FullWindow or ((WindMax and $FF) = DefaultScreenWidth - 1) then
    Write(Chr(27), '[K')
  else
  begin
    Remaining := WindowWidth - CursorX + 1;
    if Remaining > 0 then Write(Chr(27), '[', Remaining, 'X');
  end;
end;

procedure GotoXY(X, Y: Integer);
var
  AbsX, AbsY: Integer;
begin
  EnsureInit;
  if (X < 1) or (Y < 1) or (X > WindowWidth) or (Y > WindowHeight) then Exit;
  CursorX := X;
  CursorY := Y;
  AbsX := (WindMin and $FF) + X;
  AbsY := (WindMin shr 8) + Y;
  Write(Chr(27), '[', AbsY, ';', AbsX, 'H');
end;

function WhereX: Byte;
begin
  EnsureInit;
  WhereX := CursorX;
end;

function WhereY: Byte;
begin
  EnsureInit;
  WhereY := CursorY;
end;

procedure TextColor(Color: Byte);
begin
  EnsureInit;
  TextAttr := (TextAttr and $F0) or (Color and $0F);
  if Color > 15 then TextAttr := TextAttr or Blink;
  ApplyAttr;
end;

procedure TextBackground(Color: Byte);
begin
  EnsureInit;
  TextAttr := (TextAttr and $0F) or ((Color and $07) shl 4);
  if (Color and Blink) <> 0 then TextAttr := TextAttr or Blink;
  ApplyAttr;
end;

procedure Window(X1, Y1, X2, Y2: Integer);
begin
  EnsureInit;
  if (X1 < 1) or (Y1 < 1) or (X2 > DefaultScreenWidth) or
     (Y2 > DefaultScreenHeight) or (X1 > X2) or (Y1 > Y2) then Exit;
  WindMin := ((Y1 - 1) shl 8) or (X1 - 1);
  WindMax := ((Y2 - 1) shl 8) or (X2 - 1);
  GotoXY(1, 1);
end;

end.
