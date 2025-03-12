from setuptools import setup, find_packages, Extension
import numpy as np

module1 =  Extension('PixelILC',
	sources = ['source/pixel_ILC.c','source/pixel_ILC_mod.c','source/query_disc_wrapper.cpp'],
	include_dirs = ['source',np.get_include()],
	libraries=['gsl','gslcblas','gomp','healpix_cxx'],
	library_dirs = ["lib"],
	extra_compile_args=['-fPIC','-Wall','-g','-fopenmp','-std=c99'],
	extra_link_args=['-fopenmp'],
)

setup (name = 'PixelILC',
	   version = '0.1',
	   url='',
	   description = '',
	   ext_modules = [module1]
	  )
