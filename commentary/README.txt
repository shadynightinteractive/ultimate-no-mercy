THE COMMENTARY, AS A SPREADSHEET

(2026-11-18, the user's decision: the transcripts are not published, and the game's move names are its own text, so the
spreadsheet and its files are the developer's own, in the private folder: data\commentary_text\ -- commentary.csv,
_export_lines.tsv, moves.txt, the clips, and wwe13_text.tsv, the transcripts. What the repository has is the recipe the
game builds the pack from: exe\WWFNoMercyRecomp\assets\commentary\wwe13\ -- tags where the text was, and the moves
table as references into the player's ROM. See tools\commentary\README.md.)

commentary.csv: every line JR and King can say in a match (6583 rows; one clip can be on several rows, one per
moment it fits). Open it in Excel, change what you like, save it (as CSV), and tell the assistant, or run:

    python mod\gen\comcsv.py import        (checks it, makes mod\build\commentary.pak)
    .\deploy.ps1 -Commentary               (backs up the game's pack, puts this one in)

The "play" column links to the clip (clips\NNNN.ogg). Excel turns the links into plain text when it saves; that's fine.

WHAT YOU CAN CHANGE
  use       no = never said there (or delete the row). Copy a row to tie the same clip to another moment too.
  event     the moment it's for (the list below)
  wrestler  only said about this Superstar (his name as in the CSV); empty = about anyone
  is        doing it / taking it (the wrestler is the one doing the move, or the one it's done to)
  for       men / women only; empty = anyone
  move      only when this move is done (names in moves.txt); empty = any move
  repeat    yes = only when the move was just done again ("Another suplex!")
  arena     only in this arena: raw, smackdown, kotr, summerslam, survivor, wrestlemania, rumble, rrmatch
  weight    how often against the other lines that fit (1 usual, 0.5 half as often, 2 twice)
  speaker   JR, King, JR to King / King to JR (the other man answers most of the time), either
Don't change: row, clip, seconds, source. The text is only what's said: changing it doesn't change the audio.
A new clip (your own recording): python mod\gen\compak.py add commentary <file> prints its number; add a row for it
(row empty).

THE MOMENTS (event, how many lines it has now, when the game calls it)
match_start     121  the bell
lockup           10  a collar-and-elbow tie-up
strike          324  a punch / kick that lands
strike_big      136  a running or strong strike that lands
grapple         822  a weak grapple move
big_move       1274  a strong grapple move (slam, suplex, driver)
knockdown        23  put down on the mat
reversal        194  a counter
special          85  SPECIAL! lights up
finisher        247  a special (finishing) move hits
pin              86  a cover
count            17  the referee's count
kickout_1        54  kicked out at one
kickout_2       182  kicked out at two (a near fall)
pinfall         101  the three count
submission      242  a submission hold applied
rope_break        5  out of a hold by the ropes
escape           14  out of a hold / pin some other way
tap_out         101  submits
outside          87  goes out to the floor
back_in          20  back in the ring
outside_hit      46  a move on the floor / into the steps / barricade
top_rope        174  climbing the turnbuckle
aerial           33  a move off the top that hits
aerial_miss      66  a move off the top that misses
weapon          174  a weapon picked up
weapon_hit      107  a weapon shot
table            39  through a table
blood            61  busted open
tag             116  a tag
save             49  a partner breaks up a pin / hold
taunt           115  showboating
run_in          164  someone comes down to interfere
dominate        369  one man well on top (from the damage numbers)
comeback         47  the one who was down fights back
tired             4  both slow, a long match
elimination     154  out of a Rumble / over the top
rumble_entry     34  a Rumble entrant comes out
cage             56  a cage / cell spot
ladder            0  a ladder spot
win             102  the match is over (the winner)
dq                0  disqualification / count out
entrance        193  walking out to the ring
sign_off         13  after the match
filler           36  nothing happening for a while
danger          163  in trouble (spirit low / taking a beating)
name            123  a Superstar's name on its own (joined after a lead-in line)

How often each moment gets a line at all, and how long before it can be said again, is the exe's
(exe\WWFNoMercyRecomp\src\main\commentary\director.cpp, "rules"), not this file.
commentary.csv is made from the game's pack by: python mod\gen\comcsv.py export  (that overwrites your edits).
