// story_test <_roster.json> <story.json> <out.pack>: the exe's story compiler (exe\...\arcade_stories.cpp) built alone,
// for gen/storycmp.py (both compilers on every story: the same bytes). _voices.json is read from beside _roster.json.
#define ARCADE_STORIES_TEST
#include "../../exe/WWFNoMercyRecomp/src/main/arcade_stories.cpp"
int main(int argc, char** argv) {
    using namespace arcade_stories;
    if (argc != 4) { fprintf(stderr, "usage: story_test <_roster.json> <story.json> <out.pack>\n"); return 2; }
    std::string err;
    Roster r = load_roster(argv[1], err);
    if (!err.empty()) { fprintf(stderr, "roster: %s\n", err.c_str()); return 1; }
    Story s;
    if (!compile_file(argv[2], r, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
    FILE* f = fopen(argv[3], "wb");
    if (!f) return 1;
    fwrite(s.pack.data(), 1, s.pack.size(), f);
    fclose(f);
    return 0;
}
