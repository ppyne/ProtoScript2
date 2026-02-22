# Spec/Manual Test Coverage

Generated from `/Users/avialle/dev/ProtoScript2/tests/manifest.json` + `/Users/avialle/dev/ProtoScript2/tests/spec_refs.json`.

## Coverage Table

| spec_ref | tests |
|---|---:|
| `MANUAL:10.3.2` | 6 |
| `MANUAL:10.5.1` | 1 |
| `MANUAL:10.6` | 28 |
| `MANUAL:11.1` | 5 |
| `MANUAL:11.1.1` | 8 |
| `MANUAL:11.2.1` | 10 |
| `MANUAL:12.1` | 4 |
| `MANUAL:13.7` | 15 |
| `MANUAL:14.1` | 9 |
| `MANUAL:14.4.1` | 8 |
| `MANUAL:14.4.2` | 10 |
| `MANUAL:14.4.3` | 6 |
| `MANUAL:14.4.4` | 5 |
| `MANUAL:15.2.2` | 1 |
| `MANUAL:15.3` | 3 |
| `MANUAL:2.1` | 7 |
| `MANUAL:3.1` | 69 |
| `MANUAL:3.2` | 2 |
| `MANUAL:3.4` | 1 |
| `MANUAL:3.5` | 8 |
| `MANUAL:5.6` | 1 |
| `MANUAL:7.1` | 28 |
| `MANUAL:8.1` | 106 |
| `MANUAL:8.2` | 23 |
| `MANUAL:8.2.4` | 1 |
| `MANUAL:8.2.5` | 1 |
| `MANUAL:8.3` | 38 |
| `MANUAL:8.4` | 3 |
| `MANUAL:9.3` | 1 |
| `MANUAL:9.3.1` | 3 |
| `SPEC:10.5` | 3 |
| `SPEC:14.2` | 11 |
| `SPEC:2.0` | 1 |
| `SPEC:2.3` | 2 |
| `SPEC:2.4` | 1 |
| `SPEC:3.0` | 50 |
| `SPEC:3.1` | 13 |
| `SPEC:3.2` | 10 |
| `SPEC:3.4` | 4 |
| `SPEC:4.1` | 105 |
| `SPEC:4.3` | 28 |
| `SPEC:4.3.2` | 1 |
| `SPEC:4.3.3` | 6 |
| `SPEC:5.11.5` | 1 |
| `SPEC:5.6` | 69 |
| `SPEC:5.8` | 1 |
| `SPEC:5.9` | 3 |
| `SPEC:6.1` | 7 |
| `SPEC:6.2` | 1 |
| `SPEC:6.4` | 32 |
| `SPEC:6.6` | 38 |
| `SPEC:6.7` | 3 |
| `SPEC:7.2` | 1 |
| `SPEC:7.3` | 1 |
| `SPEC:8.6` | 19 |

## Coverage Details

### `MANUAL:10.3.2`

- `invalid/visibility_internal/internal_access_from_global_same_file`
- `invalid/visibility_internal/internal_access_from_unrelated_prototype_same_file`
- `invalid/visibility_internal/internal_keyword_outside_prototype`
- `valid/visibility_internal/internal_access_child_method`
- `valid/visibility_internal/internal_access_same_prototype_method`
- `valid/visibility_internal/internal_call_internal_method_via_public`

### `MANUAL:10.5.1`

- `edge/default_values_proto`

### `MANUAL:10.6`

- `edge/call_static_basic`
- `edge/call_static_nested`
- `edge/civildatetime_clone_allowed`
- `edge/civildatetime_subtype_runtime`
- `edge/clone_inherited_override`
- `edge/clone_super_initial_divergent`
- `edge/handle_clone_binaryfile_direct`
- `edge/handle_clone_dir_direct`
- `edge/handle_clone_pathentry_direct`
- `edge/handle_clone_pathinfo_direct`
- `edge/handle_clone_processevent_direct`
- `edge/handle_clone_processresult_direct`
- `edge/handle_clone_regexp_direct`
- `edge/handle_clone_regexpmatch_direct`
- `edge/handle_clone_textfile_direct`
- `edge/handle_clone_walker_direct`
- `edge/override_chain_order_self_specialization`
- `edge/override_multilevel_super_nested_self_deep`
- `edge/proto_bool_field`
- `edge/proto_field_init_clone_repeated`
- `edge/proto_field_init_inheritance`
- `edge/proto_field_init_list_literal`
- `edge/proto_field_init_override_no_impact`
- `edge/proto_field_init_scalar_const`
- `edge/prototype_basic`
- `edge/prototype_inherit_static`
- `edge/prototype_parent_call`
- `edge/super_clone_init`

### `MANUAL:11.1`

- `edge/index_get_list`
- `edge/index_get_map`
- `edge/index_set_list`
- `edge/index_set_map`
- `edge/oob_list_index`

### `MANUAL:11.1.1`

- `edge/list_join_concat`
- `edge/list_methods_basic`
- `edge/list_reverse_empty`
- `edge/list_reverse_int`
- `edge/list_reverse_proto`
- `edge/list_reverse_sort_consistency`
- `edge/list_reverse_string`
- `edge/list_sort_suite`

### `MANUAL:11.2.1`

- `edge/map_alias_chain_set_updates_root`
- `edge/map_alias_set_updates_original`
- `edge/map_empty_literal_typed_set`
- `edge/map_insert_on_set`
- `edge/map_iteration_in`
- `edge/map_iteration_of`
- `edge/map_key_present`
- `edge/map_methods_basic`
- `edge/map_remove_order`
- `edge/map_update_then_lookup`

### `MANUAL:12.1`

- `edge/slice_view_list_basic`
- `edge/variadic_view_empty_length`
- `edge/variadic_view_nonempty_length`
- `edge/view_string_basic`

### `MANUAL:13.7`

- `edge/string_indexof_emoji`
- `edge/string_indexof_notfound`
- `edge/string_length_ascii`
- `edge/string_length_combining`
- `edge/string_length_emoji`
- `edge/string_replace_basic`
- `edge/string_split_basic`
- `edge/string_split_empty`
- `edge/string_starts_ends`
- `edge/string_substring_emoji`
- `edge/string_to_int_float`
- `edge/string_trim_basic`
- `edge/string_trim_start_end`
- `edge/string_upper`
- `edge/string_utf8_roundtrip`

### `MANUAL:14.1`

- `edge/module_import_name_priority`
- `edge/module_import_name_search_paths_second`
- `edge/module_import_path_basic`
- `edge/module_import_path_selective`
- `edge/module_import_simple_add`
- `edge/module_namespace_call`
- `edge/module_try_catch`
- `edge/module_try_finally_rethrow`
- `edge/module_utf8_roundtrip`

### `MANUAL:14.4.1`

- `edge/io_binary_read_write`
- `edge/io_eof_length_text`
- `edge/io_exceptions_basic`
- `edge/io_print`
- `edge/io_stdout_stderr`
- `edge/io_temp_path`
- `edge/io_text_roundtrip`
- `edge/io_text_seek_tell`

### `MANUAL:14.4.2`

- `edge/math_abs_min_max`
- `edge/math_constants`
- `edge/math_domain_no_error`
- `edge/math_extended`
- `edge/math_floor_ceil_round_pow_exp`
- `edge/math_numeric_limits`
- `edge/math_random_loop`
- `edge/math_random_range`
- `edge/math_random_variability`
- `edge/math_trig_hyper`

### `MANUAL:14.4.3`

- `edge/json_constructors`
- `edge/json_decode_basic`
- `edge/json_decode_object_basic`
- `edge/json_decode_unicode_escape`
- `edge/json_encode_basic`
- `edge/json_isvalid`

### `MANUAL:14.4.4`

- `edge/time_dst_paris`
- `edge/time_iso_parse_format`
- `edge/time_iso_week`
- `edge/time_timezone_validation`
- `edge/time_utc_roundtrip`

### `MANUAL:15.2.2`

- `edge/overflow_int_add`

### `MANUAL:15.3`

- `edge/exception_derived`
- `edge/runtime_exception_filter`
- `edge/user_exception_filter`

### `MANUAL:2.1`

- `invalid/parse/manual_ex020`
- `invalid/parse/manual_ex026`
- `invalid/parse/manual_ex050`
- `invalid/parse/manual_ex053`
- `invalid/parse/manual_ex078`
- `invalid/parse/unclosed_block`
- `invalid/parse/unexpected_token_comma`

### `MANUAL:3.1`

- `invalid/type/arity_file_read_too_few`
- `invalid/type/arity_json_encode_too_few`
- `invalid/type/arity_list_join_too_few`
- `invalid/type/arity_list_push_too_many`
- `invalid/type/arity_math_pow_too_many`
- `invalid/type/arity_method_variadic_missing_fixed`
- `invalid/type/arity_string_concat_too_many`
- `invalid/type/arity_string_substring_too_few`
- `invalid/type/arity_textfile_seek_too_few`
- `invalid/type/arity_textfile_tell_too_many`
- `invalid/type/builtin_sealed_inheritance_binaryfile`
- `invalid/type/builtin_sealed_inheritance_dir`
- `invalid/type/builtin_sealed_inheritance_pathentry`
- `invalid/type/builtin_sealed_inheritance_pathinfo`
- `invalid/type/builtin_sealed_inheritance_processevent`
- `invalid/type/builtin_sealed_inheritance_processresult`
- `invalid/type/builtin_sealed_inheritance_regexp`
- `invalid/type/builtin_sealed_inheritance_regexpmatch`
- `invalid/type/builtin_sealed_inheritance_textfile`
- `invalid/type/builtin_sealed_inheritance_walker`
- `invalid/type/cast_byte_256`
- `invalid/type/cast_byte_3_5`
- `invalid/type/cast_byte_int_256`
- `invalid/type/cast_byte_minus1`
- `invalid/type/cast_int_3_14`
- `invalid/type/catch_non_exception`
- `invalid/type/civildatetime_field_assignment_forbidden`
- `invalid/type/empty_list_no_context`
- `invalid/type/empty_map_no_context`
- `invalid/type/group_descriptor_as_expression`
- `invalid/type/list_pop_empty_static`
- `invalid/type/list_sort_bad_compareTo_signature`
- `invalid/type/list_sort_missing_compareTo`
- `invalid/type/list_sort_type_error`
- `invalid/type/literal_byte_256`
- `invalid/type/manual_ex005`
- `invalid/type/manual_ex017`
- `invalid/type/manual_ex028`
- `invalid/type/manual_ex043`
- `invalid/type/manual_ex047`
- `invalid/type/manual_ex048`
- `invalid/type/manual_ex058`
- `invalid/type/manual_ex104`
- `invalid/type/module_import_path_bad_ext`
- `invalid/type/module_import_path_missing`
- `invalid/type/module_import_path_no_root_proto`
- `invalid/type/module_missing`
- `invalid/type/module_registry_invalid_type`
- `invalid/type/module_symbol_missing`
- `invalid/type/multiple_static_errors`
- `invalid/type/null_literal`
- `invalid/type/proto_const_field_missing_init`
- `invalid/type/proto_const_field_reassign`
- `invalid/type/proto_field_init_self_invalid`
- `invalid/type/proto_field_init_type_mismatch`
- `invalid/type/prototype_descriptor_as_expression`
- `invalid/type/redeclaration_same_scope`
- `invalid/type/return_self`
- `invalid/type/return_self_alias`
- `invalid/type/string_index_write`
- `invalid/type/super_method_missing`
- `invalid/type/super_outside_method`
- `invalid/type/switch_no_termination`
- `invalid/type/throw_exception_call`
- `invalid/type/type_mismatch_assignment`
- `invalid/type/unknown_group_member`
- `invalid/type/unknown_local_variable`
- `invalid/type/variadic_mutation_forbidden`
- `invalid/type/view_index_write`

### `MANUAL:3.2`

- `edge/glyph_basic`
- `edge/glyph_methods`

### `MANUAL:3.4`

- `edge/default_values_local`

### `MANUAL:3.5`

- `edge/float_methods`
- `edge/int64_print`
- `edge/int64_print_io`
- `edge/int_bitwise_ops`
- `edge/int_methods`
- `edge/int_min_literal`
- `edge/numeric_cast_valid`
- `edge/numeric_literal_context`

### `MANUAL:5.6`

- `edge/group_stress`

### `MANUAL:7.1`

- `edge/compound_assign`
- `regexp/compile_basic`
- `regexp/compile_forbidden_lookahead`
- `regexp/compile_unclosed_paren`
- `regexp/find_groups`
- `regexp/findall_empty_anchor`
- `regexp/findall_unlimited_neg1`
- `regexp/flags_im_s`
- `regexp/invalid_syntax`
- `regexp/match_empty_antiloop`
- `regexp/match_semantics`
- `regexp/nested_quantifiers`
- `regexp/nested_quantifiers_nomatch`
- `regexp/non_greedy_behavior`
- `regexp/range_max_invalid`
- `regexp/range_maxparts_invalid`
- `regexp/range_replace_max_invalid`
- `regexp/range_start_invalid`
- `regexp/replace_all_backrefs`
- `regexp/replace_backrefs`
- `regexp/replace_backrefs_out_of_range`
- `regexp/replaceall_unlimited_neg1`
- `regexp/shorthand_classes`
- `regexp/split_basic`
- `regexp/split_empty_match`
- `regexp/split_unlimited_neg1`
- `regexp/utf8_glyph_boundaries`
- `regexp/utf8_indices`

### `MANUAL:8.1`

- `edge/if_basic`
- `edge/manual_ex001`
- `edge/manual_ex002`
- `edge/manual_ex003`
- `edge/manual_ex004`
- `edge/manual_ex006`
- `edge/manual_ex007`
- `edge/manual_ex008`
- `edge/manual_ex009`
- `edge/manual_ex010`
- `edge/manual_ex011`
- `edge/manual_ex012`
- `edge/manual_ex013`
- `edge/manual_ex014`
- `edge/manual_ex015`
- `edge/manual_ex016`
- `edge/manual_ex018`
- `edge/manual_ex019`
- `edge/manual_ex021`
- `edge/manual_ex022`
- `edge/manual_ex023`
- `edge/manual_ex024`
- `edge/manual_ex025`
- `edge/manual_ex027`
- `edge/manual_ex029`
- `edge/manual_ex030`
- `edge/manual_ex031`
- `edge/manual_ex032`
- `edge/manual_ex033`
- `edge/manual_ex034`
- `edge/manual_ex035`
- `edge/manual_ex036`
- `edge/manual_ex037`
- `edge/manual_ex038`
- `edge/manual_ex039`
- `edge/manual_ex040`
- `edge/manual_ex041`
- `edge/manual_ex042`
- `edge/manual_ex044`
- `edge/manual_ex045`
- `edge/manual_ex046`
- `edge/manual_ex049`
- `edge/manual_ex051`
- `edge/manual_ex052`
- `edge/manual_ex054`
- `edge/manual_ex055`
- `edge/manual_ex056`
- `edge/manual_ex057`
- `edge/manual_ex059`
- `edge/manual_ex061`
- `edge/manual_ex062`
- `edge/manual_ex064`
- `edge/manual_ex065`
- `edge/manual_ex066`
- `edge/manual_ex067`
- `edge/manual_ex068`
- `edge/manual_ex069`
- `edge/manual_ex070`
- `edge/manual_ex071`
- `edge/manual_ex072`
- `edge/manual_ex073`
- `edge/manual_ex074`
- `edge/manual_ex075`
- `edge/manual_ex076`
- `edge/manual_ex077`
- `edge/manual_ex079`
- `edge/manual_ex080`
- `edge/manual_ex081`
- `edge/manual_ex081A`
- `edge/manual_ex082`
- `edge/manual_ex083`
- `edge/manual_ex084`
- `edge/manual_ex085`
- `edge/manual_ex086`
- `edge/manual_ex087`
- `edge/manual_ex087A`
- `edge/manual_ex087B`
- `edge/manual_ex088`
- `edge/manual_ex089`
- `edge/manual_ex090`
- `edge/manual_ex091`
- `edge/manual_ex092`
- `edge/manual_ex093`
- `edge/manual_ex094`
- `edge/manual_ex095`
- `edge/manual_ex096`
- `edge/manual_ex097`
- `edge/manual_ex098`
- `edge/manual_ex099`
- `edge/manual_ex100`
- `edge/manual_ex101`
- `edge/manual_ex102`
- `edge/manual_ex103`
- `edge/manual_ex105`
- `edge/manual_ex106`
- `edge/manual_ex107`
- `edge/manual_ex108`
- `edge/manual_ex109`
- `edge/manual_ex110`
- `edge/manual_ex111`
- `edge/manual_ex112`
- `edge/manual_ex113`
- `edge/manual_ex114`
- `edge/manual_ex115`
- `edge/manual_ex116`
- `edge/manual_ex117`

### `MANUAL:8.2`

- `fs/dir_iterator`
- `fs/exceptions`
- `fs/import_fs`
- `fs/module_missing`
- `fs/mutating`
- `fs/queries`
- `fs/size`
- `fs/walker`
- `sys/env`
- `sys/has_env`
- `sys/import_sys`
- `sys/module_missing`
- `sys_execute/args_variants`
- `sys_execute/both_order`
- `sys_execute/exec_failure`
- `sys_execute/exit_code`
- `sys_execute/input_echo`
- `sys_execute/invalid_program`
- `sys_execute/invalid_utf8_output`
- `sys_execute/large_binary_output`
- `sys_execute/permission_denied`
- `sys_execute/stderr_only`
- `sys_execute/stdout_only`

### `MANUAL:8.2.4`

- `edge/iter_list_of`

### `MANUAL:8.2.5`

- `edge/iter_list_in`

### `MANUAL:8.3`

- `edge/control_flow_continuation`
- `invalid/runtime/div_zero_int`
- `invalid/runtime/glyph_add`
- `invalid/runtime/glyph_unary`
- `invalid/runtime/index_get_string_oob`
- `invalid/runtime/index_get_string_oob_combining`
- `invalid/runtime/index_get_string_oob_emoji`
- `invalid/runtime/index_set_list_oob`
- `invalid/runtime/int_overflow_add`
- `invalid/runtime/int_to_byte_range`
- `invalid/runtime/io_after_close`
- `invalid/runtime/io_close_std`
- `invalid/runtime/io_invalid_mode`
- `invalid/runtime/io_read_size_invalid`
- `invalid/runtime/io_utf8_invalid`
- `invalid/runtime/io_write_type_binary`
- `invalid/runtime/io_write_type_text`
- `invalid/runtime/json_decode_invalid`
- `invalid/runtime/json_encode_nan`
- `invalid/runtime/list_index_oob`
- `invalid/runtime/list_pop_empty_runtime`
- `invalid/runtime/manual_ex060`
- `invalid/runtime/manual_ex063`
- `invalid/runtime/map_missing_key`
- `invalid/runtime/math_type_error`
- `invalid/runtime/module_badver`
- `invalid/runtime/module_noinit`
- `invalid/runtime/module_nosym`
- `invalid/runtime/module_not_found`
- `invalid/runtime/shift_range_int`
- `invalid/runtime/slice_invalidated_pop`
- `invalid/runtime/string_substring_oob`
- `invalid/runtime/string_to_float_invalid`
- `invalid/runtime/string_to_int_invalid`
- `invalid/runtime/unhandled_exception`
- `invalid/runtime/utf8_invalid_bytes`
- `invalid/runtime/view_invalidated_push`
- `invalid/runtime/view_oob`

### `MANUAL:8.4`

- `edge/switch_cfg_case`
- `edge/switch_cfg_default`
- `edge/switch_no_termination`

### `MANUAL:9.3`

- `edge/variadic_empty_call`

### `MANUAL:9.3.1`

- `edge/method_variadic_empty`
- `edge/method_variadic_fixed_plus`
- `edge/method_variadic_with_args`

### `SPEC:10.5`

- `edge/exception_derived`
- `edge/runtime_exception_filter`
- `edge/user_exception_filter`

### `SPEC:14.2`

- `edge/math_abs_min_max`
- `edge/math_constants`
- `edge/math_domain_no_error`
- `edge/math_extended`
- `edge/math_floor_ceil_round_pow_exp`
- `edge/math_numeric_limits`
- `edge/math_random_loop`
- `edge/math_random_range`
- `edge/math_random_variability`
- `edge/math_trig_hyper`
- `edge/overflow_int_add`

### `SPEC:2.0`

- `edge/default_values_local`

### `SPEC:2.3`

- `edge/glyph_basic`
- `edge/glyph_methods`

### `SPEC:2.4`

- `edge/group_stress`

### `SPEC:3.0`

- `edge/float_methods`
- `edge/int64_print`
- `edge/int64_print_io`
- `edge/int_bitwise_ops`
- `edge/int_methods`
- `edge/int_min_literal`
- `edge/numeric_cast_valid`
- `edge/numeric_literal_context`
- `edge/string_indexof_emoji`
- `edge/string_indexof_notfound`
- `edge/string_length_ascii`
- `edge/string_length_combining`
- `edge/string_length_emoji`
- `edge/string_replace_basic`
- `edge/string_split_basic`
- `edge/string_split_empty`
- `edge/string_starts_ends`
- `edge/string_substring_emoji`
- `edge/string_to_int_float`
- `edge/string_trim_basic`
- `edge/string_trim_start_end`
- `edge/string_upper`
- `edge/string_utf8_roundtrip`
- `regexp/compile_basic`
- `regexp/compile_forbidden_lookahead`
- `regexp/compile_unclosed_paren`
- `regexp/find_groups`
- `regexp/findall_empty_anchor`
- `regexp/findall_unlimited_neg1`
- `regexp/flags_im_s`
- `regexp/invalid_syntax`
- `regexp/match_empty_antiloop`
- `regexp/match_semantics`
- `regexp/nested_quantifiers`
- `regexp/nested_quantifiers_nomatch`
- `regexp/non_greedy_behavior`
- `regexp/range_max_invalid`
- `regexp/range_maxparts_invalid`
- `regexp/range_replace_max_invalid`
- `regexp/range_start_invalid`
- `regexp/replace_all_backrefs`
- `regexp/replace_backrefs`
- `regexp/replace_backrefs_out_of_range`
- `regexp/replaceall_unlimited_neg1`
- `regexp/shorthand_classes`
- `regexp/split_basic`
- `regexp/split_empty_match`
- `regexp/split_unlimited_neg1`
- `regexp/utf8_glyph_boundaries`
- `regexp/utf8_indices`

### `SPEC:3.1`

- `edge/index_get_list`
- `edge/index_get_map`
- `edge/index_set_list`
- `edge/index_set_map`
- `edge/list_join_concat`
- `edge/list_methods_basic`
- `edge/list_reverse_empty`
- `edge/list_reverse_int`
- `edge/list_reverse_proto`
- `edge/list_reverse_sort_consistency`
- `edge/list_reverse_string`
- `edge/list_sort_suite`
- `edge/oob_list_index`

### `SPEC:3.2`

- `edge/map_alias_chain_set_updates_root`
- `edge/map_alias_set_updates_original`
- `edge/map_empty_literal_typed_set`
- `edge/map_insert_on_set`
- `edge/map_iteration_in`
- `edge/map_iteration_of`
- `edge/map_key_present`
- `edge/map_methods_basic`
- `edge/map_remove_order`
- `edge/map_update_then_lookup`

### `SPEC:3.4`

- `edge/slice_view_list_basic`
- `edge/variadic_view_empty_length`
- `edge/variadic_view_nonempty_length`
- `edge/view_string_basic`

### `SPEC:4.1`

- `edge/manual_ex001`
- `edge/manual_ex002`
- `edge/manual_ex003`
- `edge/manual_ex004`
- `edge/manual_ex006`
- `edge/manual_ex007`
- `edge/manual_ex008`
- `edge/manual_ex009`
- `edge/manual_ex010`
- `edge/manual_ex011`
- `edge/manual_ex012`
- `edge/manual_ex013`
- `edge/manual_ex014`
- `edge/manual_ex015`
- `edge/manual_ex016`
- `edge/manual_ex018`
- `edge/manual_ex019`
- `edge/manual_ex021`
- `edge/manual_ex022`
- `edge/manual_ex023`
- `edge/manual_ex024`
- `edge/manual_ex025`
- `edge/manual_ex027`
- `edge/manual_ex029`
- `edge/manual_ex030`
- `edge/manual_ex031`
- `edge/manual_ex032`
- `edge/manual_ex033`
- `edge/manual_ex034`
- `edge/manual_ex035`
- `edge/manual_ex036`
- `edge/manual_ex037`
- `edge/manual_ex038`
- `edge/manual_ex039`
- `edge/manual_ex040`
- `edge/manual_ex041`
- `edge/manual_ex042`
- `edge/manual_ex044`
- `edge/manual_ex045`
- `edge/manual_ex046`
- `edge/manual_ex049`
- `edge/manual_ex051`
- `edge/manual_ex052`
- `edge/manual_ex054`
- `edge/manual_ex055`
- `edge/manual_ex056`
- `edge/manual_ex057`
- `edge/manual_ex059`
- `edge/manual_ex061`
- `edge/manual_ex062`
- `edge/manual_ex064`
- `edge/manual_ex065`
- `edge/manual_ex066`
- `edge/manual_ex067`
- `edge/manual_ex068`
- `edge/manual_ex069`
- `edge/manual_ex070`
- `edge/manual_ex071`
- `edge/manual_ex072`
- `edge/manual_ex073`
- `edge/manual_ex074`
- `edge/manual_ex075`
- `edge/manual_ex076`
- `edge/manual_ex077`
- `edge/manual_ex079`
- `edge/manual_ex080`
- `edge/manual_ex081`
- `edge/manual_ex081A`
- `edge/manual_ex082`
- `edge/manual_ex083`
- `edge/manual_ex084`
- `edge/manual_ex085`
- `edge/manual_ex086`
- `edge/manual_ex087`
- `edge/manual_ex087A`
- `edge/manual_ex087B`
- `edge/manual_ex088`
- `edge/manual_ex089`
- `edge/manual_ex090`
- `edge/manual_ex091`
- `edge/manual_ex092`
- `edge/manual_ex093`
- `edge/manual_ex094`
- `edge/manual_ex095`
- `edge/manual_ex096`
- `edge/manual_ex097`
- `edge/manual_ex098`
- `edge/manual_ex099`
- `edge/manual_ex100`
- `edge/manual_ex101`
- `edge/manual_ex102`
- `edge/manual_ex103`
- `edge/manual_ex105`
- `edge/manual_ex106`
- `edge/manual_ex107`
- `edge/manual_ex108`
- `edge/manual_ex109`
- `edge/manual_ex110`
- `edge/manual_ex111`
- `edge/manual_ex112`
- `edge/manual_ex113`
- `edge/manual_ex114`
- `edge/manual_ex115`
- `edge/manual_ex116`
- `edge/manual_ex117`

### `SPEC:4.3`

- `edge/call_static_basic`
- `edge/call_static_nested`
- `edge/civildatetime_clone_allowed`
- `edge/civildatetime_subtype_runtime`
- `edge/clone_inherited_override`
- `edge/clone_super_initial_divergent`
- `edge/handle_clone_binaryfile_direct`
- `edge/handle_clone_dir_direct`
- `edge/handle_clone_pathentry_direct`
- `edge/handle_clone_pathinfo_direct`
- `edge/handle_clone_processevent_direct`
- `edge/handle_clone_processresult_direct`
- `edge/handle_clone_regexp_direct`
- `edge/handle_clone_regexpmatch_direct`
- `edge/handle_clone_textfile_direct`
- `edge/handle_clone_walker_direct`
- `edge/override_chain_order_self_specialization`
- `edge/override_multilevel_super_nested_self_deep`
- `edge/proto_bool_field`
- `edge/proto_field_init_clone_repeated`
- `edge/proto_field_init_inheritance`
- `edge/proto_field_init_list_literal`
- `edge/proto_field_init_override_no_impact`
- `edge/proto_field_init_scalar_const`
- `edge/prototype_basic`
- `edge/prototype_inherit_static`
- `edge/prototype_parent_call`
- `edge/super_clone_init`

### `SPEC:4.3.2`

- `edge/default_values_proto`

### `SPEC:4.3.3`

- `invalid/visibility_internal/internal_access_from_global_same_file`
- `invalid/visibility_internal/internal_access_from_unrelated_prototype_same_file`
- `invalid/visibility_internal/internal_keyword_outside_prototype`
- `valid/visibility_internal/internal_access_child_method`
- `valid/visibility_internal/internal_access_same_prototype_method`
- `valid/visibility_internal/internal_call_internal_method_via_public`

### `SPEC:5.11.5`

- `edge/compound_assign`

### `SPEC:5.6`

- `invalid/type/arity_file_read_too_few`
- `invalid/type/arity_json_encode_too_few`
- `invalid/type/arity_list_join_too_few`
- `invalid/type/arity_list_push_too_many`
- `invalid/type/arity_math_pow_too_many`
- `invalid/type/arity_method_variadic_missing_fixed`
- `invalid/type/arity_string_concat_too_many`
- `invalid/type/arity_string_substring_too_few`
- `invalid/type/arity_textfile_seek_too_few`
- `invalid/type/arity_textfile_tell_too_many`
- `invalid/type/builtin_sealed_inheritance_binaryfile`
- `invalid/type/builtin_sealed_inheritance_dir`
- `invalid/type/builtin_sealed_inheritance_pathentry`
- `invalid/type/builtin_sealed_inheritance_pathinfo`
- `invalid/type/builtin_sealed_inheritance_processevent`
- `invalid/type/builtin_sealed_inheritance_processresult`
- `invalid/type/builtin_sealed_inheritance_regexp`
- `invalid/type/builtin_sealed_inheritance_regexpmatch`
- `invalid/type/builtin_sealed_inheritance_textfile`
- `invalid/type/builtin_sealed_inheritance_walker`
- `invalid/type/cast_byte_256`
- `invalid/type/cast_byte_3_5`
- `invalid/type/cast_byte_int_256`
- `invalid/type/cast_byte_minus1`
- `invalid/type/cast_int_3_14`
- `invalid/type/catch_non_exception`
- `invalid/type/civildatetime_field_assignment_forbidden`
- `invalid/type/empty_list_no_context`
- `invalid/type/empty_map_no_context`
- `invalid/type/group_descriptor_as_expression`
- `invalid/type/list_pop_empty_static`
- `invalid/type/list_sort_bad_compareTo_signature`
- `invalid/type/list_sort_missing_compareTo`
- `invalid/type/list_sort_type_error`
- `invalid/type/literal_byte_256`
- `invalid/type/manual_ex005`
- `invalid/type/manual_ex017`
- `invalid/type/manual_ex028`
- `invalid/type/manual_ex043`
- `invalid/type/manual_ex047`
- `invalid/type/manual_ex048`
- `invalid/type/manual_ex058`
- `invalid/type/manual_ex104`
- `invalid/type/module_import_path_bad_ext`
- `invalid/type/module_import_path_missing`
- `invalid/type/module_import_path_no_root_proto`
- `invalid/type/module_missing`
- `invalid/type/module_registry_invalid_type`
- `invalid/type/module_symbol_missing`
- `invalid/type/multiple_static_errors`
- `invalid/type/null_literal`
- `invalid/type/proto_const_field_missing_init`
- `invalid/type/proto_const_field_reassign`
- `invalid/type/proto_field_init_self_invalid`
- `invalid/type/proto_field_init_type_mismatch`
- `invalid/type/prototype_descriptor_as_expression`
- `invalid/type/redeclaration_same_scope`
- `invalid/type/return_self`
- `invalid/type/return_self_alias`
- `invalid/type/string_index_write`
- `invalid/type/super_method_missing`
- `invalid/type/super_outside_method`
- `invalid/type/switch_no_termination`
- `invalid/type/throw_exception_call`
- `invalid/type/type_mismatch_assignment`
- `invalid/type/unknown_group_member`
- `invalid/type/unknown_local_variable`
- `invalid/type/variadic_mutation_forbidden`
- `invalid/type/view_index_write`

### `SPEC:5.8`

- `edge/variadic_empty_call`

### `SPEC:5.9`

- `edge/method_variadic_empty`
- `edge/method_variadic_fixed_plus`
- `edge/method_variadic_with_args`

### `SPEC:6.1`

- `invalid/parse/manual_ex020`
- `invalid/parse/manual_ex026`
- `invalid/parse/manual_ex050`
- `invalid/parse/manual_ex053`
- `invalid/parse/manual_ex078`
- `invalid/parse/unclosed_block`
- `invalid/parse/unexpected_token_comma`

### `SPEC:6.2`

- `edge/if_basic`

### `SPEC:6.4`

- `edge/module_import_name_priority`
- `edge/module_import_name_search_paths_second`
- `edge/module_import_path_basic`
- `edge/module_import_path_selective`
- `edge/module_import_simple_add`
- `edge/module_namespace_call`
- `edge/module_try_catch`
- `edge/module_try_finally_rethrow`
- `edge/module_utf8_roundtrip`
- `fs/dir_iterator`
- `fs/exceptions`
- `fs/import_fs`
- `fs/module_missing`
- `fs/mutating`
- `fs/queries`
- `fs/size`
- `fs/walker`
- `sys/env`
- `sys/has_env`
- `sys/import_sys`
- `sys/module_missing`
- `sys_execute/args_variants`
- `sys_execute/both_order`
- `sys_execute/exec_failure`
- `sys_execute/exit_code`
- `sys_execute/input_echo`
- `sys_execute/invalid_program`
- `sys_execute/invalid_utf8_output`
- `sys_execute/large_binary_output`
- `sys_execute/permission_denied`
- `sys_execute/stderr_only`
- `sys_execute/stdout_only`

### `SPEC:6.6`

- `edge/control_flow_continuation`
- `invalid/runtime/div_zero_int`
- `invalid/runtime/glyph_add`
- `invalid/runtime/glyph_unary`
- `invalid/runtime/index_get_string_oob`
- `invalid/runtime/index_get_string_oob_combining`
- `invalid/runtime/index_get_string_oob_emoji`
- `invalid/runtime/index_set_list_oob`
- `invalid/runtime/int_overflow_add`
- `invalid/runtime/int_to_byte_range`
- `invalid/runtime/io_after_close`
- `invalid/runtime/io_close_std`
- `invalid/runtime/io_invalid_mode`
- `invalid/runtime/io_read_size_invalid`
- `invalid/runtime/io_utf8_invalid`
- `invalid/runtime/io_write_type_binary`
- `invalid/runtime/io_write_type_text`
- `invalid/runtime/json_decode_invalid`
- `invalid/runtime/json_encode_nan`
- `invalid/runtime/list_index_oob`
- `invalid/runtime/list_pop_empty_runtime`
- `invalid/runtime/manual_ex060`
- `invalid/runtime/manual_ex063`
- `invalid/runtime/map_missing_key`
- `invalid/runtime/math_type_error`
- `invalid/runtime/module_badver`
- `invalid/runtime/module_noinit`
- `invalid/runtime/module_nosym`
- `invalid/runtime/module_not_found`
- `invalid/runtime/shift_range_int`
- `invalid/runtime/slice_invalidated_pop`
- `invalid/runtime/string_substring_oob`
- `invalid/runtime/string_to_float_invalid`
- `invalid/runtime/string_to_int_invalid`
- `invalid/runtime/unhandled_exception`
- `invalid/runtime/utf8_invalid_bytes`
- `invalid/runtime/view_invalidated_push`
- `invalid/runtime/view_oob`

### `SPEC:6.7`

- `edge/switch_cfg_case`
- `edge/switch_cfg_default`
- `edge/switch_no_termination`

### `SPEC:7.2`

- `edge/iter_list_of`

### `SPEC:7.3`

- `edge/iter_list_in`

### `SPEC:8.6`

- `edge/io_binary_read_write`
- `edge/io_eof_length_text`
- `edge/io_exceptions_basic`
- `edge/io_print`
- `edge/io_stdout_stderr`
- `edge/io_temp_path`
- `edge/io_text_roundtrip`
- `edge/io_text_seek_tell`
- `edge/json_constructors`
- `edge/json_decode_basic`
- `edge/json_decode_object_basic`
- `edge/json_decode_unicode_escape`
- `edge/json_encode_basic`
- `edge/json_isvalid`
- `edge/time_dst_paris`
- `edge/time_iso_parse_format`
- `edge/time_iso_week`
- `edge/time_timezone_validation`
- `edge/time_utc_roundtrip`

## Clarifications Needed

## Suite Integrity

| suite | missing_refs | malformed_refs |
|---|---:|---:|
| `edge` | 0 | 0 |
| `fs` | 0 | 0 |
| `invalid/parse` | 0 | 0 |
| `invalid/runtime` | 0 | 0 |
| `invalid/type` | 0 | 0 |
| `invalid/visibility_internal` | 0 | 0 |
| `regexp` | 0 | 0 |
| `sys` | 0 | 0 |
| `sys_execute` | 0 | 0 |
| `valid/visibility_internal` | 0 | 0 |

No unresolved section IDs detected.

