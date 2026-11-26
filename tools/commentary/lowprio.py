"""Run this process at the lowest CPU priority (2026-11-14: the night pipeline run comes first; we only take spare CPU)."""
import os, sys

def lowprio():
    if os.name == "nt":
        import ctypes
        # IDLE_PRIORITY_CLASS: only runs when nothing else wants the CPU
        ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(), 0x40)
    else:
        try: os.nice(19)
        except OSError: pass

lowprio()
