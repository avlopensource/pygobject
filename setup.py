#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
# setup.py - distutils configuration for pygobject


'''Python Bindings for GObject.

PyGObject is a set of bindings for the glib, gobject and gio libraries.
It provides an object oriented interface that is slightly higher level than
the C one. It automatically does all the type casting and reference
counting that you would have to do normally with the C API. You can
find out more on the official homepage, http://www.pygtk.org/'''


import os
import sys
import glob

from distutils.command.build import build
from distutils.command.build_clib import build_clib
from distutils.command.build_scripts import build_scripts
from distutils.sysconfig import get_python_inc
from setuptools.extension import Extension
from setuptools import setup

from dsextras import GLOBAL_MACROS, GLOBAL_INC, get_m4_define, getoutput, \
                     TemplateExtension, Template, \
                     BuildExt, InstallLib, InstallData


if sys.platform != 'win32':
    msg =  '*' * 68 + '\n'
    msg += '* Building PyGObject using distutils is only supported on windows. *\n'
    msg += '* To build PyGObject in a supported way, read the INSTALL file.    *\n'
    msg += '*' * 68
    raise SystemExit(msg)

MIN_PYTHON_VERSION = (2, 6, 0)

if sys.version_info[:3] < MIN_PYTHON_VERSION:
    raise SystemExit('ERROR: Python %s or higher is required, %s found.' % (
                         '.'.join(map(str, MIN_PYTHON_VERSION)),
                         '.'.join(map(str, sys.version_info[:3]))))

PYGTK_SUFFIX = '2.0'
PYGTK_SUFFIX_LONG = 'gtk-' + PYGTK_SUFFIX

GLIB_REQUIRED = get_m4_define('glib_required_version')

MAJOR_VERSION = int(get_m4_define('pygobject_major_version'))
MINOR_VERSION = int(get_m4_define('pygobject_minor_version'))
MICRO_VERSION = int(get_m4_define('pygobject_micro_version'))
VERSION       = '%d.%d.%d' % (MAJOR_VERSION, MINOR_VERSION, MICRO_VERSION)

GLOBAL_INC += ['gobject']
GLOBAL_MACROS += [('PYGOBJECT_MAJOR_VERSION', MAJOR_VERSION),
                  ('PYGOBJECT_MINOR_VERSION', MINOR_VERSION),
                  ('PYGOBJECT_MICRO_VERSION', MICRO_VERSION),
                  ('VERSION', '\\"%s\\"' % VERSION),
                  ('PY_SSIZE_T_CLEAN', '1')]

BIN_DIR     = os.path.join('Scripts')
INCLUDE_DIR = os.path.join('include', 'pygtk-%s' % PYGTK_SUFFIX)
DEFS_DIR    = os.path.join('share', 'pygobject', PYGTK_SUFFIX, 'defs')
XSL_DIR     = os.path.join('share', 'pygobject','xsl')
HTML_DIR    = os.path.join('share', 'gtk-doc', 'html', 'pygobject')


class PyGObjectBuild(build):
    enable_threading = True

PyGObjectBuild.user_options.append(('enable-threading', None,
                                    'enable threading support'))

# glib
glib = Extension(name='glib._glib',
                 define_macros=GLOBAL_MACROS,
                 include_dirs=['glib'],
                 libraries=['pyglib', 'glib-2.0', 'gthread-2.0'],
                 sources=['glib/glibmodule.c',
                          'glib/pygiochannel.c',
                          'glib/pygmaincontext.c',
                          'glib/pygmainloop.c',
                          'glib/pygoptioncontext.c',
                          'glib/pygoptiongroup.c',
                          'glib/pygsource.c',
                          'glib/pygspawn.c',
                          ])

# GObject
gobject = Extension(name='gobject._gobject',
                    define_macros=GLOBAL_MACROS,
                    include_dirs=['glib','gobject','gi'],
                    libraries=['pyglib', 'glib-2.0', 'gobject-2.0', 'gthread-2.0'],
                    sources=['gobject/gobjectmodule.c',
                             'gobject/pygboxed.c',
                             'gobject/pygenum.c',
                             'gobject/pygflags.c',
                             'gobject/pyginterface.c',
                             'gobject/pygobject.c',
                             'gobject/pygparamspec.c',
                             'gobject/pygpointer.c',
                             'gobject/pygtype.c',
                             ])


class GioExtension(Extension):
    def __init__(self, **kwargs):
        name = kwargs['name']
        defs = kwargs['defs']

        if isinstance(defs, tuple):
            output = defs[0][:-5] + '.c'
        else:
            output = defs[:-5] + '.c'

        override = kwargs['override']
        load_types = kwargs.get('load_types')
        py_ssize_t_clean = kwargs.pop('py_ssize_t_clean', False)
        self.templates = []
        self.templates.append(Template(override, output, defs, 'pygio',
                                       kwargs['register'], load_types,
                                       py_ssize_t_clean))

        del kwargs['register'], kwargs['override'], kwargs['defs']

        if load_types:
            del kwargs['load_types']

        if 'output' in kwargs:
            kwargs['name'] = kwargs['output']
            del kwargs['output']

        Extension.__init__(self, **kwargs)

    def generate(self):
        for t in self.templates:
            t.generate()


gio = GioExtension(name='gio._gio',
                   define_macros=GLOBAL_MACROS,
                   include_dirs=['glib', 'gobject'],
                   libraries=['pyglib', 'glib-2.0', 'gio-2.0', 'gobject-2.0'],
                   sources=['gio/giomodule.c',
                            'gio/gio.c',
                            'gio/pygio-utils.c'],
                   defs='gio/gio.defs',
                   register=['gio/gio-types.defs'],
                   override='gio/gio.override')


clibs = []
data_files = []
ext_modules = []

py_modules = ['dsextras', 'pygtk']
packages = ['codegen']


#It would have been nice to create another class, such as PkgConfigCLib to
#encapsulate this dictionary, but it is impossible. build_clib.py does
#a dumb check to see if its only arguments are a 2-tuple containing a
#string and a Dictionary type - which makes it impossible to hide behind a
#subclass
#
#So we are stuck with this ugly thing
clibs.append(('pyglib', {'sources': ['glib/pyglib.c'],
                         'macros': GLOBAL_MACROS,
                         'include_dirs': ['glib', get_python_inc()]}))
#this library is not installed, so probably should not include its header
#data_files.append((INCLUDE_DIR, ('glib/pyglib.h',)))

ext_modules.append(glib)
py_modules += ['glib.__init__', 'glib.option']

ext_modules.append(gobject)
data_files.append((INCLUDE_DIR, ('gobject/pygobject.h',)))
py_modules += ['gobject.__init__', 'gobject.propertyhelper', 'gobject.constants']

ext_modules.append(gio)
py_modules += ['gio.__init__']
data_files.append((DEFS_DIR, ('gio/gio.defs', 'gio/gio-types.defs',)))

# Threading support
if '--disable-threading' in sys.argv:
    sys.argv.remove('--disable-threading')
    enable_threading = False
else:
    if '--enable-threading' in sys.argv:
        sys.argv.remove('--enable-threading')
    try:
        import _thread
    except ImportError:
        print ('* Could not import thread module, disabling threading')
        enable_threading = False
    else:
        enable_threading = True

if not enable_threading:
    GLOBAL_MACROS.append(('DISABLE_THREADING', 1))

doclines = __doc__.split('\n')
options = {'bdist_wininst': {'user_access_control': 'auto'}}

setup(name='pygobject',
      url='http://www.pygtk.org/',
      version=VERSION,
      license='LGPL',
      platforms=['MS Windows'],
      maintainer='Johan Dahlin',
      maintainer_email='johan@gnome.org',
      description=doclines[0],
      long_description='\n'.join(doclines[2:]),
      provides=['codegen', 'dsextras', 'glib', 'gobject'],
      py_modules=py_modules,
      packages=packages,
      ext_modules=ext_modules,
      libraries=clibs,
      data_files=data_files,
      options=options,
      cmdclass={'build_clib' : build_clib,
                'build_ext': BuildExt,
                'build': PyGObjectBuild})
