import ctypes,sys
lib=ctypes.CDLL('libjack.so.0');lib.jack_client_open.restype=ctypes.c_void_p
lib.jack_client_open.argtypes=[ctypes.c_char_p,ctypes.c_uint,ctypes.POINTER(ctypes.c_uint)]
lib.jack_client_close.argtypes=[ctypes.c_void_p]
status=ctypes.c_uint();client=lib.jack_client_open(b's03-readiness',1,ctypes.byref(status))
if client:lib.jack_client_close(client)
sys.exit(0 if client else 1)
