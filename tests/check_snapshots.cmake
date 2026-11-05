# Runs `sim --snapshot` and checks that representative PNGs exist and are not empty.
# Inputs: -DSIM=<path to sim> -DOUT=<output dir>
file(REMOVE_RECURSE ${OUT})
execute_process(COMMAND ${SIM} --snapshot ${OUT} RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "sim --snapshot exited with ${rc}")
endif()
foreach(f sim480/day/setup-wifi-keyboard.png sim800/night/setup-wifi.png sim800/day/setup-signalk.png
        sim480/day/setup-roles.png sim800/day/setup-discovery.png
        sim480/day/setup-menu.png sim800/day/setup-display.png sim800/night/setup-about.png sim480/night/setup-display.png
        sim480/day/engine-p0-live.png sim1024/night/anchor-p0-holding.png sim800/dusk/engine-p1-live.png
        sim800/day/engine-p0-fault.png sim480/day/engine-p0-fault.png sim1024/day/engine-p1-fault.png
        sim800/day/engine-p0-off.png sim480/day/engine-p1-off.png
        sim480/day/tanks-p0-live.png sim480/day/tanks-p1-live.png sim800/night/tanks-p0-live.png
        sim1024/day/tanks-p0-fault.png
        sim480/day/power-p0-live.png sim800/day/power-p0-fault.png sim1024/dusk/power-p1-live.png
        sim800/night/power-p2-live.png
        sim800/day/power-p0-banks-live.png sim480/day/power-p1-banks-live.png sim800/night/power-p1-banks-fault.png
        sim480/dusk/power-p2-banks-live.png sim1024/day/power-p3-banks-live.png
        sim480/day/engine-p0-alarm.png sim800/night/engine-p0-alarm-long.png sim1024/day/alerts-list.png
        sim480/day/glance-p0-live.png sim800/night/glance-p0-live.png sim1024/day/glance-p0-live.png
        sim480/dusk/glance-p0-fault.png sim1024/day/glance-p0-fault.png
        sim800/day/quick-panel.png sim480/night/quick-panel.png
        sim480/day/anchor-p0-notset.png sim480/day/anchor-p0-holding.png sim800/day/anchor-p0-warn.png
        sim800/night/anchor-p0-dragging.png sim1024/day/anchor-p0-nodata.png sim1024/dusk/anchor-p0-restoring.png
        sim480/night/glance-p0-anchor-holding.png sim800/day/glance-p0-anchor-dragging.png
        sim1024/day/glance-p0-anchor-nodata.png)
  if(NOT EXISTS ${OUT}/${f})
    message(FATAL_ERROR "missing ${f}")
  endif()
  file(SIZE ${OUT}/${f} sz)
  if(sz LESS 1024)
    message(FATAL_ERROR "${f} is only ${sz} bytes")
  endif()
endforeach()
