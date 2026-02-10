# ProtoScript2 Vim Support

This directory provides lightweight Vim support for the ProtoScript2 language.

## Features

- Filetype detection for `.pts`
- Syntax highlighting for keywords, types, preprocessor directives, comments, and strings

## Manual Installation

Copy the files into your Vim runtime path:

```bash
mkdir -p ~/.vim/ftdetect ~/.vim/syntax
cp tools/vim/ftdetect/protoscript2.vim ~/.vim/ftdetect/
cp tools/vim/syntax/protoscript2.vim ~/.vim/syntax/
```

Ensure syntax highlighting and filetype detection are enabled in your `~/.vimrc`:

```vim
filetype plugin on
syntax on
```

## Plugin Manager Installation

### Pathogen

```bash
mkdir -p ~/.vim/bundle
cp -R tools/vim ~/.vim/bundle/protoscript2
```

### vim-plug

Add to your `~/.vimrc`:

```vim
Plug '~/path/to/ProtoScript2/tools/vim'
```

Then run `:PlugInstall`.

## File Extensions

- `.pts`
