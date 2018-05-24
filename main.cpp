#include "new_buffer.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <random>
#include <benchmark/benchmark.h>
#include <sys/resource.h>

#define SEED (1337)
#define GRANULARITY (8)

#if defined(JEMALLOC) && JEMALLOC
#include <jemalloc/jemalloc.h>
#endif

#if defined(TCMALLOC) && TCMALLOC
#include <gperftools/malloc_extension.h>
#endif

#if defined(JEMALLOC) && JEMALLOC
static std::size_t malloced_bytes() {
	std::uint64_t epoch;
	std::size_t size = sizeof(epoch);
	mallctl("epoch", &epoch, &size, &epoch, size);

	std::size_t count = 0;
	size = sizeof(count);
	if(mallctl("stats.allocated", &count, &size, nullptr, 0)) {
		return 0;
	}
	return count;
}
#elif defined(TCMALLOC) && TCMALLOC
static std::size_t malloced_bytes() {
	std::size_t count = 0;
	MallocExtension::instance()->GetNumericProperty("generic.current_allocated_bytes", &count);
	return count;
}
#else
static std::size_t malloced_bytes() {
		return 0;
}
#endif

// note: recording memory usage costs on the order of 2 us on my machines
#if defined(MEASURE_MEMORY) && MEASURE_MEMORY
inline static void record_memory_usage(benchmark::State& state) {
	state.PauseTiming();
	state.counters["malloc"] = malloced_bytes();
	state.ResumeTiming();
}
#else
inline static void record_memory_usage(benchmark::State& _) {}
#endif

template<std::size_t initial_size>
static void simple_copy(benchmark::State& state) {
	using vec_t = new_buffer<unsigned, unsigned, initial_size>;
	std::mt19937_64 prng(SEED);
	for(int i = 0; i <= 10000; ++i) {
		prng();
	}
	std::uniform_int_distribution<unsigned> unsigned_distribution;

	vec_t source(state.range(0), 0u);
	assert(source.size() == state.range(0));
	for(auto& u : source) {
		u = unsigned_distribution(prng);
	}
	for(auto _ : state) {
		vec_t destination(source);
		benchmark::DoNotOptimize(destination.c_ptr());
		benchmark::ClobberMemory();
		record_memory_usage(state);
	}
}
BENCHMARK_TEMPLATE(simple_copy, 0)->RangeMultiplier(GRANULARITY)->Range(1, 1<<20);
BENCHMARK_TEMPLATE(simple_copy, -1)->RangeMultiplier(GRANULARITY)->Range(1, 1<<20);
BENCHMARK_TEMPLATE(simple_copy, -2)->RangeMultiplier(GRANULARITY)->Range(1, 1<<20);
BENCHMARK_TEMPLATE(simple_copy, 16)->RangeMultiplier(GRANULARITY)->Range(1, 1<<20);
BENCHMARK_TEMPLATE(simple_copy, 1024)->RangeMultiplier(GRANULARITY)->Range(1, 1<<20);

template<std::size_t initial_size>
static void simple_pushback_copy(benchmark::State& state) {
	using vec_t = new_buffer<unsigned, unsigned, initial_size>;
	std::mt19937_64 prng(SEED);
	for(int i = 0; i <= 10000; ++i) {
		prng();
	}
	std::uniform_int_distribution<unsigned> unsigned_distribution;

	vec_t source(state.range(0), 0u);
	assert(source.size() == state.range(0));
	for(auto& u : source) {
		u = unsigned_distribution(prng);
	}
	for(auto _ : state) {
		vec_t destination;
		for(auto const u : source) {
			destination.push_back(u);
		}
		benchmark::DoNotOptimize(destination.c_ptr());
		benchmark::ClobberMemory();
		record_memory_usage(state);
	}
}
BENCHMARK_TEMPLATE(simple_pushback_copy, 0)->RangeMultiplier(GRANULARITY)->Range(1, 1<<20);
BENCHMARK_TEMPLATE(simple_pushback_copy, -1)->RangeMultiplier(GRANULARITY)->Range(1, 1<<20);
BENCHMARK_TEMPLATE(simple_pushback_copy, -2)->RangeMultiplier(GRANULARITY)->Range(1, 1<<20);
BENCHMARK_TEMPLATE(simple_pushback_copy, 16)->RangeMultiplier(GRANULARITY)->Range(1, 1<<20);
BENCHMARK_TEMPLATE(simple_pushback_copy, 1024)->RangeMultiplier(GRANULARITY)->Range(1, 1<<20);

template<std::size_t initial_size>
static void simple_interleaved_pushback_copy(benchmark::State& state) {
	using vec_t = new_buffer<unsigned, unsigned, initial_size>;
	std::mt19937_64 prng(SEED);
	for(int i = 0; i <= 10000; ++i) {
		prng();
	}
	std::uniform_int_distribution<unsigned> unsigned_distribution;

	vec_t source1(state.range(0), 0u);
	vec_t source2(state.range(0), 0u);
	vec_t source3(state.range(0), 0u);
	vec_t source4(state.range(0), 0u);
	assert(source1.size() == state.range(0));
	assert(source2.size() == state.range(0));
	assert(source3.size() == state.range(0));
	assert(source4.size() == state.range(0));
	for(auto& u : source1) { u = unsigned_distribution(prng); }
	for(auto& u : source2) { u = unsigned_distribution(prng); }
	for(auto& u : source3) { u = unsigned_distribution(prng); }
	for(auto& u : source4) { u = unsigned_distribution(prng); }
	for(auto _ : state) {
		vec_t destination1;
		vec_t destination2;
		vec_t destination3;
		vec_t destination4;
		for(typename vec_t::size_type i = 0, end = static_cast<typename vec_t::size_type>(state.range(0)); i < end; ++i) {
			destination1.push_back(source1[i]);
			destination2.push_back(source2[i]);
			destination3.push_back(source3[i]);
			destination4.push_back(source4[i]);
		}
		benchmark::DoNotOptimize(destination1.c_ptr());
		benchmark::DoNotOptimize(destination2.c_ptr());
		benchmark::DoNotOptimize(destination3.c_ptr());
		benchmark::DoNotOptimize(destination4.c_ptr());
		benchmark::ClobberMemory();
		record_memory_usage(state);
	}
}
BENCHMARK_TEMPLATE(simple_interleaved_pushback_copy, 0)->RangeMultiplier(GRANULARITY)->Range(1, 1<<20);
BENCHMARK_TEMPLATE(simple_interleaved_pushback_copy, -1)->RangeMultiplier(GRANULARITY)->Range(1, 1<<20);
BENCHMARK_TEMPLATE(simple_interleaved_pushback_copy, -2)->RangeMultiplier(GRANULARITY)->Range(1, 1<<20);
BENCHMARK_TEMPLATE(simple_interleaved_pushback_copy, 16)->RangeMultiplier(GRANULARITY)->Range(1, 1<<20);
BENCHMARK_TEMPLATE(simple_interleaved_pushback_copy, 1024)->RangeMultiplier(GRANULARITY)->Range(1, 1<<20);

template<std::size_t initial_size>
static void complex_copy(benchmark::State& state) {
	using vec_t = new_buffer<std::string, unsigned, initial_size>;
	std::mt19937_64 prng(SEED);
	for(int i = 0; i <= 10000; ++i) {
		prng();
	}
	std::uniform_int_distribution<unsigned> unsigned_distribution;

	vec_t source(state.range(0), std::string());
	assert(source.size() == state.range(0));
	for(auto& u : source) {
		u = unsigned_distribution(prng);
	}
	for(auto _ : state) {
		vec_t destination(source);
		benchmark::DoNotOptimize(destination.c_ptr());
		benchmark::ClobberMemory();
		record_memory_usage(state);
	}
}
BENCHMARK_TEMPLATE(complex_copy, 0)->RangeMultiplier(GRANULARITY)->Range(1, 1<<20);
BENCHMARK_TEMPLATE(complex_copy, -1)->RangeMultiplier(GRANULARITY)->Range(1, 1<<20);
BENCHMARK_TEMPLATE(complex_copy, -2)->RangeMultiplier(GRANULARITY)->Range(1, 1<<20);
BENCHMARK_TEMPLATE(complex_copy, 16)->RangeMultiplier(GRANULARITY)->Range(1, 1<<20);
BENCHMARK_TEMPLATE(complex_copy, 1024)->RangeMultiplier(GRANULARITY)->Range(1, 1<<20);

template<std::size_t initial_size>
static void complex_pushback_copy(benchmark::State& state) {
	using vec_t = new_buffer<std::string, unsigned, initial_size>;
	std::mt19937_64 prng(SEED);
	for(int i = 0; i <= 10000; ++i) {
		prng();
	}
	std::uniform_int_distribution<unsigned> unsigned_distribution;

	vec_t source(state.range(0), std::string());
	assert(source.size() == state.range(0));
	for(auto _ : state) {
		vec_t destination;
		for(auto const u : source) {
			destination.push_back(u);
		}
		benchmark::DoNotOptimize(destination.c_ptr());
		benchmark::ClobberMemory();
		record_memory_usage(state);
	}
}
BENCHMARK_TEMPLATE(complex_pushback_copy, 0)->RangeMultiplier(GRANULARITY)->Range(1, 1<<20);
BENCHMARK_TEMPLATE(complex_pushback_copy, -1)->RangeMultiplier(GRANULARITY)->Range(1, 1<<20);
BENCHMARK_TEMPLATE(complex_pushback_copy, -2)->RangeMultiplier(GRANULARITY)->Range(1, 1<<20);
BENCHMARK_TEMPLATE(complex_pushback_copy, 16)->RangeMultiplier(GRANULARITY)->Range(1, 1<<20);
BENCHMARK_TEMPLATE(complex_pushback_copy, 1024)->RangeMultiplier(GRANULARITY)->Range(1, 1<<20);

template<std::size_t initial_size>
static void simple_random_assignments(benchmark::State& state) {
	using vec_t = new_buffer<unsigned, unsigned, initial_size>;
	std::mt19937_64 prng(SEED);
	for(int i = 0; i <= 10000; ++i) {
		prng();
	}
	std::uniform_int_distribution<unsigned> unsigned_distribution;
	std::uniform_int_distribution<unsigned> size_distribution(0, state.range(0) - 1);

	vec_t vec(state.range(0), 0u);
	assert(vec.size() == state.range(0));
	for(auto _ : state) {
		for(int i = 0; i < 10000; ++i) {
			vec[size_distribution(prng)] = size_distribution(prng);
			vec[size_distribution(prng)] = size_distribution(prng);
			vec[size_distribution(prng)] = size_distribution(prng);
			vec[size_distribution(prng)] = size_distribution(prng);
			vec[size_distribution(prng)] = size_distribution(prng);
			vec[size_distribution(prng)] = size_distribution(prng);
			vec[size_distribution(prng)] = size_distribution(prng);
			vec[size_distribution(prng)] = size_distribution(prng);
			vec[size_distribution(prng)] = size_distribution(prng);
			vec[size_distribution(prng)] = size_distribution(prng);
		}
		benchmark::DoNotOptimize(vec.c_ptr());
		benchmark::ClobberMemory();
		record_memory_usage(state);
	}
}
BENCHMARK_TEMPLATE(simple_random_assignments, 0)->RangeMultiplier(GRANULARITY)->Range(1, 1<<30);
BENCHMARK_TEMPLATE(simple_random_assignments, -1)->RangeMultiplier(GRANULARITY)->Range(1, 1<<30);
BENCHMARK_TEMPLATE(simple_random_assignments, -2)->RangeMultiplier(GRANULARITY)->Range(1, 1<<30);
BENCHMARK_TEMPLATE(simple_random_assignments, 16)->RangeMultiplier(GRANULARITY)->Range(1, 1<<30);
BENCHMARK_TEMPLATE(simple_random_assignments, 1024)->RangeMultiplier(GRANULARITY)->Range(1, 1<<30);

template<std::size_t initial_size>
static void simple_random_reads(benchmark::State& state) {
	using vec_t = new_buffer<unsigned, unsigned, initial_size>;
	std::mt19937_64 prng(SEED);
	for(int i = 0; i <= 10000; ++i) {
		prng();
	}
	std::uniform_int_distribution<unsigned> unsigned_distribution;
	std::uniform_int_distribution<unsigned> size_distribution(0, state.range(0) - 1);

	vec_t vec(state.range(0), 0u);
	assert(vec.size() == state.range(0));
	for(auto _ : state) {
		for(int i = 0; i < 10000; ++i) {
			unsigned x = 0;
			x ^= vec[size_distribution(prng)];
			x ^= vec[size_distribution(prng)];
			x ^= vec[size_distribution(prng)];
			x ^= vec[size_distribution(prng)];
			x ^= vec[size_distribution(prng)];
			x ^= vec[size_distribution(prng)];
			x ^= vec[size_distribution(prng)];
			x ^= vec[size_distribution(prng)];
			x ^= vec[size_distribution(prng)];
			x ^= vec[size_distribution(prng)];
			benchmark::DoNotOptimize(x);
		}
		record_memory_usage(state);
	}
}
BENCHMARK_TEMPLATE(simple_random_reads, 0)->RangeMultiplier(GRANULARITY)->Range(1, 1<<30);
BENCHMARK_TEMPLATE(simple_random_reads, -1)->RangeMultiplier(GRANULARITY)->Range(1, 1<<30);
BENCHMARK_TEMPLATE(simple_random_reads, -2)->RangeMultiplier(GRANULARITY)->Range(1, 1<<30);
BENCHMARK_TEMPLATE(simple_random_reads, 16)->RangeMultiplier(GRANULARITY)->Range(1, 1<<30);
BENCHMARK_TEMPLATE(simple_random_reads, 1024)->RangeMultiplier(GRANULARITY)->Range(1, 1<<30);

BENCHMARK_MAIN();
