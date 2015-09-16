/**
 * @file ProofSimplifier.hpp
 * Defines class ProofSimplifier.
 */

#ifndef __ProofSimplifier__
#define __ProofSimplifier__

#include "Forwards.hpp"

#include "Lib/DHMap.hpp"
#include "Lib/Stack.hpp"

#include "Kernel/InferenceStore.hpp"
#include "Kernel/Problem.hpp"

#include "AIG.hpp"
#include "AIGInliner.hpp"
#include "AIGCompressor.hpp"

namespace Shell {

using namespace Lib;
using namespace Kernel;

class ProofTransformer {
public:
  ProofTransformer(Unit* refutation);
  virtual ~ProofTransformer() {}

  void perform();

  Unit* getNewRefutation() {
    CALL("ProofTransformer::getNewRefutation");
    ASS(isRefutation(_newProof.top()));
    return _newProof.top();
  }
protected:
  virtual void preTransform() {}
  virtual Unit* transformUnit(Unit* u) = 0;

  static bool isRefutation(Unit* u);
  static void loadProof(Unit* refutation, Stack<Unit*>& tgt);

  Stack<Unit*> _origProof;
private:

  void derefInference(Unit* src, Unit* tgt);
  void registerTransformation(Unit* src, Unit* tgt);

  Unit* _refutation;

  DHMap<Unit*,Unit*> _transformationMap;

  Stack<Unit*> _newProof;
};


class ProofSimplifier : public ProofTransformer {
public:
  ProofSimplifier(const Problem& prb, Unit* refutation, UnitList* defs);
protected:
  virtual void preTransform();
  virtual Unit* transformUnit(Unit* u);

private:
  AIGRef getAIG(Unit* u);

  // const Problem& _prb; // MS: unused
  UnitList* _defs;

  AIGInliner _inl;
  AIG& _aig;
  AIGFormulaSharer& _fsh;
  BDDAIG _bddAig;
};


}

#endif // __ProofSimplifier__
