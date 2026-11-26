#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <utility>

// Ray tracing experiment (exp/raytracing): hand RT64 the real camera.
//
// Like every AKI engine, WWF No Mercy composes each MVP on the CPU (object matrix x V x P,
// latched by func_80001580 into D_80062BA8 via func_80001E08) and submits it via forced matrices, so
// RT64 never sees a projection and its "world" transforms are clip-space MVPs. Ray tracing
// needs true world space, so two hand-edits in RecompiledFuncs (tools/readd_hand_edits.py)
// call in here (same scheme as Vpw2Recomp's src/game/raytracing.cpp):
//  - wcw_rt_latch_camera (via wcw_rt_latch_camera_multiply at the top of func_80001E08, the
//    multiply that func_80001580(cam, P, V) calls; mods hook func_80001580 itself), the camera latch every camera
//    setup goes through, copies the P and V float matrices.
//  - wcw_rt_emit_camera: right after func_80002BB4 returns the frame's display-list head
//    (in func_80002FA4), writes
//      gEXEnable                (RT64 hook on F3DEX2 G_SPNOOP 0xE0; RT64 resets it per DL)
//      gEXSetViewMatrixFloat / gEXSetProjMatrixFloat (inline copies of V and P)
//      gSPBranchList            (past the inline matrices, which share the DL's lifetime)
//    and returns the advanced head. RT64 ([wcw rt], rt64_rsp.cpp) loads this camera into
//    the projection stack only while ray tracing is active (Graphics menu option or WCW_RT)
//    and ignores it otherwise, so rendering is unchanged with the option off.

namespace {
    constexpr uint32_t RT64_HOOK_OPCODE = 0xE0;
    constexpr uint32_t RT64_HOOK_MAGIC = 0x525464;
    constexpr uint32_t RT64_HOOK_OP_ENABLE = 0x1;
    constexpr uint32_t RT64_EXTENDED_OPCODE = 0x64;
    constexpr uint32_t G_EX_SETPROJMATRIXFLOAT_V1 = 0x00002D;
    constexpr uint32_t G_EX_SETVIEWMATRIXFLOAT_V1 = 0x00002E;
    constexpr uint32_t GBI_G_DL = 0xDE;
    constexpr uint32_t G_DL_NOPUSH = 0x01;

    bool camera_valid = false;
    uint32_t camera_view[16];
    uint32_t camera_proj[16];

    // The game's camera matrix (V x P, row vectors), built by func_80001580 through func_80001E08.
    // Mods rebuild it after the fact (AkiMods' camera hooks func_80001580), so the view handed
    // to RT64 is derived from it: RT64 only needs V x P to equal the matrix the game draws with.
    constexpr uint32_t CAMERA_MATRIX = 0x80062BA8u;

    float word_float(uint32_t w) {
        float f;
        memcpy(&f, &w, sizeof(f));
        return f;
    }

    uint32_t float_word(float f) {
        uint32_t w;
        memcpy(&w, &f, sizeof(w));
        return w;
    }

    // Gauss-Jordan inverse of a row-major 4x4; false when singular.
    bool invert4(const float *m, float *out) {
        float a[4][8];
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 4; c++) {
                a[r][c] = m[r * 4 + c];
                a[r][c + 4] = (r == c) ? 1.0f : 0.0f;
            }
        }

        for (int c = 0; c < 4; c++) {
            int pivot = c;
            for (int r = c + 1; r < 4; r++) {
                if (fabsf(a[r][c]) > fabsf(a[pivot][c])) {
                    pivot = r;
                }
            }

            if (fabsf(a[pivot][c]) < 1e-12f) {
                return false;
            }

            for (int k = 0; k < 8; k++) {
                std::swap(a[c][k], a[pivot][k]);
            }

            const float inv = 1.0f / a[c][c];
            for (int k = 0; k < 8; k++) {
                a[c][k] *= inv;
            }

            for (int r = 0; r < 4; r++) {
                if ((r != c) && (a[r][c] != 0.0f)) {
                    const float f = a[r][c];
                    for (int k = 0; k < 8; k++) {
                        a[r][k] -= f * a[c][k];
                    }
                }
            }
        }

        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 4; c++) {
                out[r * 4 + c] = a[r][c + 4];
            }
        }

        return true;
    }

    // RDRAM words are stored native-endian (N64Recomp's MEM_W layout); vaddr is KSEG0.
    uint32_t& rdram_word(uint8_t* rdram, uint32_t vaddr) {
        return *reinterpret_cast<uint32_t*>(rdram + (vaddr - 0x80000000u));
    }
}

extern "C" void wcw_rt_latch_camera(uint8_t* rdram, uint32_t proj_vaddr, uint32_t view_vaddr) {
    for (uint32_t i = 0; i < 16; i++) {
        camera_proj[i] = rdram_word(rdram, proj_vaddr + i * 4);
        camera_view[i] = rdram_word(rdram, view_vaddr + i * 4);
    }

    camera_valid = true;
}

// Called at the top of the matrix multiply (a0 = V, a1 = P, a2 = product): latches the camera
// when the product is the camera matrix. The camera builder itself can't carry the latch,
// since mods hook it and hooked functions are recompiled from the ROM without hand-edits.
extern "C" void wcw_rt_latch_camera_multiply(uint8_t* rdram, uint32_t view_vaddr, uint32_t proj_vaddr, uint32_t dest_vaddr) {
    if (dest_vaddr == CAMERA_MATRIX) {
        wcw_rt_latch_camera(rdram, proj_vaddr, view_vaddr);
    }
}

extern "C" bool wcw_rt_camera_proj(float* out) {
    if (!camera_valid) return false;
    memcpy(out, camera_proj, 64);
    return true;
}

extern "C" uint32_t wcw_rt_emit_camera(uint8_t* rdram, uint32_t gfx) {
    return gfx;      // (it told RT64 the camera in the list: nothing to tell now. The hand-edit's call stays till the code's turned to C++: step 4)
}

// (RT64's frame interpolation, called from the hand-edited func_80001E08 and func_80038880: the native renderer has its own. Nothing to do; the calls go with the hand-edits, step 4)
extern "C" void wcw_interp_process_multiply(uint32_t, uint32_t, uint32_t, const float*, const float*) {}
extern "C" void wcw_interp_invalidate_local_matrix(uint32_t) {}
extern "C" void wcw_interp_forward_local_matrix(uint32_t, uint32_t) {}
