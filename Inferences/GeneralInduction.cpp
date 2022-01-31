/*
 * This file is part of the source code of the software program
 * Vampire. It is protected by applicable
 * copyright laws.
 *
 * This source code is distributed under the licence found here
 * https://vprover.github.io/license.html
 * and in the source directory
 */
/**
 * @file GeneralInduction.cpp
 * Implements class GeneralInduction.
 */

#include "Kernel/TermIterators.hpp"
#include "Kernel/Signature.hpp"
#include "Kernel/Unit.hpp"
#include "Kernel/Inference.hpp"
#include "Kernel/Formula.hpp"
#include "Kernel/FormulaUnit.hpp"
#include "Kernel/FormulaVarIterator.hpp"
#include "Kernel/RobSubstitution.hpp"

#include "Saturation/SaturationAlgorithm.hpp"
#include "Saturation/Splitter.hpp"

#include "Shell/Statistics.hpp"
#include "Shell/NewCNF.hpp"
#include "Shell/NNF.hpp"
#include "Shell/Rectify.hpp"

#include "Inferences/BinaryResolution.hpp"
#include "Inferences/InductionHelper.hpp"

#include "GeneralInduction.hpp"

namespace Inferences
{

using namespace Kernel;
using namespace Lib;
using namespace Shell;

TermList TermOccurrenceReplacement::transformSubterm(TermList trm)
{
  CALL("TermOccurrenceReplacement::transformSubterm");

  if (trm.isVar()) {
    return trm;
  }
  auto rIt = _r.find(trm.term());
  if (rIt != _r.end()) {
    auto oIt = _o._m.find(make_pair(_lit, trm.term()));
    ASS(oIt != _o._m.end());
    // if current bit is one, replace
    if (oIt->second.pop_last()) {
      return TermList(rIt->second, false);
    }
  }
  return trm;
}

TermList TermMapReplacement::transformSubterm(TermList trm)
{
  CALL("TermMapReplacement::transformSubterm");

  if (trm.isVar()) {
    return trm;
  }
  auto t = trm.term();
  ASS(!t->isLiteral());
  auto rIt = _r.find(t);
  if (rIt != _r.end()) {
    // if term needs to be replaced, get its sort and map it
    // to the next replacement term within that sort
    TermList srt = env.signature->getFunction(t->functor())->fnType()->result();
    auto oIt = _ord.find(t);
    if (oIt == _ord.end()) {
      oIt = _ord.insert(make_pair(t, _curr.at(srt)++)).first;
    }
    return TermList(_m.get(srt)[oIt->second]);
  }
  return trm;
}

ClauseIterator GeneralInduction::generateClauses(Clause* premise)
{
  CALL("GeneralInduction::generateClauses");

  InductionClauseIterator res;
  if (InductionHelper::isInductionClause(premise)) {
    for (unsigned i = 0; i < premise->length(); i++) {
      process(res, premise, (*premise)[i]);
    }
  }

  return pvi(res);
}

void GeneralInduction::process(InductionClauseIterator& res, Clause* premise, Literal* literal)
{
  CALL("GeneralInduction::process");

  if(env.options->showInduction()){
    env.beginOutput();
    env.out() << "[Induction] process " << *literal << " in " << *premise << endl;
    env.endOutput();
  }

  auto pairs = selectMainSidePairs(literal, premise);

  for (auto& gen : _gen) {
    for (const auto& kv : pairs) {
      const auto& main = kv.first;
      const auto& sides = kv.second;
      static vvector<pair<InductionScheme, OccurrenceMap>> schOccMap;
      schOccMap.clear();
      gen->generate(main, sides, schOccMap);

      vvector<pair<Literal*, vset<Literal*>>> schLits;
      for (auto& kv : schOccMap) {
        // Retain side literals for further processing if:
        // (1) they contain some induction term from the current scheme
        // (2) they have either induction depth 0 or they contain some complex induction term
        //     (this has been partly checked in selectMainSidePairs but there we did not know
        //      yet whether there is a complex induction term)
        vset<pair<Literal*, Clause*>> sidesFiltered;
        for (const auto& s : sides) {
          bool filtered = true;
          for (const auto& kv2 : kv.first.inductionTerms()) {
            if (s.first->containsSubterm(TermList(kv2.first)) && (!skolem(kv2.first) || !s.second->inference().inductionDepth())) {
              sidesFiltered.insert(s);
              filtered = false;
              break;
            }
          }
          // update occurrence map
          if (filtered) {
            for (auto it = kv.second._m.begin(); it != kv.second._m.end();) {
              if (it->first.first == s.first) {
                it = kv.second._m.erase(it);
              } else {
                it++;
              }
            }
          }
        }
        // Check whether we done this induction before. Since there can
        // be other induction schemes and literals that produce the same,
        // we add the new ones at the end
        schLits.emplace_back(nullptr, vset<Literal*>());
        if (alreadyDone(literal, sidesFiltered, kv.first, schLits.back())) {
          continue;
        }
        static const bool generalize = env.options->inductionGen();
        ScopedPtr<IteratorCore<OccurrenceMap>> g;
        if (generalize) {
          static const bool heuristic = env.options->inductionGenHeur();
          g = new GeneralizationIterator(kv.second, heuristic, gen->setsFixOccurrences());
        } else {
          g = new NoGeneralizationIterator(kv.second);
        }
        while (g->hasNext()) {
          auto eg = g->next();
          // create the generalized literals by replacing the current
          // set of occurrences of induction terms by the variables
          TermOccurrenceReplacement tr(kv.first.inductionTerms(), eg, main.literal);
          auto mainLitGen = tr.transformLit();
          ASS_NEQ(mainLitGen, main.literal); // main literal should be inducted on
          vvector<pair<Literal*, SLQueryResult>> sidesGeneralized;
          for (const auto& kv2 : sidesFiltered) {
            TermOccurrenceReplacement tr(kv.first.inductionTerms(), eg, kv2.first);
            auto sideLitGen = tr.transformLit();
            if (sideLitGen != kv2.first) { // side literals may be discarded if they contain no induction term occurrence
              sidesGeneralized.push_back(make_pair(sideLitGen, SLQueryResult(kv2.first, kv2.second)));
            }
          }
          generateClauses(kv.first, mainLitGen, main, std::move(sidesGeneralized), res._clauses);
        }
      }
      for (const auto& schLit : schLits) {
        // if the pattern is already contained but we have a superset of its
        // side literals, we add the superset to cover as many as possible
        if (!_done.insert(schLit.first, schLit.second)) {
          auto curr = _done.get(schLit.first);
          if (includes(schLit.second.begin(), schLit.second.end(), curr.begin(), curr.end())) {
            _done.set(schLit.first, schLit.second);
          }
          // TODO(mhajdu): there can be cases where the current set of side literals
          // is not a superset of the already inducted on ones, in this case the new
          // ones are not added
        }
      }
    }
  }
}

void GeneralInduction::attach(SaturationAlgorithm* salg)
{
  CALL("GeneralInduction::attach");

  GeneratingInferenceEngine::attach(salg);
  _splitter=_salg->getSplitter();
  _index = static_cast<TermIndex *>(
      _salg->getIndexManager()->request(DEMODULATION_SUBTERM_SUBST_TREE));
}

void GeneralInduction::detach()
{
  CALL("GeneralInduction::detach");

  _index = 0;
  _salg->getIndexManager()->release(DEMODULATION_SUBTERM_SUBST_TREE);
  _splitter=0;
  GeneratingInferenceEngine::detach();
}

// creates implication of the form (L1\theta & ... & Ln\theta) => ~L\theta
// where L1, ..., Ln are the side literals, L is the main literal and \theta is the substitution
Formula* createImplication(Literal* mainLit, const vvector<pair<Literal*, SLQueryResult>>& sideLitQrPairs, Substitution subst = Substitution()) {
  FormulaList* ll = FormulaList::empty();
  for (const auto& kv : sideLitQrPairs) {
    FormulaList::push(new AtomicFormula(kv.first->apply(subst)), ll);
  }
  Formula* left = 0;
  if (FormulaList::isNonEmpty(ll)) {
    left = JunctionFormula::generalJunction(Connective::AND, ll);
  }
  Formula* right = new AtomicFormula(Literal::complementaryLiteral(mainLit->apply(subst)));
  return left ? new BinaryFormula(Connective::IMP, left, right) : right;
}

void GeneralInduction::generateClauses(
  const Shell::InductionScheme& scheme,
  Literal* mainLit, SLQueryResult mainQuery,
  vvector<pair<Literal*, SLQueryResult>> sideLitQrPairs,
  ClauseStack& clauses)
{
  CALL("GeneralInduction::generateClauses");

  static const bool indhrw = env.options->inductionHypRewriting();
  static const bool indmc = env.options->inductionMultiClause();

  if (env.options->showInduction()){
    env.beginOutput();
    env.out() << "[Induction] generating from scheme " << scheme
              << " with generalized literals " << *mainLit << ", ";
    for (const auto& kv : sideLitQrPairs) {
      env.out() << *kv.first << ", ";
    }
    env.out() << endl;
    env.endOutput();
  }

  vset<unsigned> hypVars;
  FormulaList* cases = FormulaList::empty();
  TermList t;
  for (const auto& c : scheme.cases()) {
    FormulaList* ll = FormulaList::empty();
    for (auto& r : c._recursiveCalls) {
      auto f = createImplication(mainLit, sideLitQrPairs, r);
      FormulaList::push(f, ll);
      // save all free variables of the hypotheses -- these are used
      // to mark the clauses as hypotheses and corresponding conclusion
      if ((indhrw && mainLit->isEquality()) || (indmc && !mainLit->isEquality())) {
        FormulaVarIterator fvit(f);
        while (fvit.hasNext()) {
          hypVars.insert(fvit.next());
        }
      }
    }
    auto right = createImplication(mainLit, sideLitQrPairs, c._step);
    Formula* left = 0;
    if (FormulaList::isNonEmpty(ll)) {
      left = JunctionFormula::generalJunction(Connective::AND, ll);
    }
    auto f = left ? new BinaryFormula(Connective::IMP, left, right) : right;
    FormulaList::push(Formula::quantify(f), cases);
  }

  // create the substitution that will be used for binary resolution
  // this is basically the reverse of the induction term map
  ASS(FormulaList::isNonEmpty(cases));
  RobSubstitution subst;
  for (const auto& kv : scheme.inductionTerms()) {
    ALWAYS(subst.match(TermList(kv.second, false), 0, TermList(kv.first), 1));
  }
  Formula* hypothesis = new BinaryFormula(Connective::IMP,
    JunctionFormula::generalJunction(Connective::AND, cases),
    Formula::quantify(createImplication(mainLit, sideLitQrPairs)));
  // cout << *hypothesis << endl;

  NewCNF cnf(0);
  cnf.setForInduction();
  Stack<Clause*> hyp_clauses;
  Inference inf = NonspecificInference0(UnitInputType::AXIOM,_rule);
  unsigned maxDepth = mainQuery.clause->inference().inductionDepth();
  for (const auto& kv : sideLitQrPairs) {
    maxDepth = max(maxDepth, kv.second.clause->inference().inductionDepth());
  }
  inf.setInductionDepth(maxDepth+1);
  auto fu = new FormulaUnit(hypothesis,inf);
  cnf.clausify(NNF::ennf(fu), hyp_clauses);
  DHMap<unsigned,unsigned> rvs;
  if ((indhrw && mainLit->isEquality()) || (indmc && !mainLit->isEquality())) {
    // NewCNF creates a mapping from newly introduced Skolem symbols
    // to the variables before Skolemization. We need the reverse of
    // this, but we double check that it is a bijection.
    // In other cases it may be non-bijective, but here it should be.
    rvs.loadFromInverted(cnf.getSkFunToVarMap());
  }
  DHSet<unsigned> info;
  for (const auto& v : hypVars) {
    info.insert(rvs.get(v));
  }
  vset<unsigned> oldSk = InductionHelper::collectInductionSkolems(mainQuery.literal, mainQuery.clause);
  for (const auto& kv : sideLitQrPairs) {
    auto oldSkSide = InductionHelper::collectInductionSkolems(kv.second.literal, kv.second.clause);
    oldSk.insert(oldSkSide.begin(), oldSkSide.end());
  }

  // Resolve all induction clauses with the main and side literals
  auto resSubst = ResultSubstitution::fromSubstitution(&subst, 0, 1);
  mainQuery.substitution = resSubst;
  // Be aware that we change mainLit and sideLitQrPairs here irreversibly
  mainLit = Literal::complementaryLiteral(mainLit);
  for (auto& kv : sideLitQrPairs) {
    kv.first = Literal::complementaryLiteral(subst.apply(kv.first, 0));
    kv.second.substitution = resSubst;
  }
  ClauseStack::Iterator cit(hyp_clauses);
  while(cit.hasNext()){
    Clause* c = cit.next();
    for (unsigned i = 0; i < c->length(); i++) {
      auto sk = InductionHelper::collectInductionSkolems((*c)[i], &info);
      for (const auto& v : sk) {
        c->inference().addToInductionInfo(v);
      }
    }
    c = BinaryResolution::generateClause(c, mainLit, mainQuery, *env.options);
    ASS(c);
    if (_splitter && !sideLitQrPairs.empty()) {
      _splitter->onNewClause(c);
    }
    unsigned i = 0;
    for (const auto& kv : sideLitQrPairs) {
      c = BinaryResolution::generateClause(c, kv.first, kv.second, *env.options);
      ASS(c);
      if (_splitter && ++i < sideLitQrPairs.size()) {
        _splitter->onNewClause(c);
      }
    }
    if(env.options->showInduction()){
      env.beginOutput();
      env.out() << "[Induction] generate " << c->toString() << endl;
      env.endOutput();
    }
    for (const auto& v : oldSk) {
      c->inference().removeFromInductionInfo(v);
    }
    clauses.push(c);
  }
  env.statistics->induction++;
}

void reserveBlanksForScheme(const InductionScheme& sch, DHMap<TermList, vvector<Term*>>& blanks)
{
  vmap<TermList, unsigned> srts;
  // count sorts in induction terms
  for (const auto& kv : sch.inductionTerms()) {
    TermList srt = env.signature->getFunction(kv.first->functor())->fnType()->result();
    auto res = srts.insert(make_pair(srt,1));
    if (!res.second) {
      res.first->second++;
    }
  }
  // introduce as many blanks for each sort as needed
  for (const auto kv : srts) {
    if (!blanks.find(kv.first)) {
      blanks.insert(kv.first, vvector<Term*>());
    }
    auto& v = blanks.get(kv.first);
    v.reserve(kv.second);
    while (v.size() < kv.second) {
      unsigned fresh = env.signature->addFreshFunction(0, "blank");
      env.signature->getFunction(fresh)->setType(OperatorType::getConstantsType(kv.first));
      v.push_back(Term::createConstant(fresh));
    }
  }
}

bool GeneralInduction::alreadyDone(Literal* mainLit, const vset<pair<Literal*,Clause*>>& sides,
  const InductionScheme& sch, pair<Literal*,vset<Literal*>>& res)
{
  CALL("GeneralInduction::alreadyDone");

  // Instead of relying on the order within the induction term set, we map induction terms
  // to blanks based on their first occurrences within the literal to avoid creating different
  // blanks for the same literal pattern. E.g. if we have a saved pattern leq(blank0,blank1)
  // and a new literal leq(sk1,sk0) should be inducted upon with induction terms { sk0, sk1 },
  // instead of using the order within the set to get the different leq(blank1,blank0) essentially
  // for the same pattern, since sk1 is the first within the literal, we map this to
  // leq(blank0,blank1) and we detect that it was already inducted upon in this form

  // introduce the blanks
  static DHMap<TermList, vvector<Term*>> blanks;
  reserveBlanksForScheme(sch, blanks);

  // place the blanks in main literal
  TermMapReplacement cr(blanks, sch.inductionTerms());
  res.first = cr.transform(mainLit);

  // place the blanks in sides (using the now fixed order from main literal)
  for (const auto& kv : sides) {
    res.second.insert(cr.transform(kv.first));
  }
  // check already existing pattern for main literal
  if (!_done.find(res.first)) {
    return false;
  }
  auto s = _done.get(res.first);
  // check if the sides for the new pattern are included in the existing one
  if (includes(s.begin(), s.end(), res.second.begin(), res.second.end())) {
    if (env.options->showInduction()) {
      env.beginOutput();
      env.out() << "[Induction] already inducted on " << *mainLit << " in " << *res.first << " form" << endl;
      env.endOutput();
    }
    return true;
  }
  return false;
}

inline bool sideLitCondition(Literal* main, Clause* mainCl, Literal* side, Clause* sideCl) {
  auto mainSk = InductionHelper::collectInductionSkolems(main, mainCl);
  auto sideSk = InductionHelper::collectInductionSkolems(side, sideCl);
  return side->ground() && main != side && mainCl != sideCl &&
    // either they are both induction depth 0 (not yet inducted on)
    ((!mainCl->inference().inductionDepth() && !sideCl->inference().inductionDepth()) ||
    // or they are non-equality hypothesis and conclusion from the same step
    (!side->isEquality() && !main->isEquality() && !mainSk.empty() && !sideSk.empty() &&
      includes(mainSk.begin(), mainSk.end(), sideSk.begin(), sideSk.end())));
}

vvector<pair<SLQueryResult, vset<pair<Literal*,Clause*>>>> GeneralInduction::selectMainSidePairs(Literal* literal, Clause* premise)
{
  CALL("GeneralInduction::selectMainSidePairs");

  vvector<pair<SLQueryResult, vset<pair<Literal*,Clause*>>>> res;
  static const bool indmc = env.options->inductionMultiClause();

  TermQueryResultIterator it = TermQueryResultIterator::getEmpty();
  if (indmc && literal->ground() && (!premise->inference().inductionDepth() ||
    (!literal->isEquality() && InductionHelper::isInductionLiteral(literal, premise))))
  {
    SubtermIterator stit(literal);
    DHSet<TermList> skolems;
    while (stit.hasNext()) {
      auto st = stit.next();
      if (st.isTerm() && skolem(st.term())) {
        skolems.insert(st);
      }
    }
    DHSet<TermList>::Iterator skit(skolems);
    while (skit.hasNext()) {
      auto st = skit.next();
      it = pvi(getConcatenatedIterator(it, _index->getGeneralizations(st)));
    }
  }

  // pair current literal as main literal with possible side literals
  // this results in any number of side literals
  const bool indLit = InductionHelper::isInductionLiteral(literal);
  if (indLit)
  {
    // first pair in result is always the current premise being the main literal
    res.emplace_back(SLQueryResult(literal, premise), vset<pair<Literal*,Clause*>>());
  }
  while (it.hasNext())
  {
    auto qr = it.next();
    // query is side literal
    if (indLit && InductionHelper::isInductionClause(qr.clause) && sideLitCondition(literal, premise, qr.literal, qr.clause))
    {
      res[0].second.emplace(qr.literal, qr.clause);
    }
    // query is main literal
    if (InductionHelper::isInductionClause(qr.clause) &&
        InductionHelper::isInductionLiteral(qr.literal) &&
        sideLitCondition(qr.literal, qr.clause, literal, premise))
    {
      res.emplace_back(SLQueryResult(qr.literal, qr.clause), vset<pair<Literal*,Clause*>>());
      res.back().second.emplace(literal, premise);
      // now add side literals other than the input
      SubtermIterator stit(qr.literal);
      while (stit.hasNext()) {
        auto st = stit.next();
        if (st.isTerm() && skolem(st.term())) {
          auto it = _index->getGeneralizations(st);
          while (it.hasNext()) {
            auto qrSide = it.next();
            if (qrSide.literal != literal &&
              InductionHelper::isInductionClause(qrSide.clause) &&
              sideLitCondition(qr.literal, qr.clause, qrSide.literal, qrSide.clause))
            {
              res.back().second.emplace(qrSide.literal, qrSide.clause);
            }
          }
        }
      }
    }
  }
  return res;
}

}