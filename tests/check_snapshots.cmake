# Runs `sim --snapshot` and checks that representative PNGs exist and are not empty.
# Inputs: -DSIM=<path to sim> -DOUT=<output dir>
file(REMOVE_RECURSE ${OUT})
execute_process(COMMAND ${SIM} --snapshot ${OUT} RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "sim --snapshot exited with ${rc}")
endif()
foreach(f sim480/day/engine-p0-live.png sim1024/night/anchor-p0.png sim800/dusk/engine-p1-live.png
        sim800/day/engine-p0-fault.png sim480/day/engine-p0-fault.png sim1024/day/engine-p1-fault.png
        sim800/day/engine-p0-off.png sim480/day/engine-p1-off.png)
  if(NOT EXISTS ${OUT}/${f})
    message(FATAL_ERROR "missing ${f}")
  endif()
  file(SIZE ${OUT}/${f} sz)
  if(sz LESS 1024)
    message(FATAL_ERROR "${f} is only ${sz} bytes")
  endif()
endforeach()
