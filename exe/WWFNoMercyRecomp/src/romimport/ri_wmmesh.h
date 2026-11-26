// ri_wmmesh.h (phase3 p3wmport, 2026-11-19): No Mercy's mesh as the arena generators handle it -- the C++ twin of
// mod\gen\arenas_ring.py Mesh (points in world units, texels with the page added, colours, triangles, two flag bytes) and
// its file form (Mesh.file: the smallest scale its points fit at, one texture page), with Python's arithmetic exactly
// (points may be whole numbers or floats: doubles hold both exactly here; round() halves to even; int() truncates).
#pragma once
#include "ri_core.h"
#include <array>
#include <optional>

namespace ri {

struct Mesh {
    std::vector<std::array<double, 3>> pts;
    std::vector<std::array<int64_t, 2>> uv;
    std::vector<std::array<uint8_t, 3>> rgb;
    std::vector<std::array<int, 3>> tris;
    int b1 = 0, b3 = 0;

    static Mesh from_file(const Bytes& d);             // arenas_ring.Mesh_from_file / Mesh.rev (the same reading)
    Mesh rot(int r) const;                              // a quarter turn about y r times ((x, z) -> (z, -x))
    Mesh lit(uint8_t bright = 0xFF) const;              // Revenge's normals (73, 73, 73) made a colour
    Mesh operator+(const Mesh& o) const;                // one after the other
    // Mesh.file(): nullopt where the Python raises (no scale fits, > 127 points / triangles, texels past a page)
    std::optional<Bytes> file_opt() const;
    Bytes file() const;                                  // Error where the Python raises
};
Mesh join(const std::vector<Mesh>& ms);
double py_round_half_even(double x);                     // round(x) (to an int value, ties to even)
inline int64_t py_int(double x) { return (int64_t)x; }  // int(x): toward zero
double py_floordiv(double a, double b);                  // a // b on floats (CPython's float_floor_div)

}  // namespace ri
