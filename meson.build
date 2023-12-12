project('C/C++ Check', 'c',
  default_options: {
    'c_std': 'c99',
  },
  version: '1.0')

sources = [
  'src/main.c',
  'src/logger.c',
]
executable('ccheck', sources)

unit_tests = {
  'cstr' : ['tests/cstr.c', 'src/logger.c'],
  }
foreach name, srcs : unit_tests
  exe = executable(name, srcs, c_args: ['-DCCHECK_TEST'])
  test(name, exe)
endforeach
