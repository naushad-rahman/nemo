SET(NEMO_INSTALLED FALSE)

FILE(GLOB GLOB_TEMP_VAR $ENV{NEMO})
IF(GLOB_TEMP_VAR)
  SET(NEMO_INSTALLED TRUE)
ENDIF(GLOB_TEMP_VAR)

if (NOT NEMO_INSTALLED)
MESSAGE(STATUS "NEMO environement not loaded.... using nemo_light")
endif (NOT NEMO_INSTALLED)
