if exists("b:current_syntax")
  finish
endif

syn keyword protoscript2Keyword prototype function return if else while import
syn keyword protoscript2Type int float bool byte string glyph list map
syn match protoscript2PreProc "^\s*#\s*\w\+.*$"
syn match protoscript2PreProcKeyword "^\s*#\s*\zs\w\+"
syn match protoscript2PreProcInclude "^\s*#\s*include\>\s*\"[^\"]*\""
syn match protoscript2Number "\v(0x[0-9A-Fa-f]+|0b[01]+|0o[0-7]+|\d+(\.\d*)?|\.\d+)([eE][+-]?\d+)?"
syn keyword protoscript2Boolean true false
syn match protoscript2Operator "\\(+\\|-\\|\\*\\|/\\|%\\|==\\|!=\\|<=\\|>=\\|<\\|>\\|&&\\|||\\|!\\|=\\|+=\\|-=\\|*=\\|/=\\|%=\\|\\+\\+|--\\)"

" Function/prototype names
syn match protoscript2Function "\v\c<(function|prototype)\s+\zs[A-Za-z_][A-Za-z0-9_]*"
syn match protoscript2Call "\v<[A-Za-z_][A-Za-z0-9_]*\ze\s*\("
syn match protoscript2TypeDecl "\v<\zs[A-Za-z_][A-Za-z0-9_]*\ze\s*:\s*%(int|float|bool|byte|string|glyph|list|map)\>"

syn match protoscript2Comment "//.*$"
syn region protoscript2Comment start="/\*" end="\*/" contains=protoscript2Comment

syn region protoscript2String start="\"" end="\"" contains=protoscript2Escape
syn region protoscript2String start="'" end="'" contains=protoscript2Escape
syn match protoscript2Escape "\\\\." contained

hi def link protoscript2Keyword Keyword
hi def link protoscript2Type Type
hi def link protoscript2PreProc PreProc
hi def link protoscript2PreProcKeyword PreProc
hi def link protoscript2PreProcInclude String
hi def link protoscript2Comment Comment
hi def link protoscript2String String
hi def link protoscript2Number Number
hi def link protoscript2Operator Operator
hi def link protoscript2Function Function
hi def link protoscript2Call Identifier
hi def link protoscript2Boolean Constant
hi def link protoscript2TypeDecl Type

let b:current_syntax = "protoscript2"
