include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/components.cmake)
bi_component(NAME roles
  SRCS src/role_bind.c src/role_engine.c src/page_engine.c src/engine_state.c src/power_calc.c src/role_spec.c src/role_tanks.c src/page_tanks.c src/role_power.c src/page_power.c src/page_power_summary.c src/power_topo.c src/page_power_banks.c src/glance_state.c src/role_glance.c src/page_glance.c src/anchor_profile.c src/anchor_state.c src/anchor_record.c src/anchor_watch.c src/anchor_ctl.c src/role_anchor.c src/page_anchor.c src/nav_state.c
  INCLUDE_DIRS include
  REQUIRES boatdata config units ui alerts platform history lvgl json)
