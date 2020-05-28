#ifndef HeterogeneousCore_CUDAUtilities_interface_HistoContainer_h
#define HeterogeneousCore_CUDAUtilities_interface_HistoContainer_h

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "KokkosCore/AtomicPairCounter.h"

using team_policy = Kokkos::TeamPolicy<KokkosExecSpace>;
using member_type = Kokkos::TeamPolicy<KokkosExecSpace>::member_type;

namespace cms {
  namespace kokkos {
    template <typename Histo, typename T>
    KOKKOS_FUNCTION void countFromVector(Kokkos::View<Histo*,KokkosExecSpace> h,
                                         const uint32_t nh,
                                         Kokkos::View<T const*,KokkosExecSpace> v,
                                         Kokkos::View<uint32_t const*,KokkosExecSpace> offsets,
                                         const member_type& teamMember) {
      uint32_t first = teamMember.league_rank() * teamMember.team_size() + teamMember.team_rank();
      uint32_t total_threads = teamMember.league_size() * teamMember.team_size();
      for (uint32_t i = first, nt = offsets(nh); i < nt; i += total_threads) {
        uint32_t index = 0;
        for(uint32_t j = 0;j<=nh;++j){
          if(offsets(j) > i){
             index = j;
             break;
          }
        }
        assert(offsets(index) > 0);
        int32_t ih = &offsets(index) - &offsets(0) - 1;
        assert(ih >= 0);
        assert(ih < int(nh));
        h(0).count(v(i), ih);
      }
    }


    template <typename Histo, typename T>
    KOKKOS_FUNCTION void fillFromVector(Kokkos::View<Histo*,KokkosExecSpace> h,
                                        uint32_t nh,
                                        Kokkos::View<T const*,KokkosExecSpace> v,
                                        Kokkos::View<uint32_t const*,KokkosExecSpace> offsets,
                                        const member_type& teamMember) {
      int first = teamMember.league_rank() * teamMember.team_size() + teamMember.team_rank();
      int total_threads = teamMember.league_size() * teamMember.team_size();

      for (uint32_t i = first, nt = offsets(nh); i < nt; i += total_threads) {
        uint32_t index = 0;
        for(uint32_t j = 0;j<=nh;++j){
          if(offsets(j) > i){
             index = j;
             break;
          }
        }
        assert(offsets(index) > 0);
        int32_t ih = &offsets(index) - &offsets(0) - 1;
        assert(ih >= 0);
        assert(ih < int(nh));
        h(0).fill(v(i), i, ih);
      }
    }

    template <typename Histo>
    inline void launchZero(Kokkos::View<Histo*,KokkosExecSpace> h) {
      Kokkos::parallel_for(Histo::totbins(),KOKKOS_LAMBDA(const size_t i) {
        h(0).off[i] = 0;
      });
    }

    template <typename Histo>
    inline void launchFinalize(Kokkos::View<Histo*,KokkosExecSpace> h) {
      Kokkos::parallel_scan(Histo::totbins(), KOKKOS_LAMBDA(const int& i, float& upd, const bool& final){
        upd += h(0).off[i];
        if(final)
          h(0).off[i] = upd;
      });
    }


    template <typename Histo, typename T>
    inline void fillManyFromVector(Kokkos::View<Histo*,KokkosExecSpace> h,
                                   const uint32_t nh,
                                   Kokkos::View<T const *,KokkosExecSpace> v,
                                   Kokkos::View<uint32_t const *,KokkosExecSpace> offsets,
                                   const uint32_t totSize,
                                   const int nthreads
    ) {
      launchZero(h);
      auto nblocks = (totSize + nthreads - 1) / nthreads;
      Kokkos::parallel_for("countFromVector",team_policy(nblocks,nthreads),
                           KOKKOS_LAMBDA(const member_type& teamMember){
        countFromVector(h,nh,v,offsets,teamMember);
      });
      launchFinalize(h);
      Kokkos::parallel_for("countFromVector",team_policy(nblocks,nthreads),
                           KOKKOS_LAMBDA(const member_type& teamMember){
        fillFromVector(h,nh,v,offsets,teamMember);
      });
    }

    template <typename Assoc>
    __global__ void finalizeBulk(AtomicPairCounter const *apc, Assoc *__restrict__ assoc) {
      assoc->bulkFinalizeFill(*apc);
    }


  }  // namespace cuda
}  // namespace cms

#ifdef TODO
// iteratate over N bins left and right of the one containing "v"
template <typename Hist, typename V, typename Func>
__host__ __device__ __forceinline__ void forEachInBins(Hist const &hist, V value, int n, Func func) {
  int bs = Hist::bin(value);
  int be = std::min(int(Hist::nbins() - 1), bs + n);
  bs = std::max(0, bs - n);
  assert(be >= bs);
  for (auto pj = hist.begin(bs); pj < hist.end(be); ++pj) {
    func(*pj);
  }
}

// iteratate over bins containing all values in window wmin, wmax
template <typename Hist, typename V, typename Func>
__host__ __device__ __forceinline__ void forEachInWindow(Hist const &hist, V wmin, V wmax, Func const &func) {
  auto bs = Hist::bin(wmin);
  auto be = Hist::bin(wmax);
  assert(be >= bs);
  for (auto pj = hist.begin(bs); pj < hist.end(be); ++pj) {
    func(*pj);
  }
}

#endif // TODO

template <typename T,                  // the type of the discretized input values
          uint32_t NBINS,              // number of bins
          uint32_t SIZE,               // max number of element
          uint32_t S = sizeof(T) * 8,  // number of significant bits in T
          typename I = uint32_t,  // type stored in the container (usually an index in a vector of the input values)
          uint32_t NHISTS = 1     // number of histos stored
          >
class HistoContainer {
public:
  using Counter = uint32_t;

  using CountersOnly = HistoContainer<T, NBINS, 0, S, I, NHISTS>;

  using index_type = I;
  using UT = typename std::make_unsigned<T>::type;

  static constexpr uint32_t ilog2(uint32_t v) {
    constexpr uint32_t b[] = {0x2, 0xC, 0xF0, 0xFF00, 0xFFFF0000};
    constexpr uint32_t s[] = {1, 2, 4, 8, 16};

    uint32_t r = 0;  // result of log2(v) will go here
    for (auto i = 4; i >= 0; i--)
      if (v & b[i]) {
        v >>= s[i];
        r |= s[i];
      }
    return r;
  }

  static constexpr uint32_t sizeT() { return S; }
  static constexpr uint32_t nbins() { return NBINS; }
  static constexpr uint32_t nhists() { return NHISTS; }
  static constexpr uint32_t totbins() { return NHISTS * NBINS + 1; }
  static constexpr uint32_t nbits() { return ilog2(NBINS - 1) + 1; }
  static constexpr uint32_t capacity() { return SIZE; }

  static constexpr auto histOff(uint32_t nh) { return NBINS * nh; }

  __host__ static size_t wsSize() {
#ifdef TODO //__CUDACC__
    uint32_t *v = nullptr;
    void *d_temp_storage = nullptr;
    size_t temp_storage_bytes = 0;
    cub::DeviceScan::InclusiveSum(d_temp_storage, temp_storage_bytes, v, v, totbins());
    return temp_storage_bytes;
#else
    return 0;
#endif
  }

  static constexpr UT bin(T t) {
    constexpr uint32_t shift = sizeT() - nbits();
    constexpr uint32_t mask = (1 << nbits()) - 1;
    return (t >> shift) & mask;
  }

  __host__ __device__ void zero() {
    for (auto &i : off)
      i = 0;
  }

  __host__ __device__ void add(CountersOnly const &co) {
    for (uint32_t i = 0; i < totbins(); ++i) {
#ifdef __CUDA_ARCH__
      atomicAdd(off + i, co.off[i]);
#else
      auto &a = (std::atomic<Counter> &)(off[i]);
      a += co.off[i];
#endif
    }
  }

  static KOKKOS_INLINE_FUNCTION uint32_t atomicIncrement(Counter &x) {
    return Kokkos::atomic_fetch_add(&x, 1);
  }

  static KOKKOS_INLINE_FUNCTION uint32_t atomicDecrement(Counter &x) {
    return Kokkos::atomic_fetch_sub(&x, 1);
  }

  __host__ __device__ __forceinline__ void countDirect(T b) {
    assert(b < nbins());
    atomicIncrement(off[b]);
  }

  __host__ __device__ __forceinline__ void fillDirect(T b, index_type j) {
    assert(b < nbins());
    auto w = atomicDecrement(off[b]);
    assert(w > 0);
    bins[w - 1] = j;
  }

  __device__ __host__ __forceinline__ int32_t bulkFill(AtomicPairCounter &apc, index_type const *v, uint32_t n) {
    auto c = apc.add(n);
    if (c.m >= nbins())
      return -int32_t(c.m);
    off[c.m] = c.n;
    for (uint32_t j = 0; j < n; ++j)
      bins[c.n + j] = v[j];
    return c.m;
  }

  __device__ __host__ __forceinline__ void bulkFinalize(AtomicPairCounter const &apc) {
    off[apc.get().m] = apc.get().n;
  }

  __device__ __host__ __forceinline__ void bulkFinalizeFill(AtomicPairCounter const &apc) {
    auto m = apc.get().m;
    auto n = apc.get().n;
    if (m >= nbins()) {  // overflow!
      off[nbins()] = uint32_t(off[nbins() - 1]);
      return;
    }
    auto first = m + blockDim.x * blockIdx.x + threadIdx.x;
    for (auto i = first; i < totbins(); i += gridDim.x * blockDim.x) {
      off[i] = n;
    }
  }

  KOKKOS_INLINE_FUNCTION void count(T t) {
    uint32_t b = bin(t);
    assert(b < nbins());
    atomicIncrement(off[b]);
  }

  KOKKOS_INLINE_FUNCTION void fill(T t, index_type j) {
    uint32_t b = bin(t);
    assert(b < nbins());
    auto w = atomicDecrement(off[b]);
    assert(w > 0);
    bins[w - 1] = j;
  }

  KOKKOS_INLINE_FUNCTION void count(T t, uint32_t nh) {
    uint32_t b = bin(t);
    assert(b < nbins());
    b += histOff(nh);
    assert(b < totbins());
    atomicIncrement(off[b]);
  }

  KOKKOS_INLINE_FUNCTION void fill(T t, index_type j, uint32_t nh) {
    uint32_t b = bin(t);
    assert(b < nbins());
    b += histOff(nh);
    assert(b < totbins());
    auto w = atomicDecrement(off[b]);
    assert(w > 0);
    bins[w - 1] = j;
  }

  __device__ __host__ __forceinline__ void finalize(Counter *ws = nullptr) {
    assert(off[totbins() - 1] == 0);
    blockPrefixScan(off, totbins(), ws);
    assert(off[totbins() - 1] == off[totbins() - 2]);
  }

  constexpr auto size() const { return uint32_t(off[totbins() - 1]); }
  constexpr auto size(uint32_t b) const { return off[b + 1] - off[b]; }

  constexpr index_type const *begin() const { return bins; }
  constexpr index_type const *end() const { return begin() + size(); }

  constexpr index_type const *begin(uint32_t b) const { return bins + off[b]; }
  constexpr index_type const *end(uint32_t b) const { return bins + off[b + 1]; }

  Counter off[totbins()];
  index_type bins[capacity()];
};

template <typename I,        // type stored in the container (usually an index in a vector of the input values)
          uint32_t MAXONES,  // max number of "ones"
          uint32_t MAXMANYS  // max number of "manys"
          >
using OneToManyAssoc = HistoContainer<uint32_t, MAXONES, MAXMANYS, sizeof(uint32_t) * 8, I, 1>;

#endif  // HeterogeneousCore_CUDAUtilities_interface_HistoContainer_h
