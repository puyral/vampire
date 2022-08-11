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
 * @file TermTransformer.hpp
 * Defines class TermTransformer.
 */

#ifndef __TermTransformer__
#define __TermTransformer__

#include "Forwards.hpp"



namespace Kernel {

/**
 * Class to allow for easy transformations of subterms in shared literals.
 *
 * The inheriting class implemets function transform(TermList)
 * and then the function transform(Literal*) uses it to transform subterms
 * of the given literal.
 *
 * The literal and subterms returned by the transform(TermList) function have
 * to be shared.
 *
 * This class can be used to transform sort arguments as well by suitably
 * implementing the transform(TermList) function
 * 
 * TermTransformer goes top down but does no recurse into the replaced term
 */
class TermTransformer {
public:
  TermTransformer(bool shared = true, bool recurseIntoReplaced = false) : 
    _sharedResult(shared), _recurseIntoReplaced(recurseIntoReplaced) {}
  virtual ~TermTransformer() {}
  Term* transform(Term* term);
  Literal* transform(Literal* lit);
  TermList transform(TermList ts);
protected:
  virtual TermList transformSubterm(TermList trm) = 0;
  Term* transformSpecial(Term* specialTerm);
  // currently this can be used to transform the
  // first-order / "green" subterms of an applicative term.
  // TODO update to allow transformation of prefix subterms
  // TODO try and code share with transform(Term*)
  Term* transformApplication(Term* appTerm);
  virtual Formula* transform(Formula* f);
  bool _sharedResult;
  // recurse into repalced only affects applicative terms currently
  // can easily be extended to standard terms if required
  bool _recurseIntoReplaced;
};

/**
 * Has similar philosophy to TermTransformer, but:
 *  goes bottom up and so subterms of currently considered terms
 *  might already be some replacements that happened earlier, e.g.:
 *  transforming g(f(a,b)) will consider (provided transformSubterm is the identity function)
 *  the following sequence: a,b,f(a,b),g(f(a,b)) 
 *  and if transformSubterm is the identitify everywhere except for f(a,b) for which it returns c,
 *  the considered sequence will be: a,b,f(a,b)->c,g(c)
 */
class BottomUpTermTransformer {
public:
  virtual ~BottomUpTermTransformer() {}
  Term* transform(Term* term);
  Literal* transform(Literal* lit);
protected:
  virtual TermList transformSubterm(TermList trm) = 0;
  /**
   * TODO: these functions are similar as in TermTransformer, code duplication could be removed
   */
  TermList transform(TermList ts);
  Formula* transform(Formula* f);
};


}

#endif // __TermTransformer__
