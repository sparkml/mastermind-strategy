/// \file Test.cpp
/// Routines for testing the algorithms.

#include <iostream>
#include <stdio.h>
#include <malloc.h>
#include <iomanip>

#include "util/call_counter.hpp"
#include "Rules.hpp"
#include "Codeword.hpp"
#include "Feedback.hpp"
#include "Algorithm.hpp"
#include "Engine.hpp"
#include "CodeBreaker.hpp"
#include "SimpleStrategy.hpp"
#include "HeuristicStrategy.hpp"

#include "HRTimer.h"

using namespace Mastermind;
using namespace Utilities;

// Dummy test driver that does nothing in the test and always returns success.
template <class Routine>
struct TestDriver
{
	Engine &e;
	Routine f;

	TestDriver(Engine &engine, Routine func) : e(engine), f(func) { }

	bool operator () () const { return true; }
};

// Compares the running time of two routines.
template <class Routine>
bool compareRoutines(
	Engine &e,
	const char *routine1,
	const char *routine2,
	long times)
{
	Routine func1 = RoutineRegistry<Routine>::get(routine1);
	Routine func2 = RoutineRegistry<Routine>::get(routine2);

	TestDriver<Routine> drv1(e, func1);
	TestDriver<Routine> drv2(e, func2);

	// Verify computation results.
	drv1();
	drv2();
	if (!(drv1 == drv2))
		return false;

	// Display results in debug mode.
	if (times == 0)
	{
		std::cout << "Result 1: " << std::endl << drv1 << std::endl;
		std::cout << "Result 2: " << std::endl << drv2 << std::endl;
	}

	// Time it.
	HRTimer timer;
	double t1 = 0, t2 = 0;

	for (int pass = 0; pass < 10; pass++)
	{
		timer.start();
		for (int k = 0; k < times / 10; k++)
			drv1();
		t1 += timer.stop();

		timer.start();
		for (int k = 0; k < times / 10; k++)
			drv2();
		t2 += timer.stop();
	}

	printf("Algorithm 1: %6.3f\n", t1);
	printf("Algorithm 2: %6.3f\n", t2);
	printf("Throughput Ratio: %5.2fX\n", (t1/t2));
	return true;
}

// Codeword generation benchmark.
// Test: Generate all codewords of 4 pegs, 10 colors, and no repeats.
//       Total 5040 items in each run.
// Results: (100,000 runs, Release mode)
// LexOrder: 4.43 s
// CombPerm: 8.54 s [legacy]
// CombPermParallel:  0.96 s [ASM][legacy]
// CombPermParallel2: 0.68 s [ASM][legacy]
template <> class TestDriver<GenerationRoutine>
{
	Engine &e;
	GenerationRoutine f;
	size_t count;
	CodewordList list;

public:
	TestDriver(Engine &engine, GenerationRoutine func)
		: e(engine), f(func), count(f(e.rules(), 0)), list(count) { }

	void operator()() { 	f(e.rules(), list.data()); }

	bool operator == (const TestDriver &r) const
	{
		return list == r.list;
	}

	friend std::ostream& operator << (std::ostream &os, const TestDriver &r)
	{
		// Display first 10 items.
		os << "First 10 of " << r.list.size() << " items:" << std::endl;
		for (size_t i = 0; i < 10 && i < r.list.size(); ++i)
			os << r.list[i] << std::endl;
		return os;
	}
};

// Codeword comparison benchmark.
// Test:     Compare a given codeword to 5040 non-repeatable codewords.
// Results:  (100,000 runs, Win32, VC++ 2011)
// generic:  1.68 s
// norepeat: 0.62 s
template <> class TestDriver<ComparisonRoutine>
{
	Engine &e;
	ComparisonRoutine f;
	CodewordList codewords;
	size_t count;
	Codeword secret;
	FeedbackList feedbacks;

public:
	TestDriver(Engine &env, ComparisonRoutine func)
		: e(env), f(func), codewords(e.generateCodewords()),
		count(codewords.size()), secret(codewords[count/2]),
		feedbacks(count) { }

	void operator()()
	{
		f(e.rules(), secret, &codewords.front(), &codewords.back()+1,
			feedbacks.data());
	}

	bool operator == (const TestDriver &r) const
	{
		if (count != r.count)
		{
			std::cout << "**** ERROR: Different sizes." << std::endl;
			return false;
		}
		for (size_t i = 0; i < count; i++)
		{
			if (feedbacks[i] != r.feedbacks[i])
			{
				std::cout << "**** ERROR: Inconsistent [" << i << "]: "
					<< "Compare(" << secret << ", " << codewords[i] << ") = "
					<< feedbacks[i] << " v " << r.feedbacks[i] << std::endl;
				return false;
			}
		}
		return true;
	}

	friend std::ostream& operator << (std::ostream &os, const TestDriver &r)
	{
		FeedbackFrequencyTable freq;
		r.e.countFrequencies(r.feedbacks.begin(), r.feedbacks.end(), freq);
		return os << freq;
	}
};

// Color-mask scanning benchmark.
// Test: Scan 5040 codewords for 100,000 times.
//
// **** Old Results ****
// These results are for "short" codewords.
//
// ScanDigitMask_v1 (C):              5.35 s
// ScanDigitMask_v2 (16-bit ASM):     2.08 s
// ScanDigitMask_v3 (v2 improved):    1.43 s
// ScanDigitMask_v4 (v3 improved):    1.12 s
// ScanDigitMask_v5 (32-bit ASM):     2.09 s
// ScanDigitMask_v6 (v5 improved):    1.10 s
// ScanDigitMask_v7 (v6 generalized): 1.10 s
//
// Observations:
//   - ASM with parallel execution and loop unrolling performs the best.
//   - There is little performance difference between 16-bit ASM and 32-bit ASM.
//   - Loop unrolling has limited effect. Seems 1.10s is the lower bound
//     the current algorithm can improve to.
//
// **** New Results ****
// These results are for "long" codewords.
// ScanDigitMask_v1 (SSE2): 0.40 s
template <> class TestDriver<MaskRoutine>
{
	Engine &e;
	MaskRoutine f;
	CodewordList list;
	unsigned short mask;

public:
	TestDriver(Engine &env, MaskRoutine func)
		: e(env), f(func), list(e.generateCodewords()), mask(0) { }

	void operator()()
	{
		mask = list.empty()? 0 : f(&list.front(), &list.back() + 1);
	}

	bool operator == (const TestDriver &r) const
	{
		if (mask != r.mask)
		{
			std::cout << "**** Inconsistent color mask ****" << std::endl;
			return false;
		}
		return true;
	}

	friend std::ostream& operator << (std::ostream &os, const TestDriver &r)
	{
		os << "Present digits:";
		unsigned short t = r.mask;
		for (int i = 0; i < 16; i++)
		{
			if (t & 1)
				os << " " << i;
			t >>= 1;
		}
		return os << std::endl;
	}
};


#if 0
int TestEquivalenceFilter(const Rules &rules, long times)
{
	CodewordList list = generateCodewords(rules);
	int total = (int)list.size();

	unsigned char eqclass[16] = {
		//0,1,2,3, 4,5,6,7, 8,9,10,11, 12,13,14,15 }; // all diff
		//0,3,2,5, 4,7,6,9, 8,1,10,11, 12,13,14,15 }; // 1,3,5,7,9 same
		//1,2,3,4, 5,6,7,8, 9,0,10,11, 12,13,14,15 }; // all same
		0,3,2,5, 4,1,6,7, 8,9,10,11, 12,13,14,15 }; // 1,3,5 same

	//__m128i *output = (__m128i*)_aligned_malloc(16*total, 16);
	util::aligned_allocator<__m128i,16> alloc;
	__m128i* output = alloc.allocate(total);

	int count;

	if (times == 0) {
		//count = FilterByEquivalenceClass_norep_v3(
		count = FilterByEquivalenceClass_rep_v1(
			(codeword_t*)list.data(), total, eqclass, (codeword_t *)output);
		if (1) {
			for (int i = 0; i < count; i++)
			{
				std::cout << Codeword(output[i]) << " ";
			}
		}
		alloc.deallocate(output,total);
		//_aligned_free(output);
		printf("\nCount: %d\n", count);
		system("PAUSE");
		return 0;
	}

	HRTimer timer;
	double t1, t2;

	t1 = t2 = 0;
	for (int pass = 0; pass < 10; pass++) {
		timer.start();
		for (int k = 0; k < times / 10; k++) {
//			count = FilterByEquivalenceClass_norep_v1(
//				(__m128i*)list.GetData(), list.GetCount(), eqclass, output);
			count = FilterByEquivalenceClass_norep_v2(
				(codeword_t*)list.data(), list.size(), eqclass, (codeword_t *)output);
		}
		t1 += timer.stop();

		timer.start();
		for (int k = 0; k < times / 10; k++) {
			count = FilterByEquivalenceClass_norep_v3(
				(codeword_t*)list.data(), list.size(), eqclass, (codeword_t *)output);
		}
		t2 += timer.stop();
	}
	printf("Equivalence 1: %6.3f\n", t1);
	printf("Equivalence 2: %6.3f\n", t2);
	printf("Count: %d\n", count);

	alloc.deallocate(output,total);
	//_aligned_free(output);
	system("PAUSE");
	return 0;
}
#endif

#if 0
// Compare FilterByEquivalence Algorithms
int TestEquivalenceFilter(Rules rules, long times)
{
	CodewordList list = CodewordList::Enumerate(rules);

	unsigned char eqclass[16] = {
		//0,1,2,3, 4,5,6,7, 8,9,10,11, 12,13,14,15 }; // all diff
		//0,3,2,5, 4,7,6,1, 8,9,10,11, 12,13,14,15 }; // 1,3,5,7 same
		1,2,3,4, 5,6,7,8, 9,0,10,11, 12,13,14,15 }; // all same
	unsigned short output[5040];
	int count;

	if (times == 0) {
		count = FilterByEquivalenceClass_norep_16_v1(
			list.GetData(), list.GetCount(), eqclass, output);
		if (1) {
			for (int i = 0; i < count; i++) {
				printf("%04x ", output[i]);
			}
		}
		printf("\nCount: %d\n", count);
		system("PAUSE");
		return 0;
	}

	HRTimer timer;
	double t1, t2;

	t1 = t2 = 0;
	for (int pass = 0; pass < 10; pass++) {
		timer.Start();
		for (int k = 0; k < times / 10; k++) {
			count = FilterByEquivalenceClass_norep_16_v1(
				list.GetData(), list.GetCount(), eqclass, output);
		}
		t1 += timer.Stop();

		timer.Start();
		for (int k = 0; k < times / 10; k++) {
			count = FilterByEquivalenceClass_norep_16_v2(
				list.GetData(), list.GetCount(), eqclass, output);
		}
		t2 += timer.Stop();
	}
	printf("Equivalence 1: %6.3f\n", t1);
	printf("Equivalence 2: %6.3f\n", t2);
	printf("Count: %d\n", count);

	system("PAUSE");
	return 0;
}
#endif

#if 0
#ifndef NTEST
/// Compares frequency counting algorithms.
///
/// Test: Compute the frequency table of 5040 feedbacks.
/// Run the test for 500,000 times.
///
/// Results:
/// <pre>
/// count_freq_v1 (plain C):              5.55 s
/// count_freq_v2 (ASM, 2-parallel OOE):  3.85 s
/// count_freq_v3 (C, 4-parallel OOE):    5.51 s
/// count_freq_v4 (ASM, loop-unroll):     4.54 s
/// count_freq_v5 (ASM, 4-parallel OOE):  2.99 s
/// count_freq_v6 (improved ASM from v5): 2.88 s
/// </pre>
/// Conclusion:
/// Due to the memory-intensive nature of the algorithm, the performance
/// cannot be improved much. count_freq_v6() is the chosen implementation.
int TestFrequencyCounting(const Rules &rules, long times)
{
	CodewordList list = generateCodewords(rules);
	FeedbackList fblist = compare(rules, list[0], list.cbegin(), list.cend());
	int count = fblist.size();
	const unsigned char *fbl = (const unsigned char *)fblist.data();
	const unsigned char maxfb = 63;
	unsigned int freq[(int)maxfb+1];

	FREQUENCY_COUNTING_ROUTINE *func1 = CountFrequenciesImpl->GetRoutine("c");
	FREQUENCY_COUNTING_ROUTINE *func2 = CountFrequenciesImpl->GetRoutine("c_luf4");

	if (times == 0) {
		int total = 0;
		//count = 11;
		func2(fbl, count, freq, maxfb);
		for (int i = 0; i <= maxfb; i++) {
			if (freq[i] > 0)
			{
				std::cout << Feedback(i) << " = " << freq[i] << std::endl;
				total += freq[i];
			}
		}
		std::cout << "Expected count: " << count << std::endl;
		std::cout << "Actual total:   " << total << std::endl;
		system("PAUSE");
		return 0;
	}

	HRTimer timer;
	double t1, t2;
	t1 = t2 = 0;

	for (int pass = 0; pass < 10; pass++) {
		timer.start();
		for (int j = 0; j < times / 10; j++) {
			func1(fbl, count, freq, maxfb);
		}
		t1 += timer.stop();

		timer.start();
		for (int j = 0; j < times / 10; j++) {
			func2(fbl, count, freq, maxfb);
		}
		t2 += timer.stop();
	}

	printf("Algorithm 1: %6.3f\n", t1);
	printf("Algorithm 2: %6.3f\n", t2);
	printf("Improvement: %5.1f%%\n", (t1/t2-1)*100);

	system("PAUSE");
	return 0;
}
#endif
#endif

static bool testSumSquares(
	const Engine &e,
	const char *routine1,
	const char *routine2,
	long times)
{
	CodewordList list = e.generateCodewords();
	FeedbackList fbl = e.compare(list[0], list);
	FeedbackFrequencyTable freq;
	e.countFrequencies(fbl.begin(), fbl.end(), freq);
	unsigned char maxfb = Feedback::maxValue(e.rules()); // fbl.GetMaxFeedbackValue();
	size_t count = maxfb + 1;

	SumSquaresRoutine func1 = RoutineRegistry<SumSquaresRoutine>::get(routine1);
	SumSquaresRoutine func2 = RoutineRegistry<SumSquaresRoutine>::get(routine2);

	// Verify results.
	unsigned int ss1 = func1(freq.data(), freq.data() + count);
	unsigned int ss2 = func2(freq.data(), freq.data() + count);
	if (ss1 != ss2)
	{
		std::cout << "**** ERROR: Result mismatch: " << ss1
			<< " v " << ss2 << std::endl;
		return false;
	}

	// Print result if in debug mode
	if (times == 0)
	{
		std::cout << "SS1 = " << ss1 << std::endl;
		std::cout << "SS2 = " << ss2 << std::endl;
		return true;
	}

	HRTimer timer;
	double t1 = 0, t2 = 0;

	for (int pass = 0; pass < 10; pass++)
	{
		timer.start();
		for (int j = 0; j < times / 10; j++)
		{
			ss1 = func1(freq.data(), freq.data() + count);
		}
		t1 += timer.stop();

		timer.start();
		for (int j = 0; j < times / 10; j++)
		{
			ss2 = func2(freq.data(), freq.data() + count);
		}
		t2 += timer.stop();
	}

	printf("Algorithm 1: %6.3f\n", t1);
	printf("Algorithm 2: %6.3f\n", t2);
	// printf("Speed Ratio: %5.2fX\n", t1/t2);

	// system("PAUSE");
	return 0;
}

static void simulate_guessing(
	Engine &e, Strategy* strats[], size_t n,
	const CodeBreakerOptions &options)
{
	CodewordList all = e.generateCodewords();
	Rules rules = e.rules();

	std::cout
		<< "Game Settings" << std::endl
		<< "---------------" << std::endl
		<< "Number of pegs:      " << rules.pegs() << std::endl
		<< "Number of colors:    " << rules.colors() << std::endl
		<< "Color repeatable:    " << std::boolalpha << rules.repeatable() << std::endl
		<< "Number of codewords: " << rules.size() << std::endl;

	// Pick a secret "randomly".
	Codeword secret = all[all.size()/4*3];
	std::cout << std::endl;
	std::cout << "Secret: " << secret << std::endl;

	// Use an array to store the status of each codebreaker.
	std::vector<bool> finished(n);

	// Create an array of codebreakers and output strategy names.
	// BUG: when changing from CodeBreaker* to CodeBreaker, there is
	// a memory error. Find out why.
	std::vector<CodeBreaker*> breakers;
	std::cout << std::left << " # ";
	for (size_t i = 0; i < n; ++i)
	{
		std::string name = strats[i]->name();
		std::cout << std::setw(10) << name;
		breakers.push_back(new CodeBreaker(e, strats[i], options));
	}
	std::cout << std::right << std::endl;

	// Output horizontal line.
	std::cout << "---";
	for (size_t i = 0; i < n; ++i)
	{
		std::cout << "----------";
	}
	std::cout << std::endl;

	// Step-by-step guessing.
	int step = 0;
	for (size_t finished_count = 0; finished_count < n; )
	{
		std::cout << std::setw(2) << (++step);
		std::cout.flush();

		// Test each code breaker in turn.
		for (size_t i = 0; i < n; ++i)
		{
			if (finished[i])
				continue;

			CodeBreaker &breaker = *breakers[i];
			Codeword guess = breaker.MakeGuess();
			if (guess.empty())
			{
				std::cout << " FAIL";
				finished[i] = true;
				++finished_count;
			}
			else
			{
				Feedback feedback = e.compare(secret, guess);
				std::cout << " " << guess << ":" << feedback;
				std::cout.flush();
				if (feedback == Feedback::perfectValue(e.rules()))
				{
					finished[i] = true;
					++finished_count;
				}
				breaker.AddConstraint(guess, feedback);
			}
		}
		std::cout << std::endl;
	}
}

#if 1
static void test_strategy_tree(
	Engine &e,
	Strategy *strategies[],
	size_t n,
	const CodeBreakerOptions &options)
	// const Codeword& first_guess)
{
	//CodewordList all = e.generateCodewords();
	Rules rules = e.rules();
	//Feedback target = Feedback::perfectValue(rules);
	Utilities::HRTimer timer;

	std::cout
		<< "Game Settings" << std::endl
		<< "---------------" << std::endl
		<< "Number of pegs:      " << rules.pegs() << std::endl
		<< "Number of colors:    " << rules.colors() << std::endl
		<< "Color repeatable:    " << std::boolalpha << rules.repeatable() << std::endl
		<< "Number of codewords: " << rules.size() << std::endl;

	std::cout << std::endl
		<< "Options" << std::endl
		<< "---------" << std::endl
		<< "Optimize obvious guess: " << std::boolalpha
			<< options.optimize_obvious << std::endl
		<< "Guess possibility only: " << std::boolalpha
			<< options.possibility_only << std::endl;

	//printf("\n");
	//printf("Algorithm Descriptions\n");
	//printf("------------------------\n");
	//for (int i = 0; i < nb; i++) {
	//	printf("  A%d: %s\n", (i + 1), breakers[i]->GetDescription().c_str());
	//}

	std::cout << std::endl
		<< "Frequency Table" << std::endl
		<< "-----------------" << std::endl
		<< "Strategy: Total   Avg    1    2    3    4    5    6    7    8    9   >9   Time" << std::endl;

	for (size_t i = 0; i < n; ++i)
	{
		Strategy *strat = strategies[i];

		// Build a strategy tree of this code breaker
		timer.start();
		StrategyTree tree = BuildStrategyTree(e, strat, options);
		double t = timer.stop();

		// Count the steps used to get the answers
		const int max_depth = 10;
		unsigned int freq[max_depth];
		unsigned int total = tree.getDepthInfo(freq, max_depth);
		size_t count = rules.size();

//			if (i*100/count > pct) {
//				pct = i*100/count;
//				printf("\r  A%d: running... %2d%%", ib + 1, pct);
//				fflush(stdout);
//			}

		// Display statistics
		std::cout << "\r" << std::setw(8) << strat->name() << ":"
			<< std::setw(6) << total << " "
			<< std::setw(5) << std::setprecision(3)
			<< std::fixed << (double)total / count << ' ';

		for (int i = 1; i <= max_depth; i++) {
			if (freq[i-1] > 0)
				std::cout << std::setw(4) << freq[i-1] << ' ';
			else
				std::cout << "   - ";
		}
		std::cout << std::fixed << std::setw(6) << std::setprecision(2) << t << std::endl;
	}
}
#endif

/// Runs regression and benchmark tests.
int test(const Rules &rules)
{
#ifdef NDEBUG
#define LOOP_FLAG 1
#else
#define LOOP_FLAG 0
#endif

	// Set up the standard engine.
	Engine e(rules);

#if 0
	//compareRoutines<GenerationRoutine>(e, "generic", "generic", 100*LOOP_FLAG);
	//compareRoutines<ComparisonRoutine>(e, "generic", "norepeat", 100000*LOOP_FLAG);
	compareRoutines<MaskRoutine>(e, "generic", "unrolled", 100000*LOOP_FLAG);

	//testSumSquares(rules, "generic", "generic", 10000000*LOOP_FLAG);
	//return TestFrequencyCounting(rules, 250000*LOOP_FLAG);
	//return TestEquivalenceFilter(rules, 10000*LOOP_FLAG);
	system("PAUSE");
	return 0;
#endif

#if 1
	extern void test_morphism(Engine &);
	test_morphism(e);
	system("PAUSE");
	return 0;
#endif

	using namespace Mastermind::Heuristics;

	// todo: we can use "optimize obvious" to reduce the strategy tree size.
	// todo: output strategy tree size info.
	CodeBreakerOptions options;
	options.optimize_obvious = true;
	options.possibility_only = false;

	//Codeword first_guess = Codeword::emptyValue();
	//Codeword first_guess = Codeword::Parse("0011", rules);

	Strategy* strats[] = {
		new SimpleStrategy(e),
		new HeuristicStrategy<MinimizeWorstCase<1>>(e),
		new HeuristicStrategy<MinimizeAverage>(e),
		new HeuristicStrategy<MaximizeEntropy<false>>(e),
		new HeuristicStrategy<MaximizeEntropy<true>>(e),
		new HeuristicStrategy<MaximizePartitions>(e),
		//new HeuristicCodeBreaker<Heuristics::MinimizeSteps>(rules, posonly),
		//new OptimalCodeBreaker(rules),
	};

	//simulate_guessing(e, strats, sizeof(strats)/sizeof(strats[0]), options);
	test_strategy_tree(e, strats, sizeof(strats)/sizeof(strats[0]), options);

#if 0
	// Display some statistics.
	std::cout << std::endl
		<< "Call statistics" << std::endl
		<< "-----------------" << std::endl;

	std::cout << "** Comparison **" << std::endl
		<< util::call_counter::get("Comparison") << std::endl;
#endif

#if 0
	if (0) {
		printf("\nRun again:\n");
		CountFrequenciesImpl->SelectRoutine("c_p8_il_os");
		TestGuessingByTree(rules, breakers, sizeof(breakers)/sizeof(breakers[0]), first_guess);
		printf("\n");
	}

	void PrintFrequencyStatistics();
	//PrintFrequencyStatistics();

	void PrintCompareStatistics();
	//PrintCompareStatistics();

	void PrintMakeGuessStatistics();
	//PrintMakeGuessStatistics();

	void OCB_PrintStatistics();
	OCB_PrintStatistics();

#endif

	system("PAUSE");
	return 0;
}