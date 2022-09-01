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
 * @file ApplicativeHelper.hpp
 * Defines class ApplicativeHelper.
 */

#ifndef __ApplicativeHelper__
#define __ApplicativeHelper__

#include "Forwards.hpp"
#include "Signature.hpp"
#include "Lib/Deque.hpp"
#include "Lib/BiMap.hpp"
#include "Kernel/TermTransformer.hpp"

using namespace Kernel;
using namespace Shell;

/**
 * A class with function @b elimLambda() that eliminates a lambda expressions
 * It does this by applying the well known rewrite rules for SKIBC combinators.
 *
 * These can be found:
 * https://en.wikipedia.org/wiki/Combinatory_logic
 */
class ApplicativeHelper {
public:

  // reduce a term to normal form
  // uses a applicative order reduction stragegy
  // (inner-most left-most redex first)
  class BetaNormaliser : public BottomUpTermTransformer
  {
    TermList transformSubterm(TermList t) override;
  };
  
  class RedexReducer : public TermTransformer 
  {
    TermList reduce(TermList redex);
    TermList transformSubterm(TermList t) override;    
  }

  static TermList createAppTerm(TermList sort, TermList arg1, TermList arg2);
  static TermList createAppTerm(TermList s1, TermList s2, TermList arg1, TermList arg2, bool shared = true);
  static TermList createAppTerm3(TermList sort, TermList arg1, TermList arg2, TermList arg3);
  static TermList createAppTerm(TermList sort, TermList arg1, TermList arg2, TermList arg3, TermList arg4); 
  static TermList createAppTerm(TermList sort, TermList head, TermStack& terms); 
  static TermList createAppTerm(TermList sort, TermList head, TermList* args, unsigned arity, bool shared = true); 
  static TermList createLambdaTerm(TermList varSort, TermList termSort, TermList term); 
  static TermList getDeBruijnIndex(int index, TermList sort);
  static TermList getNthArg(TermList arrowSort, unsigned argNum);
  static TermList getResultApplieadToNArgs(TermList arrowSort, unsigned argNum);
  static TermList getResultSort(TermList sort);
  static unsigned getArity(TermList sort);
  static void getHeadAndAllArgs(TermList term, TermList& head, TermStack& args); 
  static void getHeadAndArgs(TermList term, TermList& head, TermStack& args); 
  static void getHeadAndArgs(Term* term, TermList& head, TermStack& args);  
  static void getHeadAndArgs(const Term* term, TermList& head, Deque<TermList>& args); 
  static void getHeadSortAndArgs(TermList term, TermList& head, TermList& headSort, TermStack& args); 
  static Signature::Proxy getProxy(const TermList t);
  static TermList getHead(TermList t);
  static TermList getHead(Term* t);  
  static bool isBool(TermList t);
  static bool isTrue(TermList term);
  static bool isFalse(TermList term);
  // reduces a single redex
  static TermList betaReduce(TermList redex);
};

#endif // __ApplicativeHelper__
