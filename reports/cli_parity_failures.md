# CLI Runtime Parity Failures

Source: `reports/proof_pack/03_cli_runtime_parity.log`

| case_id | rc_node | rc_cli | classification | diff minimal |
|---|---:|---:|---|---|
| `invalid/runtime/int_overflow_add` | 1 | 1 | `diagnostics-only` | --- /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.M3H6y54Q22	2026-02-19 01:12:56 +++ /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.SUP8t3YA4x	2026-02-19 01:12:56 @@ -1 +1 @@ -/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/int_overflow_add.pts:5:13 R1001 RUNTIME_INT_OVERFLOW: int overflow. got value; expected value within int range +/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/int_overflow_add.pts:5:13 R1001 RUNTIME_INT_OVERFLOW: int overflow. got 9223372036854775807 + 1; expected value within int range |
| `invalid/runtime/glyph_add` | 1 | 1 | `diagnostics-only` | --- /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.ugOoFE90d8	2026-02-19 01:12:56 +++ /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.y0Z0dTbVdg	2026-02-19 01:12:56 @@ -1 +1 @@ -/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/glyph_add.pts:4:17 R1010 RUNTIME_TYPE_ERROR: invalid glyph operation. got value; expected compatible type +/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/glyph_add.pts:4:17 R1010 RUNTIME_TYPE_ERROR: invalid operand types. got value + value; expected numeric operands |
| `invalid/runtime/glyph_unary` | 1 | 1 | `diagnostics-only` | --- /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.cWRmtM9qkY	2026-02-19 01:12:56 +++ /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.rtAKSVU6jP	2026-02-19 01:12:56 @@ -1 +1 @@ -/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/glyph_unary.pts:3:15 R1010 RUNTIME_TYPE_ERROR: invalid glyph operation. got value; expected compatible type +/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/glyph_unary.pts:3:15 R1010 RUNTIME_TYPE_ERROR: invalid operand type. got value; expected int |
| `invalid/runtime/list_index_oob` | 1 | 1 | `diagnostics-only` | --- /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.XjMEqLr9Ig	2026-02-19 01:12:57 +++ /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.LCpjZioOHs	2026-02-19 01:12:57 @@ -1 +1 @@ -/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/list_index_oob.pts:5:13 R1002 RUNTIME_INDEX_OOB: index out of bounds. got 10; expected index within bounds +/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/list_index_oob.pts:5:13 R1002 RUNTIME_INDEX_OOB: index out of bounds. got 10; expected 0..2 |
| `invalid/runtime/index_get_string_oob` | 1 | 1 | `diagnostics-only` | --- /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.RyYQF3c2vo	2026-02-19 01:12:57 +++ /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.MgE5vvw1n1	2026-02-19 01:12:57 @@ -1 +1 @@ -/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/index_get_string_oob.pts:5:15 R1002 RUNTIME_INDEX_OOB: index out of bounds. got 2; expected index within bounds +/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/index_get_string_oob.pts:5:15 R1002 RUNTIME_INDEX_OOB: index out of bounds. got 2; expected 0..1 |
| `invalid/runtime/index_set_list_oob` | 1 | 1 | `diagnostics-only` | --- /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.5z2HPBgLti	2026-02-19 01:12:57 +++ /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.v0uPdpNhWO	2026-02-19 01:12:57 @@ -1 +1 @@ -/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/index_set_list_oob.pts:5:5 R1002 RUNTIME_INDEX_OOB: index out of bounds. got 5; expected index within bounds +/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/index_set_list_oob.pts:5:5 R1002 RUNTIME_INDEX_OOB: index out of bounds. got 5; expected 0..0 |
| `invalid/runtime/index_get_string_oob_emoji` | 1 | 1 | `diagnostics-only` | --- /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.PwWfiF4Rr7	2026-02-19 01:12:57 +++ /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.ktyZv7B29t	2026-02-19 01:12:57 @@ -1 +1 @@ -/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/index_get_string_oob_emoji.pts:5:15 R1002 RUNTIME_INDEX_OOB: index out of bounds. got 3; expected index within bounds +/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/index_get_string_oob_emoji.pts:5:15 R1002 RUNTIME_INDEX_OOB: index out of bounds. got 3; expected 0..2 |
| `invalid/runtime/index_get_string_oob_combining` | 1 | 1 | `diagnostics-only` | --- /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.yauQENC9g3	2026-02-19 01:12:57 +++ /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.KTeL8WlJWU	2026-02-19 01:12:57 @@ -1 +1 @@ -/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/index_get_string_oob_combining.pts:5:15 R1002 RUNTIME_INDEX_OOB: index out of bounds. got 2; expected index within bounds +/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/index_get_string_oob_combining.pts:5:15 R1002 RUNTIME_INDEX_OOB: index out of bounds. got 2; expected 0..1 |
| `invalid/runtime/int_to_byte_range` | 1 | 1 | `diagnostics-only` | --- /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.JeW96wnk0t	2026-02-19 01:12:58 +++ /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.nWlFVhFxUp	2026-02-19 01:12:58 @@ -1 +1 @@ -/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/int_to_byte_range.pts:3:15 R1008 RUNTIME_BYTE_RANGE: byte out of range. got value; expected 0..255 +/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/int_to_byte_range.pts:3:15 R1008 RUNTIME_BYTE_RANGE: byte out of range. got 300; expected 0..255 |
| `invalid/runtime/string_to_int_invalid` | 1 | 1 | `diagnostics-only` | --- /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.mgeRIt8RlO	2026-02-19 01:12:58 +++ /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.nLhh16C87C	2026-02-19 01:12:58 @@ -1 +1 @@ -/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/string_to_int_invalid.pts:3:14 R1010 RUNTIME_TYPE_ERROR: invalid int format. got string; expected int literal +/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/string_to_int_invalid.pts:3:14 R1010 RUNTIME_TYPE_ERROR: invalid int format. got abc; expected int literal |
| `invalid/runtime/string_to_float_invalid` | 1 | 1 | `diagnostics-only` | --- /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.s1ePIOM06o	2026-02-19 01:12:58 +++ /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.l1VOi7JoOX	2026-02-19 01:12:58 @@ -1 +1 @@ -/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/string_to_float_invalid.pts:3:16 R1010 RUNTIME_TYPE_ERROR: invalid float format. got string; expected float literal +/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/string_to_float_invalid.pts:3:16 R1010 RUNTIME_TYPE_ERROR: invalid float format. got x1; expected float literal |
| `invalid/runtime/string_substring_oob` | 1 | 1 | `diagnostics-only` | --- /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.50U7dNElx0	2026-02-19 01:12:58 +++ /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.xlZYHReDVt	2026-02-19 01:12:58 @@ -1 +1 @@ -/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/string_substring_oob.pts:5:17 R1002 RUNTIME_INDEX_OOB: index out of bounds. got start=2, length=2; expected range within string +/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/string_substring_oob.pts:5:17 R1002 RUNTIME_INDEX_OOB: index out of bounds. got start/length; expected range within string |
| `invalid/runtime/view_oob` | 1 | 1 | `diagnostics-only` | --- /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.PSbR5w2BaY	2026-02-19 01:12:58 +++ /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.ULzZPe2vRQ	2026-02-19 01:12:58 @@ -1 +1 @@ -/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/view_oob.pts:3:21 R1002 RUNTIME_INDEX_OOB: index out of bounds. got offset/length; expected within source +/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/view_oob.pts:3:21 R1002 RUNTIME_INDEX_OOB: index out of bounds. got offset=2 len=2; expected offset+len <= 3 |
| `invalid/runtime/module_noinit` | 1 | 1 | `diagnostics-only` | --- /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.gbZUzy0QSQ	2026-02-19 01:13:01 +++ /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.KPo0vwa3Yh	2026-02-19 01:13:01 @@ -1 +1 @@ -/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/module_noinit.pts:1:21 R1010 RUNTIME_MODULE_ERROR: module not found. got module or symbol; expected available module/symbol +/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/module_noinit.pts:4:9 R1010 RUNTIME_MODULE_ERROR: module not found. got test.noinit; expected available module |
| `invalid/runtime/module_badver` | 1 | 1 | `diagnostics-only` | --- /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.g7QQEy3ELy	2026-02-19 01:13:01 +++ /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.OEgIpflxJ9	2026-02-19 01:13:01 @@ -1 +1 @@ -/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/module_badver.pts:1:21 R1010 RUNTIME_MODULE_ERROR: module not found. got module or symbol; expected available module/symbol +/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/module_badver.pts:4:9 R1010 RUNTIME_MODULE_ERROR: module not found. got test.badver; expected available module |
| `invalid/runtime/module_nosym` | 1 | 1 | `diagnostics-only` | --- /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.faOXr37z3v	2026-02-19 01:13:02 +++ /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.cGD4lQ9Xfd	2026-02-19 01:13:02 @@ -1 +1 @@ -/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/module_nosym.pts:1:20 R1010 RUNTIME_MODULE_ERROR: symbol not found. got module or symbol; expected available module/symbol +/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/module_nosym.pts:4:12 R1010 RUNTIME_MODULE_ERROR: symbol not found. got missing; expected exported symbol |
| `invalid/runtime/module_not_found` | 1 | 1 | `diagnostics-only` | --- /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.QRU4gDYqwr	2026-02-19 01:13:02 +++ /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.6KEAE0jsnj	2026-02-19 01:13:02 @@ -1 +1 @@ -/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/module_not_found.pts:1:22 R1010 RUNTIME_MODULE_ERROR: module not found. got module or symbol; expected available module/symbol +/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/module_not_found.pts:4:9 R1010 RUNTIME_MODULE_ERROR: module not found. got test.missing; expected available module |
| `invalid/runtime/math_type_error` | 1 | 1 | `diagnostics-only` | --- /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.SsP1ZRkLB5	2026-02-19 01:13:02 +++ /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.OJNJ3zU3DR	2026-02-19 01:13:02 @@ -1 +1 @@ -/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/math_type_error.pts:4:9 R1010 RUNTIME_TYPE_ERROR: invalid argument. got string; expected float +/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/math_type_error.pts:4:9 R1010 RUNTIME_TYPE_ERROR: expected float |
| `invalid/runtime/io_utf8_invalid` | 1 | 1 | `diagnostics-only` | --- /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.AX3RMAJ2t7	2026-02-19 01:13:04 +++ /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.NzBDLiC7Xt	2026-02-19 01:13:04 @@ -1 +1 @@ -ps_tmp:13:10 R1011 UNHANDLED_EXCEPTION: unhandled exception. got Utf8DecodeException("invalid UTF-8 sequence"); expected matching catch +/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/io_utf8_invalid.pts:13:10 R1011 UNHANDLED_EXCEPTION: unhandled exception. got Utf8DecodeException("invalid UTF-8"); expected matching catch |
| `invalid/runtime/json_decode_invalid` | 1 | 1 | `diagnostics-only` | --- /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.Fkgxv15qKa	2026-02-19 01:13:05 +++ /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.DIxMs4jR49	2026-02-19 01:13:05 @@ -1 +1 @@ -/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/json_decode_invalid.pts:4:9 R1010 RUNTIME_JSON_ERROR: invalid JSON. got JSON value; expected expected JSON type +/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/json_decode_invalid.pts:4:9 R1010 RUNTIME_JSON_ERROR: invalid JSON object |
| `invalid/runtime/json_encode_nan` | 1 | 1 | `diagnostics-only` | --- /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.RHJjJGE3fx	2026-02-19 01:13:05 +++ /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.3VsysGTnib	2026-02-19 01:13:05 @@ -1 +1 @@ -/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/json_encode_nan.pts:5:9 R1010 RUNTIME_JSON_ERROR: invalid JSON number. got JSON value; expected expected JSON type +/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/json_encode_nan.pts:5:9 R1010 RUNTIME_JSON_ERROR: invalid JSON number |
| `invalid/runtime/manual_ex060` | 1 | 1 | `diagnostics-only` | --- /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.VB6XbtznAf	2026-02-19 01:13:05 +++ /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.PWJm4mp2MN	2026-02-19 01:13:05 @@ -1 +1 @@ -/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/manual_ex060.pts:4:5 R1002 RUNTIME_INDEX_OOB: index out of bounds. got 3; expected index within bounds +EX-060.pts:4:5 R1002 RUNTIME_INDEX_OOB: index out of bounds. got 3; expected 0..0 |
| `invalid/runtime/manual_ex063` | 1 | 1 | `diagnostics-only` | --- /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.oPzAO4WMSK	2026-02-19 01:13:06 +++ /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.zUWFiBnclR	2026-02-19 01:13:06 @@ -1 +1 @@ -/Users/avialle/dev/ProtoScript2/tests/invalid/runtime/manual_ex063.pts:5:13 R1003 RUNTIME_MISSING_KEY: missing key. got "absent"; expected present key +EX-063.pts:5:13 R1003 RUNTIME_MISSING_KEY: missing key. got "absent"; expected present key |
| `regexp/compile_forbidden_lookahead` | 1 | 1 | `diagnostics-only` | --- /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.oh7u6w7mhP	2026-02-19 01:13:07 +++ /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.n4BnmGaKxr	2026-02-19 01:13:07 @@ -1 +1 @@ -/Users/avialle/dev/ProtoScript2/tests/regexp/compile_forbidden_lookahead.pts:4:11 R1010 RUNTIME_MODULE_ERROR: RegExpSyntax: forbidden metasyntax (backreference/lookaround). got module or symbol; expected available module/symbol +/Users/avialle/dev/ProtoScript2/tests/regexp/compile_forbidden_lookahead.pts:4:11 R1011 UNHANDLED_EXCEPTION: unhandled exception. got RuntimeException("RegExpSyntax: forbidden metasyntax (backreference/lookaround)"); expected matching catch |
| `regexp/compile_unclosed_paren` | 1 | 1 | `diagnostics-only` | --- /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.Uj2hKrsIZ4	2026-02-19 01:13:07 +++ /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.5tBHB91cvm	2026-02-19 01:13:07 @@ -1 +1 @@ -/Users/avialle/dev/ProtoScript2/tests/regexp/compile_unclosed_paren.pts:4:11 R1010 RUNTIME_MODULE_ERROR: RegExpSyntax: Invalid regular expression: /(abc/: Unterminated group. got module or symbol; expected available module/symbol +/Users/avialle/dev/ProtoScript2/tests/regexp/compile_unclosed_paren.pts:4:11 R1011 UNHANDLED_EXCEPTION: unhandled exception. got RuntimeException("RegExpSyntax: unclosed parenthesis"); expected matching catch |
| `regexp/range_start_invalid` | 1 | 1 | `diagnostics-only` | --- /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.cnxCB55mvE	2026-02-19 01:13:08 +++ /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.diO8qgDztq	2026-02-19 01:13:08 @@ -1 +1 @@ -/Users/avialle/dev/ProtoScript2/tests/regexp/range_start_invalid.pts:5:6 R1010 RUNTIME_MODULE_ERROR: RegExpRange: start out of range. got module or symbol; expected available module/symbol +/Users/avialle/dev/ProtoScript2/tests/regexp/range_start_invalid.pts:5:6 R1011 UNHANDLED_EXCEPTION: unhandled exception. got RuntimeException("RegExpRange: start out of range"); expected matching catch |
| `regexp/range_max_invalid` | 1 | 1 | `diagnostics-only` | --- /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.2r1pQlFLGL	2026-02-19 01:13:08 +++ /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.hNnbO4AR2h	2026-02-19 01:13:08 @@ -1 +1 @@ -/Users/avialle/dev/ProtoScript2/tests/regexp/range_max_invalid.pts:5:6 R1010 RUNTIME_MODULE_ERROR: RegExpRange: max out of range. got module or symbol; expected available module/symbol +/Users/avialle/dev/ProtoScript2/tests/regexp/range_max_invalid.pts:5:6 R1011 UNHANDLED_EXCEPTION: unhandled exception. got RuntimeException("RegExpRange: max out of range"); expected matching catch |
| `regexp/range_replace_max_invalid` | 1 | 1 | `diagnostics-only` | --- /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.8XfsnV7pPc	2026-02-19 01:13:08 +++ /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.m3N2aPADaR	2026-02-19 01:13:08 @@ -1 +1 @@ -/Users/avialle/dev/ProtoScript2/tests/regexp/range_replace_max_invalid.pts:5:6 R1010 RUNTIME_MODULE_ERROR: RegExpRange: max out of range. got module or symbol; expected available module/symbol +/Users/avialle/dev/ProtoScript2/tests/regexp/range_replace_max_invalid.pts:5:6 R1011 UNHANDLED_EXCEPTION: unhandled exception. got RuntimeException("RegExpRange: max out of range"); expected matching catch |
| `regexp/range_maxparts_invalid` | 1 | 1 | `diagnostics-only` | --- /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.OZSVJkdgVe	2026-02-19 01:13:08 +++ /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.ufwlPyW23Q	2026-02-19 01:13:08 @@ -1 +1 @@ -/Users/avialle/dev/ProtoScript2/tests/regexp/range_maxparts_invalid.pts:5:6 R1010 RUNTIME_MODULE_ERROR: RegExpRange: maxParts out of range. got module or symbol; expected available module/symbol +/Users/avialle/dev/ProtoScript2/tests/regexp/range_maxparts_invalid.pts:5:6 R1011 UNHANDLED_EXCEPTION: unhandled exception. got RuntimeException("RegExpRange: maxParts out of range"); expected matching catch |
| `edge/overflow_int_add` | 1 | 1 | `diagnostics-only` | --- /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.HIz2eHzaaV	2026-02-19 01:13:12 +++ /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.waSKFhtpRO	2026-02-19 01:13:12 @@ -1 +1 @@ -/Users/avialle/dev/ProtoScript2/tests/edge/overflow_int_add.pts:5:13 R1001 RUNTIME_INT_OVERFLOW: int overflow. got value; expected value within int range +/Users/avialle/dev/ProtoScript2/tests/edge/overflow_int_add.pts:5:13 R1001 RUNTIME_INT_OVERFLOW: int overflow. got 9223372036854775807 + 1; expected value within int range |
| `edge/oob_list_index` | 1 | 1 | `diagnostics-only` | --- /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.uOQouuxwz2	2026-02-19 01:13:12 +++ /var/folders/88/nw43wg1s4xb4dr4z8k09jw100000gn/T/tmp.3FCPtKt591	2026-02-19 01:13:12 @@ -1 +1 @@ -/Users/avialle/dev/ProtoScript2/tests/edge/oob_list_index.pts:5:13 R1002 RUNTIME_INDEX_OOB: index out of bounds. got 5; expected index within bounds +/Users/avialle/dev/ProtoScript2/tests/edge/oob_list_index.pts:5:13 R1002 RUNTIME_INDEX_OOB: index out of bounds. got 5; expected 0..2 |
| `edge/clone_super_initial_divergent` | 0 | 2 | `static-C-only(E3001)` | /Users/avialle/dev/ProtoScript2/tests/edge/clone_super_initial_divergent.pts:30:1 E3001 TYPE_MISMATCH_ASSIGNMENT: override signature mismatch |
| `edge/super_clone_init` | 0 | 1 | `semantic-runtime` | /Users/avialle/dev/ProtoScript2/tests/edge/super_clone_init.pts:11:20 R1011 UNHANDLED_EXCEPTION: unhandled exception. got RuntimeException("runtime error. got missing exception; expected exception or error"); expected matching catch |
| `edge/override_multilevel_super_nested_self_deep` | 0 | 2 | `static-C-only(E3001)` | /Users/avialle/dev/ProtoScript2/tests/edge/override_multilevel_super_nested_self_deep.pts:20:1 E3001 TYPE_MISMATCH_ASSIGNMENT: override signature mismatch |
| `edge/override_chain_order_self_specialization` | 0 | 2 | `static-C-only(E3001)` | /Users/avialle/dev/ProtoScript2/tests/edge/override_chain_order_self_specialization.pts:26:1 E3001 TYPE_MISMATCH_ASSIGNMENT: override signature mismatch |
| `fs/exceptions` | 0 | 2 | `static-C-only(E3001)` | /Users/avialle/dev/ProtoScript2/tests/fs/exceptions.pts:24:37 E3001 TYPE_MISMATCH_ASSIGNMENT: catch type must derive from Exception |
| `fs/size` | 0 | 2 | `static-C-only(E3001)` | /Users/avialle/dev/ProtoScript2/tests/fs/size.pts:18:32 E3001 TYPE_MISMATCH_ASSIGNMENT: catch type must derive from Exception |
| `sys/has_env` | 0 | 2 | `static-C-only(E3001)` | /Users/avialle/dev/ProtoScript2/tests/sys/has_env.pts:12:46 E3001 TYPE_MISMATCH_ASSIGNMENT: catch type must derive from Exception |
| `sys/env` | 0 | 2 | `static-C-only(E3001)` | /Users/avialle/dev/ProtoScript2/tests/sys/env.pts:17:41 E3001 TYPE_MISMATCH_ASSIGNMENT: catch type must derive from Exception |
| `sys_execute/exit_code` | 0 | 2 | `static-C-only(E3001)` | /Users/avialle/dev/ProtoScript2/tests/sys_execute/exit_code.pts:8:55 E3001 TYPE_MISMATCH_ASSIGNMENT: incompatible operands |
| `sys_execute/both_order` | 0 | 2 | `static-C-only(E3001)` | /Users/avialle/dev/ProtoScript2/tests/sys_execute/both_order.pts:8:34 E3001 TYPE_MISMATCH_ASSIGNMENT: incompatible operands |
| `sys_execute/input_echo` | 0 | 2 | `static-C-only(E3001)` | /Users/avialle/dev/ProtoScript2/tests/sys_execute/input_echo.pts:9:77 E3001 TYPE_MISMATCH_ASSIGNMENT: incompatible operands |
| `sys_execute/large_binary_output` | 0 | 2 | `static-C-only(E3001)` | /Users/avialle/dev/ProtoScript2/tests/sys_execute/large_binary_output.pts:14:31 E3001 TYPE_MISMATCH_ASSIGNMENT: incompatible operands |
| `sys_execute/invalid_program` | 0 | 2 | `static-C-only(E3001)` | /Users/avialle/dev/ProtoScript2/tests/sys_execute/invalid_program.pts:9:41 E3001 TYPE_MISMATCH_ASSIGNMENT: catch type must derive from Exception |
| `sys_execute/permission_denied` | 0 | 2 | `static-C-only(E3001)` | /Users/avialle/dev/ProtoScript2/tests/sys_execute/permission_denied.pts:16:41 E3001 TYPE_MISMATCH_ASSIGNMENT: catch type must derive from Exception |
| `sys_execute/exec_failure` | 0 | 2 | `static-C-only(E3001)` | /Users/avialle/dev/ProtoScript2/tests/sys_execute/exec_failure.pts:15:40 E3001 TYPE_MISMATCH_ASSIGNMENT: catch type must derive from Exception |

## Classification Summary

- total fails: **46**
- diagnostics-only: **31**
- static-C-only(E3001): **14**
- semantic-runtime: **1**
- exit-code-only: **0**
- path/filename-only: **0**

## diagnostics-only

- invalid/runtime/int_overflow_add
- invalid/runtime/glyph_add
- invalid/runtime/glyph_unary
- invalid/runtime/list_index_oob
- invalid/runtime/index_get_string_oob
- invalid/runtime/index_set_list_oob
- invalid/runtime/index_get_string_oob_emoji
- invalid/runtime/index_get_string_oob_combining
- invalid/runtime/int_to_byte_range
- invalid/runtime/string_to_int_invalid
- invalid/runtime/string_to_float_invalid
- invalid/runtime/string_substring_oob
- invalid/runtime/view_oob
- invalid/runtime/module_noinit
- invalid/runtime/module_badver
- invalid/runtime/module_nosym
- invalid/runtime/module_not_found
- invalid/runtime/math_type_error
- invalid/runtime/io_utf8_invalid
- invalid/runtime/json_decode_invalid
- invalid/runtime/json_encode_nan
- invalid/runtime/manual_ex060
- invalid/runtime/manual_ex063
- regexp/compile_forbidden_lookahead
- regexp/compile_unclosed_paren
- regexp/range_start_invalid
- regexp/range_max_invalid
- regexp/range_replace_max_invalid
- regexp/range_maxparts_invalid
- edge/overflow_int_add
- edge/oob_list_index

## static-C-only(E3001)

- edge/clone_super_initial_divergent
- edge/override_multilevel_super_nested_self_deep
- edge/override_chain_order_self_specialization
- fs/exceptions
- fs/size
- sys/has_env
- sys/env
- sys_execute/exit_code
- sys_execute/both_order
- sys_execute/input_echo
- sys_execute/large_binary_output
- sys_execute/invalid_program
- sys_execute/permission_denied
- sys_execute/exec_failure

## semantic-runtime

- edge/super_clone_init

## exit-code-only
(none)

## path/filename-only
(none)
