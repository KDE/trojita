# check for lupdate and lrelease: we can't
# do it using qmake as it doesn't have
# QMAKE_LUPDATE and QMAKE_LRELEASE variables :(
#
#  I18N_LANGUAGE - if not empty, wraps only chosen language
#

get_filename_component(LINGUIST_PATH ${Qt5LinguistTools_DIR} PATH)
get_filename_component(LINGUIST_PATH ${LINGUIST_PATH} PATH)
get_filename_component(LINGUIST_PATH ${LINGUIST_PATH} PATH)
set(LINGUIST_PATH ${LINGUIST_PATH}/bin)

if(TARGET Qt5::lupdate)
    set(QT_LUPDATE_EXECUTABLE Qt5::lupdate)
else()
    find_program(QT_LUPDATE_EXECUTABLE NAMES lupdate-qt5 lupdate PATHS
        ${LINGUIST_PATH}
        NO_DEFAULT_PATH
    )
endif()

if(QT_LUPDATE_EXECUTABLE)
  message(STATUS "Found lupdate: ${QT_LUPDATE_EXECUTABLE}")
else()
  if(Qt5LinguistForTrojita_FIND_REQUIRED)
    message(FATAL_ERROR "Could NOT find lupdate")
  else()
    message(WARNING "Could NOT find lupdate")
  endif()
endif()

if(TARGET Qt5::lrelease)
    set(QT_LRELEASE_EXECUTABLE Qt5::lrelease)
else()
    find_program(QT_LRELEASE_EXECUTABLE NAMES lrelease-qt5 lrelease PATHS
        ${LINGUIST_PATH}
        NO_DEFAULT_PATH
    )
endif()

if(QT_LRELEASE_EXECUTABLE)
  message(STATUS "Found lrelease: ${QT_LRELEASE_EXECUTABLE}")
else()
  if(Qt5LinguistForTrojita_FIND_REQUIRED)
    message(FATAL_ERROR "Could NOT find lrelease")
  else()
    message(WARNING "Could NOT find lrelease")
  endif()
endif()

if(TARGET Qt5::lconvert)
    set(QT_LCONVERT_EXECUTABLE Qt5::lconvert)
else()
    find_program(QT_LCONVERT_EXECUTABLE NAMES lconvert-qt5 lconvert PATHS
        ${LINGUIST_PATH}
        NO_DEFAULT_PATH
    )
endif()

if(QT_LCONVERT_EXECUTABLE)
  message(STATUS "Found lconvert: ${QT_LCONVERT_EXECUTABLE}")
else()
  if(Qt5LinguistForTrojita_FIND_REQUIRED)
    message(FATAL_ERROR "Could NOT find lconvert")
  else()
    message(WARNING "Could NOT find lconvert")
  endif()
endif()

mark_as_advanced(QT_LUPDATE_EXECUTABLE QT_LRELEASE_EXECUTABLE QT_LCONVERT_EXECUTABLE)

if(QT_LUPDATE_EXECUTABLE AND QT_LRELEASE_EXECUTABLE AND QT_LCONVERT_EXECUTABLE)
  set(Qt5LinguistForTrojita_FOUND TRUE)

# QT5_WRAP_TS(outfiles infiles ...)
# outfiles receives .qm generated files from
# .ts files in arguments
# a target lupdate is created for you
# update/generate your translations files
# example: QT5_WRAP_TS(foo_QM ${foo_TS})
macro(QT5_WRAP_TS outfiles)
  # a target to manually run lupdate
  #add_custom_target(lupdate
                    #COMMAND ${QT_LUPDATE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR} -ts ${ARGN}
                    #WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  #)
  foreach(it ${ARGN})
    get_filename_component(it ${it} ABSOLUTE)
    get_filename_component(outfile ${it} NAME_WE)

    set(outfile ${CMAKE_CURRENT_BINARY_DIR}/${outfile}.qm)
    add_custom_command(OUTPUT ${outfile}
                       COMMAND ${QT_LRELEASE_EXECUTABLE}
                       ARGS -compress -removeidentical -silent ${it} -qm ${outfile}
                       DEPENDS ${it}
    )

    set(${outfiles} ${${outfiles}} ${outfile})
  endforeach()
endmacro()

# QT_WRAP_PO(outfiles infiles ...)
# outfiles receives .qm generated files from
# .po files in arguments
# example: QT5_WRAP_PO(foo_TS ${foo_PO})
macro(QT5_WRAP_PO outfiles)
   foreach(it ${ARGN})
      get_filename_component(it ${it} ABSOLUTE)
      # PO files are foo-en_GB.po not foo_en_GB.po like Qt expects
      get_filename_component(fileWithDash ${it} NAME_WE)
      if(NOT I18N_LANGUAGE)
        set(do_wrap ON)
      else()
        string(REGEX MATCH "${I18N_LANGUAGE}" ln ${fileWithDash})
        if(ln)
          set(do_wrap ON)
        else()
          set(do_wrap OFF)
        endif()
      endif()
      if(do_wrap)
        string(REPLACE "-" "_" filenameBase "${fileWithDash}")
        file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/locale)
        set(tsfile ${CMAKE_CURRENT_BINARY_DIR}/locale/${filenameBase}.ts)
        set(qmfile ${CMAKE_CURRENT_BINARY_DIR}/locale/${filenameBase}.qm)

        if(NOT EXISTS "${it}")
           get_filename_component(path ${it} PATH)
           string(REGEX MATCH "[^-]+$" lang "${fileWithDash}")
           set(it "${path}/${lang}.po")
       endif()

        # lconvert from PO to TS and then run lupdate to generate the correct strings
        # finally run lrelease as used above
        add_custom_command(OUTPUT ${qmfile}
                         COMMAND ${QT_LCONVERT_EXECUTABLE}
                         ARGS -i ${it} -o ${tsfile}
                         COMMAND ${QT_LUPDATE_EXECUTABLE}
                         ARGS ${CMAKE_CURRENT_SOURCE_DIR} -silent -noobsolete -ts ${tsfile}
                         COMMAND ${QT_LRELEASE_EXECUTABLE}
                         ARGS -compress -removeidentical -nounfinished -silent ${tsfile} -qm ${qmfile}
                         DEPENDS ${it}
                         )

        set(${outfiles} ${${outfiles}} ${qmfile})
      endif()
   endforeach()
endmacro()

else()
  set(Qt5LinguistForTrojita_FOUND FALSE)
endif()
