#ifndef ROSSA_UTIL_H
#define ROSSA_UTIL_H

#include "third_party/combinations.h"

#include <algorithm>
#include <cassert>
#include <iterator>
#include <random>
#include <ranges>
#include <vector>

template<typename G>
void random_fill(std::vector<double>& v, const int num, G&& g) {
    v.clear();
    v.reserve(num);
    std::generate_n(std::back_inserter(v), num, [&g] { return std::uniform_real_distribution{}(g); });
}


template<class C> concept is_permutation_container = requires(C c) {
    requires std::convertible_to<decltype(c(0,0)), bool>;
    requires std::convertible_to<C, typename C::container_type>;
};
template<typename T>
class permutation_container {
public:
    permutation_container() = default;
    explicit permutation_container(std::size_t permutation_count) {
        permutations.reserve(permutation_count);
    }
    template <class It>
    bool operator()(It first, It last) {  // called for each permutation
        permutations.emplace_back(first, last);
        return false;
    }
    using container_type = std::vector<std::vector<T>>;
    operator container_type() { return permutations; }
private:
    container_type permutations;
};
template<std::size_t size, typename T>
class sized_permutation_container {
public:
    sized_permutation_container() = default;
    explicit sized_permutation_container(std::size_t permutation_count) {
        permutations.reserve(permutation_count);
    }
    template <class It>
    bool operator()(It first, It last) {  // called for each permutation
        assert(size == std::distance(first, last));
        auto& permutation = permutations.emplace_back(); //[permutation_index++];
        for (std::size_t j = 0; first != last; ++j, ++first) permutation.at(j) = *first;
        return false;
    }
    using container_type = std::vector<std::array<T, size>>;
    operator container_type() { return permutations; }
private:
    container_type permutations;
};

template<class C, typename I> requires is_permutation_container<C>
class sampled_permutations {
public:
    explicit sampled_permutations(C&& container, std::vector<I>&& sampled_ids)
    : container(std::forward<C>(container)), sampled_ids(std::move(sampled_ids)) {
        std::ranges::sort(this->sampled_ids);  // Needs to be sorted for operator() to work as intended
    }

    template <class It>
    bool operator()(It first, It last) {  // called for each permutation
        if (call_count++ == sampled_ids[sample_count]) {
            if (container(first, last)) return true;
            if (++sample_count == sampled_ids.size()) return true;
        }
        return false;
    }
    operator typename C::container_type() { return container; }
private:
    C container;
    std::vector<I> sampled_ids;
    I call_count = 0;
    std::vector<I>::size_type sample_count = 0;
};

template<typename C, std::ranges::input_range R, typename G> requires is_permutation_container<C>
C::container_type sample_partial_permutations(C&& c, R&& r, std::size_t partial_permutation_size, std::size_t sample_count, G&& g) {
    auto elems = std::ranges::to<std::vector>(std::forward<R>(r));
    auto size = count_each_permutation(elems.begin(), elems.begin() + partial_permutation_size, elems.end());
    sample_count = std::min(size, sample_count);
    std::vector<decltype(size)> sampled_ids;
    sampled_ids.resize(sample_count);
    std::ranges::sample(std::ranges::views::iota(static_cast<decltype(size)>(0), size), sampled_ids.begin(), sample_count, std::forward<G>(g));
    // It could be more efficient to jump directly to the next sample, but that would require intricate calculations in the permutation generation...
    return for_each_permutation(elems.begin(), elems.begin() + partial_permutation_size, elems.end(),
                                sampled_permutations<C, decltype(size)>(std::forward<C>(c), std::move(sampled_ids)));
}
template<std::ranges::input_range R, typename G>
auto sample_partial_permutations(R&& r, std::size_t partial_permutation_size, std::size_t sample_count, G&& g) {
    using T = std::ranges::range_value_t<R>;
    return sample_partial_permutations(permutation_container<T>(sample_count), std::forward<R>(r),
                                       partial_permutation_size, sample_count, std::forward<G>(g));
}
// Version with constexpr permutation size, to allow structured binding of each permutation
template<std::size_t partial_permutation_size, std::ranges::input_range R, typename G>
auto sample_partial_permutations(R&& r, std::size_t sample_count, G&& g) {
    using T = std::ranges::range_value_t<R>;
    return sample_partial_permutations(sized_permutation_container<partial_permutation_size, T>(sample_count),
                                       std::forward<R>(r), partial_permutation_size, sample_count, std::forward<G>(g));
}
inline auto count_partial_permutations(std::size_t range_size, std::size_t partial_permutation_size) {
    return count_each_permutation(partial_permutation_size, range_size - partial_permutation_size);
}
template<std::ranges::input_range R>
auto count_partial_permutations(R&& r, std::size_t partial_permutation_size) {
    return count_partial_permutations(std::ranges::size(r), partial_permutation_size);
}


#endif //ROSSA_UTIL_H