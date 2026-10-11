# eer_package_app(<target> NAME <name> ID <reverse dns id> [ICON <set>]
#                 [COMMENT <text>])
#
# Makes a target a proper desktop app on each platform: a .app bundle with an
# icon on macOS, an icon compiled into the .exe on Windows, and a .desktop
# entry plus hicolor icons on Linux. Every app calls this once.
#
# ICON names a directory under assets/, and the files inside repeat the name:
#   assets/<set>/<set>.icon        Apple Icon Composer, needs actool (Xcode)
#   assets/<set>/<set>.png         1024x1024, the macOS fallback
#   assets/<set>/<set>-<size>.png  48, 128, 256, 512 for Linux
#   assets/<set>/<set>.ico         Windows
# Every icon step is skipped when its file is missing, so an app still builds
# with no art.
#
# ID must equal AppInfo::id in the app's main.cpp. On Linux the compositor
# matches the two to find the icon.

include(GNUInstallDirs)

function(eer_package_app TARGET)
  cmake_parse_arguments(APP "" "NAME;ID;ICON;COMMENT" "" ${ARGN})

  if(NOT APP_ICON)
    set(APP_ICON trident)
  endif()

  set(ICON_DIR ${PROJECT_SOURCE_DIR}/assets/${APP_ICON})
  set(ICON_ICON ${ICON_DIR}/${APP_ICON}.icon)
  set(ICON_PNG ${ICON_DIR}/${APP_ICON}.png)
  set(ICON_ICO ${ICON_DIR}/${APP_ICON}.ico)

  if(APPLE)
    set(ICON_OUT ${CMAKE_CURRENT_BINARY_DIR}/icon)

    # actool is inside Xcode, not the Command Line Tools.
    execute_process(
      COMMAND xcrun --find actool
      OUTPUT_VARIABLE ACTOOL
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
      RESULT_VARIABLE ACTOOL_RESULT
    )

    set(EER_ICON_NAME ${APP_ICON})
    set(EER_ICON_NAME_KEY "")
    set(ICON_FILES "")

    if(ACTOOL_RESULT EQUAL 0 AND EXISTS ${ICON_ICON})
      message(STATUS "${TARGET} icon: actool, from ${APP_ICON}.icon")

      # CFBundleIconName sends macOS to Assets.car. It must not be written on
      # the fallback path, which makes no Assets.car for it to name.
      set(EER_ICON_NAME_KEY
        "\n\t<key>CFBundleIconName</key>\n\t<string>${APP_ICON}</string>")

      # The .icon is an opaque directory with no manifest, hence the glob.
      file(GLOB_RECURSE ICON_DEPS CONFIGURE_DEPENDS ${ICON_ICON}/*)

      add_custom_command(
        OUTPUT ${ICON_OUT}/Assets.car ${ICON_OUT}/${APP_ICON}.icns
        COMMAND ${CMAKE_COMMAND} -E make_directory ${ICON_OUT}
        COMMAND ${ACTOOL} ${ICON_ICON}
                --compile ${ICON_OUT}
                --app-icon ${APP_ICON}
                --output-partial-info-plist ${ICON_OUT}/icon.plist
                --platform macosx
                --minimum-deployment-target 13.0
                > /dev/null
        DEPENDS ${ICON_DEPS}
        COMMENT "Compiling ${APP_ICON}.icon (actool)"
      )

      set(ICON_FILES ${ICON_OUT}/Assets.car ${ICON_OUT}/${APP_ICON}.icns)

    elseif(EXISTS ${ICON_PNG})
      # If Xcode is installed but not selected, configure with DEVELOPER_DIR
      # pointing at its Developer directory to get the layered icon.
      message(STATUS
        "${TARGET} icon: no actool or no ${APP_ICON}.icon, using the flat "
        "${APP_ICON}.png")

      add_custom_command(
        OUTPUT ${ICON_OUT}/${APP_ICON}.icns
        COMMAND ${CMAKE_COMMAND} -E make_directory ${ICON_OUT}
        COMMAND ${CMAKE_COMMAND}
                -DSRC_PNG=${ICON_PNG}
                -DOUT_DIR=${ICON_OUT}
                -DICON_NAME=${APP_ICON}
                -P ${PROJECT_SOURCE_DIR}/cmake/make_icns.cmake
        DEPENDS ${ICON_PNG}
        COMMENT "Building ${APP_ICON}.icns (sips/iconutil)"
      )

      set(ICON_FILES ${ICON_OUT}/${APP_ICON}.icns)
    else()
      message(STATUS "${TARGET} icon: none under ${ICON_DIR}")
    endif()

    set(EER_APP_DISPLAY_NAME "${APP_NAME}")

    # @ONLY keeps the MACOSX_BUNDLE_ placeholders for CMake to fill in later.
    configure_file(
      ${PROJECT_SOURCE_DIR}/cmake/BundleInfo.plist.in
      ${CMAKE_CURRENT_BINARY_DIR}/BundleInfo.plist.in
      @ONLY
    )

    # A bundle and not a bare executable, because the Dock reads the name and
    # icon from Info.plist. Running Contents/MacOS/<target> directly still
    # behaves like a normal command line program.
    set_target_properties(${TARGET} PROPERTIES
      MACOSX_BUNDLE TRUE
      MACOSX_BUNDLE_BUNDLE_NAME "${APP_NAME}"
      MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
      MACOSX_BUNDLE_GUI_IDENTIFIER "${APP_ID}"
      MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_BINARY_DIR}/BundleInfo.plist.in
    )

    if(ICON_FILES)
      target_sources(${TARGET} PRIVATE ${ICON_FILES})

      # Marking them as resources also stops CMake trying to compile a .car.
      set_source_files_properties(${ICON_FILES} PROPERTIES
        GENERATED TRUE
        MACOSX_PACKAGE_LOCATION Resources
      )
    endif()

  elseif(WIN32)
    # Stops a console window opening behind the app.
    set_target_properties(${TARGET} PROPERTIES WIN32_EXECUTABLE TRUE)

    # Explorer reads the icon out of the .exe, so it goes through a resource
    # script. The RC language is enabled in the top-level CMakeLists.
    if(EXISTS ${ICON_ICO})
      set(RC_FILE ${CMAKE_CURRENT_BINARY_DIR}/icon.rc)
      file(WRITE ${RC_FILE} "IDI_ICON1 ICON \"${ICON_ICO}\"\n")
      target_sources(${TARGET} PRIVATE ${RC_FILE})
    endif()

    # Windows looks for DLLs next to the .exe. GStreamer's are not covered,
    # its bin directory has to be on PATH.
    add_custom_command(TARGET ${TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:${TARGET}> $<TARGET_FILE_DIR:${TARGET}>
      COMMAND_EXPAND_LISTS
    )

  elseif(UNIX)
    # StartupWMClass has to match the app ID, or the running window is not
    # tied to this launcher and the dock shows a second unnamed entry.
    set(DESKTOP_FILE ${CMAKE_CURRENT_BINARY_DIR}/${APP_ID}.desktop)

    file(WRITE ${DESKTOP_FILE}
      "[Desktop Entry]\n"
      "Type=Application\n"
      "Name=${APP_NAME}\n"
      "Comment=${APP_COMMENT}\n"
      "Exec=${TARGET}\n"
      "Icon=${TARGET}\n"
      "Terminal=false\n"
      "Categories=Utility;\n"
      "StartupWMClass=${APP_ID}\n")

    install(FILES ${DESKTOP_FILE}
      DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)

    # Each image has to really be the size its directory claims, or some
    # desktops ignore it.
    foreach(SIZE 48 128 256 512)
      set(SIZED_PNG ${ICON_DIR}/${APP_ICON}-${SIZE}.png)

      if(EXISTS ${SIZED_PNG})
        install(FILES ${SIZED_PNG}
          DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/${SIZE}x${SIZE}/apps
          RENAME ${TARGET}.png)
      endif()
    endforeach()

    # The icon in the window's own title bar is separate from the above. X11
    # takes it from the app, so the PNG is compiled in and passed to
    # AppInfo::iconPng.
    set(WINDOW_ICON_PNG ${ICON_DIR}/${APP_ICON}-128.png)

    if(EXISTS ${WINDOW_ICON_PNG})
      set(WINDOW_ICON_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
      file(MAKE_DIRECTORY ${WINDOW_ICON_DIR})

      add_custom_command(
        OUTPUT ${WINDOW_ICON_DIR}/window_icon.h
        COMMAND ${CMAKE_COMMAND}
                -DINPUT=${WINDOW_ICON_PNG}
                -DOUTPUT=${WINDOW_ICON_DIR}/window_icon.h
                -DSYMBOL=kWindowIconPng
                -P ${PROJECT_SOURCE_DIR}/cmake/embed_file.cmake
        DEPENDS ${WINDOW_ICON_PNG}
        COMMENT "Embedding ${APP_ICON}-128.png as the window icon"
      )

      target_sources(${TARGET} PRIVATE ${WINDOW_ICON_DIR}/window_icon.h)
      target_include_directories(${TARGET} PRIVATE ${WINDOW_ICON_DIR})
      target_compile_definitions(${TARGET} PRIVATE EER_WINDOW_ICON=1)
    endif()
  endif()

  install(TARGETS ${TARGET}
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  )
endfunction()
