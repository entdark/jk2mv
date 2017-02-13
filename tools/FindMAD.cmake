include(FindPackageHandleStandardArgs)

find_path(MAD_INCLUDE_DIR mad.h)
find_library(MAD_LIBRARIES mad)

find_package_handle_standard_args(
	MAD
	DEFAULT_MSG
	MAD_INCLUDE_DIR
	MAD_LIBRARIES
)
