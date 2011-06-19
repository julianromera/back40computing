/******************************************************************************
 * 
 * Copyright 2010-2011 Duane Merrill
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. 
 * 
 * For more information, see our Google Code project site: 
 * http://code.google.com/p/back40computing/
 * 
 ******************************************************************************/


/******************************************************************************
 * Simple test utilities for segmented scan
 ******************************************************************************/

#pragma once

#include <stdio.h> 

// SegmentedScan includes
#include <b40c/segmented_scan/enactor.cuh>

// Test utils
#include "b40c_test_util.h"

/******************************************************************************
 * Test wrappers for binary, associative operations
 ******************************************************************************/

template <typename T>
struct Sum
{
	static __host__ __device__ __forceinline__ T Op(const T &a, const T &b)
	{
		return a + b;
	}

	static __host__ __device__ __forceinline__ T Identity()
	{
		return 0;
	}
};

template <typename T>
struct Max
{
	static __host__ __device__ __forceinline__ T Op(const T &a, const T &b)
	{
		return (a > b) ? a : b;
	}

	static __host__ __device__ __forceinline__ T Identity()
	{
		return 0;
	}
};


/******************************************************************************
 * Utility Routines
 ******************************************************************************/

/**
 * Timed segmented scan.  Uses the GPU to copy the specified vector of elements for the given
 * number of iterations, displaying runtime information.
 */
template <
	typename T,
	typename Flag,
	bool EXCLUSIVE,
	T BinaryOp(const T&, const T&),
	T Identity(),
	b40c::segmented_scan::ProbSizeGenre PROB_SIZE_GENRE>
double TimedSegmentedScan(
	T *h_data,
	Flag *h_flag_data,
	T *h_reference,
	size_t num_elements,
	int max_ctas,
	bool verbose,
	int iterations)
{
	using namespace b40c;

	// Allocate device storage
	T *d_src, *d_dest;
	Flag *d_flag_src;
	if (util::B40CPerror(cudaMalloc((void**) &d_src, sizeof(T) * num_elements),
		"TimedSegmentedScan cudaMalloc d_src failed: ", __FILE__, __LINE__)) exit(1);
	if (util::B40CPerror(cudaMalloc((void**) &d_dest, sizeof(T) * num_elements),
		"TimedSegmentedScan cudaMalloc d_dest failed: ", __FILE__, __LINE__)) exit(1);
	if (util::B40CPerror(cudaMalloc((void**) &d_flag_src, sizeof(Flag) * num_elements),
		"TimedSegmentedScan cudaMalloc d_flag_src failed: ", __FILE__, __LINE__)) exit(1);

	// Create enactor
	segmented_scan::Enactor enactor;

	// Move a fresh copy of the problem into device storage
	if (util::B40CPerror(cudaMemcpy(d_src, h_data, sizeof(T) * num_elements, cudaMemcpyHostToDevice),
		"TimedSegmentedScan cudaMemcpy d_src failed: ", __FILE__, __LINE__)) exit(1);
	if (util::B40CPerror(cudaMemcpy(d_flag_src, h_flag_data, sizeof(Flag) * num_elements, cudaMemcpyHostToDevice),
		"TimedSegmentedScan cudaMemcpy d_flag_src failed: ", __FILE__, __LINE__)) exit(1);

	// Perform a single iteration to allocate any memory if needed, prime code caches, etc.
	printf("\n");
	enactor.DEBUG = true;
	enactor.template Scan<T, EXCLUSIVE, BinaryOp, Identity, PROB_SIZE_GENRE>(
		d_dest, d_src, d_flag_src, num_elements, max_ctas);
	enactor.DEBUG = false;

	// Perform the timed number of iterations
	GpuTimer timer;

	double elapsed = 0;
	for (int i = 0; i < iterations; i++) {

		// Start timing record
		timer.Start();

		// Call the segmented scan API routine
		enactor.template Scan<T, EXCLUSIVE, BinaryOp, Identity, PROB_SIZE_GENRE>(
			d_dest, d_src, d_flag_src, num_elements, max_ctas);

		// End timing record
		timer.Stop();
		elapsed += (double) timer.ElapsedMillis();
	}

	// Display timing information
	double avg_runtime = elapsed / iterations;
	double throughput = ((double) num_elements) / avg_runtime / 1000.0 / 1000.0;
	printf("\nB40C %s segmented scan: %d iterations, %lu elements, ",
		EXCLUSIVE ? "exclusive" : "inclusive", iterations, (unsigned long) num_elements);
    printf("%f GPU ms, %f x10^9 elts/sec, %f x10^9 B/sec, ",
		avg_runtime, throughput, throughput * ((sizeof(T) * 3) + (sizeof(Flag) * 2)));

	// Flushes any stdio from the GPU
	cudaThreadSynchronize();

    // Verify solution
	CompareDeviceResults(h_reference, d_dest, num_elements, verbose, verbose);
	printf("\n");
	fflush(stdout);

    // Free allocated memory
    if (d_src) cudaFree(d_src);
    if (d_dest) cudaFree(d_dest);
    if (d_flag_src) cudaFree(d_flag_src);

	return throughput;
}

