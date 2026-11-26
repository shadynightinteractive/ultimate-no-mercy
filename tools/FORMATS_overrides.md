## 13. Overrides: wrestler.override.json (read by the game from 0.85)

The importers make every wrestler folder of the imported collections (No Mercy, Revenge, WrestleMania 2000, Imported)
from the player's own ROMs, and make them again on every re-import: `wrestler.json`, `moves.json`, `model\`, `photo.bin`,
`theme.bin` ... are the importer's. Whatever the user (or the editor) changes goes into a file of his own beside them:

    <wrestler folder>\wrestler.override.json

### 13.1 What the game does with it (exe `arcade_wrestlers.cpp` apply_override; `tools/wrestler_override.py` the same in Python)

Each time the game reads a wrestler.json it lays the folder's override over it, as a **JSON Merge Patch (RFC 7386)**:

| rule | |
|---|---|
| O1 | An object in the override goes into the object at the same place, key by key, recursively: only the keys it names change. `{"body": {"nm_size": 70}}` changes `body.nm_size`, the rest of `body` stays. |
| O2 | Any other value -- text, a number, true/false, a LIST -- replaces what wrestler.json has there. A list is replaced whole: to change one of the four `looks`, write all four. |
| O3 | `null` deletes the key. `{"nick": null}`: no nickname of his own (the man he is based on keeps his). The only way to delete. |
| O4 | Precedence: override > wrestler.json > the wrestler it is `based_on` (or the built-in it is). |
| O5 | Identity is the folder's: `id`, `string_id`, `builtin`, `replaces` in an override are left out (log line; checker warning). His ID, number and what he replaces never change through an override. |
| O6 | An override that isn't a JSON object is not used at all (log line; checker error): he loads as his wrestler.json says. No file: nothing changes. |
| O7 | Only wrestler.json is overridden. moves.json, model\, photo.bin, theme.bin ... are not (a later round may add `moves.override.json`). |

Example: `{"name": "THE LADY", "nick": null, "height": 8, "body": {"nm_size": 70}}`.

The game's log: `[wrestlers] <folder>\wrestler.override.json: laid over his wrestler.json (N keys)`, or why it isn't used.

### 13.2 Who writes it

- The importers NEVER write or delete it; a re-import leaves it where it is (install a new import by copying over the old
  folders, never by mirroring them).
- The editor writes every change to an imported or stock wrestler as an override: only the keys the user changed, with
  the user's values; a key the user "resets" is taken OUT of the override (not written as the generated value). To remove
  a value the generated file has, write `null` (O3). The editor shows the effective wrestler: `wrestler_override.effective(folder)`.
- A wrestler the user made himself (his own folder, no importer) may keep editing wrestler.json directly: nothing will
  overwrite it. The override works there too.

### 13.3 The checker

`validators/wrestler_override.py` (beside `families_check.py`, standard library only):

    python wrestler_override.py <wrestlers folder>          every override: ERRORs (O6, wrong types, a `looks` list not of 4) and
                                                            warnings (O5 identity keys, keys the game doesn't read, text too long)
    python wrestler_override.py --show <wrestler folder>    his wrestler.json with the override laid over it

`families_check.py` lists the same results with the families files. Functions for the editor: `merge_patch(target, patch)`,
`effective(folder)`, `check_override(folder)`, `check_tree(root)`.
