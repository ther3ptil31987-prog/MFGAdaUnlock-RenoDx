# 1.5.8: the numeric FILEVERSION/PRODUCTVERSION of the addon's version resource
# must match its string values. Only the strings were bumped from 1.5.3 to
# 1.5.7, so Windows and the module log reported 1.5.2.0 for those releases.
file(READ "${RC}" text)
string(REGEX MATCH "FILEVERSION ([0-9]+),([0-9]+),([0-9]+),([0-9]+)" _ "${text}")
set(file_numeric "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}.${CMAKE_MATCH_4}")
string(REGEX MATCH "PRODUCTVERSION ([0-9]+),([0-9]+),([0-9]+),([0-9]+)" _ "${text}")
set(product_numeric "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}.${CMAKE_MATCH_4}")
string(REGEX MATCH "\"FileVersion\", \"([0-9.]+)" _ "${text}")
set(file_string "${CMAKE_MATCH_1}")
string(REGEX MATCH "\"ProductVersion\", \"([0-9.]+)" _ "${text}")
set(product_string "${CMAKE_MATCH_1}.0")
if(NOT file_numeric STREQUAL file_string OR NOT product_numeric STREQUAL product_string OR NOT file_numeric STREQUAL product_numeric)
  message(FATAL_ERROR "version resource mismatch: FILEVERSION ${file_numeric}, FileVersion ${file_string}, PRODUCTVERSION ${product_numeric}, ProductVersion ${product_string}")
endif()
message(STATUS "version resource ${file_numeric}")
