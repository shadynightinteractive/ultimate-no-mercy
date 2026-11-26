// slots.h -- the per-slot match model's names, for the match types' jobs (slots.c, lumberjack.c).
// New feature (Ultimate No Mercy) -- lumberjack, 2026-11-21 (the enums were slots.c's own, any32 2026-11-18).
#pragma once
enum { MS_EMPTY = 0, MS_COMPETITOR = 1, MS_EXTRA = 2 };          // a slot's role
enum { MS_LEGAL = 0, MS_WAITING = 1, MS_ELIMINATED = 2 };         // its state
// Extras' rules, the match type's choice: 0 ringside (a Lumberjack's: the referee never counts a pin on them, they never
// decide the match); 1 elimination (a pin of three on an extra puts him out; the match goes on).
enum { MS_RULE_RINGSIDE = 0, MS_RULE_ELIMINATION = 1 };
// What an extra does: the job is what he does, the role / side / state who he is in the match. MS_JOB_NONE: any32's free
// brawler (the computer's own choices); MS_JOB_LUMBERJACK: his post round the ring, the man out of the ring, never in it
// (lumberjack.c). A Survivor Series partner's corner would be a job too.
enum { MS_JOB_NONE = 0, MS_JOB_LUMBERJACK = 1 };
int match_slots_add(int n, const u16* ids, const u8* looks, int rule, int wait);
int match_slots_add_at(int base, int n, const u16* ids, const u8* looks, int rule, int wait, int job);
int match_slots_count(void);          // the slots in use this match (0: no extra slots)
int match_slots_competitors(void);    // the match type's own (slots 0 .. this - 1)
int match_slots_is_extra(int s);
int match_slots_steps(void);          // the match's steps so far (30 a second)
void lumberjack_setup(void); void lumberjack_cpu(s32 s); void lumberjack_step(void); void lumberjack_after(void);
void lumberjack_lead_in(void);        // (lumberjack2: every step of the match loop, the lead-in too -- the group walk-out)
int lumberjack_on_records(void);
