/*
 Crown Copyright 2012 AWE.

 This file is part of CloverLeaf.

 CloverLeaf is free software: you can redistribute it and/or modify it under
 the terms of the GNU General Public License as published by the
 Free Software Foundation, either version 3 of the License, or (at your option)
 any later version.

 CloverLeaf is distributed in the hope that it will be useful, but
 WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 details.

 You should have received a copy of the GNU General Public License along with
 CloverLeaf. If not, see http://www.gnu.org/licenses/.
 */

#pragma once

#include <CL/sycl.hpp>
#include <iostream>
#include <utility>

#include "shared.h"

#define SYCL_DEBUG   // enable for debugging SYCL related things, also syncs kernel calls
#define SYNC_KERNELS // enable for fully synchronous (e.g queue.wait_and_throw()) kernel calls

using namespace cl;

namespace clover {

struct chunk_context {};
struct context {
  sycl::queue queue;
};

template <typename T> struct Buffer1D {
  size_t size;
  T *data;
  context *mctx;

  explicit Buffer1D(context &ctx, size_t size) : size(size), data(sycl::malloc_device<T>(size, ctx.queue)) {
    mctx = new context { ctx.queue };
  }
  T &operator[](size_t i) const { return data[i]; }

  template <size_t D> [[nodiscard]] size_t extent() const {
    static_assert(D < 1);
    return size;
  }

  std::vector<T> mirrored() const {
    std::vector<T> buffer(size);
    mctx->queue.copy(data, buffer.data(), buffer.size()).wait_and_throw();
    return buffer;
  }
};

template <typename T> struct Buffer2D {
  size_t sizeX, sizeY;
  T *data;
  context *mctx;

  Buffer2D(context &ctx, size_t sizeX, size_t sizeY) : sizeX(sizeX), sizeY(sizeY), data(sycl::malloc_device<T>(sizeX * sizeY, ctx.queue)) {
    mctx = new context { ctx.queue };
  }
  T &operator()(size_t i, size_t j) const { return data[j + i * sizeY]; }

  template <size_t D> [[nodiscard]] size_t extent() const {
    if constexpr (D == 0) {
      return sizeX;
    } else if (D == 1) {
      return sizeY;
    } else {
      static_assert(D < 2);
    }
  }

  std::vector<T> mirrored() const {
    size_t size = sizeX * sizeY;
    std::vector<T> buffer(size);
    mctx->queue.copy(data, buffer.data(), buffer.size()).wait_and_throw();
    return buffer;
  }
  clover::BufferMirror2D<T> mirrored2() { return {mirrored(), extent<0>(), extent<1>()}; }
};
template <typename T> using StagingBuffer1D = Buffer1D<T> &;

template <typename T> void free(sycl::queue &q, T &&b) {
  sycl::free(b.data, q);
}

template <typename T, typename... Ts> void free(sycl::queue &q, T &&t, Ts &&...ts) {
  free(q, t);
  free(q, std::forward<Ts>(ts)...);
}

template <class F> constexpr void par_ranged1(sycl::queue &q, const Range1d &range, F functor, bool nowait = false) {
  auto event = q.parallel_for(sycl::range<1>(range.size), [=](sycl::id<1> idx) { functor(range.from + idx[0]); });
  if(!nowait) event.wait_and_throw();
}

// delegates to parallel_for, handles flipping if enabled
template <class functorT> static inline sycl::event par_ranged2(sycl::queue &q, const Range2d &range, functorT functor, bool nowait = false) {

#define RANGE2D_NORMAL 0x01
#define RANGE2D_LINEAR 0x02
#define RANGE2D_ROUND 0x04

#ifndef RANGE2D_MODE
  #error "RANGE2D_MODE not set"
#endif

#if RANGE2D_MODE == RANGE2D_NORMAL
  auto event = q.parallel_for(sycl::range<2>(range.sizeX, range.sizeY),
                              [=](sycl::id<2> idx) { functor(idx[0] + range.fromX, idx[1] + range.fromY); });
#elif RANGE2D_MODE == RANGE2D_LINEAR
  auto event = q.parallel_for(sycl::range<1>(range.sizeX * range.sizeY), [=](sycl::id<1> id) {
    const auto x = (id[0] / range.sizeY) + range.fromX;
    const auto y = (id[0] % range.sizeY) + range.fromY;
    functor(x, y);
  });
#elif RANGE2D_MODE == RANGE2D_ROUND
  const size_t minBlockSize = 32;
  const size_t roundedX = range.sizeX % minBlockSize == 0 ? range.sizeX //
                                                          : ((range.sizeX + minBlockSize - 1) / minBlockSize) * minBlockSize;
  const size_t roundedY = range.sizeY % minBlockSize == 0 ? range.sizeY //
                                                          : ((range.sizeY + minBlockSize - 1) / minBlockSize) * minBlockSize;
  auto event = q.parallel_for(sycl::range<2>(roundedX, roundedY), [=](sycl::id<2> idx) {
    if (idx[0] >= range.sizeX) return;
    if (idx[1] >= range.sizeY) return;
    functor(idx[0] + range.fromX, idx[1] + range.fromY);
  });
#else
  #error "Unsupported RANGE2D_MODE"
#endif
  // It's an error to not sync with USM
  if(!nowait) event.wait_and_throw();

  return event;
}

} // namespace clover

using clover::Range1d;
using clover::Range2d;
