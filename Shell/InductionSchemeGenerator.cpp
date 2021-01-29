/*
 * This file is part of the source code of the software program
 * Vampire. It is protected by applicable
 * copyright laws.
 *
 * This source code is distributed under the licence found here
 * https://vprover.github.io/license.html
 * and in the source directory
 */

#include "InductionSchemeGenerator.hpp"

#include "Kernel/Clause.hpp"
#include "Kernel/Formula.hpp"
#include "Kernel/FormulaVarIterator.hpp"
#include "Kernel/Matcher.hpp"
#include "Kernel/Problem.hpp"
#include "Kernel/Renaming.hpp"
#include "Kernel/RobSubstitution.hpp"
#include "Kernel/Term.hpp"
#include "Kernel/TermIterators.hpp"
#include "Kernel/Unit.hpp"

#include "InductionSchemeFilter.hpp"

using namespace Kernel;

namespace Shell {

bool isSkolem(TermList t) {
  CALL("isSkolem");

  if (t.isVar()) {
    return false;
  }
  auto fn = t.term()->functor();
  auto symb = t.term()->isLiteral() ? env.signature->getPredicate(fn) : env.signature->getFunction(fn);
  return symb->skolem();
}

bool canInductOn(TermList t)
{
  CALL("canInductOn");

  if (t.isVar()) {
    return false;
  }
  static bool complexTermsAllowed = env.options->inductionOnComplexTerms();
  return t.freeVariables() == IntList::empty() && (isSkolem(t) || (complexTermsAllowed && !isTermAlgebraCons(t)));
}

OperatorType* getType(TermList t)
{
  CALL("getType");

  //TODO(mhajdu): maybe handle variables?
  auto fn = t.term()->functor();
  auto symb = t.term()->isLiteral() ? env.signature->getPredicate(fn) : env.signature->getFunction(fn);
  return t.term()->isLiteral() ? symb->predType() : symb->fnType();
}

/**
 * Returns all subterms which can be inducted on for a term.
 */
vvector<TermList> getInductionTerms(TermList t)
{
  CALL("getInductionTerms");

  vvector<TermList> v;
  if (t.isVar()) {
    return v;
  }
  if (canInductOn(t)) {
    v.push_back(t);
  }
  unsigned f = t.term()->functor();
  bool isPred = t.term()->isFormula();
  auto type = getType(t);

  // If function with recursive definition,
  // recurse in its active arguments
  if (env.signature->hasInductionTemplate(f, isPred)) {
    auto& templ = env.signature->getInductionTemplate(f, isPred);
    const auto& indVars = templ._inductionVariables;

    Term::Iterator argIt(t.term());
    unsigned i = 0;
    while (argIt.hasNext()) {
      auto arg = argIt.next();
      if (indVars.at(i) && type->arg(i) == type->result()) {
        auto indTerms = getInductionTerms(arg);
        v.insert(v.end(), indTerms.begin(), indTerms.end());
      }
      i++;
    }
  } else if (isTermAlgebraCons(t)) {
    //TODO(mhajdu): eventually check whether we really recurse on a specific
    // subterm of the constructor terms
    for (unsigned i = 0; i < t.term()->arity(); i++) {
      auto st = *t.term()->nthArgument(i);
      if (type->arg(i) == type->result()) {
        auto indTerms = getInductionTerms(st);
        v.insert(v.end(), indTerms.begin(), indTerms.end());
      }
    }
  }
  return v;
}

TermList TermOccurrenceReplacement::transformSubterm(TermList trm)
{
  CALL("TermOccurrenceReplacement::transformSubterm");

  auto rIt = _r.find(trm);

  // The induction generalization heuristic is stored here:
  // - if we have only one active occurrence, induct on all
  // - otherwise only induct on the active occurrences
  if (rIt != _r.end()) {
    if (!_c.find(trm)) {
      _c.insert(trm, 0);
    } else {
      _c.get(trm)++;
    }
    const auto& o = _o.get(trm);
    auto one = env.options->inductionTermOccurrenceSelectionHeuristic()
      == Options::InductionTermOccurrenceSelectionHeuristic::ONE;
    auto oc = _oc.get(trm);
    if (o->size() == 1 || (!one && oc == o->size() + 1) || o->contains(_c.get(trm))) {
      return _r.at(trm);
    }
  }
  // otherwise we may replace it with a variable
  if ((_replaceSkolem && isSkolem(trm)) || trm.isVar()) {
    if (!_r_g.count(trm)) {
      _r_g.insert(make_pair(trm, TermList(_v++,false)));
    }
    return _r_g.at(trm);
  }
  return trm;
}

TermList VarReplacement::transformSubterm(TermList trm)
{
  CALL("VarReplacement::transformSubterm");

  if(trm.isVar()) {
    if (!_varMap.find(trm.var())) {
      _varMap.insert(trm.var(), _v++);
    }
    return TermList(_varMap.get(trm.var()), false);
  }
  return trm;
}

TermList VarShiftReplacement::transformSubterm(TermList trm) {
  if (trm.isVar()) {
    return TermList(trm.var()+_shift, trm.isSpecialVar());
  }
  return trm;
}

bool IteratorByInductiveVariables::hasNext()
{
  ASS(_it.hasNext() == (_indVarIt != _end));

  while (_indVarIt != _end && !*_indVarIt && _it.hasNext()) {
    _indVarIt++;
    _it.next();
  }
  return _indVarIt != _end;
}

TermList IteratorByInductiveVariables::next()
{
  ASS(hasNext());
  _indVarIt++;
  return _it.next();
}

Formula* applyVarReplacement(Formula* f, VarReplacement& vr) {
  if (f->connective() == Connective::LITERAL) {
    auto lit = vr.transform(f->literal());
    return new AtomicFormula(lit);
  }

  switch (f->connective()) {
    case Connective::AND:
    case Connective::OR: {
      FormulaList* res = f->args();
      FormulaList::RefIterator it(res);
      while (it.hasNext()) {
        auto& curr = it.next();
        curr = applyVarReplacement(curr, vr);
      }
      return JunctionFormula::generalJunction(f->connective(), res);
    }
    case Connective::IMP:
    case Connective::XOR:
    case Connective::IFF: {
      auto left = applyVarReplacement(f->left(), vr);
      auto right = applyVarReplacement(f->right(), vr);
      return new BinaryFormula(f->connective(), left, right);
    }
    case Connective::NOT: {
      return new NegatedFormula(applyVarReplacement(f->uarg(), vr));
    }
    default:
      ASSERTION_VIOLATION;
  }
}

Formula* applySubst(RobSubstitution& subst, int index, Formula* f) {
  if (f->connective() == Connective::LITERAL) {
    auto lit = subst.apply(f->literal(), index);
    return new AtomicFormula(lit);
  }

  switch (f->connective()) {
    case Connective::AND:
    case Connective::OR: {
      FormulaList* res = f->args();
      FormulaList::RefIterator it(res);
      while (it.hasNext()) {
        auto& curr = it.next();
        curr = applySubst(subst, index, curr);
      }
      return JunctionFormula::generalJunction(f->connective(), res);
    }
    case Connective::IMP:
    case Connective::XOR:
    case Connective::IFF: {
      auto left = applySubst(subst, index, f->left());
      auto right = applySubst(subst, index, f->right());
      return new BinaryFormula(f->connective(), left, right);
    }
    case Connective::NOT: {
      return new NegatedFormula(applySubst(subst, index, f->uarg()));
    }
    default:
      ASSERTION_VIOLATION;
  }
}

bool subsumes(Formula* subsumer, Formula* subsumed) {
  if (subsumer->connective() != subsumed->connective()) {
    return false;
  }
  switch (subsumer->connective()) {
    case LITERAL: {
      return subsumer->literal() == subsumed->literal();
      break;
    }
    case NOT: {
      return subsumes(subsumer->uarg(), subsumed->uarg());
    }
    case AND:
    case OR:
    case IMP:
    case IFF:
    case XOR:
    case FORALL:
    case EXISTS:
    case BOOL_TERM:
    case FALSE:
    case TRUE:
    case NAME:
    case NOCONN: {
      break;
    }
  }
  return false;
}

bool RDescriptionInst::contains(const RDescriptionInst& other) const
{
  vmap<TermList, RobSubstitutionSP> substs;
  for (const auto& kv : other._step) {
    // we only check this on relations with the same
    // induction terms
    ASS (_step.count(kv.first));
    auto s2 = _step.at(kv.first);
    RobSubstitutionSP subst(new RobSubstitution);
    // try to unify the step cases
    if (!subst->unify(s2, 0, kv.second, 1)) {
      return false;
    }
    auto t1 = subst->apply(kv.second, 1);
    Renaming r1, r2;
    r1.normalizeVariables(kv.second);
    r2.normalizeVariables(s2);
    auto t2 = subst->apply(s2, 0);
    if (t1 != r1.apply(kv.second) || t2 != r2.apply(s2)) {
      return false;
    }
    substs.insert(make_pair(kv.first, subst));
  }
  // check condition subsumption
  for (const auto& c1 : other._conditions) {
    bool found = false;
    for (const auto& c2 : _conditions) {
      // TODO(mhajdu): this check should be based on the unification on the arguments
      if (subsumes(c2, c1)) {
        found = true;
        break;
      }
    }
    if (!found) {
      return false;
    }
  }
  // if successful, find pair for each recCall in sch1
  // don't check if recCall1 or recCall2 contain kv.first
  // as they should by definition
  for (const auto& recCall1 : other._recursiveCalls) {
    bool found = false;
    for (const auto& recCall2 : _recursiveCalls) {
      for (const auto& kv : recCall1) {
        if (!recCall1.count(kv.first) || !recCall2.count(kv.first)) {
          continue;
        }
        auto subst = substs.at(kv.first);
        const auto& r1 = subst->apply(recCall1.at(kv.first), 1);
        const auto& r2 = subst->apply(recCall2.at(kv.first), 0);
        if (r1 == r2) {
          found = true;
          break;
        }
      }
    }
    if (!found) {
      return false;
    }
  }
  return true;
}

bool InductionScheme::init(const vvector<TermList>& argTerms, const InductionTemplate& templ)
{
  CALL("InductionScheme::init");

  unsigned var = 0;
  const bool strengthen = env.options->inductionStrengthen();
  for (auto& rdesc : templ._rDescriptions) {
    // for each RDescription, use a new substitution and variable
    // replacement as these cases should be independent
    vmap<TermList,TermList> stepSubst;
    vvector<Formula*> condSubstList;
    IntList::Iterator fvIt(rdesc._step.freeVariables());
    vset<unsigned> stepFreeVars;
    vset<unsigned> freeVars;
    while (fvIt.hasNext()) {
      stepFreeVars.insert(fvIt.next());
    }
    for (auto& c : rdesc._conditions) {
      // TODO(mhajdu): check if this hack is okay
      // this substitutes non-induction terms into conditions
      // as well, induction terms are already there from the template
      // ALWAYS(false);
      auto cond = c;
      for (unsigned i = 0; i < templ._inductionVariables.size(); i++) {
        if (!templ._inductionVariables[i]) {
          auto arg = *(rdesc._step.term()->nthArgument(i));
          TermListReplacement tr(arg, argTerms.at(i));
          cond = tr.transform(cond);
        }
      }
      condSubstList.push_back(cond);
      IntList::Iterator cIt(cond->freeVariables());
      while (cIt.hasNext()) {
        freeVars.insert(cIt.next());
      }
    }
    auto& recCalls = rdesc._recursiveCalls;
    for (auto& r : recCalls) {
      IntList::Iterator rIt(r.freeVariables());
      while (rIt.hasNext()) {
        freeVars.insert(rIt.next());
      }
    }
    if (!includes(stepFreeVars.begin(), stepFreeVars.end(), freeVars.begin(), freeVars.end())) {
      return false;
    }
    vvector<vmap<TermList,TermList>> recCallSubstList(recCalls.size());
    vvector<bool> changed(recCalls.size(), false);
    vvector<bool> invalid(recCalls.size(), false);

    // We first map the inductive terms of t to the arguments of
    // the function header stored in the step case
    bool mismatch = false;
    for (const auto& vars : templ._order) {
      vvector<bool> changing(recCalls.size(), false);
      for (const auto& v : vars) {
        auto argTerm = argTerms.at(v);
        auto argStep = *rdesc._step.term()->nthArgument(v);
        RobSubstitution subst;
        // This argument might have already been mapped
        if (stepSubst.count(argTerm)) {
          if (!subst.unify(stepSubst.at(argTerm), 0, argStep, 1)) {
            mismatch = true;
            break;
          }
          stepSubst.at(argTerm) = subst.apply(stepSubst.at(argTerm), 0);
          if (!condSubstList.empty()) {
            return false;
          }
          for (auto& c : condSubstList) {
            c = applySubst(subst, 0, c);
          }
        } else {
          stepSubst.insert(make_pair(argTerm, argStep));
        }
        for (unsigned i = 0; i < recCalls.size(); i++) {
          // if this recursive call is already invalid, skip it
          if (invalid[i]) {
            continue;
          }
          auto argRecCall = *recCalls[i].term()->nthArgument(v);
          if (recCallSubstList[i].count(argTerm)) {
            auto t1 = subst.apply(recCallSubstList[i].at(argTerm), 0);
            // if we would introduce here a fresh variable,
            // just save the application of unification,
            // otherwise try to unify the recursive terms
            if (!changed[i] || !strengthen) {
              auto t2 = subst.apply(argRecCall, 1);
              if (t1 != t2) {
                invalid[i] = true;
                continue;
              }
            }
            recCallSubstList[i].at(argTerm) = t1;
          } else {
            // if strengthen option is on and this
            // induction term is irrelevant for
            // the order, we add a fresh variable
            if (changed[i] && strengthen) {
              recCallSubstList[i].insert(make_pair(
                argTerm, TermList(var++, false)));
            } else {
              recCallSubstList[i].insert(make_pair(argTerm, argRecCall));
            }
          }
          // find out if this is the changing set
          if (argStep != argRecCall) {
            changing[i] = true;
          }
        }
        _inductionTerms.insert(argTerm);
      }
      if (mismatch) {
        break;
      }
      for (unsigned i = 0; i < changing.size(); i++) {
        if (changing[i]) {
          changed[i] = true;
        }
      }
    }
    if (mismatch) {
      // We cannot properly create this case because
      // there is a mismatch between the ctors for
      // a substituted term
      continue;
    }

    for (const auto& kv : stepSubst) {
      DHMap<unsigned, unsigned> varMap;
      VarReplacement vr(varMap, var);
      stepSubst.at(kv.first) = applyVarReplacement(stepSubst.at(kv.first), vr);
      for (auto& c : condSubstList) {
        c = applyVarReplacement(c, vr);
      }
      for (unsigned i = 0; i < recCalls.size(); i++) {
        if (invalid[i]) {
          continue;
        }
        recCallSubstList[i].at(kv.first) = applyVarReplacement(recCallSubstList[i].at(kv.first), vr);
      }
    }

    vvector<vmap<TermList,TermList>> recCallSubstFinal;
    for (unsigned i = 0; i < recCallSubstList.size(); i++) {
      if (!invalid[i]) {
        recCallSubstFinal.push_back(recCallSubstList[i]);
      }
    }
    _rDescriptionInstances.emplace_back(std::move(recCallSubstFinal), std::move(stepSubst), std::move(condSubstList));
  }
  _maxVar = var;
  // clean();
  return true;
}

void InductionScheme::init(vvector<RDescriptionInst>&& rdescs)
{
  CALL("InductionScheme::init");

  _rDescriptionInstances = rdescs;
  _inductionTerms.clear();
  unsigned var = 0;

  for (auto& rdesc : _rDescriptionInstances) {
    DHMap<unsigned, unsigned> varMap;
    VarReplacement vr(varMap, var);
    for (auto& kv : rdesc._step) {
      kv.second = kv.second.isVar()
        ? vr.transformSubterm(kv.second)
        : TermList(vr.transform(kv.second.term()));
      _inductionTerms.insert(kv.first);
    }
    for (auto& recCall : rdesc._recursiveCalls) {
      for (auto& kv : recCall) {
        kv.second = kv.second.isVar()
          ? vr.transformSubterm(kv.second)
          : TermList(vr.transform(kv.second.term()));
      }
    }
    for (auto& f : rdesc._conditions) {
      f = vr.transform(f);
    }
  }
  _maxVar = var;
  clean();
}

void InductionScheme::clean()
{
  for (unsigned i = 0; i < _rDescriptionInstances.size(); i++) {
    for (unsigned j = i+1; j < _rDescriptionInstances.size();) {
      if (_rDescriptionInstances[i].contains(_rDescriptionInstances[j])) {
        _rDescriptionInstances[j] = _rDescriptionInstances.back();
        _rDescriptionInstances.pop_back();
      } else {
        j++;
      }
    }
  }
  _rDescriptionInstances.shrink_to_fit();
}

InductionScheme InductionScheme::makeCopyWithVariablesShifted(unsigned shift) const {
  InductionScheme res;
  res._inductionTerms = _inductionTerms;
  VarShiftReplacement vsr(shift);

  for (const auto& rdesc : _rDescriptionInstances) {
    vvector<vmap<TermList, TermList>> resRecCalls;
    for (const auto& recCall : rdesc._recursiveCalls) {
      vmap<TermList, TermList> resRecCall;
      for (auto kv : recCall) {
        resRecCall.insert(make_pair(kv.first,
          kv.second.isVar()
            ? vsr.transformSubterm(kv.second)
            : TermList(vsr.transform(kv.second.term()))));
      }
      resRecCalls.push_back(resRecCall);
    }
    vmap<TermList, TermList> resStep;
    for (auto kv : rdesc._step) {
      resStep.insert(make_pair(kv.first,
        kv.second.isVar()
          ? vsr.transformSubterm(kv.second)
          : TermList(vsr.transform(kv.second.term()))));
    }
    vvector<Formula*> resCond;
    for (auto f : rdesc._conditions) {
      resCond.push_back(vsr.transform(f));
    }
    res._rDescriptionInstances.emplace_back(std::move(resRecCalls),
      std::move(resStep), std::move(resCond));
  }
  res._maxVar = _maxVar + shift;
  return res;
}

void InductionScheme::addInductionTerms(const vset<TermList>& terms) {
  for (const auto& t : terms) {
    for (auto& rdesc : _rDescriptionInstances) {
      if (rdesc._recursiveCalls.empty()) {
        continue;
      }
      auto it = rdesc._step.find(t);
      if (it == rdesc._step.end()) {
        TermList var(_maxVar++, false);
        rdesc._step.insert(make_pair(t, var));
        for (auto& recCall : rdesc._recursiveCalls) {
          recCall.insert(make_pair(t, var));
        }
      }
    }
  }
}

bool InductionScheme::checkWellFoundedness()
{
  vvector<pair<vmap<TermList,TermList>&,vmap<TermList,TermList>&>> relations;
  for (auto& rdesc : _rDescriptionInstances) {
    for (auto& recCall : rdesc._recursiveCalls) {
      relations.push_back(
        pair<vmap<TermList,TermList>&,vmap<TermList,TermList>&>(
          recCall, rdesc._step));
    }
  }
  return checkWellFoundedness(relations, _inductionTerms);
}

bool InductionScheme::checkWellFoundedness(
  vvector<pair<vmap<TermList,TermList>&,vmap<TermList,TermList>&>> relations,
  vset<TermList> inductionTerms)
{
  if (relations.empty()) {
    return true;
  }
  if (inductionTerms.empty()) {
    return false;
  }
  for (const auto& indTerm : inductionTerms) {
    vvector<pair<vmap<TermList,TermList>&,vmap<TermList,TermList>&>> remaining;
    bool check = true;
    for (const auto& rel : relations) {
      auto it1 = rel.first.find(indTerm);
      auto it2 = rel.second.find(indTerm);
      // if either one is missing or the step term
      // does not contain the recursive term as subterm
      if (it1 == rel.first.end() || it2 == rel.second.end()
        || !it2->second.containsSubterm(it1->second))
      {
        check = false;
        break;
      }
      if (it1->second == it2->second) {
        remaining.push_back(rel);
      }
    }
    if (check) {
      auto remIndTerms = inductionTerms;
      remIndTerms.erase(indTerm);
      if (checkWellFoundedness(remaining, std::move(remIndTerms))) {
        return true;
      }
    }
  }
  return false;
}

ostream& operator<<(ostream& out, const InductionScheme& scheme)
{
  unsigned k = 0;
  unsigned l = scheme._inductionTerms.size();
  for (const auto& indTerm : scheme._inductionTerms) {
    out << indTerm;
    if (++k < l) {
      out << ',';
    }
  }
  out << ':';
  unsigned i;
  unsigned j = 0;
  for (const auto& rdesc : scheme._rDescriptionInstances) {
    i = 0;
    for (const auto& cond : rdesc._conditions) {
      out << '[' << *cond << ']';
      if (++i < rdesc._conditions.size()) {
        out << ',';
      }
    }
    i = 0;
    for (const auto& recCall : rdesc._recursiveCalls) {
      out << '[';
      k = 0;
      for (const auto& indTerm : scheme._inductionTerms) {
        auto it = recCall.find(indTerm);
        out << ((it != recCall.end()) ? it->second.toString() : "_");
        if (++k < l) {
          out << ',';
        }
      }
      out << ']';
      if (++i < rdesc._recursiveCalls.size()) {
        out << ',';
      }
    }
    if (!rdesc._conditions.empty() || !rdesc._recursiveCalls.empty()) {
      out << "=>";
    }
    k = 0;
    out << '[';
    for (const auto& indTerm : scheme._inductionTerms) {
      auto it = rdesc._step.find(indTerm);
      out << ((it != rdesc._step.end()) ? it->second.toString() : "_");
      if (++k < l) {
        out << ',';
      }
      k++;
    }
    out << ']';
    if (++j < scheme._rDescriptionInstances.size()) {
      out << ';';
    }
  }

  return out;
}

InductionSchemeGenerator::~InductionSchemeGenerator()
{
  DHMap<Literal*, DHMap<TermList, DHSet<unsigned>*>*>::Iterator it(_actOccMaps);
  while (it.hasNext()) {
    DHMap<TermList, DHSet<unsigned>*>::Iterator aoIt(*it.next());
    while (aoIt.hasNext()) {
      delete aoIt.next();
    }
  }
  for (auto& kv : _primarySchemes) {
    delete kv.second;
  }
}

void InductionSchemeGenerator::generatePrimary(Clause* premise, Literal* lit)
{
  // we can only hope to simplify anything by function definitions if
  // this flag is on, otherwise maybe no more simplifications can be
  // applied and we have to induct anyway

  // static bool rewriting = env.options->functionDefinitionRewriting();
  static bool simplify = env.options->simplifyBeforeInduction();
  if (!generate(premise, lit, _primarySchemes, simplify)) {
    _primarySchemes.clear();
  };
}

void InductionSchemeGenerator::generateSecondary(Clause* premise, Literal* lit)
{
  generate(premise, lit, _secondarySchemes, false);
}

vvector<pair<Formula*, vmap<Literal*, pair<Literal*, Clause*>>>> InductionSchemeGenerator::instantiateSchemes() {
  CALL("InductionSchemeGenerator::instantiateSchemes");

  InductionSchemeFilter f;
  f.filter(_primarySchemes, _secondarySchemes);
  f.filterComplex(_primarySchemes, &_currOccMaps);

  vvector<pair<Formula*, vmap<Literal*, pair<Literal*, Clause*>>>> res;
  for (unsigned i = 0; i < _primarySchemes.size(); i++) {
    if(env.options->showInduction()){
      env.beginOutput();
      env.out() << "[Induction] generating scheme " << _primarySchemes[i].first << " for literals ";
      DHMap<Literal*, Clause*>::Iterator litClIt(*_primarySchemes[i].second);
      while (litClIt.hasNext()) {
        Literal* lit;
        Clause* cl;
        litClIt.next(lit, cl);
        env.out() << lit->toString() << " in " << cl->toString() << ", ";
      }
      env.out() << endl;
      env.endOutput();
    }
    res.push_back(instantiateScheme(i));
  }
  return res;
}

bool InductionSchemeGenerator::generate(Clause* premise, Literal* lit,
  vvector<pair<InductionScheme, DHMap<Literal*, Clause*>*>>& schemes,
  bool returnOnMatch)
{
  CALL("InductionSchemeGenerator::generate");

  // Process all subterms of the literal to
  // be able to store occurrences of induction
  // terms. The literal itself and both sides
  // of the equality count as active positions.
  if (_actOccMaps.find(lit)) {
    return true;
  }
  _actOccMaps.insert(lit, new DHMap<TermList, DHSet<unsigned>*>());
  _currOccMaps.insert(lit, new DHMap<TermList, unsigned>());

  Stack<bool> actStack;
  if (lit->isEquality()) {
    actStack.push(true);
    actStack.push(true);
  } else {
    if (!process(TermList(lit), true, actStack, premise, lit, schemes, returnOnMatch)
        /* short circuit */ && returnOnMatch) {
      return false;
    }
  }
  SubtermIterator it(lit);
  while(it.hasNext()){
    TermList curr = it.next();
    bool active = actStack.pop();
    if (!process(curr, active, actStack, premise, lit, schemes, returnOnMatch)
        /* short circuit */ && returnOnMatch) {
      return false;
    }
  }
  ASS(actStack.isEmpty());
  return true;
}

bool InductionSchemeGenerator::process(TermList curr, bool active,
  Stack<bool>& actStack, Clause* premise, Literal* lit,
  vvector<pair<InductionScheme, DHMap<Literal*, Clause*>*>>& schemes,
  bool returnOnMatch)
{
  CALL("InductionSchemeGenerator::process");

  if (!curr.isTerm()) {
    return true;
  }
  auto t = curr.term();

  // If induction term, store the occurrence
  if (canInductOn(curr)) {
    if (!_currOccMaps.get(lit)->find(curr)) {
      _currOccMaps.get(lit)->insert(curr, 0);
      _actOccMaps.get(lit)->insert(curr, new DHSet<unsigned>());
    }
    if (active) {
      _actOccMaps.get(lit)->get(curr)->insert(_currOccMaps.get(lit)->get(curr));
    }
    _currOccMaps.get(lit)->get(curr)++;
  }

  unsigned f = t->functor();
  bool isPred = t->isLiteral();

  // If function with recursive definition, create a scheme
  if (env.signature->hasInductionTemplate(f, isPred)) {
    auto& templ = env.signature->getInductionTemplate(f, isPred);
    const auto& indVars = templ._inductionVariables;

    for (auto it = indVars.rbegin(); it != indVars.rend(); it++) {
      actStack.push(*it && active);
    }

    if (returnOnMatch) {
      for (const auto& rdesc : templ._rDescriptions) {
        if (MatchingUtils::matchTerms(rdesc._step, curr)) {
          return false;
        }
      }
    }

    if (!active) {
      return true;
    }

    Term::Iterator argIt(t);
    unsigned i = 0;
    vvector<vvector<TermList>> argTermsList(1); // initially 1 empty vector
    while (argIt.hasNext()) {
      auto arg = argIt.next();
      if (!indVars[i]) {
        for (auto& argTerms : argTermsList) {
          argTerms.push_back(arg);
        }
      } else {
        auto its = getInductionTerms(arg);
        vvector<vvector<TermList>> newArgTermsList;
        for (const auto& indTerm : its) {
          for (auto argTerms : argTermsList) {
            argTerms.push_back(indTerm);
            newArgTermsList.push_back(std::move(argTerms));
          }
        }
        argTermsList = newArgTermsList;
      }
      i++;
    }

    for (const auto& argTerms : argTermsList) {
      InductionScheme scheme;
      if (!scheme.init(argTerms, templ)) {
        continue;
      }
      if (!scheme.checkWellFoundedness()) {
        if (env.options->showInduction()) {
          env.beginOutput();
          env.out() << "[Induction] induction scheme is not well-founded: " << endl
            << scheme << endl << "suggested by template " << templ << endl << "and terms ";
          for (const auto& argTerm : argTerms) {
            env.out() << argTerm << ",";
          }
          env.out() << endl;
          env.endOutput();
        }
        stringstream str;
        str << "induction scheme is not well-founded: " << scheme << endl;
        USER_ERROR(str.str());
      } else {
        auto litClMap = new DHMap<Literal*, Clause*>();
        litClMap->insert(lit, premise);
        if(env.options->showInduction()){
          env.beginOutput();
          env.out() << "[Induction] induction scheme " << scheme
                    << " was suggested by term " << *t << " in " << *lit << endl;
          env.endOutput();
        }
        schemes.push_back(make_pair(std::move(scheme), litClMap));
      }
    }
  } else {
    for (unsigned i = 0; i < t->arity(); i++) {
      actStack.push(active);
    }
  }
  return true;
}

pair<Formula*, vmap<Literal*, pair<Literal*, Clause*>>> InductionSchemeGenerator::instantiateScheme(unsigned index) const
{
  CALL("InductionSchemeGenerator::instantiateScheme");

  const auto& schemePair = _primarySchemes[index];
  const auto& scheme = schemePair.first;
  const auto& litClMap = schemePair.second;
  FormulaList* formulas = FormulaList::empty();
  unsigned var = scheme._maxVar;
  const bool strengthen = env.options->inductionStrengthen();

  for (auto& desc : scheme._rDescriptionInstances) {
    // We replace all induction terms with the corresponding step case terms
    FormulaList* stepFormulas = FormulaList::empty();
    DHMap<Literal*, Clause*>::Iterator litClIt(*litClMap);
    ASS(litClIt.hasNext());
    vmap<TermList, TermList> empty;
    while (litClIt.hasNext()) {
      auto lit = litClIt.nextKey();
      TermOccurrenceReplacement tr(desc._step, *_actOccMaps.get(lit), *_currOccMaps.get(lit), var, empty);
      stepFormulas = new FormulaList(new AtomicFormula(Literal::complementaryLiteral(tr.transform(lit))), stepFormulas);
    }
    auto right = JunctionFormula::generalJunction(Connective::OR, stepFormulas);

    FormulaList* hyp = FormulaList::empty();

    // Then we replace the arguments of the term with the
    // corresponding recursive cases for this step case (if not base case)
    for (const auto& r : desc._recursiveCalls) {
      FormulaList* innerHyp = FormulaList::empty();
      DHMap<Literal*, Clause*>::Iterator litClIt(*litClMap);
      vmap<TermList, TermList> r_g;
      while (litClIt.hasNext()) {
        auto lit = litClIt.nextKey();
        TermOccurrenceReplacement tr(r, *_actOccMaps.get(lit), *_currOccMaps.get(lit), var, r_g, strengthen);
        innerHyp = new FormulaList(new AtomicFormula(Literal::complementaryLiteral(tr.transform(lit))),innerHyp);
      }
      hyp = new FormulaList(JunctionFormula::generalJunction(Connective::OR,innerHyp),hyp);
    }

    // add conditions
    if (!desc._conditions.empty()) {
      for (const auto& cond : desc._conditions) {
        hyp = new FormulaList(cond, hyp);
      }
    }
    Formula* res = nullptr;
    if (hyp == 0) {
      // base case
      res = right;
    } else {
      auto left = JunctionFormula::generalJunction(Connective::AND,hyp);
      // there may be free variables present only in the conditions or
      // hypoheses, quantify these first so that they won't be skolemized away
      auto leftVarLst = left->freeVariables();
      FormulaVarIterator fvit(right);
      while(fvit.hasNext()) {
        auto v = fvit.next();
        if (Formula::VarList::member(v, leftVarLst)) {
          leftVarLst = Formula::VarList::remove(v, leftVarLst);
        }
      }
      if (leftVarLst) {
        left = new QuantifiedFormula(FORALL, leftVarLst, 0, left);
      }
      res = new BinaryFormula(Connective::IMP,left,right);
    }
    formulas = new FormulaList(Formula::quantify(res), formulas);
  }
  ASS(formulas != 0);
  Formula* indPremise = JunctionFormula::generalJunction(Connective::AND,formulas);

  // After creating all cases, we need the main implicant to be resolved with
  // the literal. For this, we use new variables starting from the max. var of
  // the scheme.
  vmap<TermList, TermList> r;
  for (const auto& desc : scheme._rDescriptionInstances) {
    for (const auto& kv : desc._step) {
      if (r.count(kv.first) > 0) {
        continue;
      }
      r.insert(make_pair(kv.first, TermList(var++,false)));
    }
  }
  vmap<Literal*, pair<Literal*,Clause*>> conclusionToOrigLitClauseMap;
  FormulaList* conclusionList = FormulaList::empty();
  DHMap<Literal*, Clause*>::Iterator litClIt(*litClMap);
  vmap<TermList, TermList> empty;
  while (litClIt.hasNext()) {
    Literal* origLit;
    Clause* origClause;
    litClIt.next(origLit, origClause);
    TermOccurrenceReplacement tr(r, *_actOccMaps.get(origLit),
      *_currOccMaps.get(origLit), var, empty);
    auto conclusion = Literal::complementaryLiteral(tr.transform(origLit));
    conclusionToOrigLitClauseMap.insert(make_pair(conclusion, make_pair(origLit, origClause)));
    conclusionList = new FormulaList(new AtomicFormula(conclusion), conclusionList);
  }
  Formula* conclusions = JunctionFormula::generalJunction(Connective::OR, conclusionList);
  Formula* hypothesis = new BinaryFormula(Connective::IMP,
                            Formula::quantify(indPremise),
                            Formula::quantify(conclusions));

  // cout << hypothesis->toString() << endl << endl;

  return make_pair(hypothesis, conclusionToOrigLitClauseMap);
}

} // Shell