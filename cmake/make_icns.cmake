# PNG to .icns, for macOS builds without Xcode. Run with cmake -P and:
#   SRC_PNG    the 1024x1024 source
#   OUT_DIR    where the .icns should land
#   ICON_NAME  basename for the .icns, matching CFBundleIconFile
#
# sips and iconutil are part of macOS itself, so this works on any Mac. It
# cannot make the layered icon, which needs actool from Xcode.

set(ICONSET ${OUT_DIR}/${ICON_NAME}.iconset)

# From scratch every time, because iconutil packs whatever is in the directory.
file(REMOVE_RECURSE ${ICONSET})
file(MAKE_DIRECTORY ${ICONSET})

foreach(SIZE 16 32 128 256 512)
  math(EXPR RETINA "${SIZE} * 2")

  execute_process(
    COMMAND sips -z ${SIZE} ${SIZE} ${SRC_PNG}
            --out ${ICONSET}/icon_${SIZE}x${SIZE}.png
    RESULT_VARIABLE RESULT
    OUTPUT_QUIET
    ERROR_QUIET
  )

  if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "sips failed on ${SRC_PNG} at ${SIZE}x${SIZE}")
  endif()

  execute_process(
    COMMAND sips -z ${RETINA} ${RETINA} ${SRC_PNG}
            --out ${ICONSET}/icon_${SIZE}x${SIZE}@2x.png
    RESULT_VARIABLE RESULT
    OUTPUT_QUIET
    ERROR_QUIET
  )

  if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "sips failed on ${SRC_PNG} at ${RETINA}x${RETINA}")
  endif()
endforeach()

execute_process(
  COMMAND iconutil --convert icns ${ICONSET} --output ${OUT_DIR}/${ICON_NAME}.icns
  RESULT_VARIABLE RESULT
)

if(NOT RESULT EQUAL 0)
  message(FATAL_ERROR "iconutil failed to pack ${ICONSET}")
endif()
