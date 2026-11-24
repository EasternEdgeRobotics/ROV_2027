# StarStruck: a photo booth on an ROV camera, with the ROV's lights as the
# flash and a Canon Selphy CP1500 printer.

add_executable(eer_starstruck
  src/main.cpp
  src/Config.cpp
  src/Lights.cpp
)

target_compile_options(eer_starstruck PRIVATE ${EER_WARNINGS})

target_link_libraries(eer_starstruck
  PRIVATE
    eer::app
    eer::video
    nlohmann_json::nlohmann_json
)

eer_package_app(eer_starstruck
  NAME "StarStruck"
  ID com.easternedgerobotics.starstruck
  ICON trident
  COMMENT "Eastern Edge ROV photo booth"
)
