# Keep pinned dependencies pristine. Compile small, verified source overrides
# from the tracked project instead of leaving unrecorded edits in submodules.
function(moonvibe_override_source target original output content)
    get_target_property(sources ${target} SOURCES)
    get_target_property(source_dir ${target} SOURCE_DIR)
    set(updated)
    set(found FALSE)
    foreach(source IN LISTS sources)
        get_filename_component(absolute "${source}" ABSOLUTE BASE_DIR "${source_dir}")
        if(absolute STREQUAL original)
            list(APPEND updated "${output}")
            set(found TRUE)
        else()
            list(APPEND updated "${source}")
        endif()
    endforeach()
    if(NOT found)
        message(FATAL_ERROR "Moonvibe motion override target source missing: ${original}")
    endif()
    file(WRITE "${output}" "${content}")
    set_property(TARGET ${target} PROPERTY SOURCES "${updated}")
endfunction()

function(moonvibe_remove_required variable fragment)
    string(FIND "${${variable}}" "${fragment}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Pinned Borealis source changed; review Moonvibe motion override")
    endif()
    string(REPLACE "${fragment}" "" patched "${${variable}}")
    set(${variable} "${patched}" PARENT_SCOPE)
endfunction()

if(PLATFORM_SWITCH)
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/moonvibe-motion")
    set(borealis_original "${BOREALIS_LIBRARY}/lib/platforms/switch/switch_input.cpp")
    file(READ "${borealis_original}" borealis_motion)
    string(REPLACE "\r\n" "\n" borealis_motion "${borealis_motion}")
    foreach(fragment IN ITEMS
        "    hidStartSixAxisSensor(this->m_six_axis_sensor_handle[0]);\n"
        "    hidStartSixAxisSensor(this->m_six_axis_sensor_handle[1]);\n"
        "    hidStartSixAxisSensor(this->m_six_axis_sensor_handle[2]);\n"
        "    hidStartSixAxisSensor(this->m_six_axis_sensor_handle[3]);\n"
        "    hidStopSixAxisSensor(this->m_six_axis_sensor_handle[0]);\n"
        "    hidStopSixAxisSensor(this->m_six_axis_sensor_handle[1]);\n"
        "    hidStopSixAxisSensor(this->m_six_axis_sensor_handle[2]);\n"
        "    hidStopSixAxisSensor(this->m_six_axis_sensor_handle[3]);\n"
        "    handleControllerSensors();\n")
        moonvibe_remove_required(borealis_motion "${fragment}")
    endforeach()
    moonvibe_override_source(borealis "${borealis_original}"
        "${CMAKE_BINARY_DIR}/moonvibe-motion/switch_input.cpp" "${borealis_motion}")

    set(common_original "${EXTERN_PATH}/moonlight-common-c/src/InputStream.c")
    file(READ "${common_original}" common_input)
    string(REPLACE "\r\n" "\n" common_input "${common_input}")
    set(function_start "int LiSendControllerMotionEvent(")
    string(FIND "${common_input}" "${function_start}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Moonlight motion entrypoint missing")
    endif()
    string(SUBSTRING "${common_input}" 0 ${position} prefix)
    string(SUBSTRING "${common_input}" ${position} -1 motion_function)
    set(old_gate "if (!(SunshineFeatureFlags & LI_FF_CONTROLLER_TOUCH_EVENTS)) {")
    string(FIND "${motion_function}" "${old_gate}" gate_position)
    if(gate_position EQUAL -1)
        message(FATAL_ERROR "Pinned Moonlight motion gate changed; review compatibility override")
    endif()
    string(REPLACE "${old_gate}"
        "if (!(SunshineFeatureFlags & LI_FF_CONTROLLER_TOUCH_EVENTS) &&\n        !(IS_SUNSHINE() && MoonvibeHostRequestedMotion(controllerNumber, motionType))) {"
        motion_function "${motion_function}")
    set(common_input "${prefix}extern bool MoonvibeHostRequestedMotion(uint8_t, uint8_t);\n\n${motion_function}")
    moonvibe_override_source(moonlight-common-c "${common_original}"
        "${CMAKE_BINARY_DIR}/moonvibe-motion/InputStream.c" "${common_input}")
    target_include_directories(moonlight-common-c PRIVATE "${EXTERN_PATH}/moonlight-common-c/src")
endif()
