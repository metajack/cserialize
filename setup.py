from distutils.core import setup, Extension

mod = Extension('cserialize',
                sources=['cserialize.c'])

setup(name='cserialize',
      version='1.0',
      description='Domish XML serializer in C.',
      ext_modules=[mod])
