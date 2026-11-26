"""lowpri.py <script> [args...]: runs a script at below-normal priority (its children inherit it). modnative4, 2026-11-13."""
import ctypes, runpy, sys, os
ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(), 0x4000)   # BELOW_NORMAL_PRIORITY_CLASS
sys.argv = sys.argv[1:]
sys.path.insert(0, os.path.dirname(os.path.abspath(sys.argv[0])))
runpy.run_path(sys.argv[0], run_name='__main__')
