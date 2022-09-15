" Vim syntax file

" Language:     Terminal Calendar Files
" Maintainer:   Sam Christy <samchristywork@gmail.com>
" Last Change:  Wed, Sep 14, 2022
" Version:      0.02

if !exists("main_syntax")
  " quit when a syntax file was already loaded
  if exists("b:current_syntax")
    finish
  endif
  let main_syntax = 'termcal'
endif

" Remove the leading character designator in preparation to replace it with
" something else.
function RemoveLeader()
  :s/\!\?/
  :s/^+\?/
  :s/^-\?/
  :s/^o\?/
  :s/^x\?/
  :s/\s\?/
endfunction

" Set some sensible settings for this kind of file.
set nowrap
set nonu
set nohlsearch

" Map shortcuts to modify the character designator.
nmap 1 :call RemoveLeader()<cr>:s/^/+ /<cr>
nmap 2 :call RemoveLeader()<cr>:s/^/- /<cr>
nmap 3 :call RemoveLeader()<cr>:s/^/o /<cr>
nmap 4 :call RemoveLeader()<cr>:s/^/x /<cr>
nmap 5 :call RemoveLeader()<cr>:s/^/! /<cr>
nmap 6 :call RemoveLeader()<cr>:s/^//<cr>
nmap 9 :read !date "+\%Y/\%m/\%d (\%a)"<cr>

" Set the styling for each category of message.
hi TermCalAlternative ctermbg=blue
hi TermCalDone        ctermfg=green
hi TermCalFail        ctermfg=red
hi TermCalUnused      ctermfg=blue
hi TermCalWillDo      ctermfg=yellow
hi TermCalImportant   ctermbg=yellow ctermfg=black

" Tell Vim how to find each type of message.
syn match TermCalAlternative /.\+\$/
syn match TermCalDone        /^+.\+/
syn match TermCalFail        /^-.\+/
syn match TermCalUnused      /^x.\+/
syn match TermCalWillDo      /^o.\+/
syn match TermCalImportant   /^!.\+/

let b:current_syntax = 'termcal'
if main_syntax == 'termcal'
  unlet main_syntax
endif
