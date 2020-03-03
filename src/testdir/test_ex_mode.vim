" Test editing line in Ex mode (see :help Q and :help gQ).

" Helper function to test editing line in Q Ex mode
func Ex_Q(cmd)
  " Is there a simpler way to test editing Ex line?
  call feedkeys("Q"
        \    .. "let s:test_ex =<< END\<CR>"
        \    .. a:cmd .. "\<CR>"
        \    .. "END\<CR>"
        \    .. "visual\<CR>", 'tx')
  return s:test_ex[0]
endfunc

" Helper function to test editing line in gQ Ex mode
func Ex_gQ(cmd)
  call feedkeys("gQ" .. a:cmd .. "\<C-b>\"\<CR>", 'tx')
  let ret = @:[1:] " Remove leading quote.
  call feedkeys("visual\<CR>", 'tx')
  return ret
endfunc

" Helper function to test editing line with both Q and gQ Ex mode.
func Ex(cmd)
 return [Ex_Q(a:cmd), Ex_gQ(a:cmd)]
endfunc

" Test editing line in Ex mode (both Q and gQ)
func Test_ex_mode()
  let encoding_save = &encoding
  set sw=2

  for e in ['utf8', 'latin1']
    exe 'set encoding=' . e

    call assert_equal(['bar', 'bar'],             Ex("foo bar\<C-u>bar"), e)
    call assert_equal(["1\<C-u>2", "1\<C-u>2"],   Ex("1\<C-v>\<C-u>2"), e)
    call assert_equal(["1\<C-b>2\<C-e>3", '213'], Ex("1\<C-b>2\<C-e>3"), e)
    call assert_equal(['0123', '2013'],           Ex("01\<Home>2\<End>3"), e)
    call assert_equal(['0123', '0213'],           Ex("01\<Left>2\<Right>3"), e)
    call assert_equal(['01234', '0342'],          Ex("012\<Left>\<Left>\<Insert>3\<Insert>4"), e)
    call assert_equal(["foo bar\<C-w>", 'foo '],  Ex("foo bar\<C-w>"), e)
    call assert_equal(['foo', 'foo'],             Ex("fooba\<Del>\<Del>"), e)
    call assert_equal(["foo\tbar", 'foobar'],     Ex("foo\<Tab>bar"), e)
    call assert_equal(["abbrev\t", 'abbreviate'], Ex("abbrev\<Tab>"), e)
    call assert_equal(['    1', "1\<C-t>\<C-t>"], Ex("1\<C-t>\<C-t>"), e)
    call assert_equal(['  1', "1\<C-t>\<C-t>"],   Ex("1\<C-t>\<C-t>\<C-d>"), e)
    call assert_equal(['  foo', '    foo'],       Ex("    foo\<C-d>"), e)
    call assert_equal(['foo', '    foo0'],        Ex("    foo0\<C-d>"), e)
    call assert_equal(['foo', '    foo^'],        Ex("    foo^\<C-d>"), e)
  endfor

  set sw&
  let &encoding = encoding_save
endfunc
