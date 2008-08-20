#!/usr/bin/env python

from distutils.core import setup, Extension
from askant import about

setup(name="askant",
	  version=about.version,
	  description="File system performance analysis tool",
	  author="Andrew Price",
	  author_email="andy@andrewprice.me.uk",
	  url="http://andrewprice.me.uk/projects/askant",
	  packages = ['askant','askant.fs'],
	  ext_modules = [Extension("askant.fs.gfs2",
		  sources = ["fsplugins/gfs2/gfs2module.c","fsplugins/gfs2/gfs2.c"])],
	  scripts = ['scripts/askant']) 
