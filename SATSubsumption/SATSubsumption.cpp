#include "SATSubsumption.hpp"
#include "Indexing/LiteralMiniIndex.hpp"
#include "Lib/STL.hpp"
#include "Lib/BinaryHeap.hpp"
#include "Lib/DArray.hpp"
#include "Lib/DHMap.hpp"
#include "Kernel/Matcher.hpp"
#include "Kernel/MLMatcher.hpp"
#include "Kernel/ColorHelper.hpp"
#include "Debug/RuntimeStatistics.hpp"

#include <chrono>
#include <iostream>
#include <iomanip>
#include <new>
#include <cstdint>
#include <climits>

#include "SATSubsumptionImpl2.hpp"
#include "SATSubsumptionImpl3.hpp"
#include "SATSubsumption/cdebug.hpp"

using namespace Indexing;
using namespace Kernel;
using namespace SATSubsumption;

/*
struct SubsumptionBenchmark {
  vvector<SubsumptionInstance> subsumptions;
  vvector<SubsumptionResolutionInstance> subsumptionResolutions;
  vvector<SubsumptionRound> rounds;
};
*/

template <typename PredS, typename PredSR>
void filter_benchmark(SubsumptionBenchmark& b, PredS should_keep_s, PredSR should_keep_sr) {
  ASS_EQ(b.rounds.back().s_end, b.subsumptions.size());
  ASS_EQ(b.rounds.back().sr_end, b.subsumptionResolutions.size());

  unsigned i = 0;
  unsigned j = 0;
  for (auto& r : b.rounds) {
    unsigned s_end = r.s_end;
    while (i < s_end) {
      if (should_keep_s(b.subsumptions[i])) {
        b.subsumptions[j++] = b.subsumptions[i++];
      } else {
        i++;
      }
    }
    r.s_end = j;
  }
  ASS_EQ(i, b.subsumptions.size());
  b.subsumptions.resize(j);
  ASS_EQ(b.rounds.back().s_end, b.subsumptions.size());

  i = 0;
  j = 0;
  for (auto& r : b.rounds) {
    unsigned sr_end = r.sr_end;
    while (i < sr_end) {
      if (should_keep_sr(b.subsumptionResolutions[i])) {
        b.subsumptionResolutions[j++] = b.subsumptionResolutions[i++];
      } else {
        i++;
      }
    }
    r.sr_end = j;
  }
  ASS_EQ(i, b.subsumptionResolutions.size());
  b.subsumptionResolutions.resize(j);
  ASS_EQ(b.rounds.back().sr_end, b.subsumptionResolutions.size());
}


/****************************************************************************
 * Original subsumption implementation (from ForwardSubsumptionAndResolution)
 ****************************************************************************/

namespace OriginalSubsumption {


struct ClauseMatches {
  CLASS_NAME(OriginalSubsumption::ClauseMatches);
  USE_ALLOCATOR(ClauseMatches);

private:
  //private and undefined operator= and copy constructor to avoid implicitly generated ones
  ClauseMatches(const ClauseMatches&);
  ClauseMatches& operator=(const ClauseMatches&);
public:
  ClauseMatches(Clause* cl) : _cl(cl), _zeroCnt(cl->length())
  {
    unsigned clen=_cl->length();
    _matches=static_cast<LiteralList**>(ALLOC_KNOWN(clen*sizeof(void*), "Inferences::ClauseMatches"));
    for(unsigned i=0;i<clen;i++) {
      _matches[i]=0;
    }
  }
  ~ClauseMatches()
  {
    unsigned clen=_cl->length();
    for(unsigned i=0;i<clen;i++) {
      LiteralList::destroy(_matches[i]);
    }
    DEALLOC_KNOWN(_matches, clen*sizeof(void*), "Inferences::ClauseMatches");
  }

  void addMatch(Literal* baseLit, Literal* instLit)
  {
    addMatch(_cl->getLiteralPosition(baseLit), instLit);
  }
  void addMatch(unsigned bpos, Literal* instLit)
  {
    if(!_matches[bpos]) {
      _zeroCnt--;
    }
    LiteralList::push(instLit,_matches[bpos]);
  }
  void fillInMatches(LiteralMiniIndex* miniIndex)
  {
    unsigned blen=_cl->length();

    for(unsigned bi=0;bi<blen;bi++) {
      LiteralMiniIndex::InstanceIterator instIt(*miniIndex, (*_cl)[bi], false);
      while(instIt.hasNext()) {
	Literal* matched=instIt.next();
	addMatch(bi, matched);
      }
    }
  }
  bool anyNonMatched() { return _zeroCnt; }

  Clause* _cl;
  unsigned _zeroCnt;
  LiteralList** _matches;

  class ZeroMatchLiteralIterator
  {
  public:
    ZeroMatchLiteralIterator(ClauseMatches* cm)
    : _lits(cm->_cl->literals()), _mlists(cm->_matches), _remaining(cm->_cl->length())
    {
      if(!cm->_zeroCnt) {
	_remaining=0;
      }
    }
    bool hasNext()
    {
      while(_remaining>0 && *_mlists) {
	_lits++; _mlists++; _remaining--;
      }
      return _remaining;
    }
    Literal* next()
    {
      _remaining--;
      _mlists++;
      return *(_lits++);
    }
  private:
    Literal** _lits;
    LiteralList** _mlists;
    unsigned _remaining;
  };
};



bool checkForSubsumptionResolutionSetup(Kernel::MLMatcher& matcher, Clause* cl, ClauseMatches* cms, Literal* resLit)
{
  Clause* mcl = cms->_cl;
  unsigned mclen = mcl->length();

  ClauseMatches::ZeroMatchLiteralIterator zmli(cms);
  if (zmli.hasNext()) {
    while (zmli.hasNext()) {
      Literal* bl = zmli.next();
      if (!MatchingUtils::match(bl, resLit, true)) {
        return false;
      }
    }
  }
  else {
    bool anyResolvable = false;
    for (unsigned i = 0; i < mclen; i++) {
      if (MatchingUtils::match((*mcl)[i], resLit, true)) {
        anyResolvable = true;
        break;
      }
    }
    if (!anyResolvable) {
      return false;
    }
  }

  matcher.init(mcl, cl, cms->_matches, resLit);
  // just return true here; we're only measuring the setup.
  return true;
}

bool checkForSubsumptionResolution(Kernel::MLMatcher& matcher, Clause* cl, ClauseMatches* cms, Literal* resLit)
{
  Clause* mcl = cms->_cl;
  unsigned mclen = mcl->length();

  ClauseMatches::ZeroMatchLiteralIterator zmli(cms);
  if (zmli.hasNext()) {
    while (zmli.hasNext()) {
      Literal* bl = zmli.next();
      if (!MatchingUtils::match(bl, resLit, true)) {
        return false;
      }
    }
  }
  else {
    bool anyResolvable = false;
    for (unsigned i = 0; i < mclen; i++) {
      if (MatchingUtils::match((*mcl)[i], resLit, true)) {
        anyResolvable = true;
        break;
      }
    }
    if (!anyResolvable) {
      return false;
    }
  }

  matcher.init(mcl, cl, cms->_matches, resLit);
  return matcher.nextMatch();
}



class OriginalSubsumptionImpl
{
  private:
    Kernel::MLMatcher matcher;
    ClauseMatches* cms = nullptr;
  public:

    void printStats(std::ostream& out)
    {
      out << "Stats: " << matcher.getStats() << std::endl;
    }

    bool setup(Clause* side_premise, Clause* main_premise)
    {
      Clause* mcl = side_premise;
      Clause* cl = main_premise;
      LiteralMiniIndex miniIndex(cl);  // TODO: to benchmark forward subsumption, we might want to move this to the benchmark setup instead? as the work may be shared between differed side premises.

      // unsigned mlen = mcl->length();

      if (cms) { delete cms; cms = nullptr; }
      ASS(cms == nullptr);
      cms = new ClauseMatches(mcl);  // NOTE: why "new"? because the original code does it like this as well.
      cms->fillInMatches(&miniIndex);

      if (cms->anyNonMatched()) {
        delete cms;
        cms = nullptr;
        return false;
      }

      matcher.init(mcl, cl, cms->_matches, true);
      return true;
    }

    bool solve()
    {
      ASS(cms);
      bool isSubsumed = matcher.nextMatch();
      delete cms;
      cms = nullptr;
      return isSubsumed;
    }

    bool checkSubsumption(Kernel::Clause* side_premise, Kernel::Clause* main_premise)
    {
      return setup(side_premise, main_premise) && solve();
    }
};
using Impl = OriginalSubsumptionImpl;  // shorthand if we use qualified namespace


}  // namespace OriginalSubsumption

/****************************************************************************/
/****************************************************************************/
/****************************************************************************/








/****************************************************************************
 * --mode stest
 ****************************************************************************/

void ProofOfConcept::test(Clause* side_premise, Clause* main_premise)
{
  CALL("ProofOfConcept::test");
  std::cout << "\% SATSubsumption::test" << std::endl;
  std::cout << "\% side_premise: " << side_premise->toString() << std::endl;
  std::cout << "\% main_premise: " << main_premise->toString() << std::endl;
  std::cout << std::endl;
  BYPASSING_ALLOCATOR;

  {
    SATSubsumptionImpl2 impl;
    std::cout << "\nTESTING 'subsat' subsumption (v2)" << std::endl;
    subsat::print_config(std::cout);
    std::cout << "SETUP" << std::endl;
    bool subsumed1 = impl.setupSubsumption(side_premise, main_premise);
    std::cout << "  => " << subsumed1 << std::endl;
    std::cout << "SOLVE" << std::endl;
    bool subsumed = subsumed1 && impl.solve();
    std::cout << "  => " << subsumed << std::endl;
  }

  {
    SATSubsumptionImpl3 impl;
    std::cout << "\nTESTING 'subsat' subsumption (v3)" << std::endl;
    subsat::print_config(std::cout);
    std::cout << "SETUP" << std::endl;
    auto token = impl.setupMainPremise(main_premise);
    bool subsumed1 = impl.setupSubsumption(side_premise);
    std::cout << "  => " << subsumed1 << std::endl;
    std::cout << "SOLVE" << std::endl;
    bool subsumed = subsumed1 && impl.solve();
    std::cout << "  => " << subsumed << std::endl;
  }

  {
    std::cout << "\nTESTING 'MLMatcher'" << std::endl;
    OriginalSubsumption::Impl orig;
    bool subsumed = orig.checkSubsumption(side_premise, main_premise);
    std::cout << "  => " << subsumed << std::endl;
    // if (subsumed != expected_subsumed) {
    //   std::cout << "UNEXPECTED RESULT: " << subsumed << std::endl;
    // }
    orig.printStats(std::cout);
  }

  {
    SATSubsumptionImpl2 impl;
    std::cout << "\nTESTING 'subsat' subsumption resolution (v2)" << std::endl;
    subsat::print_config(std::cout);
    std::cout << "SETUP" << std::endl;
    bool res1 = impl.setupSubsumptionResolution(side_premise, main_premise);
    std::cout << "  => " << res1 << std::endl;
    std::cout << "SOLVE" << std::endl;
    bool res = res1 && impl.solve();
    std::cout << "  => " << res << std::endl;
    if (res)
      std::cout << "conclusion = " << impl.getSubsumptionResolutionConclusion()->toString() << std::endl;
  }

  {
    SATSubsumptionImpl3 impl;
    std::cout << "\nTESTING 'subsat' subsumption resolution (v3)" << std::endl;
    subsat::print_config(std::cout);
    std::cout << "SETUP" << std::endl;
    auto token = impl.setupMainPremise(main_premise);
    bool res1 = impl.setupSubsumptionResolution(side_premise);
    std::cout << "  => " << res1 << std::endl;
    std::cout << "SOLVE" << std::endl;
    bool res = res1 && impl.solve();
    std::cout << "  => " << res << std::endl;
    if (res)
      std::cout << "conclusion = " << impl.getSubsumptionResolutionConclusion(side_premise)->toString() << std::endl;
  }

}

ProofOfConcept::ProofOfConcept()
{
  m_subsat_impl2.reset(new SATSubsumptionImpl2());
  m_subsat_impl3.reset(new SATSubsumptionImpl3());
}

ProofOfConcept::~ProofOfConcept() = default;

Token::~Token() {}
Token::Token(std::unique_ptr<SATSubsumptionImpl3_Token> tok) : tok(std::move(tok)) {}
Token::Token(Token&&) = default;

Token ProofOfConcept::setupMainPremise(Kernel::Clause* new_instance) {
  return {std::make_unique<SATSubsumptionImpl3_Token>(m_subsat_impl3->setupMainPremise(new_instance))};
}

bool ProofOfConcept::checkSubsumption(Kernel::Clause* base, Kernel::Clause* instance)
{
  CALL("ProofOfConcept::checkSubsumption");
  ASS(m_subsat_impl2);
  ASS(m_subsat_impl3);
  bool res2 = m_subsat_impl2->checkSubsumption(base, instance);
  bool res3 = m_subsat_impl3->checkSubsumption(base, instance);
  if (res2 != res3) {
    std::cerr << "\% ***WRONG RESULT: MISMATCH S2 (" << res2 << ") VS S3 (" << res3 << ")***" << std::endl;
    std::cerr << "\%    base       = " << base->toString() << std::endl;
    std::cerr << "\%    instance   = " << instance->toString() << std::endl;
  }
  return res2 & res3;
}

bool ProofOfConcept::checkSubsumptionResolution(Kernel::Clause* base, Kernel::Clause* instance, Kernel::Clause* conclusion)
{
  CALL("ProofOfConcept::checkSubsumptionResolution");
  ASS(m_subsat_impl2);
  ASS(m_subsat_impl3);
  bool res2 = m_subsat_impl2->checkSubsumptionResolution(base, instance, conclusion);
  bool res3 = m_subsat_impl3->checkSubsumptionResolution(base, instance, conclusion);
  if (res2 != res3) {
    std::cerr << "\% ***WRONG RESULT: MISMATCH SR2 (" << res2 << ") VS SR3 (" << res3 << ")***" << std::endl;
    std::cerr << "\%    base       = " << base->toString() << std::endl;
    std::cerr << "\%    instance   = " << instance->toString() << std::endl;
  }
  return res2 & res3;
}


// void ProofOfConcept::setupMainPremise(Kernel::Clause* instance)
// {
//   ASS(m_subsat_impl3);
//   m_subsat_impl3->setupMainPremise(instance);
// }

bool ProofOfConcept::setupSubsumption(Kernel::Clause* base)
{
  ASS(m_subsat_impl3);
  return m_subsat_impl3->setupSubsumption(base);
}

bool ProofOfConcept::solve()
{
  ASS(m_subsat_impl3);
  return m_subsat_impl3->solve();
}




/****************************************************************************
 * Benchmarks
 ****************************************************************************/


#if ENABLE_BENCHMARK
// Google benchmark library
// https://github.com/google/benchmark
#include <benchmark/benchmark.h>
#endif  // ENABLE_BENCHMARK


#if ENABLE_BENCHMARK

template<typename It>
struct Iterable {
  It m_begin;
  It m_end;
  It begin() const { return m_begin; }
  It end() const { return m_end; }
};

class FwSubsumptionRound
{
public:
  using s_iter = vvector<SubsumptionInstance>::const_iterator;
  using sr_iter = vvector<SubsumptionResolutionInstance>::const_iterator;
  Iterable<s_iter> subsumptions() const { return {s_begin, s_end}; }
  Iterable<sr_iter> subsumptionResolutions() const { return {sr_begin, sr_end}; }
  Kernel::Clause* main_premise() const { return m_main_premise; }

  FwSubsumptionRound(SubsumptionBenchmark const& b, size_t round) {
    size_t s_begin_idx = (round == 0) ? 0 : b.rounds[round-1].s_end;
    size_t s_end_idx = (round < b.rounds.size()) ? b.rounds[round].s_end : b.subsumptions.size();
    size_t sr_begin_idx = (round == 0) ? 0 : b.rounds[round-1].sr_end;
    size_t sr_end_idx = (round < b.rounds.size()) ? b.rounds[round].sr_end : b.subsumptionResolutions.size();
    s_begin = b.subsumptions.begin() + s_begin_idx;
    s_end = b.subsumptions.begin() + s_end_idx;
    sr_begin = b.subsumptionResolutions.begin() + sr_begin_idx;
    sr_end = b.subsumptionResolutions.begin() + sr_end_idx;

    if (s_begin != s_end) {
      m_main_premise = s_begin->main_premise;
    }
    else if (sr_begin != sr_end) {
      m_main_premise = sr_begin->main_premise;
    }
    else {
      m_main_premise = nullptr;
    }
    ASS(std::all_of(s_begin, s_end, [this](auto const& s){ return s.main_premise == m_main_premise; }));
    ASS(std::all_of(sr_begin, sr_end, [this](auto const& sr){ return sr.main_premise == m_main_premise; }));
  }

  FwSubsumptionRound withoutSubsumptionResolution() const {
    FwSubsumptionRound r = *this;
    r.sr_end = r.sr_begin;
    return r;
  }

private:
  Kernel::Clause* m_main_premise;  ///< also called "instance clause"
  s_iter s_begin;
  s_iter s_end;
  sr_iter sr_begin;
  sr_iter sr_end;
};


void bench_sat2_run_setup(benchmark::State& state, vvector<FwSubsumptionRound> const& fw_rounds)
{
  for (auto _ : state) {
    SATSubsumptionImpl2 impl;
    int count = 0;
    for (auto const& round : fw_rounds) {
      Kernel::Clause::requestAux();
      Kernel::Clause* main_premise = round.main_premise();
      for (auto const& s : round.subsumptions()) {
        if (!impl.setupSubsumption(s.side_premise, main_premise)) {
          count++;
          if (s.result > 0) { state.SkipWithError("Wrong result!"); Kernel::Clause::releaseAux(); return; }
        }
        // no solve since we only measure the setup
      }
      for (auto const& sr : round.subsumptionResolutions()) {
        if (sr.side_premise->hasAux())
          continue;
        sr.side_premise->setAux(nullptr);  // other than original SR we only need to handle each side premise once
        if (!impl.setupSubsumptionResolution(sr.side_premise, main_premise)) {
          count++;
          // can't check result here because the logged result might cover only one resLit.
          // if (sr.result > 0) { state.SkipWithError("Wrong result!"); return; }
        }
        // no solve since we only measure the setup
      }
      Kernel::Clause::releaseAux();
    }
    benchmark::DoNotOptimize(count);
    benchmark::ClobberMemory();
  }
}

void bench_sat2_run(benchmark::State& state, vvector<FwSubsumptionRound> const& fw_rounds)
{
  for (auto _ : state) {
    SATSubsumptionImpl2 impl;
    int count = 0;
    for (auto const& round : fw_rounds) {
      Kernel::Clause::requestAux();
      Kernel::Clause* main_premise = round.main_premise();
      for (auto const& s : round.subsumptions()) {
        bool res = impl.setupSubsumption(s.side_premise, main_premise) && impl.solve();
        if (s.result >= 0 && res != s.result) {
          state.SkipWithError("Wrong result!");
          Kernel::Clause::releaseAux();
          return;
        }
        count += res;
      }
      for (auto const& sr : round.subsumptionResolutions()) {
        if (sr.side_premise->hasAux())
          continue;
        sr.side_premise->setAux(nullptr);  // other than original SR we only need to handle each side premise once
        bool res = impl.setupSubsumptionResolution(sr.side_premise, main_premise) && impl.solve();
        // can't check result here because the logged result might cover only one resLit.
        count += res;
      }
      Kernel::Clause::releaseAux();
    }
    benchmark::DoNotOptimize(count);
    benchmark::ClobberMemory();
  }
}

void bench_sat3_fwrun_setup(benchmark::State& state, vvector<FwSubsumptionRound> const& fw_rounds)
{
  for (auto _ : state) {
    int count = 0;  // counter to introduce data dependency which should prevent compiler optimization from removing code

    SATSubsumptionImpl3 impl;
    for (auto const& round : fw_rounds) {
      // Set up main premise
      auto token = impl.setupMainPremise(round.main_premise());
      // Test subsumptions
      for (auto const& s : round.subsumptions()) {
        if (!impl.setupSubsumption(s.side_premise)) {
          count++;
          if (s.result > 0) { state.SkipWithError("Wrong result!"); return; }
        }
        // not solving since we only measure the setup
      }
      // Test subsumption resolutions
      for (auto const& sr : round.subsumptionResolutions()) {
        if (!impl.setupSubsumptionResolution(sr.side_premise)) {
          count++;
          // can't check result here because the logged result might cover only one resLit.
          // if (sr.result > 0) { state.SkipWithError("Wrong result!"); return; }
        }
        // not solving since we only measure the setup
      }
    }
    benchmark::DoNotOptimize(count);
    benchmark::ClobberMemory();
  }
}

void bench_sat3_fwrun(benchmark::State& state, vvector<FwSubsumptionRound> const& fw_rounds)
{
  for (auto _ : state) {
    int count = 0;  // counter to introduce data dependency which should prevent compiler optimization from removing code

    SATSubsumptionImpl3 impl;

    for (auto const& round : fw_rounds) {
      // Set up main premise
      auto token = impl.setupMainPremise(round.main_premise());
      // Test subsumptions
      for (auto const& s : round.subsumptions()) {
        bool const subsumed = impl.setupSubsumption(s.side_premise) && impl.solve();
        if (s.result >= 0 && subsumed != s.result) {
          state.SkipWithError("Wrong result!");
          return;
        }
        if (subsumed) { count++; }  // NOTE: since we record subsumption log from a real fwsubsumption run, this will only happen at the last iteration.
      }
      // Test subsumption resolutions
      for (auto const& sr : round.subsumptionResolutions()) {
        bool const result = impl.setupSubsumptionResolution(sr.side_premise) && impl.solve();
        // can't check result here because the logged result might cover only one resLit.
        count += result;
      }
    }
    benchmark::DoNotOptimize(count);
    benchmark::ClobberMemory();
  }
}

void bench_orig_fwrun_setup(benchmark::State& state, vvector<FwSubsumptionRound> const& fw_rounds)
{
  for (auto _ : state) {

    int count = 0;  // counter to introduce data dependency which should prevent compiler optimization from removing code

    using namespace OriginalSubsumption;
    using CMStack = Stack<ClauseMatches*>;

    // the static variables from the original implementation
    Kernel::MLMatcher matcher;
    CMStack cmStore{64};

    for (auto const& round : fw_rounds) {
      Clause::requestAux();

      // Set up main premise
      ASS(cmStore.isEmpty());
      Kernel::Clause* cl = round.main_premise();

      LiteralMiniIndex miniIndex(cl);

      // Test subsumption setup
      for (auto const& s : round.subsumptions()) {
        Clause* mcl = s.side_premise;
        // unsigned mlen = mcl->length();

        ClauseMatches* cms = new ClauseMatches(mcl);  // NOTE: why "new" here? because the original code does it like this as well.
        cmStore.push(cms);
        cms->fillInMatches(&miniIndex);
        mcl->setAux(cms);

        if (cms->anyNonMatched()) {
          // NOT SUBSUMED
          count++;
          if (s.result > 0) { state.SkipWithError("Wrong result!"); Kernel::Clause::releaseAux(); return; }
          continue;
        }

        matcher.init(mcl, cl, cms->_matches, true);
        // nothing else to do here, since we only want to measure the setup.
      }

      // Test SR setup
      for (auto const& sr : round.subsumptionResolutions()) {
        Clause* mcl = sr.side_premise;
        ClauseMatches* cms = nullptr;
        if (mcl->hasAux()) {
          cms = mcl->getAux<ClauseMatches>();
        }
        if (!cms) {
          cms = new ClauseMatches(mcl);
          mcl->setAux(cms);
          cmStore.push(cms);
          cms->fillInMatches(&miniIndex);
        }
        if (sr.res_lit == UINT_MAX) {
          state.SkipWithError("unexpected reslit *");
          Kernel::Clause::releaseAux();
          return;
        } else {
          Literal* resLit = (*cl)[sr.res_lit];
          bool result = checkForSubsumptionResolutionSetup(matcher, cl, cms, resLit);
          // only false answers (i.e., early exits) from the setup function are correct
          if (!result && sr.result >= 0 && result != sr.result) {
            state.SkipWithError("Wrong SR result (2)!");
            Kernel::Clause::releaseAux();
            return;
          }
          if (result) { count++; }
        }
      }

      // Cleanup
      Clause::releaseAux();
      while (cmStore.isNonEmpty()) {
        delete cmStore.pop();
      }

    }
    benchmark::DoNotOptimize(count);
    benchmark::ClobberMemory();
  }
}

void bench_orig_fwrun(benchmark::State& state, vvector<FwSubsumptionRound> const& fw_rounds)
{
  for (auto _ : state) {

    int count = 0;  // counter to introduce data dependency which should prevent compiler optimization from removing code

    using namespace OriginalSubsumption;
    using CMStack = Stack<ClauseMatches*>;

    // the static variables from the original implementation
    Kernel::MLMatcher matcher;
    CMStack cmStore{64};

    for (auto const& round : fw_rounds) {
      Clause::requestAux();

      // Set up main premise
      ASS(cmStore.isEmpty());
      Kernel::Clause* cl = round.main_premise();

      LiteralMiniIndex miniIndex(cl);

      // Test subsumptions
      for (auto const& s : round.subsumptions()) {
        Clause* mcl = s.side_premise;
        // unsigned mlen = mcl->length();

        ClauseMatches* cms = new ClauseMatches(mcl);  // NOTE: why "new" here? because the original code does it like this as well.
        cmStore.push(cms);
        cms->fillInMatches(&miniIndex);
        mcl->setAux(cms);

        if (cms->anyNonMatched()) {
          // NOT SUBSUMED
          if (s.result > 0) { state.SkipWithError("Wrong result!"); Kernel::Clause::releaseAux(); return; }
          continue;
        }

        matcher.init(mcl, cl, cms->_matches, true);
        bool const subsumed = matcher.nextMatch();
        if (s.result >= 0 && subsumed != s.result) {
          state.SkipWithError("Wrong result!");
          Kernel::Clause::releaseAux();
          return;
        }
        if (subsumed) { count++; }  // NOTE: since we record subsumption log from a real fwsubsumption run, this will only happen at the last iteration anyway.
      }

      // Test subsumption resolutions
      for (auto const& sr : round.subsumptionResolutions()) {
        Clause* mcl = sr.side_premise;
        ClauseMatches* cms = nullptr;
        if (mcl->hasAux()) {
          cms = mcl->getAux<ClauseMatches>();
        }
        if (!cms) {
          cms = new ClauseMatches(mcl);
          mcl->setAux(cms);
          cmStore.push(cms);
          cms->fillInMatches(&miniIndex);
        }
        if (sr.res_lit == UINT_MAX) {
          state.SkipWithError("unexpected reslit *");
          Kernel::Clause::releaseAux();
          return;
          // ASS(mcl->hasAux());
          // for (unsigned li = 0; li < cl->length(); li++) {
          //   Literal* resLit = (*cl)[li];
          //   bool result = checkForSubsumptionResolution(cl, cms, resLit);
          //   // note: in this case the result is only logged for the first res_lit.
          //   //       however, we can't check it.
          //   //       because the clause may have been reordered due to literal selection,
          //   //       and we do not know which literal was the first one at time of logging the inference.
          //   // if (li == 0 && sr.result >= 0 && result != sr.result) {
          //   //   std::cerr << "expect " << sr.result << "  got " << result << std::endl;
          //   //   std::cerr << "     slog line: " << sr.number << "\n";
          //   //   std::cerr << "     mcl: " << mcl->toString() << "\n";
          //   //   std::cerr << "      cl: " <<  cl->toString() << "\n";
          //   //   std::cerr << "      resLit: " <<  resLit->toString() << "     index " << sr.res_lit << "\n";
          //   //   state.SkipWithError("Wrong SR result (1)!");
          //   //   Kernel::Clause::releaseAux();
          //   //   return;
          //   // }
          //   if (result) { count++; }
          // }
        } else {
          Literal* resLit = (*cl)[sr.res_lit];
          bool result = checkForSubsumptionResolution(matcher, cl, cms, resLit);
          if (sr.result >= 0 && result != sr.result) {
            // std::cerr << "expect " << sr.result << "  got " << result << std::endl;
            // std::cerr << "     slog line: " << sr.number << "\n";
            // std::cerr << "     mcl: " << mcl->toString() << "\n";
            // std::cerr << "      cl: " <<  cl->toString() << "\n";
            // std::cerr << "      resLit: " <<  resLit->toString() << "     index " << sr.res_lit << "\n";
            state.SkipWithError("Wrong SR result (2)!");
            Kernel::Clause::releaseAux();
            return;
          }
          if (result) { count++; }
        }
      }

      // Cleanup
      Clause::releaseAux();
      while (cmStore.isNonEmpty()) {
        delete cmStore.pop();
      }

    }
    benchmark::DoNotOptimize(count);
    benchmark::ClobberMemory();
  }
}


#define SAT_VS_UNSAT 0

void ProofOfConcept::benchmark_run(SubsumptionBenchmark b)
{
  CALL("ProofOfConcept::benchmark_run");
  BYPASSING_ALLOCATOR;  // google-benchmark needs its own memory

  std::cerr << "\% SATSubsumption: benchmarking " << b.subsumptions.size() << " S and " << b.subsumptionResolutions.size() << " SR" << std::endl;
#if VDEBUG
  std::cerr << "\n\n\nWARNING: compiled in debug mode!\n\n\n" << std::endl;
#endif

#if SAT_VS_UNSAT
  std::cerr << "trying to decide unknown subsumptions, if any..." << std::endl;
  for (auto& s : b.subsumptions) {
    if (s.result < 0) {
      std::cerr << "working on unknown subsumption, number= " << s.number << std::endl;
      std::cerr << "    main premise: " << s.main_premise->toString() << std::endl;
      std::cerr << "    side premise: " << s.side_premise->toString() << std::endl;
      SATSubsumptionImpl3 impl;
      auto token = impl.setupMainPremise(s.main_premise);
      bool const subsumed = impl.setupSubsumption(s.side_premise) && impl.solve();
      std::cerr << "    subsumed? " << subsumed << std::endl;
      s.result = subsumed;
    }
  }

  long n_total = 0;
  long n_sat = 0;
  long n_unsat = 0;
  long n_unknown = 0;
  for (auto const& s : b.subsumptions) {
    n_total += 1;
    if (s.result == 0) {
      n_unsat += 1;
    } else if (s.result == 1) {
      n_sat += 1;
    } else if (s.result < 0) {
      n_unknown += 1;
    } else {
      n_sat = std::numeric_limits<long>::min();
      n_unsat = std::numeric_limits<long>::min();
      n_unknown = std::numeric_limits<long>::min();
      std::cerr << "got unexpected result" << std::endl;
    }
  }
  std::cerr << "Subsumption SAT vs. UNSAT, updated: total= " << n_total << " sat= " << n_sat << " unsat= " << n_unsat << " unknown= " << n_unknown << std::endl;
#endif

  vvector<FwSubsumptionRound> fw_rounds;
  for (size_t round = 0; round <= b.rounds.size(); ++round) {
    fw_rounds.emplace_back(b, round);
  }
  // pop empty round at the end
  if (!fw_rounds.back().main_premise()) {
    fw_rounds.pop_back();
  }
  // all remaining ones should be non-empty
  ASS(std::all_of(fw_rounds.begin(), fw_rounds.end(), [](auto const& r) { return !!r.main_premise(); }));

  vvector<FwSubsumptionRound> fw_rounds_only_subsumption;
  for (auto const& r : fw_rounds) {
    fw_rounds_only_subsumption.push_back(r.withoutSubsumptionResolution());
  }

#if SAT_VS_UNSAT
  SubsumptionBenchmark b_sat = b;
  filter_benchmark(b_sat,
    [](SubsumptionInstance const& s) {
      ASS(s.result >= 0 && s.result <= 1);
      return s.result == 1;
    },
    [](SubsumptionResolutionInstance const& sr) {
      return false;
    });
  vvector<FwSubsumptionRound> fw_rounds_sat;
  for (size_t round = 0; round <= b.rounds.size(); ++round) {
    fw_rounds_sat.emplace_back(b_sat, round);
    if (!fw_rounds_sat.back().main_premise()) {
      fw_rounds_sat.pop_back();
    }
  }

  SubsumptionBenchmark b_unsat = b;
  filter_benchmark(b_unsat,
    [](SubsumptionInstance const& s) {
      ASS(s.result >= 0 && s.result <= 1);
      return s.result == 0;
    },
    [](SubsumptionResolutionInstance const& sr) {
      return false;
    });
  vvector<FwSubsumptionRound> fw_rounds_unsat;
  for (size_t round = 0; round <= b.rounds.size(); ++round) {
    fw_rounds_unsat.emplace_back(b_unsat, round);
    if (!fw_rounds_unsat.back().main_premise()) {
      fw_rounds_unsat.pop_back();
    }
  }

  ASS(b_sat.subsumptions.size() + b_unsat.subsumptions.size() == b.subsumptions.size());
  ASS(b_sat.subsumptionResolutions.size() == 0);
  ASS(b_unsat.subsumptionResolutions.size() == 0);
#endif

  vvector<vstring> args = {
    "vampire-sbench-run",
    // "--help",
  };

  bool also_setup = true;

  // if (also_setup)
  //   benchmark::RegisterBenchmark("sat2 S    (setup)", bench_sat2_run_setup, fw_rounds_only_subsumption);
  // benchmark::RegisterBenchmark(  "sat2 S    (full)", bench_sat2_run, fw_rounds_only_subsumption);
  // if (also_setup)
  //   benchmark::RegisterBenchmark("sat2 S+SR (setup)", bench_sat2_run_setup, fw_rounds);
  // benchmark::RegisterBenchmark(  "sat2 S+SR (full)", bench_sat2_run, fw_rounds);

  if (also_setup)
    benchmark::RegisterBenchmark("sat3 S    (setup)", bench_sat3_fwrun_setup, fw_rounds_only_subsumption);
  benchmark::RegisterBenchmark(  "sat3 S    (full)", bench_sat3_fwrun, fw_rounds_only_subsumption);
  // if (also_setup)
  //   benchmark::RegisterBenchmark("sat3 S+SR (setup)", bench_sat3_fwrun_setup, fw_rounds);
  // benchmark::RegisterBenchmark(  "sat3 S+SR (full)", bench_sat3_fwrun, fw_rounds);

  if (also_setup)
    benchmark::RegisterBenchmark("orig S    (setup)", bench_orig_fwrun_setup, fw_rounds_only_subsumption);
  benchmark::RegisterBenchmark(  "orig S    (full)", bench_orig_fwrun, fw_rounds_only_subsumption);
  // if (also_setup)
  //   benchmark::RegisterBenchmark("orig S+SR (setup)", bench_orig_fwrun_setup, fw_rounds);
  // benchmark::RegisterBenchmark(  "orig S+SR (full)", bench_orig_fwrun, fw_rounds);

#if SAT_VS_UNSAT
  if (also_setup)
    benchmark::RegisterBenchmark("sat3 S    (setup) sat", bench_sat3_fwrun_setup, fw_rounds_sat);
  benchmark::RegisterBenchmark(  "sat3 S    (full)  sat", bench_sat3_fwrun, fw_rounds_sat);
  if (also_setup)
    benchmark::RegisterBenchmark("orig S    (setup) sat", bench_orig_fwrun_setup, fw_rounds_sat);
  benchmark::RegisterBenchmark(  "orig S    (full)  sat", bench_orig_fwrun, fw_rounds_sat);
  if (also_setup)
    benchmark::RegisterBenchmark("sat3 S    (setup) unsat", bench_sat3_fwrun_setup, fw_rounds_unsat);
  benchmark::RegisterBenchmark(  "sat3 S    (full)  unsat", bench_sat3_fwrun, fw_rounds_unsat);
  if (also_setup)
    benchmark::RegisterBenchmark("orig S    (setup) unsat", bench_orig_fwrun_setup, fw_rounds_unsat);
  benchmark::RegisterBenchmark(  "orig S    (full)  unsat", bench_orig_fwrun, fw_rounds_unsat);
#endif

  init_benchmark(std::move(args));
  benchmark::RunSpecifiedBenchmarks();
  std::cerr << "Benchmarking done, shutting down..." << std::endl;
  Kernel::Clause::requestAux();
  Kernel::Clause::releaseAux();
}

#else

void ProofOfConcept::benchmark_run(vvector<SubsumptionInstance> instances)
{
  CALL("ProofOfConcept::benchmark_run");
  USER_ERROR("compiled without benchmarking!");
}

#endif  // ENABLE_BENCHMARK


// Example commutativity:
// side: f(x) = y
// main: f(d) = f(e)
// possible matchings:
// - x->d, y->f(e)
// - x->e, y->f(d)

// Example by Bernhard re. problematic subsumption demodulation:
// side: x1=x2 or x3=x4 or x5=x6 or x7=x8
// main: x9=x10 or x11=x12 or x13=14 or P(t)


void ProofOfConcept::benchmark_micro(SubsumptionBenchmark b)
{
  CALL("ProofOfConcept::benchmark_micro");
  BYPASSING_ALLOCATOR;  // google-benchmark needs its own memory
  std::cerr << "obsolete mode\n";
  std::exit(1);
}