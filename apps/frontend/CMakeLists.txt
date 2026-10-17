# The surface frontend: four cameras, screenshots, settings.

add_executable(eer_frontend
  src/main.cpp
  src/BindingsEditor.cpp
  src/Config.cpp
  src/Controls.cpp
  src/DummyRov.cpp
  src/Pilot.cpp
)

target_compile_options(eer_frontend PRIVATE ${EER_WARNINGS})

target_link_libraries(eer_frontend
  PRIVATE
    eer::app
    eer::video
    nlohmann_json::nlohmann_json
)

eer_package_app(eer_frontend
  NAME "EER Frontend"
  ID com.easternedgerobotics.frontend2027
  ICON trident
  COMMENT "Eastern Edge ROV surface frontend"
)
