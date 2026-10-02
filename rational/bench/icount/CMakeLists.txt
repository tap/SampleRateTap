# One binary per scenario (name:id): bare-metal targets have no argv, and
# per-binary instruction totals are what the ratchet compares
# (scripts/icount.py --engine rational). The workloads are PLAN.md section
# 5's: the by-2 and by-3 stages both ways in float and Q15 at economy, the
# by-4 chain (down 2 . down 2) in Q15, the by-2 pair in float at
# transparent, and construction alone (the Q15 by-4 chain's two designs and
# tables, no streaming) as its own scenario from the first baseline.
set(_rational_icount_scenarios
    up2_float_eco:0
    down2_float_eco:1
    up3_float_eco:2
    down3_float_eco:3
    up2_q15_eco:4
    down2_q15_eco:5
    up3_q15_eco:6
    down3_q15_eco:7
    down2_down2_q15_eco:8
    up2_float_tr:9
    down2_float_tr:10
    construct_q15_eco:11)

foreach(_sc IN LISTS _rational_icount_scenarios)
    string(REPLACE ":" ";" _parts "${_sc}")
    list(GET _parts 0 _name)
    list(GET _parts 1 _id)
    add_executable(tap_sr_rational_icount_${_name} icount_main.cpp)
    target_compile_definitions(tap_sr_rational_icount_${_name} PRIVATE TAP_SR_RATIONAL_SC=${_id})
    target_link_libraries(tap_sr_rational_icount_${_name} PRIVATE tap::sr::rational tap_sr_rational_warnings)
endforeach()
