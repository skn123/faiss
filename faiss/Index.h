/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// -*- c++ -*-

#ifndef FAISS_INDEX_H
#define FAISS_INDEX_H

#include <faiss/MetricType.h>
#include <faiss/impl/FaissAssert.h>

#include <cstdio>
#include <sstream>

#define FAISS_VERSION_MAJOR 1
#define FAISS_VERSION_MINOR 11
#define FAISS_VERSION_PATCH 0

// Macro to combine the version components into a single string
#ifndef FAISS_STRINGIFY
#define FAISS_STRINGIFY(ARG) #ARG
#endif
#ifndef FAISS_TOSTRING
#define FAISS_TOSTRING(ARG) FAISS_STRINGIFY(ARG)
#endif
#define VERSION_STRING                                          \
    FAISS_TOSTRING(FAISS_VERSION_MAJOR)                         \
    "." FAISS_TOSTRING(FAISS_VERSION_MINOR) "." FAISS_TOSTRING( \
            FAISS_VERSION_PATCH)

/**
 * @namespace faiss
 *
 * Throughout the library, vectors are provided as float * pointers.
 * Most algorithms can be optimized when several vectors are processed
 * (added/searched) together in a batch. In this case, they are passed
 * in as a matrix. When n vectors of size d are provided as float * x,
 * component j of vector i is
 *
 *   x[ i * d + j ]
 *
 * where 0 <= i < n and 0 <= j < d. In other words, matrices are
 * always compact. When specifying the size of the matrix, we call it
 * an n*d matrix, which implies a row-major storage.
 */

namespace faiss {

/// Forward declarations see impl/AuxIndexStructures.h, impl/IDSelector.h
/// and impl/DistanceComputer.h
struct IDSelector;
struct RangeSearchResult;
struct DistanceComputer;

enum NumericType {
    Float32,
    Float16,
    UInt8,
    Int8,
};

inline size_t get_numeric_type_size(NumericType numeric_type) {
    switch (numeric_type) {
        case NumericType::Float32:
            return 4;
        case NumericType::Float16:
            return 2;
        case NumericType::UInt8:
        case NumericType::Int8:
            return 1;
        default:
            FAISS_THROW_MSG(
                    "Unknown Numeric Type. Only supports Float32, Float16");
    }
}

/** Parent class for the optional search paramenters.
 *
 * Sub-classes with additional search parameters should inherit this class.
 * Ownership of the object fields is always to the caller.
 */
struct SearchParameters {
    /// if non-null, only these IDs will be considered during search.
    IDSelector* sel = nullptr;
    /// make sure we can dynamic_cast this
    virtual ~SearchParameters() {}
};

/** Abstract structure for an index, supports adding vectors and searching
 * them.
 *
 * All vectors provided at add or search time are 32-bit float arrays,
 * although the internal representation may vary.
 */
struct Index {
    using component_t = float;
    using distance_t = float;

    int d;        ///< vector dimension
    idx_t ntotal; ///< total nb of indexed vectors
    bool verbose; ///< verbosity level

    /// set if the Index does not require training, or if training is
    /// done already
    bool is_trained;

    /// type of metric this index uses for search
    MetricType metric_type;
    float metric_arg; ///< argument of the metric type

    explicit Index(idx_t d = 0, MetricType metric = METRIC_L2)
            : d(d),
              ntotal(0),
              verbose(false),
              is_trained(true),
              metric_type(metric),
              metric_arg(0) {}

    virtual ~Index();

    /** Perform training on a representative set of vectors
     *
     * @param n      nb of training vectors
     * @param x      training vecors, size n * d
     */
    virtual void train(idx_t n, const float* x);

    virtual void train(idx_t n, const void* x, NumericType numeric_type) {
        if (numeric_type == NumericType::Float32) {
            train(n, static_cast<const float*>(x));
        } else {
            FAISS_THROW_MSG("Index::train: unsupported numeric type");
        }
    }

    /** Add n vectors of dimension d to the index.
     *
     * Vectors are implicitly assigned labels ntotal .. ntotal + n - 1
     * This function slices the input vectors in chunks smaller than
     * blocksize_add and calls add_core.
     * @param n      number of vectors
     * @param x      input matrix, size n * d
     */
    virtual void add(idx_t n, const float* x) = 0;

    virtual void add(idx_t n, const void* x, NumericType numeric_type) {
        if (numeric_type == NumericType::Float32) {
            add(n, static_cast<const float*>(x));
        } else {
            FAISS_THROW_MSG("Index::add: unsupported numeric type");
        }
    }

    /** Same as add, but stores xids instead of sequential ids.
     *
     * The default implementation fails with an assertion, as it is
     * not supported by all indexes.
     *
     * @param n         number of vectors
     * @param x         input vectors, size n * d
     * @param xids      if non-null, ids to store for the vectors (size n)
     */
    virtual void add_with_ids(idx_t n, const float* x, const idx_t* xids);
    virtual void add_with_ids(
            idx_t n,
            const void* x,
            NumericType numeric_type,
            const idx_t* xids) {
        if (numeric_type == NumericType::Float32) {
            add_with_ids(n, static_cast<const float*>(x), xids);
        } else {
            FAISS_THROW_MSG("Index::add_with_ids: unsupported numeric type");
        }
    }

    /** query n vectors of dimension d to the index.
     *
     * return at most k vectors. If there are not enough results for a
     * query, the result array is padded with -1s.
     *
     * @param n           number of vectors
     * @param x           input vectors to search, size n * d
     * @param k           number of extracted vectors
     * @param distances   output pairwise distances, size n*k
     * @param labels      output labels of the NNs, size n*k
     */
    virtual void search(
            idx_t n,
            const float* x,
            idx_t k,
            float* distances,
            idx_t* labels,
            const SearchParameters* params = nullptr) const = 0;

    virtual void search(
            idx_t n,
            const void* x,
            NumericType numeric_type,
            idx_t k,
            float* distances,
            idx_t* labels,
            const SearchParameters* params = nullptr) const {
        if (numeric_type == NumericType::Float32) {
            search(n,
                   static_cast<const float*>(x),
                   k,
                   distances,
                   labels,
                   params);
        } else {
            FAISS_THROW_MSG("Index::search: unsupported numeric type");
        }
    }

    /** query n vectors of dimension d to the index.
     *
     * return all vectors with distance < radius. Note that many
     * indexes do not implement the range_search (only the k-NN search
     * is mandatory).
     *
     * @param n           number of vectors
     * @param x           input vectors to search, size n * d
     * @param radius      search radius
     * @param result      result table
     */
    virtual void range_search(
            idx_t n,
            const float* x,
            float radius,
            RangeSearchResult* result,
            const SearchParameters* params = nullptr) const;

    /** return the indexes of the k vectors closest to the query x.
     *
     * This function is identical as search but only return labels of
     * neighbors.
     * @param n           number of vectors
     * @param x           input vectors to search, size n * d
     * @param labels      output labels of the NNs, size n*k
     * @param k           number of nearest neighbours
     */
    virtual void assign(idx_t n, const float* x, idx_t* labels, idx_t k = 1)
            const;

    /// removes all elements from the database.
    virtual void reset() = 0;

    /** removes IDs from the index. Not supported by all
     * indexes. Returns the number of elements removed.
     */
    virtual size_t remove_ids(const IDSelector& sel);

    /** Reconstruct a stored vector (or an approximation if lossy coding)
     *
     * this function may not be defined for some indexes
     * @param key         id of the vector to reconstruct
     * @param recons      reconstucted vector (size d)
     */
    virtual void reconstruct(idx_t key, float* recons) const;

    /** Reconstruct several stored vectors (or an approximation if lossy
     * coding)
     *
     * this function may not be defined for some indexes
     * @param n           number of vectors to reconstruct
     * @param keys        ids of the vectors to reconstruct (size n)
     * @param recons      reconstucted vector (size n * d)
     */
    virtual void reconstruct_batch(idx_t n, const idx_t* keys, float* recons)
            const;

    /** Reconstruct vectors i0 to i0 + ni - 1
     *
     * this function may not be defined for some indexes
     * @param i0          index of the first vector in the sequence
     * @param ni          number of vectors in the sequence
     * @param recons      reconstucted vector (size ni * d)
     */
    virtual void reconstruct_n(idx_t i0, idx_t ni, float* recons) const;

    /** Similar to search, but also reconstructs the stored vectors (or an
     * approximation in the case of lossy coding) for the search results.
     *
     * If there are not enough results for a query, the resulting arrays
     * is padded with -1s.
     *
     * @param n           number of vectors
     * @param x           input vectors to search, size n * d
     * @param k           number of extracted vectors
     * @param distances   output pairwise distances, size n*k
     * @param labels      output labels of the NNs, size n*k
     * @param recons      reconstructed vectors size (n, k, d)
     **/
    virtual void search_and_reconstruct(
            idx_t n,
            const float* x,
            idx_t k,
            float* distances,
            idx_t* labels,
            float* recons,
            const SearchParameters* params = nullptr) const;

    /** Computes a residual vector after indexing encoding.
     *
     * The residual vector is the difference between a vector and the
     * reconstruction that can be decoded from its representation in
     * the index. The residual can be used for multiple-stage indexing
     * methods, like IndexIVF's methods.
     *
     * @param x           input vector, size d
     * @param residual    output residual vector, size d
     * @param key         encoded index, as returned by search and assign
     */
    virtual void compute_residual(const float* x, float* residual, idx_t key)
            const;

    /** Computes a residual vector after indexing encoding (batch form).
     * Equivalent to calling compute_residual for each vector.
     *
     * The residual vector is the difference between a vector and the
     * reconstruction that can be decoded from its representation in
     * the index. The residual can be used for multiple-stage indexing
     * methods, like IndexIVF's methods.
     *
     * @param n           number of vectors
     * @param xs          input vectors, size (n x d)
     * @param residuals   output residual vectors, size (n x d)
     * @param keys        encoded index, as returned by search and assign
     */
    virtual void compute_residual_n(
            idx_t n,
            const float* xs,
            float* residuals,
            const idx_t* keys) const;

    /** Get a DistanceComputer (defined in AuxIndexStructures) object
     * for this kind of index.
     *
     * DistanceComputer is implemented for indexes that support random
     * access of their vectors.
     */
    virtual DistanceComputer* get_distance_computer() const;

    /* The standalone codec interface */

    /** size of the produced codes in bytes */
    virtual size_t sa_code_size() const;

    /** encode a set of vectors
     *
     * @param n       number of vectors
     * @param x       input vectors, size n * d
     * @param bytes   output encoded vectors, size n * sa_code_size()
     */
    virtual void sa_encode(idx_t n, const float* x, uint8_t* bytes) const;

    /** decode a set of vectors
     *
     * @param n       number of vectors
     * @param bytes   input encoded vectors, size n * sa_code_size()
     * @param x       output vectors, size n * d
     */
    virtual void sa_decode(idx_t n, const uint8_t* bytes, float* x) const;

    /** moves the entries from another dataset to self.
     * On output, other is empty.
     * add_id is added to all moved ids
     * (for sequential ids, this would be this->ntotal) */
    virtual void merge_from(Index& otherIndex, idx_t add_id = 0);

    /** check that the two indexes are compatible (ie, they are
     * trained in the same way and have the same
     * parameters). Otherwise throw. */
    virtual void check_compatible_for_merge(const Index& otherIndex) const;

    /** Add vectors that are computed with the standalone codec
     *
     * @param codes  codes to add size n * sa_code_size()
     * @param xids   corresponding ids, size n
     */
    virtual void add_sa_codes(idx_t n, const uint8_t* codes, const idx_t* xids);
};

} // namespace faiss

#endif
