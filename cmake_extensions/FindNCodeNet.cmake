# - Try to find ncode
#
# Usage of this module as follows:
#
#     find_package(NCodeCommon)
#
# Variables defined by this module:
#
#  NCODE_NET_FOUND        Whether or not ncode-common was found
#  NCODE_NET_INCLUDE_DIR  The ncode-net include directory.
#  NCODE_NET_LIBRARY      The ncode-net library

find_path(NCODE_NET_INCLUDE_DIR
    NAMES ncode/ncode_net/net_common.h)

find_library(NCODE_NET_LIBRARY
  NAMES ncode_net)

message("NCodeNet include dir = ${NCODE_NET_INCLUDE_DIR}")
message("NCodeNet lib = ${NCODE_NET_LIBRARY}")

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(NCodeNet DEFAULT_MSG
    NCODE_NET_LIBRARY
    NCODE_NET_INCLUDE_DIR)
  
mark_as_advanced(
    NCODE_NET_INCLUDE_DIR
    NCODE_NET_LIBRARY)
