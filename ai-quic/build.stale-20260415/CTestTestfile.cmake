# CMake generated Testfile for 
# Source directory: /home/racosel/Desktop/quic-1 (copy)/ai-quic
# Build directory: /home/racosel/Desktop/quic-1 (copy)/ai-quic/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[ai_quic_smoke_test]=] "/home/racosel/Desktop/quic-1 (copy)/ai-quic/build/bin/ai_quic_smoke_test")
set_tests_properties([=[ai_quic_smoke_test]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/racosel/Desktop/quic-1 (copy)/ai-quic/CMakeLists.txt;167;add_test;/home/racosel/Desktop/quic-1 (copy)/ai-quic/CMakeLists.txt;0;")
subdirs("boringssl")
