# #include headers from this folder
# include_directories(${UTIL_SOURCE_DIR}/include)

include_directories(
  ${MALIGNER_SOURCE_DIR}/src/common
)

# source files
set(DP_SOURCES
  "align.cpp"
  "globals.cpp"
  "ScoreCell.cpp"
  # "ScoreMatrix.cc"
  "alignment.cpp"
  "chunk.cpp"
  "matched_chunk.cpp"
  "partialsums.cpp"
)

# Build Library
add_library(dp STATIC ${DP_SOURCES})

# install directory
# install(TARGETS dp ARCHIVE DESTINATION "${MALIGNER_LIB_DIR}")

# CAN ATTEMPT TO BUILD THE PYTHON LIBRARY WITH HELP FROM THIS:
# http://stackoverflow.com/questions/5851016/boost-python-and-cmake-link-and-load-errors
# Best to move the malignpy glue code to a difference directory, outside of dp.
# Also boost python may have trouble using the std::shared_ptr. Please test tomorrow.