#ifndef __InductionHelper__
#define __InductionHelper__

#include "Forwards.hpp"
#include "Kernel/Term.hpp"
#include "Kernel/TermTransformer.hpp"
#include "Lib/STL.hpp"

namespace Shell {

using namespace Kernel;
using namespace Lib;

class TermListReplacement : public TermTransformer {
public:
  TermListReplacement(TermList o, TermList r) : _o(o), _r(r) {}
  TermList transformSubterm(TermList trm) override;
private:
  TermList _o;
  TermList _r;
};

class TermOccurrenceReplacement : public TermTransformer {
public:
  TermOccurrenceReplacement(const vmap<TermList, TermList>& r,
                            const DHMap<TermList, DHSet<unsigned>*>& o) : _r(r), _o(o), _c() {}
  TermList transformSubterm(TermList trm) override;

private:
  const vmap<TermList, TermList>& _r;
  const DHMap<TermList, DHSet<unsigned>*>& _o;
  DHMap<TermList, unsigned> _c;
};

class VarReplacement : public TermTransformer {
public:
  VarReplacement(DHMap<unsigned, unsigned>& varMap, unsigned& v) : _varMap(varMap), _v(v) {}
  TermList transformSubterm(TermList trm) override;

private:
  DHMap<unsigned, unsigned>& _varMap;
  unsigned& _v;
};

class IteratorByInductiveVariables
{
public:
  IteratorByInductiveVariables(Term* term,
                               const vvector<bool>& indVars)
    : _it(term), _indVarIt(indVars.cbegin()), _end(indVars.cend())
  {}

  bool hasNext();
  TermList next();

private:
  Term::Iterator _it;
  vvector<bool>::const_iterator _indVarIt;
  vvector<bool>::const_iterator _end;
};

struct RDescription {
  RDescription(const vvector<TermList>& recursiveCalls,
               TermList step, Formula* cond);
  RDescription(TermList step, Formula* cond);

  vvector<TermList> _recursiveCalls;
  TermList _step;
  Formula* _condition;
};

ostream& operator<<(ostream& out, const RDescription& rdesc);

struct RDescriptionInst {
  RDescriptionInst(vvector<vmap<TermList, TermList>>&& recursiveCalls,
                   vmap<TermList, TermList>&& step,
                   Formula* cond);

  vvector<vmap<TermList, TermList>> _recursiveCalls;
  vmap<TermList, TermList> _step;
  Formula* _condition;
};

ostream& operator<<(ostream& out, const RDescriptionInst& inst);

struct InductionTemplate {
  void postprocess();

  vvector<RDescription> _rDescriptions;
  vvector<bool> _inductionVariables;
};

ostream& operator<<(ostream& out, const InductionTemplate& templ);

struct InductionScheme {
  void init(Term* term, vvector<RDescription>& rdescs, const vvector<bool>& indVars);

  vvector<RDescriptionInst> _rDescriptionInstances;
  unsigned _maxVar;
};

ostream& operator<<(ostream& out, const InductionScheme& scheme);

class InductionPreprocessor {
public:
  void preprocess(Problem& prb);

private:
  void preprocess(UnitList* units);
  void processBody(TermList& body, TermList& header, InductionTemplate& templ);

  void processCase(const unsigned recFun, TermList& body, vvector<TermList>& recursiveCalls);
  unsigned findMatchedArgument(unsigned matched, TermList& header);
};

struct InductionSchemeGenerator {
  ~InductionSchemeGenerator();

  void generatePrimary(Clause* premise, Literal* lit);
  void generateSecondary(Clause* premise, Literal* lit);
  void filter();

  vvector<pair<InductionScheme, DHMap<Literal*, Clause*>*>> _primarySchemes;
  DHMap<Literal*, DHMap<TermList, DHSet<unsigned>*>*> _actOccMaps;

private:
  void generate(Clause* premise, Literal* lit, vvector<pair<InductionScheme, DHMap<Literal*, Clause*>*>>& schemes);
  void processIteration(TermList curr, bool active,
    DHMap<TermList, unsigned>& currOccMap,
    Stack<bool>& actStack, Clause* premise, Literal* lit,
    vvector<pair<InductionScheme, DHMap<Literal*, Clause*>*>>& schemes);
  void filter(vvector<pair<InductionScheme, DHMap<Literal*, Clause*>*>>& schemes);

  vvector<pair<InductionScheme, DHMap<Literal*, Clause*>*>> _secondarySchemes;
};

} // Shell

#endif