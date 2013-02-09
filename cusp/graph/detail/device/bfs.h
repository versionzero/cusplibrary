/*
 *  Copyright 2008-2009 NVIDIA Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#undef B40C_LOG_MEM_BANKS
#undef B40C_LOG_WARP_THREADS
#undef B40C_WARP_THREADS
#undef TallyWarpVote
#undef WarpVoteAll
#undef FastMul

#include <b40c/graph/bfs/csr_problem.cuh>
#include <b40c/graph/bfs/enactor_hybrid.cuh>

#undef B40C_LOG_MEM_BANKS
#undef B40C_LOG_WARP_THREADS
#undef B40C_WARP_THREADS
#undef TallyWarpVote
#undef WarpVoteAll
#undef FastMul

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <cusp/exception.h>

namespace cusp
{
namespace graph
{
namespace detail
{
namespace device
{

template<bool MARK_PREDECESSORS, typename MatrixType, typename ArrayType>
void bfs(const MatrixType& G, const typename MatrixType::index_type src,
         ArrayType& labels)
{
    typedef typename MatrixType::index_type VertexId;
    typedef typename MatrixType::index_type SizeT;

    typedef b40c::graph::bfs::CsrProblem<VertexId, SizeT, MARK_PREDECESSORS> CsrProblem;
    typedef typename CsrProblem::GraphSlice  GraphSlice;

    int max_grid_size = 0;
    double max_queue_sizing = 1.15;

    int nodes = G.num_rows;
    int edges = G.num_entries;

    CsrProblem csr_problem;
    csr_problem.nodes = nodes;
    csr_problem.edges = edges;
    csr_problem.num_gpus = 1;

    if( labels.size() != G.num_rows )
    {
        throw cusp::runtime_exception("BFS traversal labels is not large enough for result.");
    }

    // Create a single GPU slice for the currently-set gpu
    int gpu;
    if (b40c::util::B40CPerror(cudaGetDevice(&gpu), "CsrProblem cudaGetDevice failed", __FILE__, __LINE__))
    {
        throw cusp::runtime_exception("B40C cudaGetDevice failed.");
    }
    csr_problem.graph_slices.push_back(new GraphSlice(gpu, 0));
    csr_problem.graph_slices[0]->nodes = nodes;
    csr_problem.graph_slices[0]->edges = edges;

    if (b40c::util::B40CPerror(cudaMalloc(
                                   (void**) &csr_problem.graph_slices[0]->d_column_indices,
                                   csr_problem.graph_slices[0]->edges * sizeof(VertexId)),
                               "CsrProblem cudaMalloc d_column_indices failed", __FILE__, __LINE__))
    {
        throw cusp::runtime_exception("B40C cudaMalloc failed.");
    }

    if (b40c::util::B40CPerror(cudaMemcpy(
                                   csr_problem.graph_slices[0]->d_column_indices,
                                   thrust::raw_pointer_cast(&G.column_indices[0]),
                                   csr_problem.graph_slices[0]->edges * sizeof(VertexId),
                                   cudaMemcpyDeviceToDevice),
                               "CsrProblem cudaMemcpy d_column_indices failed", __FILE__, __LINE__))
    {
        throw cusp::runtime_exception("B40C cudaMemcpy failed.");
    }

    if (b40c::util::B40CPerror(cudaMalloc(
                                   (void**) &csr_problem.graph_slices[0]->d_row_offsets,
                                   (csr_problem.graph_slices[0]->nodes + 1) * sizeof(SizeT)),
                               "CsrProblem cudaMalloc d_row_offsets failed", __FILE__, __LINE__))
    {
        throw cusp::runtime_exception("B40C cudaMalloc failed.");
    }

    if (b40c::util::B40CPerror(cudaMemcpy(
                                   csr_problem.graph_slices[0]->d_row_offsets,
                                   thrust::raw_pointer_cast(&G.row_offsets[0]),
                                   (csr_problem.graph_slices[0]->nodes + 1) * sizeof(SizeT),
                                   cudaMemcpyDeviceToDevice),
                               "CsrProblem cudaMemcpy d_row_offsets failed", __FILE__, __LINE__))
    {
        throw cusp::runtime_exception("B40C cudaMemcpy failed.");
    }

    b40c::graph::bfs::EnactorHybrid<false/*INSTRUMENT*/>  hybrid(false);
    csr_problem.Reset(hybrid.GetFrontierType(), max_queue_sizing);

    hybrid.EnactSearch(csr_problem, src, max_grid_size);

    // Set device
    /*if (b40c::util::B40CPerror(cudaSetDevice(csr_problem.graph_slices[0]->gpu),
                               "CsrProblem cudaSetDevice failed", __FILE__, __LINE__))
    {
        throw cusp::runtime_exception("B40C results extraction failed.");
    }*/

    VertexId* labels_ptr = thrust::raw_pointer_cast(&labels[0]);

    // Special case for only one GPU, which may be set as with
    // an ordinal other than 0.
    if (b40c::util::B40CPerror(cudaMemcpy(
                                   labels_ptr,
                                   csr_problem.graph_slices[0]->d_labels,
                                   sizeof(VertexId) * csr_problem.graph_slices[0]->nodes,
                                   cudaMemcpyDeviceToDevice),
                               "CsrProblem cudaMemcpy d_labels failed", __FILE__, __LINE__))
    {
        throw cusp::runtime_exception("B40C results extraction failed.");
    }
}

} // end namespace device
} // end namespace detail
} // end namespace graph
} // end namespace cusp
