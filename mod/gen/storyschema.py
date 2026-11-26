#!/usr/bin/env python3
# storyschema.py: mod\stories\story.schema.json (a JSON Schema for the custom story format, so an editor gives
# autocomplete and errors) made from storyc.py's own tables and the roster -- run again when they change:
#   python mod/gen/storyschema.py mod/stories/story.schema.json
import os, sys, json, runpy
HERE = os.path.dirname(os.path.abspath(__file__))
S = runpy.run_path(os.path.join(HERE, "storyc.py"), run_name="storyc_lib")

def build(names, groups, arenas):
    # (startup 2026-11-19, the user: Superstars by their string ID -- a folder's name, or the built-ins' "the_rock" -- never
    #  the name on screen; names are still read for older stories, but the schema offers the string IDs)
    text = {"type": "string", "description": "Text. \\n starts a line. {role} / {string_id} = his name; {role.nickname}, "
            "{role.catchphrase}, {role.signoff}, {role.finisher}, {role.insult} = his voice (filled in as it plays)."}
    line = {"oneOf": [text, {"type": "object", "description": "One line per player: Superstars' string IDs, \"group:<name>\", "
                             "and \"default\" (required).", "required": ["default"],
                             "properties": {"default": text}, "additionalProperties": text}]}
    lines = {"oneOf": [line, {"type": "array", "items": line}]}
    who = {"oneOf": [
        {"type": "string", "description": "A role (player, partner, champion, champion2, or one of \"cast\") or a "
         "Superstar's string ID (\"the_rock\", a wrestler folder's name).", "examples": names},
        {"type": "object", "description": "A Superstar by his string ID and his game number: the number when it is his, "
         "else the string ID.", "minProperties": 1, "additionalProperties": False,
         "properties": {"id": {"type": "string"}, "game_number": {"type": ["string", "integer"]}}}]}
    cond = {"type": "string", "description": "Terms joined by and / or / not and ( ): win, lose, notlose, win by X, "
            "lose by X, ended by X (X: %s), choice N, chance N%%, <var> <op> <number> (== != < <= > >=), <role> is "
            "<Superstar>, <role> in <group> (groups: %s)." % (", ".join(sorted(set(S["HOW"]))), ", ".join(groups))}
    place = {"enum": sorted(S["PLACES"])}
    anim = {"oneOf": [{"enum": sorted(S["ANIMS"])}, {"type": "integer", "minimum": 0, "maximum": 60}]}
    castent = {"oneOf": [who, {"type": "object", "required": ["who"], "properties": {
        "who": who, "at": {"type": "integer", "minimum": 1, "maximum": 10}, "do": anim,
        "mark": {"type": "integer"}, "wait": {"type": ["integer", "boolean"]}}, "additionalProperties": False}]}
    camera = {"oneOf": [{"enum": ["close", "two", "wide", "auto"]}, {"type": "integer"}, {"type": "boolean"}]}
    step = {"type": "object", "description": "One scene step: exactly one of place / say / tron / commentary / ask / narrate / if / "
            "set / who (do) / camera / music / wait / effect / raw.", "properties": {
        "place": place, "cast": {"type": "array", "items": castent}, "fade": {"type": "integer", "minimum": 0, "maximum": 3},
        "say": lines, "tron": who, "commentary": {"enum": sorted(S["COMMENTARY"])}, "who": who, "do": anim, "at": {"type": "integer", "minimum": 1, "maximum": 10},
        "with": who, "door": {"type": "integer", "minimum": 1, "maximum": 3}, "mark": {"type": "integer"},
        "wait": {"type": ["integer", "boolean"]}, "camera": camera,
        "on": {"oneOf": [who, {"type": "array", "items": who}]},
        "ask": {"type": "string"}, "options": {"type": "array", "items": {"type": "string"}, "minItems": 2, "maxItems": 4},
        "store": {"type": "string", "description": "A variable that keeps the answer (1, 2, ...)."},
        "narrate": lines, "if": cond, "then": {"$ref": "#/$defs/steps"}, "else": {"$ref": "#/$defs/steps"},
        "set": {"$ref": "#/$defs/set"}, "music": {"enum": ["on", "off"]},
        "effect": {"type": "integer", "minimum": 0, "maximum": 10}, "raw": {"type": "string", "pattern": "^[0-9A-Fa-f]{8}$"}},
        "additionalProperties": False}
    scene = {"oneOf": [{"$ref": "#/$defs/steps"}, {"type": "object", "properties": {
        "place": place, "cast": {"type": "array", "items": castent}, "fade": {"type": "integer"}, "camera": camera,
        "steps": {"$ref": "#/$defs/steps"}}, "additionalProperties": False}]}
    rules = sorted({k for v in S["RULES"].values() for k, e in v})
    slot = {"oneOf": [who, {"type": "array", "items": who, "maxItems": 5}]}
    match = {"type": "object", "properties": {
        "type": {"enum": [t for t in S["TYPES"] if not t.endswith("_runin")]},
        "slots": {"type": "array", "items": slot},
        "teams": {"type": "array", "description": "In place of slots: 1 on 1, 2 on 2, 1 on 2, 2 on 1, 1 on 1 on 1, or four of one.",
                  "items": {"type": "array", "items": slot, "minItems": 1, "maxItems": 4}, "minItems": 2, "maxItems": 4},
        "script": {"type": "array", "maxItems": 16, "description": "Triggers: each fires once, as the match plays.", "items": {
            "type": "object", "required": ["when", "do"], "properties": {
                "when": {"type": "string", "description": "time M:SS | health <role> < N% | <role> outside | <role> in a hold | finisher by <role>"},
                "do": {"enum": ["end", "runin", "no dq", "no countout"]},
                "winner": who, "loser": who, "how": {"enum": sorted(S["END_HOW"])},
                "side": {"enum": sorted(S["RUNIN"])}}, "additionalProperties": False}},
        "arena": {"type": "string", "description": "An arena's name, any capitals.", "examples": arenas},
        "rules": {"enum": rules}, "title": {"enum": sorted(S["BELTS"])}, "location": {"enum": sorted(S["LOCATIONS"])},
        "runins": {"type": "array", "items": {"type": "object", "required": ["who"], "properties": {
            "who": who, "helps": {"enum": sorted(S["RUNIN"])}, "at": {"type": ["string", "integer"], "description": "When he comes: %s." % ", ".join("%d:%02d" % (k // 60, k % 60) for k in sorted(S["RUNIN_AT"]))}}, "additionalProperties": False}}},
        "additionalProperties": False}
    branch = {"type": "object", "properties": {
        "if": cond, "set": {"$ref": "#/$defs/set"}, "money": {"type": "integer"}, "pages": {"type": "array", "items": line},
        "scene": scene, "next": {"type": "string", "description": "A node, \"end\" or \"gameover\"."}},
        "additionalProperties": False}
    node = {"type": "object", "required": ["match"], "properties": {
        "chapter": {"type": "string"}, "pages": {"type": "array", "items": line}, "scene": scene, "match": match,
        "requirement": {"type": "string"}, "branches": {"type": "array", "items": branch},
        "retries": {"type": "integer", "minimum": 0, "maximum": 15}, "raw": {"type": "object"}},
        "additionalProperties": False}
    return {
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "$id": "story.schema.json",
        "title": "Ultimate No Mercy custom story",
        "description": "Made by mod/gen/storyschema.py from the compiler's tables (mod/gen/storyc.py). The compiler "
                       "checks more than this (text limits, who is on screen, names); its messages say what's wrong.",
        "type": "object", "required": ["nodes"],
        "properties": {
            "$schema": {"type": "string"},
            "title": {"type": "string"}, "menu": {"type": "string", "maxLength": 24},
            "championship": {"enum": sorted(S["BELTS"])},
            "cast": {"type": "object", "maxProperties": 4, "additionalProperties": {"oneOf": [who, {
                "type": "array", "items": who, "minItems": 1, "maxItems": 8,
                "description": "A recast: the first of these who isn't the player (or another role's man)."}]}},
            "vars": {"type": "object", "maxProperties": S["VARS_MAX"], "additionalProperties": {"type": "integer"}},
            "start": {"type": "string"},
            "nodes": {"type": "object", "minProperties": 1, "additionalProperties": node}},
        "additionalProperties": False,
        "$defs": {"steps": {"type": "array", "items": step},
                  "set": {"type": "object", "description": "Variables: a number sets it, \"+1\" / \"-1\" adds. Roles: "
                          "another role (\"rival\": \"partner\") or a Superstar.",
                          "additionalProperties": {"type": ["integer", "string"]}}}}

if __name__ == "__main__":
    R = S["roster"]()
    names = sorted({k[4:] for k in R if k.startswith("@ID:")})     # (startup 2026-11-19: the string IDs, not the names)
    groups = sorted(k.lower() for k in S["groups"](R))
    xb, xn = S["xarenas"]()
    arenas = [a.upper() for a in S["ARENAS"] + xn]
    out = sys.argv[1]
    json.dump(build(names, groups, arenas), open(out, "w", encoding="utf-8", newline="\n"), indent=1)
    print("%s: written" % out)
