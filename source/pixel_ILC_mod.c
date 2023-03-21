#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <numpy/ndarrayobject.h>
#include <pixel_ILC.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_linalg.h>
#include <gsl/gsl_blas.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_spline.h>
#include <omp.h>

#define H_PLANCK 6.6260755e-34
#define K_BOLTZ 1.380658e-23
#define T_CMB 2.72548
#define PI 3.14159265358979323846

static PyObject *doNILC_CovarPixelSpace_SingleField(PyObject *self, PyObject *args){
	/* Getting the elements */
	// ipix_arr will be the array with all the pixel indices
	//a will be an array with shape [Nfreqs] which contains the CMB SED (in thermo units)
	PyObject *Covar_maps = NULL; // Covar_maps will be a numpy array with the shape [npix,Nfreqs2], which is empty here and will be filled
	PyObject *Field_filtered_map = NULL; // this is a numpy array with shape [Nfreqs,npix] and contains the filtered map of field F (E or B) for window w
	PyObject *Mask = NULL; // This is the mask, with size [npix]
	PyObject *nside = NULL;
	PyObject *a = NULL;
	PyObject *fwhm = NULL;
	PyObject *Nfreqs = NULL;
	PyObject *ipix_arr=NULL;
	PyObject *Npixels=NULL;
	if (!PyArg_ParseTuple(args, "OOOOOOOOO" , &Covar_maps, &Field_filtered_map, &Mask, &nside, &a, &fwhm, &Nfreqs, &ipix_arr, &Npixels)) return NULL;

	int nside_map = (int) PyLong_AsLong(nside);
	int Nfreqs_ = (int) PyLong_AsLong(Nfreqs);
	int Nfreqs2 = (int) Nfreqs_*(Nfreqs_+1)/2;
	long Npixels_ = (long) PyLong_AsLong(Npixels);
	long *ipix_ptr = PyArray_DATA(ipix_arr);
	double *Covar_maps_ = PyArray_DATA(Covar_maps);
	double *Field_filtered_map_ = PyArray_DATA(Field_filtered_map);
	double *Mask_ = PyArray_DATA(Mask);
	double *a_ = PyArray_DATA(a);
	double fwhm_ = PyFloat_AsDouble(fwhm);
	double* weights = calloc(Npixels_*Nfreqs_,sizeof(double));
	
	#pragma omp parallel
	{
	gsl_matrix *CovF = gsl_matrix_calloc(Nfreqs_, Nfreqs_);
	gsl_matrix *CovFi = gsl_matrix_calloc(Nfreqs_, Nfreqs_);
	long* pixel_buffer = calloc(60000,sizeof(long)); // this is to hold the pixels inside the disc when I call query_disc
	int p;
	//printf("I am here, after allocating the pixel buffer\n");
	#pragma omp for schedule(static)
	for(p=0;p<Npixels_;p++){
		long ipix = ipix_ptr[p];
		gsl_matrix_set_zero(CovF);
		gsl_matrix_set_zero(CovFi);
		pixelILC_DefineCovMat_NILC_CovarPixelSpace_SingleField(ipix,Nfreqs_, nside_map, Covar_maps_, Field_filtered_map_, Mask_, pixel_buffer, CovF, Nfreqs2, fwhm_);
		invert_a_matrix(CovF,CovFi,Nfreqs_);
		pixelILC_CalculateILCWeight_NILC_SingleField(a_,CovFi,weights,Nfreqs_,p);
	}
	gsl_matrix_free(CovF);
	gsl_matrix_free(CovFi);
	free(pixel_buffer);
	}
	npy_intp npy_shape[2] = {Npixels_,Nfreqs_};
	PyObject *arr 		= PyArray_SimpleNewFromData(2,npy_shape, NPY_DOUBLE, weights);
	PyArray_ENABLEFLAGS((PyArrayObject *)arr, NPY_OWNDATA);
	return(arr);
}
static PyObject *doNILC_SHTSmoothing_SingleField(PyObject *self, PyObject *args){
	/* Getting the elements */
	// TEB2maps will be a numpy array with the shape [npix,Nfreqs2]
	// ipix_arr will be the array with all the pixel indices
	//a will be an array with shape [Nfreqs] which contains the CMB SED (in RJ units)
	PyObject *TEBmaps = NULL;
	PyObject *nside = NULL;
	PyObject *a = NULL;
	PyObject *Nfreqs = NULL;
	PyObject *ipix_arr=NULL;
	PyObject *Npixels=NULL;
	PyObject *rank=NULL;
	PyObject *field=NULL;
	PyObject *Nthreads=NULL;
	
	if (!PyArg_ParseTuple(args, "OOOOOOOOO" , &TEBmaps, &nside, &a, &Nfreqs, &ipix_arr, &Npixels, &rank, &field, &Nthreads))
		return NULL;
	int nside_map = (int) PyLong_AsLong(nside);
	int Nfreqs_ = (int) PyLong_AsLong(Nfreqs);
	int Nfreqs2 = (int) Nfreqs_*(Nfreqs_+1)/2;
	long Npixels_ = (long) PyLong_AsLong(Npixels);
	int rank_ = (int) PyLong_AsLong(rank);
	int Nthreads_ = (int) PyLong_AsLong(Nthreads);
	long *ipix_ptr = PyArray_DATA(ipix_arr);
	double *TEBmaps_ = PyArray_DATA(TEBmaps);
	double *a_ = PyArray_DATA(a);
	// We dont need the fwhm anymore, because it is implicit in the TEB2maps 
	// This is for a single field
	
	double* weights = calloc(Npixels_*Nfreqs_,sizeof(double));
	
	omp_set_num_threads(Nthreads_);
	#pragma omp parallel
	{
	gsl_matrix *CovF = gsl_matrix_calloc(Nfreqs_, Nfreqs_);
	gsl_matrix *CovFi = gsl_matrix_calloc(Nfreqs_, Nfreqs_);
	int p;
	#pragma omp for schedule(static)
	for(p=0;p<Npixels_;p++){
		if(p%1000000==0){
			// print only every 1 million pixels
			//printf("Rank %i is working on pixel number %i of %i\n",rank_,p,Npixels_);
		}
		// This is the index of the pixel to process
		long ipix = ipix_ptr[p];
		//printf("Working on pixel %i\n",ipix);
		// Cov is a gsl_matrix and has shape [Nfreqs,Nfreqs] with indices n,nn
		// I need 3 of them, for T,E,B
		// I need to make sure to empty their content from the previous iteration
		gsl_matrix_set_zero(CovF);
		gsl_matrix_set_zero(CovFi);
		pixelILC_DefineCovMat_NILC_SHTSmoothing_SingleField(ipix, Nfreqs_, TEBmaps_, CovF, Nfreqs2);
		// Now we need to invert the Cov matrices
		invert_a_matrix(CovF,CovFi,Nfreqs_);
		pixelILC_CalculateILCWeight_NILC_SingleField(a_,CovFi,weights,Nfreqs_,p);
	}
	gsl_matrix_free(CovF);
	gsl_matrix_free(CovFi);
	}
	npy_intp npy_shape[2] = {Npixels_,Nfreqs_};
	PyObject *arr 		= PyArray_SimpleNewFromData(2,npy_shape, NPY_DOUBLE, weights);
	PyArray_ENABLEFLAGS((PyArrayObject *)arr, NPY_OWNDATA);
	return(arr);
}

static PyObject *doNILC_SHTSmoothing_SingleField_pixpixcorr(PyObject *self, PyObject *args){
	/* Getting the elements */
	// CovarianceMaps will be a numpy array with the shape [npix,Nfreqs2]
	// ipix_arr will be the array with all the pixel indices
	// cbeta_arr will be 1000 values between -1 and 1, representing the cos(beta) where beta is the angular distance between 2 pixels
	// Func will be a 2D array with shape [Nfreqs2,1000]
	// vec_arr will be a 2D array with shape [3,12*nside**2] and it contains the unit vector pointing to all pixels in a map. We use it for cos(beta) = n1 dot n2
	// a should be a vector with size Nfreqs*Npixels
	PyObject *CovarianceMaps = NULL;
	PyObject *nside = NULL;
	PyObject *a = NULL;
	PyObject *Nfreqs = NULL;
	PyObject *ipix_arr=NULL;
	PyObject *Npixels=NULL;
	PyObject *cbeta_arr=NULL;
	PyObject *Func=NULL;
	PyObject *vec_arr=NULL;
	PyObject *i_map=NULL;
	PyObject *j_map=NULL;
	
	if (!PyArg_ParseTuple(args, "OOOOOOOOOOO" , &CovarianceMaps, &nside, &a, &Nfreqs, &ipix_arr, &Npixels, &cbeta_arr, &Func, &vec_arr, &i_map, &j_map))
		return NULL;
	int nside_map = (int) PyLong_AsLong(nside);
	int Nfreqs_ = (int) PyLong_AsLong(Nfreqs);
	int Nfreqs2 = (int) Nfreqs_*(Nfreqs_+1)/2;
	long Npixels_ = (long) PyLong_AsLong(Npixels);
	int npix_map = 12 * nside_map * nside_map;
	long *ipix_ptr = PyArray_DATA(ipix_arr);
	double *CovarianceMaps_ = PyArray_DATA(CovarianceMaps);
	double *a_ = PyArray_DATA(a);
	double *cbeta_arr_ = PyArray_DATA(cbeta_arr);
	double *Func_ = PyArray_DATA(Func);
	double *vec_arr_ = PyArray_DATA(vec_arr);
	int *i_map_ = PyArray_DATA(i_map);
	int *j_map_ = PyArray_DATA(j_map);
	// This is for a single field
	
	double* weights = calloc(Npixels_*Nfreqs_,sizeof(double));
	
	// the covariance matrix for the full map
	gsl_matrix *Cov_matrix = gsl_matrix_calloc(Nfreqs_*Npixels_, Nfreqs_*Npixels_), *iCov_matrix = gsl_matrix_calloc(Nfreqs_*Npixels_, Nfreqs_*Npixels_) ;
	gsl_matrix_set_zero(Cov_matrix);
	gsl_matrix_set_zero(iCov_matrix);
	
	// the parallel block will parallelize over the combination of pixels 
	long Npixels2 = Npixels_*(Npixels_+1)/2 ;
	
	int n,nn,c;
	c = 0;
	for(n=0;n<Nfreqs_;n++){
		for(nn=n;nn<Nfreqs_;nn++){
			#pragma omp parallel
			{
			long p;
			int i_,j_ ;
			double function[1000];
			// do the interpolation for frequency pair n, nn
			gsl_interp_accel *acc    = gsl_interp_accel_alloc ();
			gsl_spline       *spline = gsl_spline_alloc(gsl_interp_cspline, 1000);
			for(int mm=0;mm<1000;mm++) function[mm] = Func_[c * 1000 + mm];
			gsl_spline_init (spline, cbeta_arr_, function, 1000);
			
			#pragma omp for schedule(static)
			for(p=0;p<Npixels2;p++){
				// p is a combination of 2 pixels being correlated p_i and p_j
				i_ = i_map_[p];
				j_ = j_map_[p];
				int ipix_int_i = (int) ipix_ptr[i_];
				// if i=j, then we fill the diagonal term first
				double vF;
				if (i_ == j_){
					// TEBmaps is a numpy array with shape npix_per_window,Nfreqs2 = Nfreqs*(Nfreqs+1)/2
					vF = CovarianceMaps_[ipix_int_i*Nfreqs2 + c] ;
					gsl_matrix_set(Cov_matrix, i_*Nfreqs_+n, j_*Nfreqs_+nn, vF );
					if (n!=nn) gsl_matrix_set(Cov_matrix, i_*Nfreqs_+nn, j_*Nfreqs_+n, vF );
				}
				else{
					// if not, then we should fill the other terms
					int ipix_int_j = (int) ipix_ptr[j_];
					double cbeta_pair = 0.0;
					for (int k=0;k<3;k++) cbeta_pair += vec_arr_[k*npix_map+ipix_int_i]*vec_arr_[k*npix_map+ipix_int_j];
					// interpolate the value
					vF = gsl_spline_eval(spline, cbeta_pair, acc);
					gsl_matrix_set(Cov_matrix, i_*Nfreqs_+n, j_*Nfreqs_+nn, vF );
					gsl_matrix_set(Cov_matrix, j_*Nfreqs_+n, i_*Nfreqs_+nn, vF );
					gsl_matrix_set(Cov_matrix, i_*Nfreqs_+nn, j_*Nfreqs_+n, vF );
					gsl_matrix_set(Cov_matrix, j_*Nfreqs_+nn, i_*Nfreqs_+n, vF );
				}
			}
			gsl_spline_free (spline);
			gsl_interp_accel_free (acc);
			}
			c += 1 ;
		}
	}
	// the covariance matrix is filled, now I invert it
	invert_a_matrix(Cov_matrix, iCov_matrix, Nfreqs_*Npixels_ );
	// now calculate the weights
	// shape of weights Npixels_*Nfreqs_
	double aCia_F=0.0;
	int i,j;
	for(i=0;i<Nfreqs_*Npixels_;i++){
		for(j=0;j<Nfreqs_*Npixels_;j++){
			aCia_F += a_[i] * gsl_matrix_get(iCov_matrix,i,j) * a_[j] ;
		}
	}
	for(i=0;i<Nfreqs_*Npixels_;i++){
		for(j=0;j<Nfreqs_*Npixels_;j++){
			// This is the F weight
			weights[i] += a_[j] * gsl_matrix_get(iCov_matrix,j,i) / aCia_F ;
		}
	}
	// after this weights will have the calculated weights.
	gsl_matrix_free(Cov_matrix);
	gsl_matrix_free(iCov_matrix);
	npy_intp npy_shape[1] = {Npixels_*Nfreqs_};
	PyObject *arr 		= PyArray_SimpleNewFromData(1,npy_shape, NPY_DOUBLE, weights);
	PyArray_ENABLEFLAGS((PyArrayObject *)arr, NPY_OWNDATA);
	return(arr);
}

static PyObject *doCNILC_SHTSmoothing_SingleField(PyObject *self, PyObject *args){
	/* Getting the elements */
	// TEB2maps will be a numpy array with the shape [Nfreqs2,npix]
	// ipix_arr will be the array with all the pixel indices
	//a will be an array with shape [Nfreqs] which contains the CMB SED (in RJ units)
	//b will be an array with shape [Nfreqs] which contains the dust SED (in RJ units)
	PyObject *TEBmaps = NULL;
	PyObject *nside = NULL;
	PyObject *a = NULL;
	PyObject *b = NULL;
	PyObject *Nfreqs = NULL;
	PyObject *ipix_arr=NULL;
	PyObject *Npixels=NULL;
	PyObject *rank=NULL;
	PyObject *field=NULL;
	PyObject *Nthreads=NULL;
	
	if (!PyArg_ParseTuple(args, "OOOOOOOOOO" , &TEBmaps, &nside, &a, &b, &Nfreqs, &ipix_arr, &Npixels, &rank, &field, &Nthreads))
		return NULL;
	int nside_map = (int) PyLong_AsLong(nside);
	int Nfreqs_ = (int) PyLong_AsLong(Nfreqs);
	int Nfreqs2 = (int) Nfreqs_*(Nfreqs_+1)/2;
	long Npixels_ = (long) PyLong_AsLong(Npixels);
	int rank_ = (int) PyLong_AsLong(rank);
	int Nthreads_ = (int) PyLong_AsLong(Nthreads);
	long *ipix_ptr = PyArray_DATA(ipix_arr);
	double *TEBmaps_ = PyArray_DATA(TEBmaps);
	double *a_ = PyArray_DATA(a);
	double *b_ = PyArray_DATA(b);
	// This is for a single field
	
	double* weights = calloc(Npixels_*Nfreqs_,sizeof(double));
	
	omp_set_num_threads(Nthreads_);
	#pragma omp parallel
	{
	gsl_matrix *CovF = gsl_matrix_calloc(Nfreqs_, Nfreqs_);
	gsl_matrix *CovFi = gsl_matrix_calloc(Nfreqs_, Nfreqs_);
	int p;
	#pragma omp for schedule(static)
	for(p=0;p<Npixels_;p++){
		if(p%1000000==0){
			// print only every 1 million pixels
			//printf("Rank %i is working on pixel number %i of %i\n",rank_,p,Npixels_);
		}
		// This is the index of the pixel to process
		long ipix = ipix_ptr[p];
		//printf("Working on pixel %i\n",ipix);
		// Cov is a gsl_matrix and has shape [Nfreqs,Nfreqs] with indices n,nn
		// I need 3 of them, for T,E,B
		// I need to make sure to empty their content from the previous iteration
		gsl_matrix_set_zero(CovF);
		gsl_matrix_set_zero(CovFi);
		pixelILC_DefineCovMat_NILC_SHTSmoothing_SingleField(ipix, Nfreqs_, TEBmaps_, CovF, Nfreqs2);
		// Now we need to invert the Cov matrices
		invert_a_matrix(CovF,CovFi,Nfreqs_);
		//pixelILC_CalculateILCWeight_NILC_SingleField(a_,CovFi,weights,Nfreqs_,p);
		pixelILC_CalculateILCWeight_CNILC_SingleField(a_,b_,CovFi,weights,Nfreqs_,p);
	}
	gsl_matrix_free(CovF);
	gsl_matrix_free(CovFi);
	}
	npy_intp npy_shape[2] = {Npixels_,Nfreqs_};
	PyObject *arr 		= PyArray_SimpleNewFromData(2,npy_shape, NPY_DOUBLE, weights);
	PyArray_ENABLEFLAGS((PyArrayObject *)arr, NPY_OWNDATA);
	return(arr);
}

static PyObject *doCNILC_ThermalDust_SHTSmoothing_SingleField(PyObject *self, PyObject *args){
	/* Getting the elements */
	// TEB2maps will be a numpy array with the shape [Nfreqs2,npix]
	// ipix_arr will be the array with all the pixel indices
	//a will be an array with shape [Nfreqs] which contains the CMB SED (in thermo units)
	//beta_dust_map is the map of beta_dust, in the same pixelization as TEB2maps
	//T_dust_map is the map of beta_dust, in the same pixelization as TEB2maps
	PyObject *TEBmaps = NULL;
	PyObject *nside = NULL;
	PyObject *a = NULL;
	PyObject *beta_dust_map = NULL;
	PyObject *T_dust_map = NULL;
	PyObject *freq_arr = NULL;
	PyObject *Nfreqs = NULL;
	PyObject *ipix_arr=NULL;
	PyObject *Npixels=NULL;
	PyObject *rank=NULL;
	PyObject *field=NULL;
	PyObject *Nthreads=NULL;
	
	if (!PyArg_ParseTuple(args, "OOOOOOOOOOOO" , &TEBmaps, &nside, &a, &beta_dust_map, &T_dust_map, &freq_arr, &Nfreqs, &ipix_arr, &Npixels, &rank, &field, &Nthreads))
		return NULL;
	int nside_map = (int) PyLong_AsLong(nside);
	double *freq_arr_ = PyArray_DATA(freq_arr);
	int Nfreqs_ = (int) PyLong_AsLong(Nfreqs);
	int Nfreqs2 = (int) Nfreqs_*(Nfreqs_+1)/2;
	long Npixels_ = (long) PyLong_AsLong(Npixels);
	int rank_ = (int) PyLong_AsLong(rank);
	int Nthreads_ = (int) PyLong_AsLong(Nthreads);
	long *ipix_ptr = PyArray_DATA(ipix_arr);
	double *TEBmaps_ = PyArray_DATA(TEBmaps);
	double *a_ = PyArray_DATA(a);
	double *beta_dust_map_ = PyArray_DATA(beta_dust_map);
	double *T_dust_map_ = PyArray_DATA(T_dust_map);
	// This is for a single field
	double* weights = calloc(Npixels_*Nfreqs_,sizeof(double));
	
	double* thermo_2_rj = calloc(Nfreqs_,sizeof(double));
	for(int n=0;n<Nfreqs_;n++){
		double x_cmb = H_PLANCK * freq_arr_[n] * 1.0e9 / (K_BOLTZ*T_CMB) ;
		thermo_2_rj[n] = pow(x_cmb,2) * exp(x_cmb) / pow(exp(x_cmb) - 1.0,2) ; // multiplying by this factor transform thermo 2 RJ units, divide for the reverse conversion
	}
	
	omp_set_num_threads(Nthreads_);
	#pragma omp parallel
	{
	gsl_matrix *CovF = gsl_matrix_calloc(Nfreqs_, Nfreqs_);
	gsl_matrix *CovFi = gsl_matrix_calloc(Nfreqs_, Nfreqs_);
	double* b_ = calloc(Nfreqs_,sizeof(double));
	int p;
	#pragma omp for schedule(static)
	for(p=0;p<Npixels_;p++){
		if(p%1000000==0){
			// print only every 1 million pixels
			//printf("Rank %i is working on pixel number %i of %i\n",rank_,p,Npixels_);
		}
		// This is the index of the pixel to process
		long ipix = ipix_ptr[p];
		// Cov is a gsl_matrix and has shape [Nfreqs,Nfreqs] with indices n,nn
		// I need 3 of them, for T,E,B
		// I need to make sure to empty their content from the previous iteration
		gsl_matrix_set_zero(CovF);
		gsl_matrix_set_zero(CovFi);
		pixelILC_DefineCovMat_NILC_SHTSmoothing_SingleField(ipix, Nfreqs_, TEBmaps_, CovF, Nfreqs2);
		// Now we need to invert the Cov matrices
		invert_a_matrix(CovF,CovFi,Nfreqs_);
		// calculate the b vector with the Thermal dust SED
		for(int nn=0;nn<Nfreqs_;nn++){
			double x_d_nu = H_PLANCK * freq_arr_[nn] * 1.e9 / ( K_BOLTZ * T_dust_map_[ipix] );
			b_[nn] = pow(freq_arr_[nn],beta_dust_map_[ipix]+1.0)/(exp(x_d_nu)-1.0) / thermo_2_rj[nn]  ;
		}
		pixelILC_CalculateILCWeight_CNILC_SingleField(a_,b_,CovFi,weights,Nfreqs_,p);
	}
	gsl_matrix_free(CovF);
	gsl_matrix_free(CovFi);
	free(b_);
	}
	free(thermo_2_rj);
	npy_intp npy_shape[2] = {Npixels_,Nfreqs_};
	PyObject *arr 		= PyArray_SimpleNewFromData(2,npy_shape, NPY_DOUBLE, weights);
	PyArray_ENABLEFLAGS((PyArrayObject *)arr, NPY_OWNDATA);
	return(arr);
}

static PyObject *doCNILC_ThermalDust_Synchrotron_SHTSmoothing_SingleField(PyObject *self, PyObject *args){
	/* Getting the elements */
	// TEB2maps will be a numpy array with the shape [Nfreqs2,npix]
	// ipix_arr will be the array with all the pixel indices
	//a will be an array with shape [Nfreqs] which contains the CMB SED (in thermo units)
	//beta_dust_map is the map of beta_dust, in the same pixelization as TEB2maps
	//T_dust_map is the map of beta_dust, in the same pixelization as TEB2maps
	PyObject *TEBmaps = NULL;
	PyObject *nside = NULL;
	PyObject *a = NULL;
	PyObject *beta_dust_map = NULL;
	PyObject *T_dust_map = NULL;
	PyObject *beta_syn_map = NULL;
	PyObject *freq_arr = NULL;
	PyObject *Nfreqs = NULL;
	PyObject *ipix_arr=NULL;
	PyObject *Npixels=NULL;
	PyObject *rank=NULL;
	PyObject *field=NULL;
	PyObject *Nthreads=NULL;
	
	if (!PyArg_ParseTuple(args, "OOOOOOOOOOOOO" , &TEBmaps, &nside, &a, &beta_dust_map, &T_dust_map, &beta_syn_map, &freq_arr, &Nfreqs, &ipix_arr, &Npixels, &rank, &field, &Nthreads))
		return NULL;
	int nside_map = (int) PyLong_AsLong(nside);
	double *freq_arr_ = PyArray_DATA(freq_arr);
	int Nfreqs_ = (int) PyLong_AsLong(Nfreqs);
	int Nfreqs2 = (int) Nfreqs_*(Nfreqs_+1)/2;
	long Npixels_ = (long) PyLong_AsLong(Npixels);
	int rank_ = (int) PyLong_AsLong(rank);
	int Nthreads_ = (int) PyLong_AsLong(Nthreads);
	long *ipix_ptr = PyArray_DATA(ipix_arr);
	double *TEBmaps_ = PyArray_DATA(TEBmaps);
	double *a_ = PyArray_DATA(a);
	double *beta_dust_map_ = PyArray_DATA(beta_dust_map);
	double *T_dust_map_ = PyArray_DATA(T_dust_map);
	double *beta_syn_map_ = PyArray_DATA(beta_syn_map);
	// This is for a single field
	double* weights = calloc(Npixels_*Nfreqs_,sizeof(double));
	
	double* thermo_2_rj = calloc(Nfreqs_,sizeof(double));
	for(int n=0;n<Nfreqs_;n++){
		double x_cmb = H_PLANCK * freq_arr_[n] * 1.0e9 / (K_BOLTZ*T_CMB) ;
		thermo_2_rj[n] = pow(x_cmb,2) * exp(x_cmb) / pow(exp(x_cmb) - 1.0,2) ; // multiplying by this factor transform thermo 2 RJ units, divide for the reverse conversion
	}
	// this follows the nomencleture of arxiv:2006.0862
	omp_set_num_threads(Nthreads_);
	#pragma omp parallel
	{
	gsl_matrix *CovF = gsl_matrix_calloc(Nfreqs_, Nfreqs_);
	gsl_matrix *CovFi = gsl_matrix_calloc(Nfreqs_, Nfreqs_);
	gsl_matrix *A = gsl_matrix_calloc(Nfreqs_, 3); // the 3 is because we do CMB, dust, syn
	gsl_matrix *first = gsl_matrix_calloc(Nfreqs_, 3), *second=gsl_matrix_calloc(3, 3), *second_i=gsl_matrix_calloc(3, 3), *third=gsl_matrix_calloc(3, Nfreqs_), *fourth=gsl_matrix_calloc(3, Nfreqs_), *fifth=gsl_matrix_calloc(1, Nfreqs_) ;
	gsl_matrix *e_t = gsl_matrix_calloc(1,3);
	gsl_matrix_set(e_t,0,0,1.0); gsl_matrix_set(e_t,0,1,0.0); gsl_matrix_set(e_t,0,2,0.0);
	//double* b_ = calloc(Nfreqs_,sizeof(double));
	int p;
	#pragma omp for schedule(static)
	for(p=0;p<Npixels_;p++){
		if(p%1000000==0){
			// print only every 1 million pixels
			//printf("Rank %i is working on pixel number %i of %i\n",rank_,p,Npixels_);
		}
		// This is the index of the pixel to process
		long ipix = ipix_ptr[p];
		// Cov is a gsl_matrix and has shape [Nfreqs,Nfreqs] with indices n,nn
		// I need 3 of them, for T,E,B
		// I need to make sure to empty their content from the previous iteration
		//gsl_matrix_set_zero(CovF); gsl_matrix_set_zero(CovFi); 
		pixelILC_DefineCovMat_NILC_SHTSmoothing_SingleField(ipix, Nfreqs_, TEBmaps_, CovF, Nfreqs2);
		// we need to fill the A matrix
		for(int nn=0;nn<Nfreqs_;nn++){
			gsl_matrix_set(A,nn,0,1.0) ;
			double x_d_nu = H_PLANCK * freq_arr_[nn] * 1.e9 / ( K_BOLTZ * T_dust_map_[ipix] );
			gsl_matrix_set(A,nn,1,pow(freq_arr_[nn],beta_dust_map_[ipix]+1.0)/(exp(x_d_nu)-1.0) / thermo_2_rj[nn]) ; // this is dust
			gsl_matrix_set(A,nn,2,pow(freq_arr_[nn],beta_syn_map_[ipix])/thermo_2_rj[nn] ) ; // this is syn
		}
		// Now we need to invert the Cov matrices
		invert_a_matrix(CovF,CovFi,Nfreqs_);
		// multiply C^-1 with A, which is a Nfreqs x 3 size matrix. this is called first
		gsl_blas_dgemm(CblasNoTrans, CblasNoTrans, 1.0, CovFi, A, 0.0, first);
		// now multiply A_t and first, which is size (3,3), this is second
		gsl_blas_dgemm(CblasTrans, CblasNoTrans, 1.0, A, first, 0.0, second);
		// now we need to invert the matrix second
		invert_a_matrix(second,second_i,3);
		// we need to multiply A_t with C^-1, which is size (3,Nfreq) and we call it third
		gsl_blas_dgemm(CblasTrans, CblasNoTrans, 1.0, A, CovFi, 0.0, third);
		//for(int nn=0;nn<Nfreqs_;nn++) printf("third %.4f %.4f %.4f \n",gsl_matrix_get(third,0,nn),gsl_matrix_get(third,1,nn),gsl_matrix_get(third,2,nn)) ;
		// we need to multiply second_i and third, which is size (3,Nfreq) and we call it fourth
		gsl_blas_dgemm(CblasNoTrans, CblasNoTrans, 1.0, second_i, third, 0.0, fourth);
		// we need to multiply e_t and fourth, which is size (1,Nfreq) and we call it fifth
		gsl_blas_dgemm(CblasNoTrans, CblasNoTrans, 1.0, e_t, fourth, 0.0, fifth);
		// fifth is a matrix that contains the weights
		for(int nn=0;nn<Nfreqs_;nn++){
			weights[p*Nfreqs_ + nn] = gsl_matrix_get(fifth,0,nn);
			//printf("weight for freq %i = %.2f \n",nn,weights[p*Nfreqs_ + nn]);
		} 
	}
	gsl_matrix_free(CovF);
	gsl_matrix_free(CovFi);
	gsl_matrix_free(first);gsl_matrix_free(second);gsl_matrix_free(second_i);gsl_matrix_free(third);gsl_matrix_free(fourth);gsl_matrix_free(fifth);
	gsl_matrix_free(A);gsl_matrix_free(e_t);
	//free(b_);
	}
	free(thermo_2_rj);
	npy_intp npy_shape[2] = {Npixels_,Nfreqs_};
	PyObject *arr 		= PyArray_SimpleNewFromData(2,npy_shape, NPY_DOUBLE, weights);
	PyArray_ENABLEFLAGS((PyArrayObject *)arr, NPY_OWNDATA);
	return(arr);
}


static PyMethodDef PixelILCMethods[] = {
	{"doNILC_CovarPixelSpace_SingleField", doNILC_CovarPixelSpace_SingleField, METH_VARARGS,NULL},
	{"doNILC_SHTSmoothing_SingleField", doNILC_SHTSmoothing_SingleField,METH_VARARGS,NULL},
	{"doCNILC_SHTSmoothing_SingleField",doCNILC_SHTSmoothing_SingleField,METH_VARARGS,NULL},
  {"doCNILC_ThermalDust_SHTSmoothing_SingleField",doCNILC_ThermalDust_SHTSmoothing_SingleField,METH_VARARGS,NULL},
  {"doCNILC_ThermalDust_Synchrotron_SHTSmoothing_SingleField",doCNILC_ThermalDust_Synchrotron_SHTSmoothing_SingleField,METH_VARARGS,NULL},
	{"doNILC_SHTSmoothing_SingleField_pixpixcorr",doNILC_SHTSmoothing_SingleField_pixpixcorr,METH_VARARGS,NULL},
 {NULL, NULL, 0, NULL}        /* Sentinel */
};

static struct PyModuleDef PixelILC_module = {
    PyModuleDef_HEAD_INIT,
    "PixelILC",   /* name of module */
    NULL, /* module documentation, may be NULL */
    -1,       /* size of per-interpreter state of the module,
                 or -1 if the module keeps state in global variables. */
    PixelILCMethods
};

PyMODINIT_FUNC PyInit_PixelILC(void){
  PyObject *m;
  m = PyModule_Create(&PixelILC_module);
  import_array();  // This is important for using the numpy_array api, otherwise segfaults!
  return(m);
}