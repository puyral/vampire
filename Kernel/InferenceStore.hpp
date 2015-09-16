/**
 * @file InferenceStore.hpp
 * Defines class InferenceStore.
 */


#ifndef __InferenceStore__
#define __InferenceStore__

#include <utility>
#include <ostream>

#include "Forwards.hpp"

#include "Lib/Allocator.hpp"
#include "Lib/DHMap.hpp"
#include "Lib/DHMultiset.hpp"
#include "Lib/Stack.hpp"
#include "Lib/VString.hpp"

#include "Kernel/Signature.hpp"
#include "Kernel/Clause.hpp"
#include "Kernel/Inference.hpp"

namespace Kernel {

using namespace Lib;

class InferenceStore
{
public:
  CLASS_NAME(InferenceStore);
  USE_ALLOCATOR(InferenceStore);
  
  static InferenceStore* instance();

  typedef List<int> IntList;

  struct FullInference
  {
    FullInference(unsigned premCnt) : csId(0), premCnt(premCnt) { }

    void* operator new(size_t,unsigned premCnt)
    {
      size_t size=sizeof(FullInference)+premCnt*sizeof(Unit*);
      size-=sizeof(Unit*);

      return ALLOC_KNOWN(size,"InferenceStore::FullInference");
    }

    size_t occupiedBytes()
    {
      size_t size=sizeof(FullInference)+premCnt*sizeof(Unit*);
      size-=sizeof(Unit*);
      return size;
    }

    void increasePremiseRefCounters();

    int csId;
    unsigned premCnt;
    Inference::Rule rule;
    Unit* premises[1];
  };

  void recordSplittingNameLiteral(Unit* us, Literal* lit);
  void recordIntroducedSymbol(Unit* u, bool func, unsigned number);

  void outputProof(ostream& out, Unit* refutation);
  void outputProof(ostream& out, UnitList* units);

  UnitIterator getParents(Unit* us, Inference::Rule& rule);
  UnitIterator getParents(Unit* us);

  vstring getUnitIdStr(Unit* cs);

private:
  InferenceStore();

  struct ProofPrinter;
  struct TPTPProofPrinter;
  struct ProofCheckPrinter;

  ProofPrinter* createProofPrinter(ostream& out);

  DHMultiset<Clause*, PtrIdentityHash> _nextClIds;

  DHMap<Unit*, Literal*> _splittingNameLiterals;


  /** first is true for function symbols, second is symbol number */
  typedef pair<bool,unsigned> SymbolId;
  typedef Stack<SymbolId> SymbolStack;
  DHMap<unsigned,SymbolStack> _introducedSymbols;

};


};

#endif /* __InferenceStore__ */
