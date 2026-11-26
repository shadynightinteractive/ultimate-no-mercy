// ri_capture.h (phase3, 2026-11-19): a wrestler's model read out of a match RAM image (ri_models) -- the C++ twin of
// mod\gen\wmport.py capture / nm_descriptor / rev_capture, which the pack builder and the importers share.
#pragma once
#include "ri_core.h"
#include "ri_models.h"
#include <map>
#include <optional>

namespace ri {

// ---- WrestleMania 2000 (and Badd Blood: the same layout) ---------------------------------------------------------------
struct WmPart {
    int k, at;                         // the part (1-20; 22-24 the attachments) and its 8 bytes' place in the descriptor
    uint32_t mesh, tex_id, pal_id;     // the descriptor's file numbers
    std::optional<Bytes> tex, pal;     // what the part struct holds: the picture WM2000 made (the file, or drawn on)
};
struct WmCapture { Bytes desc; std::vector<WmPart> parts; };
WmCapture wm_capture(const Bytes& ram, int slot);

// WM2000's descriptor (0x258 bytes) -> No Mercy's layout (0x270 bytes); files: WM2000 file number -> the number No Mercy
// reads it by (null keeps them).
Bytes nm_descriptor(const Bytes& wm, const std::map<uint32_t, uint32_t>* files = nullptr);

uint32_t tex_size(const uint8_t* hdr);     // a picture's bytes by its 8-byte header
uint32_t pal_size(const uint8_t* hdr);     // its palette's bytes

// ---- WCW/nWo Revenge ----------------------------------------------------------------------------------------------------
struct RevPart { uint32_t mesh; std::optional<Bytes> tex, pal; };
// {part k: (mesh file, picture, palette)} of the slot (std::map: k in order, as the Python's dict is filled)
std::map<int, RevPart> rev_capture(const Bytes& ram, const std::vector<RevCall>& calls, int slot);

}  // namespace ri
