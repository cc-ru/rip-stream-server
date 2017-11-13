import os
import ycm_core

flags = [ '-D DEBUG', '-Wall', '-Wextra', '-pedantic', '-std=gnu99', ]

SOURCE_EXTENSIONS = [ '.c', '.h' ]

def FlagsForFile( filename, **kwargs ):
    return {
        'flags': flags,
        'do_cache': True
    }

