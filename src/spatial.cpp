
#include "Cardinal.h"

#include <cmath>

#define RADIAL		1
#define MANHATTAN 	2
#define MINKOWSKI	3
#define CHEBYSHEV	4

template<typename T>
SEXP find_neighbors(SEXP coord, SEXP r, SEXP groups, SEXP dist)
{
	int nrow = Rf_nrows(coord);
	int ncol = Rf_ncols(coord);
	T * pCoord = DataPtr<T>(coord);
	int * pGroups = INTEGER(groups);
	bool is_neighbor[nrow];
	double R = Rf_asReal(r);
	int dist_type = Rf_asInteger(dist);
	SEXP ret;
	PROTECT(ret = Rf_allocVector(VECSXP, nrow));
	for ( int i = 0; i < nrow; ++i ) {
		int num_neighbors = 0;
		for ( int ii = 0; ii < nrow; ++ii ) {
			is_neighbor[ii] = true;
			if ( pGroups[i] != pGroups[ii] ) {
				is_neighbor[ii] = false;
				continue;
			}
			double d, d2 = 0;
			for ( int j = 0; j < ncol; ++j ) {
				d = pCoord[j * nrow + i] - pCoord[j * nrow + ii];
				switch ( dist_type ) {
					case RADIAL:
						d2 += d * d;
						break;
					case MANHATTAN:
						d2 += fabs(d);
						break;
					case MINKOWSKI:
						d2 += pow(fabs(d), ncol);
					case CHEBYSHEV:
						d2 = fabs(d) > d2 ? fabs(d) : d2;
						break;
				}
			}
			switch ( dist_type ) {
				case RADIAL:
					is_neighbor[ii] = sqrt(d2) <= R ? true : false;
					break;
				case MANHATTAN:
					is_neighbor[ii] = d2 <= R ? true : false;
					break;
				case MINKOWSKI:
					is_neighbor[ii] = pow(d2, 1.0 / ncol) <= R ? true : false;
					break;
				case CHEBYSHEV:
					is_neighbor[ii] = d2 <= R ? true : false;
					break;
			}
			if ( is_neighbor[ii] )
				num_neighbors++;
		}
		SEXP neighbors;
		PROTECT(neighbors = Rf_allocVector(INTSXP, num_neighbors));
		int * pNeighbors = INTEGER(neighbors);
		int ix = 0;
		for ( int ii = 0; ii < nrow && ix < num_neighbors; ++ii ) {
			if ( is_neighbor[ii] ) {
				pNeighbors[ix] = ii + 1;
				ix++;
			}
		}
		SET_VECTOR_ELT(ret, i, neighbors);
		UNPROTECT(1);
	}
	UNPROTECT(1);
	return ret;
}

template<typename T>
SEXP get_spatial_offsets(SEXP coord, SEXP neighbors, int k)
{
	int nrow = LENGTH(neighbors);
	int ncol = Rf_ncols(coord);
	int n = Rf_nrows(coord);
	T * pCoord = DataPtr<T>(coord);
	int * ii = INTEGER(neighbors);
	SEXP offsets;
	PROTECT(offsets = Rf_allocMatrix(DataType<T>(), nrow, ncol));
	T * pOffsets = DataPtr<T>(offsets);
	for ( int i = 0; i < nrow; ++i )
		for ( int j = 0; j < ncol; ++j )
			pOffsets[j * nrow + i] = pCoord[j * n + ii[i]] - pCoord[j * n + k];
	UNPROTECT(1);
	return offsets;
}

template<typename T1, typename T2>
SEXP get_spatial_weights(SEXP x, SEXP offsets, double sigma, bool bilateral)
{
	int npixels = Rf_nrows(offsets);
	int ndims = Rf_ncols(offsets);
	SEXP w, alpha, beta;
	PROTECT(w = Rf_allocVector(VECSXP, 2));
	PROTECT(alpha = Rf_allocVector(REALSXP, npixels));
	PROTECT(beta = Rf_allocVector(REALSXP, npixels));
	double * pAlpha = REAL(alpha);
	double * pBeta = REAL(beta);
	T2 * pOffsets = DataPtr<T2>(offsets);
	int k = 0; // center pixel
	bool is_center;
	double d1, d2, sigma2 = sigma * sigma;
	for ( int i = 0; i < npixels; ++i ) {
		d2 = 0;
		is_center = true;
		for ( int j = 0; j < ndims; ++j ) {
			d1 = pOffsets[j * npixels + i];
			d2 += d1 * d1;
			if ( pOffsets[j * npixels + i] != 0 )
				is_center = false;
		}
		pAlpha[i] = exp(-d2 / (2 * sigma2));
		if ( is_center )
			k = i;
	}
	if ( bilateral )
	{
		int nfeatures = Rf_nrows(x);
		T1 * pX = DataPtr<T1>(x);
		double lambda, max_d2 = R_NegInf, min_d2 = R_PosInf;
		for ( int i = 0; i < npixels; ++i ) {
			d2 = 0;
			for ( int j = 0; j < nfeatures; ++j ) {
				d1 = pX[i * nfeatures + j] - pX[k * nfeatures + j];
				d2 += d1 * d1;
			}
			if ( d2 > max_d2 )
				max_d2 = d2;
			if ( d2 < min_d2 )
				min_d2 = d2;
			pBeta[i] = d2;
		}
		lambda = (sqrt(max_d2) - sqrt(min_d2)) / 2;
		lambda = lambda * lambda;
		for ( int i = 0; i < npixels; ++i )
			pBeta[i] = exp(-pBeta[i] / (2 * lambda));
	}
	else
	{
		for ( int i = 0; i < npixels; ++i )
			pBeta[i] = 1;
	}
	SET_VECTOR_ELT(w, 0, alpha);
	SET_VECTOR_ELT(w, 1, beta);
	UNPROTECT(3);
	return w;
}

template<typename T1, typename T2>
SEXP get_spatial_distance(SEXP x, SEXP ref, SEXP offsets, SEXP ref_offsets,
			SEXP weights, SEXP ref_weights, SEXP neighbors, double tol_dist)
{
	int ndims = Rf_ncols(ref_offsets);
	int nfeatures = Rf_nrows(x);
	int npixels = LENGTH(neighbors);
	T1 * pX = DataPtr<T1>(x);
	T1 * pRef = DataPtr<T1>(ref);
	SEXP dist;
	PROTECT(dist = Rf_allocVector(REALSXP, npixels));
	double * pDist = REAL(dist);
	for ( int i = 0; i < npixels; ++i ) {
		SEXP nb = VECTOR_ELT(neighbors, i);
		int * pNb = INTEGER(nb);
		SEXP wt = VECTOR_ELT(weights, i);
		double * alpha = REAL(VECTOR_ELT(wt, 0));
		double * beta = REAL(VECTOR_ELT(wt, 1));
		double * ref_alpha = REAL(VECTOR_ELT(ref_weights, 0));
		double * ref_beta = REAL(VECTOR_ELT(ref_weights, 1));
		T2 * pOffsets = DataPtr<T2>(VECTOR_ELT(offsets, i));
		T2 * pRef_offsets = DataPtr<T2>(ref_offsets);
		int nx = Rf_nrows(VECTOR_ELT(offsets, i));
		int ny = Rf_nrows(ref_offsets);
		double d1, d2, alpha_i, beta_i, a_i, dist1, dist2 = 0;
		for ( int ix = 0; ix < nx; ++ix ) {
			int ii = pNb[ix] - 1;
			for ( int iy = 0; iy < ny; ++iy ) {
				d2 = 0;
				for ( int k = 0; k < ndims; ++k ) {
					d1 = pOffsets[k * nx + ix] - pRef_offsets[k * ny + iy];
					d2 += d1 * d1;
				}
				if ( d2 < tol_dist ) {
					alpha_i = alpha[ix] * ref_alpha[iy];
					beta_i = beta[ix] * ref_beta[iy];
					a_i = sqrt(alpha_i * beta_i);
					for ( int j = 0; j < nfeatures; ++j ) {
						dist1 = pX[ii * nfeatures + j] - pRef[iy * nfeatures + j];
						dist2 += a_i * dist1 * dist1;
					}
				}
			}
		}
		pDist[i] = sqrt(dist2);
	}
	UNPROTECT(1);
	return dist;
}

template<typename T1, typename T2>
SEXP get_spatial_scores(SEXP x, SEXP centers, SEXP weights,
						SEXP neighbors, SEXP sd)
{
	int nfeatures = Rf_nrows(x);
	int npixels = LENGTH(neighbors);
	int ncenters = Rf_ncols(centers);
	double * sdev = REAL(sd);
	T1 * pX = DataPtr<T1>(x);
	T2 * pCenters = DataPtr<T2>(centers);
	SEXP scores;
	PROTECT(scores = Rf_allocMatrix(REALSXP, npixels, ncenters));
	double * pScores = REAL(scores);
	double a_l, dist, auc;
	for ( int i = 0; i < npixels; ++i ) {
		SEXP nb = VECTOR_ELT(neighbors, i);
		int * pNb = INTEGER(nb);
		SEXP wt = VECTOR_ELT(weights, i);
		double * alpha = REAL(VECTOR_ELT(wt, 0));
		double * beta = REAL(VECTOR_ELT(wt, 1));
		int nneighbors = LENGTH(nb);
		auc = 0;
		for ( int l = 0; l < nneighbors; ++l )
			auc += alpha[l] * beta[l];
		for ( int k = 0; k < ncenters; ++k ) {
			pScores[k * npixels + i] = 0;
			for ( int l = 0; l < nneighbors; ++l ) {
				int ii = pNb[l] - 1;
				a_l = alpha[l] * beta[l] / auc;
				double score_l = 0;
				for ( int j = 0; j < nfeatures; ++j ) {
					dist = pX[ii * nfeatures + j] - pCenters[k * nfeatures + j];
					score_l += (dist * dist) / (sdev[j] * sdev[j]);
				}
				pScores[k * npixels + i] += a_l * score_l;
			}
		}
	}
	UNPROTECT(1);
	return scores;
}

template<typename T>
SEXP get_spatial_filter(SEXP x, SEXP weights, SEXP neighbors)
{
	int nc = LENGTH(neighbors);
	int nr = Rf_nrows(x);
	T * pX = DataPtr<T>(x);
	SEXP nb, wt, y;
	PROTECT(y = Rf_allocMatrix(REALSXP, nr, nc));
	double * pY = REAL(y);
	double a_k, auc;
	for ( int i = 0; i < nc; ++i ) {
		wt = VECTOR_ELT(weights, i);
		double * alpha = REAL(VECTOR_ELT(wt, 0));
		double * beta = REAL(VECTOR_ELT(wt, 1));
		nb = VECTOR_ELT(neighbors, i);
		int K = LENGTH(nb);
		int * ii = INTEGER(nb);
		auc = 0;
		for ( int k = 0; k < K; ++k )
			auc += alpha[k] * beta[k];
		for ( int j = 0; j < nr; ++j )
			pY[i * nr + j] = 0;
		for ( int k = 0; k < K; ++k ) {
			a_k = alpha[k] * beta[k] / auc;
			for ( int j = 0; j < nr; ++j )
				pY[i * nr + j] += a_k * pX[(ii[k] - 1) * nr + j];
		}
	}
	UNPROTECT(1);
	return y;
}

extern "C" {

	SEXP findNeighbors(SEXP coord, SEXP r, SEXP group, SEXP dist) {
		if ( TYPEOF(coord) == INTSXP )
			return find_neighbors<int>(coord, r, group, dist);
		else if ( TYPEOF(coord) == REALSXP )
			return find_neighbors<double>(coord, r, group, dist);
		else
			return R_NilValue;
	}

	SEXP spatialOffsets(SEXP coord, SEXP neighbors, SEXP k) {
		if ( TYPEOF(coord) == INTSXP )
			return get_spatial_offsets<int>(coord, neighbors, Rf_asInteger(k));
		else if ( TYPEOF(coord) == REALSXP )
			return get_spatial_offsets<double>(coord, neighbors, Rf_asInteger(k));
		else
			return R_NilValue;
	}

	SEXP spatialWeights(SEXP x, SEXP offsets, SEXP sigma, SEXP bilateral) {
		if ( TYPEOF(x) == INTSXP && TYPEOF(offsets) == INTSXP )
			return get_spatial_weights<int,int>(x, offsets, Rf_asReal(sigma), Rf_asLogical(bilateral));
		else if ( TYPEOF(x) == INTSXP && TYPEOF(offsets) == REALSXP )
			return get_spatial_weights<int,double>(x, offsets, Rf_asReal(sigma), Rf_asLogical(bilateral));
		else if ( TYPEOF(x) == REALSXP && TYPEOF(offsets) == INTSXP )
			return get_spatial_weights<double,int>(x, offsets, Rf_asReal(sigma), Rf_asLogical(bilateral));
		else if ( TYPEOF(x) == REALSXP && TYPEOF(offsets) == REALSXP )
			return get_spatial_weights<double,double>(x, offsets, Rf_asReal(sigma), Rf_asLogical(bilateral));
		else
			return R_NilValue;
	}

	SEXP spatialDistance(SEXP x, SEXP ref, SEXP offsets, SEXP ref_offsets,
			SEXP weights, SEXP ref_weights, SEXP neighbors, SEXP tol_dist)
	{
		if ( TYPEOF(x) == INTSXP && TYPEOF(ref_offsets) == INTSXP )
			return get_spatial_distance<int,int>(x, ref, offsets, ref_offsets, weights, ref_weights, neighbors, Rf_asReal(tol_dist));
		else if ( TYPEOF(x) == INTSXP && TYPEOF(ref_offsets) == REALSXP )
			return get_spatial_distance<int,double>(x, ref, offsets, ref_offsets, weights, ref_weights, neighbors, Rf_asReal(tol_dist));
		else if ( TYPEOF(x) == REALSXP && TYPEOF(ref_offsets) == INTSXP )
			return get_spatial_distance<double,int>(x, ref, offsets, ref_offsets, weights, ref_weights, neighbors, Rf_asReal(tol_dist));
		else if ( TYPEOF(x) == REALSXP && TYPEOF(ref_offsets) == REALSXP )
			return get_spatial_distance<double,double>(x, ref, offsets, ref_offsets, weights, ref_weights, neighbors, Rf_asReal(tol_dist));
		else
			return R_NilValue;
	}

	SEXP spatialScores(SEXP x, SEXP centers, SEXP weights, SEXP neighbors, SEXP sd) {
		if ( TYPEOF(x) == INTSXP && TYPEOF(centers) == INTSXP )
			return get_spatial_scores<int,int>(x, centers, weights, neighbors, sd);
		else if ( TYPEOF(x) == INTSXP && TYPEOF(centers) == REALSXP )
			return get_spatial_scores<int,double>(x, centers, weights, neighbors, sd);
		else if ( TYPEOF(x) == REALSXP && TYPEOF(centers) == INTSXP )
			return get_spatial_scores<double,int>(x, centers, weights, neighbors, sd);
		else if ( TYPEOF(x) == REALSXP && TYPEOF(centers) == REALSXP )
			return get_spatial_scores<double,double>(x, centers, weights, neighbors, sd);
		else
			return R_NilValue;
	}

	SEXP spatialFilter(SEXP x, SEXP weights, SEXP neighbors) {
		if ( TYPEOF(x) == INTSXP )
			return get_spatial_filter<int>(x, weights, neighbors);
		else if ( TYPEOF(x) == REALSXP )
			return get_spatial_filter<double>(x, weights, neighbors);
		else
			return R_NilValue;
	}

}

