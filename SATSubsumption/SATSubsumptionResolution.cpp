#include "SATSubsumptionResolution.hpp"
#include "Kernel/Matcher.hpp"
#include "Debug/RuntimeStatistics.hpp"
#include "Util.hpp"
#include <iostream>

using namespace Indexing;
using namespace Kernel;
using namespace SMTSubsumption;

#define SAT_SR_IMPL 2

/**
 * Updates the first set to hold only the matches that are also in the second set.
 * Assumes that the sets are sorted.
 */
static void intersect(vvector<unsigned> &first, vvector<unsigned> &second)
{
  #ifndef NDEBUG
  for (unsigned i = 1; i < first.size(); i++) {
    ASS(first[i-1] <= first[i]);
  }
  for (unsigned i = 1; i < second.size(); i++) {
    ASS(second[i-1] <= second[i]);
  }
  #endif
  vvector<unsigned> result;
  result.reserve(first.size());
  unsigned i = 0;
  unsigned j = 0;
  while (i < first.size() && j < second.size()) {
    if (first[i] == second[j]) {
      result.push_back(first[i]);
      i++;
      j++;
    } else if (first[i] < second[j]) {
      i++;
    } else {
      j++;
    }
  }
  first.swap(result);
}

/****************************************************************************/
/*                       SATSubsumption::MatchSet                           */
/****************************************************************************/
SATSubsumption::MatchSet::MatchSet(unsigned nBaseLits, unsigned nInstanceLits)
    : _iMatches(nBaseLits),
      _jMatches(nInstanceLits),
      _iStates(nBaseLits / 4 + 1, 0),
      _jStates(nInstanceLits / 4 + 1, 0),
      _m(nBaseLits),
      _n(nInstanceLits),
      _varToMatch(),
      _nUsedMatches(0),
      _nAllocatedMatches(1),
      _allocatedMatches(nullptr)
{
  ASS(nBaseLits > 0);
  _allocatedMatches = (Match**) malloc(_nAllocatedMatches * sizeof(Match*));
  _allocatedMatches[0] = (Match*) malloc(sizeof(Match));
}

SATSubsumption::MatchSet::~MatchSet()
{
  for (unsigned i = 1; i < _nAllocatedMatches; i *= 2) {
    free(_allocatedMatches[i]);
  }
  free(_allocatedMatches);
}

SATSubsumption::Match *SATSubsumption::MatchSet::allocateMatch()
{
  ASS(_nUsedMatches <= _nAllocatedMatches);
  if (_nUsedMatches == _nAllocatedMatches) {
    _nAllocatedMatches *= 2;
    _allocatedMatches = (Match **) realloc(_allocatedMatches, _nAllocatedMatches * sizeof(Match*));

    Match* newMatches = (Match*) malloc((_nAllocatedMatches - _nUsedMatches) * sizeof(Match));
    for (unsigned i = 0; i < _nAllocatedMatches - _nUsedMatches; i++) {
      _allocatedMatches[_nUsedMatches + i] = newMatches + i;
    }
  }
  return _allocatedMatches[_nUsedMatches++];
}

void SATSubsumption::MatchSet::resize(unsigned nBaseLits, unsigned nInstanceLits)
{
  ASS(nBaseLits > 0);
  ASS(nInstanceLits > 0);
  ASS(nBaseLits >= _m);
  ASS(nInstanceLits >= _n);
  if (nBaseLits > _iMatches.size()) {
    _iMatches.resize(nBaseLits);
  }
  if (nInstanceLits > _iMatches.size()) {
    _jMatches.resize(nInstanceLits);
  }
  _m = nBaseLits;
  _n = nInstanceLits;

  if (nBaseLits / 4 + 1 > _iStates.size()) {
    _iStates.resize(nBaseLits / 4 + 1, 0);
  }
  if (nInstanceLits / 4 + 1 > _jStates.size()) {
    _jStates.resize(nInstanceLits / 4 + 1, 0);
  }
}

SATSubsumption::Match *SATSubsumption::MatchSet::addMatch(unsigned i, unsigned j, bool polarity, subsat::Var var)
{
  ASS(i < _m);
  ASS(j < _n);

  Match* match = allocateMatch();
  *match = Match(i, j, polarity, var);
  _iMatches[i].push_back(match);
  _jMatches[j].push_back(match);
  unsigned index = match->_var.index();
  if (_varToMatch.size() <= index) {
    _varToMatch.resize(index * 2 + 1);
  }
  _varToMatch[index] = match;
  // update the match state
  // the wizardry is explained in the header file
  if (match->_polarity) {
    _iStates[i / 4] |= 1 << (2 * (i % 4));
    _jStates[j / 4] |= 1 << (2 * (j % 4));
  } else {
    _iStates[i / 4] |= 1 << (2 * (i % 4) + 1);
    _jStates[j / 4] |= 1 << (2 * (j % 4) + 1);
  }
  return match;
}

vvector<SATSubsumption::Match *> &SATSubsumption::MatchSet::getIMatches(unsigned baseLitIndex)
{
  ASS(baseLitIndex < _m);
  return _iMatches[baseLitIndex];
}

vvector<SATSubsumption::Match *> &SATSubsumption::MatchSet::getJMatches(unsigned instanceLitIndex)
{
  ASS(instanceLitIndex < _n);
  return _jMatches[instanceLitIndex];
}

vvector<SATSubsumption::Match *> SATSubsumption::MatchSet::getAllMatches()
{
  vvector<SATSubsumption::Match*> result(_allocatedMatches, _allocatedMatches + _nUsedMatches);
  return result;
}

SATSubsumption::Match *SATSubsumption::MatchSet::getMatchForVar(subsat::Var satVar)
{

  if (satVar.index() >= _nUsedMatches) {
    return nullptr;
  }
  Match* match = _varToMatch[satVar.index()];
  ASS(match != nullptr);
  return match;
}

bool SATSubsumption::MatchSet::hasPositiveMatchI(unsigned i) {
  // the wizardry is explained in the header file
  return (_iStates[i / 4] & (1 << (2 * (i % 4)))) != 0;
}

bool SATSubsumption::MatchSet::hasNegativeMatchI(unsigned i) {
  // the wizardry is explained in the header file
  return (_iStates[i / 4] & (2 << (2 * (i % 4)))) != 0;
}

bool SATSubsumption::MatchSet::hasPositiveMatchJ(unsigned j) {
  // the wizardry is explained in the header file
  return (_jStates[j / 4] & (1 << (2 * (j % 4)))) != 0;
}

bool SATSubsumption::MatchSet::hasNegativeMatchJ(unsigned j) {
  // the wizardry is explained in the header file
  return (_jStates[j / 4] & (2 << (2 * (j % 4)))) != 0;
}

void SATSubsumption::MatchSet::clear()
{
  for (unsigned i = 0; i < _m; i++) {
    _iMatches[i].clear();
  }
  for (unsigned j = 0; j < _n; j++) {
    _jMatches[j].clear();
  }
  _nUsedMatches = 0;
  _varToMatch.clear();

  fill(_iStates.begin(), _iStates.end(), 0);
  fill(_jStates.begin(), _jStates.end(), 0);
}

/****************************************************************************/
/*                    SATSubsumption::SATSubsumption                        */
/****************************************************************************/

SATSubsumption::SATSubsumption() :
  _L(nullptr),
  _M(nullptr),
  _m(0),
  _n(0),
  _solver(new SolverWrapper()),
  _bindingsManager(new BindingsManager()),
  _atMostOneVars(),
  _matchSet(1, 1),
  _model()
{
  // nothing to do
}

SATSubsumption::~SATSubsumption()
{
  delete _bindingsManager;
}

void SATSubsumption::addBinding(BindingsManager::Binder *binder, unsigned varNumber, unsigned i, unsigned j, bool polarity)
{
  subsat::Var satVar = _solver->s.new_variable(varNumber);
  _matchSet.addMatch(i, j, polarity, satVar);
  _bindingsManager->commit_bindings(*binder, satVar, i, j);
}

unsigned SATSubsumption::fillMatches()
{
  CALL("SATSubsumption::fillMatches");
  ASS(_L);
  ASS(_M);
  ASS(_m > 0);
  ASS(_n > 0);
  ASS(_matchSet._m == _m);
  ASS(_matchSet._n == _n);
  ASS(_bindingsManager);

  _solver->s.clear();

  _matchSet.resize(_m, _n);
  _matchSet.clear();

  // number of matches found - is equal to the number of variables in the SAT solver
  unsigned nMatches = 0;
  // used ot check that the SR is even possible
  // the intersection represents the set of M_j that have been matched only negatively by some L_i
  vvector<unsigned> intersection;

  // for some L_i, stores the set of M_j that have been negatively matched by L_i
  vvector<unsigned> negativeMatches = vvector<unsigned>(_n);

  bool hasNegativeMatch = false;

  unsigned lastHeader = numeric_limits<unsigned>::max();
  for (unsigned i = 0; i < _m; ++i)
  {
    Literal* baseLit = _L->literals()[i];
    Literal* baseLitNeg = Literal::complementaryLiteral(baseLit);
    bool foundPositiveMatch = false;

    negativeMatches.clear();
    for (unsigned j = 0; j < _n; ++j)
    {
      Literal* instLit = _M->literals()[j];
      // very fast check that can discard most substitutions
      if (!Literal::headersMatch(baseLit, instLit, false)
       && !Literal::headersMatch(baseLitNeg, instLit, false)) {
        continue;
      }

      if(baseLit->polarity() == instLit->polarity())
      {
        // Search for positive literal matches
        {
          auto binder = _bindingsManager->start_binder();
          if (baseLit->arity() == 0 || MatchingUtils::matchArgs(baseLit, instLit, binder)) {
            addBinding(&binder, ++nMatches, i, j, true);
            foundPositiveMatch = true;
          }
        }
        // In case of commutative predicates such as equality, we also need to check the other way around
        if (baseLit->commutative()) {
          auto binder = _bindingsManager->start_binder();
          if (MatchingUtils::matchReversedArgs(baseLit, instLit, binder)) {
            addBinding(&binder, ++nMatches, i, j, true);
            foundPositiveMatch = true;
          }
        }
      } // end of positive literal match
      else {
        // Search for negative literal matches
        {
          auto binder = _bindingsManager->start_binder();
          if (baseLitNeg->arity() == 0 || MatchingUtils::matchArgs(baseLitNeg, instLit, binder)) {
            addBinding(&binder, ++nMatches, i, j, false);
            negativeMatches.push_back(j);
            hasNegativeMatch = true;
          }
        }
        // In case of commutative predicates such as equality, we also need to check the other way around
        if (baseLitNeg->commutative()) {
          auto binder = _bindingsManager->start_binder();
          if (MatchingUtils::matchReversedArgs(baseLitNeg, instLit, binder)) {
            addBinding(&binder, ++nMatches, i, j, false);
            if (negativeMatches[negativeMatches.size() - 1] != j) {
              negativeMatches.push_back(j);
              hasNegativeMatch = true;
            }
          }
        }
      } // end of negative literal matches
    } // for (unsigned j = 0; j < _nInstanceLits; ++j)

    if (!foundPositiveMatch) {
      // If the headers are different, then the SR is impossible
      if (lastHeader == numeric_limits<unsigned>::max()) {
        lastHeader = baseLit->header();
        // set up the first intersection
        if (negativeMatches.empty()) {
          return 0;
        }
        intersection = vvector<unsigned>(negativeMatches);
        continue;
      } else if (lastHeader != baseLit->header()) {
        return 0;
      }
      if (!_matchSet.hasNegativeMatchI(i)) {
        // If there is no negative match for L_i, then the SR is impossible
        // Would be checked by next condition, but this one is faster
        return 0;
      }

      intersect(intersection, negativeMatches);
      if (intersection.empty()) {
        // It is impossible to find a SR because some L_i have no possible match
        return 0;
      }
    } // if (!foundPositiveMatch)
  } // for (unsigned i = 0; i < _nBaseLits; ++i)

  if (!hasNegativeMatch) {
    // If there are no negative matches, then the SR is not possible
    return 0;
  }

  return nMatches;
} // SATSubsumption::fillMatches()

#if SAT_SR_IMPL == 1
/**
 * First encoding of the sat subsumption resolution
 *
 * @pre assumes that base and instance literals are set
 *
 * Let the base clause be L and the instance clause be M.
 *  - We name the literals in L as L1, L2, ..., Ln
 *  - We name the literals in M as M1, M2, ..., Mk
 *
 * We introduce the variables
 *  - b_ij+ if L_i has a substitution S such that S(L_i) = M_j
 *    we will say that b_ij+ as a positive polarity
 *  - b_ij- if L_i has a substitution S such that S(L_i) = ~M_j
 *    we will say that b_ij- as a negative polarity
 *  - c_j if M_j is matched by at least one negative polarity variable b_ij-
 * b_ij+ and b_ij- are mutually existentially exclusive, therefore, we only introduce one variable b_ij for the sat solver, and remember its polarity.
 * It may be that both b_ij+ and b_ij- do not exist.
 *
 *
 * We introduce the following subsumption resolution clauses clauses:
 *  - At least one negative polarity match exists
 *    c_1 V c_2 V ... V c_k
 *  - At most one M_j is matched by a negative polarity variable
 *    At most one c_j is true (embeded in the sat solver)
 *    (~c_1 V ~c_2) /\ (~c_1 V ~c_3) /\ ... /\ (~c_2 V ~c_3) /\ ...
 *  - Each L_i is matched by at least one M_j
 *    b_11 V ... V b_1k /\ b_21 V ... V b_2k /\ ... /\ b_n1 V ... V b_nk
 *  - c_j is true if and only if there is a negative polarity match b_ij-
 *    for each j : c_j <=> b_ij- for some i
 *    for each j :(~c_j V b_1j V ... V b_nj)
 *             /\ (c_j V ~b_1j) /\ ... /\ (c_j V ~bnj)
 *  - b_ij implies a certain substitution is valid
 *    for each i, j : b_ij => (S(L_i) = M_j V S(L_i) = ~M_j)
 *
 */
bool SATSubsumption::setupSubsumptionResolution()
{
  CALL("SATSubsumptionResolution::setupSubsumptionResolution");
  ASS(_L);
  ASS(_M);
  ASS(_matchSet.getAllMatches().size() == 0)
  ASS(_solver->s.empty());
  ASS(_solver->s.theory().empty());
  // Checks for all the literal mapings and store them in a cache
  // nBVar is the number of variables that are of the form b_ij
  unsigned nBVar = fillMatches();
  if (nBVar == 0) {
    return false;
  }

  // -> b_ij implies a certain substitution is valid
  //    for each i, j : b_ij => (S(L_i) = M_j V S(L_i) = ~M_j)
  // These constraints are created in the fillMatches() function by filling the _bindingsManager
  Solver& solver = _solver->s;
  solver.theory().setBindings(_bindingsManager);

  // Set up the _atMostOneVars vectors to store the c_j
  if(_atMostOneVars.capacity() < _n) {
    _atMostOneVars.reserve(_n);
  }
  _atMostOneVars.clear();
  // Create the set of clauses

  // -> At least one negative polarity match exists
  //    c_1 V c_2 V ... V c_k
  cout << "----- c_1 V c_2 V ... V c_k" << endl;
  solver.constraint_start();
  string s = "";
  for (unsigned j = 0; j < _n; ++j) {
    // Do not add useless variables in the solver
    // It would compromise correctness since a c_j without binding coud be true and satisfy c_1 V ... V c_k without making any other clause false.
    if(_matchSet.hasNegativeMatchJ(j)) {
      subsat::Var c_j = solver.new_variable(++nBVar);
      _atMostOneVars.push_back(pair<unsigned, subsat::Var>(j, c_j));
      s += "c" + to_string(c_j.index()) + " V ";
      solver.constraint_push_literal(c_j);
    }
  }
  // remove the last " V "
  s = s.substr(0, s.size() - 3);
  cout << s << endl;
  auto build = solver.constraint_end();
  // add the c_1 V ... V c_k clause
  solver.add_clause_unsafe(build);
  // -> At most one M_j is matched by a negative polarity variable
  //    At most one c_j is true (embeded in the sat solver)
  solver.add_atmostone_constraint_unsafe(build);
  // -> Each L_i is matched by at least one M_j
  //    b_11 V ... V b_1k
  // /\ b_21 V ... V b_2k
  // /\ ...
  // /\ b_n1 V ... V b_nk
  cout << "----- b_11 V ... V b_1k /\\ ... /\\ b_n1 V ... V b_nk" << endl;
  for (unsigned i = 0; i < _m; ++i) {
    solver.constraint_start();
    vvector<Match*> &matches = _matchSet.getIMatches(i);
    for (Match *match : matches) {
      cout << match->_var;
      if (match != matches.back()) {
        cout << " V ";
      }
      solver.constraint_push_literal(match->_var);
    }
    cout << endl;
    auto build = solver.constraint_end();
    solver.add_clause_unsafe(build);
  }

  // -> c_j is true if and only if there is a negative polarity match b_ij-
  //    for each j : c_j <=> b_ij- for some i
  //    for each j :(~c_j V b_1j- V ... V b_nj-)
  //            /\ (c_j V ~b_1j-) /\ ... /\ (c_j V ~b_nj-)
  cout << "----- c_j <=> b_ij-" << endl;
  for (auto &pair : _atMostOneVars) {
    unsigned j = pair.first;
    vvector<Match*> &matches = _matchSet.getJMatches(j);
    subsat::Var c_j = pair.second;
    solver.constraint_start();
    //   (~c_j V b_1j- V ... V b_nj-)
    cout << "----- (~c_j V b_1j- V ... V b_nj-)" << endl;
    cout << ~c_j;
    solver.constraint_push_literal(~c_j);
    for (Match* match : matches) {
      if (!match->_polarity) {
        cout << " V " << match->_var;
        solver.constraint_push_literal(match->_var);
      }
    }
    cout << endl;
    auto build = solver.constraint_end();
    solver.add_clause_unsafe(build);
    //   (c_j V ~b_1j-) /\ ... /\ (c_j V ~b_nj-)
    cout << "----- (c_j V ~b_1j-) /\\ ... /\\ (c_j V ~b_nj-)" << endl;
    for (Match* match : matches) {
      if (!match->_polarity) {
        solver.constraint_start();
        cout << c_j << " V " << ~match->_var << endl;
        solver.constraint_push_literal(c_j);
        solver.constraint_push_literal(~match->_var);
        auto build = solver.constraint_end();
        solver.add_clause_unsafe(build);
      }
    }
    cout << endl;
  }
  return true;
} // setupSubsumptionResolution

#else // SAT_SR_IMPL == 2
/**
 * Second encoding of the sat subsumption resolution
 *
 * @pre assumes that base and instance literals are set, the match set is empty and the solver is empty
 *
 * Let the base clause be L and the instance clause be M.
 *  - We name the literals in L as L1, L2, ..., Ln
 *  - We name the literals in M as M1, M2, ..., Mk
 *
 * We introduce the variables
 *  - b_ij+ if L_i has a substitution S such that S(L_i) = M_j
 *    we will say that b_ij+ as a positive polarity
 *  - b_ij- if L_i has a substitution S such that S(L_i) = ~M_j
 *    we will say that b_ij- as a negative polarity
 * b_ij+ and b_ij- are mutually existentially exclusive, therefore, we only introduce one variable b_ij for the sat solver, and remember its polarity.
 * It may be that both b_ij+ and b_ij- do not exist.
 *
 *
 * We introduce the following subsumption resolution clauses clauses:
 *  - At least one negative polarity match exists
 *    b_11- V ... V b_1m- V ... V b_n1 V ... V b_nm-
 *  - Each L_i is matched by at least one M_j
 *    b_11 V ... V b_1k /\ b_21 V ... V b_2k /\ ... /\ b_n1 V ... V b_nk
 *  - At most one M_j is matched by a negative polarity variable
 *    for each j : b_1j- V ... V b_nj- => ~b_ij', j' != j
 *  - b_ij implies a certain substitution is valid
 *    for each i, j : b_ij => (S(L_i) = M_j V S(L_i) = ~M_j)
 *
 */
bool SATSubsumption::setupSubsumptionResolution()
{
  CALL("SATSubsumptionResolution::setupSubsumptionResolution");
  ASS(_L);
  ASS(_M);
  ASS(_matchSet.getAllMatches().size() == 0)
  ASS(_solver->s.empty());
  ASS(_solver->s.theory().empty());

  // Checks for all the literal mapings and store them in a cache
  // nBVar is the number of variables that are of the form b_ij
  unsigned nBVar = fillMatches();
  if (nBVar == 0) {
    return false;
  }

  // -> b_ij implies a certain substitution is valid
  //    for each i, j : b_ij => (S(L_i) = M_j V S(L_i) = ~M_j)
  // These constraints are created in the fillMatches() function by filling the _bindingsManager
  Solver& solver = _solver->s;
  solver.theory().setBindings(_bindingsManager);

  // -> At least one negative polarity match exists
  //    b_11- V ... V b_1m- V ... V b_n1 V ... V b_nm-
  vvector<Match*> allMatches = _matchSet.getAllMatches();
  solver.constraint_start();
  for (Match* match : allMatches) {
    if (!match->_polarity) {
      solver.constraint_push_literal(match->_var);
    }
  }
  auto build = solver.constraint_end();
  solver.add_clause_unsafe(build);

  // -> Each L_i is matched by at least one M_j
  //    b_11 V ... V b_1k
  // /\ b_21 V ... V b_2k
  // /\ ...
  // /\ b_n1 V ... V b_nk
  for (unsigned i=0; i<_m; i++) {
    solver.constraint_start();
    string s = "";
    for (Match* match : _matchSet.getIMatches(i)) {
      solver.constraint_push_literal(match->_var);
      s += to_string(match->_var.index()) + " V ";
    }
    s = s.substr(0, s.size() - 3);
    cout << s << endl;
    auto build = solver.constraint_end();
    solver.add_clause_unsafe(build);
  }

  // -> At most one M_j is matched by a negative polarity variable
  //    for each j : b_1j- V ... V b_nj- => ~b_ij', j' != j
  for(unsigned it1=0; it1<allMatches.size(); it1++) {
    Match* match1 = allMatches[it1];
    if(match1->_polarity) {
      continue;
    }
    for(unsigned it2=it1+1; it2<allMatches.size(); it2++) {
      Match* match2 = allMatches[it2];
      if (match2->_polarity
       || match1->_j == match2->_j) {
        continue;
      }
      solver.constraint_start();
      solver.constraint_push_literal(~match1->_var);
      solver.constraint_push_literal(~match1->_var);
      auto build = solver.constraint_end();
      solver.add_clause_unsafe(build);
      cout << ~match1->_var << " V " << ~match2->_var << endl;
    }
  }
  return true;
} // setupSubsumptionResolution

#endif

/**
 * Creates a new clause that is the result of the subsumption resolution
 * of the base and instance clauses.
 *
 * This process is achieved using the output of the sat solver. Search for the negative polarity match and remove the M_j from the clause.
 *
 * Assumes that the solver has already been called and that the model is already stored in _model.
 */
Kernel::Clause *SATSubsumption::generateConclusion()
{
  CALL("SATSubsumptionResolution::generateConclusion");
  ASS(_L);
  ASS(_M);
  ASS(_m > 0);
  ASS(_n > 0);
  ASS(_matchSet._m == _m);
  ASS(_matchSet._n == _n);
  ASS(_bindingsManager);
  ASS(_model.size() > 0);

  // Provided the solution of the sat solver, we can create the conclusion clause
  #ifndef NDEBUG
  unsigned j = 0;
  // Check that there is only one negative polarity match to j inside the model
  for (subsat::Lit lit : _model) {
    if (lit.is_positive()) {
      Match *match = _matchSet.getMatchForVar(lit.var());
      // matches can be null if the variable is a c_j
      if (match && !match->_polarity) {
        if(j == 0) {
          j = match->_j;
        } else {
          ASS(j == match->_j);
        }
      }
    }
  }
  #endif
  unsigned toRemove = numeric_limits<unsigned>::max();
  // find the negative polarity match to j inside the model
  for (subsat::Lit lit : _model) {
    if (lit.is_positive()) {
      Match *match = _matchSet.getMatchForVar(lit.var());
      // matches can be null if the variable is a c_j
      if (match && !match->_polarity) {
        toRemove = match->_j;
        break;
      }
    }
  }
  ASS(toRemove != numeric_limits<unsigned>::max());
  // Create the conclusion clause
  Kernel::Clause *conclusion = new (_n - 1) Kernel::Clause(_n - 1, SimplifyingInference2(InferenceRule::SUBSUMPTION_RESOLUTION, _M, _L));
  unsigned k = 0;
  for (unsigned j = 0; j < _n; ++j) {
    if (j != toRemove) {
      conclusion->literals()[k] = _M->literals()[j];
      k++;
    }
  }
  ASS(k == _n - 1);
  cout << "Solution : " << conclusion->toString() << endl;
  return conclusion;
}

Kernel::Clause* SATSubsumption::checkSubsumptionResolution(Kernel::Clause *L, Kernel::Clause *M)
{
  cout << "----------------- CHECK SUBSUMPTION RESOLUTION -----------------" << endl;
  CALL("SATSubsumptionResolution::checkSubsumptionResolution");
  if(M->length() < L->length()) {
    return nullptr;
  }

  for (unsigned i=0; i < L->length(); i++) {
    cout << L->literals()[i]->toString();
    if(i != L->length() - 1) {
      cout << " V ";
    }
  }
  cout << "  /\\  ";
  for (unsigned j=0; j < M->length(); j++) {
    cout << M->literals()[j]->toString();
    if(j != M->length() - 1) {
      cout << " V ";
    }
  }
  cout << endl;

  // Set up the problem
  _L = L;
  _M = M;
  _m = L->length();
  _n = M->length();
  if(_bindingsManager) {
    delete _bindingsManager;
  }
  _bindingsManager = new BindingsManager();

  _matchSet.clear();
  _matchSet.resize(_m, _n);
  _solver->s.clear();

  // First set up the clauses
  if (!setupSubsumptionResolution()) {
    return nullptr;
  }

  // Solve the SAT problem
  Solver& solver = _solver->s;
  cout << "Solving" << endl;
  if(solver.solve() != subsat::Result::Sat) {
    cout << "Unsat" << endl;
    return nullptr;
  }
  _model.clear();
  solver.get_model(_model);

  // If the problem is SAT, then generate the conclusion clause
  return generateConclusion();
}
