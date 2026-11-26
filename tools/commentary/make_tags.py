"""The commentary recipe without the transcripts' text and without the game's move names (the user's decision,
2026-11-18: tags only on GitHub; the standing rule: no extracted game material). Developer side, run once:

    python -I make_tags.py <lines.tsv with text> <out recipe dir> <out developer text file> <names_index.json>

1. lines.tsv: the last column (what is said) becomes a TAG, the only thing the game reads from it (director.cpp):
     name rows   "way:N"    takes with the same text are one way of saying the name (Director::name_for groups by it);
                            N numbers the distinct texts in first-seen order, so equal texts get equal tags
     count rows  "count:N"  the number the referee's count line says, as Director::event read it from the text
     the rest    "-"
2. the developer's text, kept private (git-ignored): clip, its SHA-1, the row's event, the text -- comcsv.py export puts it
   back into commentary.csv when the file is there.
3. moves_refs.tsv: the moves table's rows as REFERENCES into the player's own No Mercy (USA) (Rev 1) ROM -- the move's
   number (move id / 10), the AKI file and the offset of its 0-ended name (romimport's names_index.json) -- from which the
   game makes the table on the player's PC (packbuild/comm_moves.cpp): the same rows as the old moves.tsv, byte for byte.
"""
import json, os, sys

def count_of(text):
    """Director::event's EV_COUNT parse (director.cpp): 3 if it says three, 2 if two, else 1"""
    l = text.lower()
    if "three" in l or "3" in l: return 3
    if "two" in l or "2" in l: return 2
    return 1

def main():
    src, out_dir, text_out, names_index = sys.argv[1:5]
    rows = open(src, encoding="utf-8", newline="").read().split("\n")
    assert rows[-1] == ""
    rows = rows[:-1]
    clips = open(os.path.join(out_dir, "clips.tsv"), encoding="utf-8").read().splitlines()
    cols = clips[0].split("\t")
    sha = {int(c.split("\t")[cols.index("clip")]): c.split("\t")[cols.index("sha1")] for c in clips[1:]}
    ways, tagged, texts = {}, [], []
    for r in rows:
        f = r.split("\t")
        assert len(f) == 11, r
        text = f[10]
        if f[1] == "name":
            if text not in ways: ways[text] = len(ways) + 1
            tag = "way:%d" % ways[text]
        elif f[1] == "count":
            tag = "count:%d" % count_of(text)
        else:
            tag = "-"
        tagged.append("\t".join(f[:10] + [tag]))
        texts.append("%s\t%s\t%s\t%s" % (f[0], sha[int(f[0])], f[1], text))
    open(os.path.join(out_dir, "lines.tsv"), "w", encoding="utf-8", newline="").write("\n".join(tagged) + "\n")
    os.makedirs(os.path.dirname(text_out), exist_ok=True)
    open(text_out, "w", encoding="utf-8", newline="").write(
        "# the commentary's transcripts (developer only, never published): clip, its SHA-1, event, what is said\n" +
        "\n".join(texts) + "\n")
    idx = json.load(open(names_index))["nomercy"]
    refs = sorted((k, f, o) for k, f, o in idx if f is not None and k % 10 == 0 and k < 65530)
    open(os.path.join(out_dir, "moves_refs.tsv"), "w", encoding="utf-8", newline="").write(
        "# the moves table, as references into the player's No Mercy (USA) (Rev 1) ROM: move number (id / 10), AKI file, "
        "offset of its 0-ended name (romimport names_index.json)\n" + "".join("%d\t%d\t%d\n" % (k // 10, f, o) for k, f, o in refs))
    print("rows %d (name ways %d), moves refs %d" % (len(tagged), len(ways), len(refs)))

if __name__ == "__main__":
    main()
