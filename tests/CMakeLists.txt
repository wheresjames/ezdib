add_executable(ezdib_tests ezdib_tests.c ../ezdib.c)

target_include_directories(ezdib_tests PRIVATE ..)

target_compile_options(ezdib_tests PRIVATE
    $<$<CONFIG:Debug>:-g>
    $<$<CONFIG:Release>:-O2>
)

# Mirror the math-mode selection from the parent build
if(EZD_MATH_MODE STREQUAL "CORDIC")
    target_compile_definitions(ezdib_tests PRIVATE EZD_MATH_CORDIC)
elseif(EZD_MATH_MODE STREQUAL "NONE")
    target_compile_definitions(ezdib_tests PRIVATE EZD_MATH_NONE)
else()
    target_compile_definitions(ezdib_tests PRIVATE EZD_MATH_SYSTEM)
    target_link_libraries(ezdib_tests PRIVATE m)
endif()

# Register every test with CTest.  Each name matches a function in the
# dispatch table in ezdib_tests.c; a new test only needs to appear here
# and in that table.
set(EZDIB_TESTS
    # Image lifecycle
    create_24bpp
    create_32bpp
    create_1bpp
    create_negative_height
    create_user_buffer
    create_overflow
    header_size
    save_load_24bpp
    save_load_32bpp
    save_load_1bpp
    save_1bpp_file_size
    load_nonexistent
    # Pixels and colour
    color_format
    pixels_24bpp
    pixels_32bpp
    pixels_1bpp
    pixel_oob
    fill_24bpp
    fill_32bpp
    palette_ops
    color_threshold
    # Lines
    line_horizontal
    line_vertical
    line_diagonal
    line_clipped
    line_offscreen
    line_1bpp
    # Rectangles
    fill_rect_24bpp
    fill_rect_32bpp
    fill_rect_1bpp
    rect_outline
    fill_rect_coords_swapped
    # Circles and arcs
    circle_24bpp
    circle_32bpp
    circle_zero_radius
    arc_partial
    arc_full_covers_circle
    # Flood fill
    flood_fill_24bpp
    flood_fill_32bpp
    flood_fill_no_bleed
    flood_fill_already_colour
    # Fonts and text
    font_load_small
    font_load_medium
    font_rejects_malformed
    text_renders_pixels
    text_size
    text_size_wider
    text_newline
    text_null_terminated
    text_bottom_up_top_edge
    # Scaling
    scale_nearest_1to1_24bpp
    scale_nearest_1to1_32bpp
    scale_nearest_source_vals
    scale_upscale_solid
    scale_bilinear_monotone
    scale_area_24bpp
    scale_area_32bpp
    scale_1bpp_nearest
    scale_mismatched_bpp
    scale_sub_region
    # Math
    sin_cos_zero
    sin_cos_pi_over_2
    sin_cos_pi
    sin_cos_identity
    sin_cos_symmetry
    sin_cos_wrap
    # Robustness
    null_handle
    pixel_callback
    scale_clipped_rects
)

foreach(t IN LISTS EZDIB_TESTS)
    add_test(NAME ${t} COMMAND ezdib_tests ${t})
endforeach()
